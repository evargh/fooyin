/*
 * Fooyin
 * Copyright © 2024, Luke Taylor <LukeT1@proton.me>
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "playlistinteractor.h"

#include "guiutils.h"
#include "playlistcontroller.h"

#include <core/library/musiclibrary.h>
#include <core/playlist/playlist.h>
#include <core/playlist/playlisthandler.h>
#include <core/track.h>
#include <gui/widgets/elapsedprogressdialog.h>
#include <utils/datastream.h>
#include <utils/fileutils.h>
#include <utils/utils.h>

#include <QIODevice>
#include <QMainWindow>

using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

namespace {
template <typename Func>
void scanFiles(Fooyin::MusicLibrary* library, const QList<QUrl>& files, Func&& func)
{
    auto* scanDialog = new Fooyin::ElapsedProgressDialog(Fooyin::PlaylistInteractor::tr("Reading tracks…"),
                                                         Fooyin::PlaylistInteractor::tr("Abort"), 0, 100,
                                                         Fooyin::Utils::getMainWindow());
    scanDialog->setAttribute(Qt::WA_DeleteOnClose);
    scanDialog->setModal(true);
    scanDialog->setMinimumDuration(500ms);

    const Fooyin::ScanRequest request = library->scanFiles(files);
    scanDialog->startTimer();

    QObject::connect(library, &Fooyin::MusicLibrary::scanProgress, scanDialog,
                     [scanDialog, request](const Fooyin::ScanProgress& progress) {
                         if(progress.id != request.id) {
                             return;
                         }

                         if(scanDialog->wasCancelled()) {
                             request.cancel();
                             scanDialog->close();
                         }

                         scanDialog->setValue(progress.percentage());

                         if(!progress.file.isEmpty()) {
                             scanDialog->setText(Fooyin::PlaylistInteractor::tr("Current file") + ":\n"_L1
                                                 + progress.file);
                         }
                     });

    QObject::connect(library, &Fooyin::MusicLibrary::tracksScanned, scanDialog,
                     [scanDialog, request, func](int id, const Fooyin::TrackList& scannedTracks) {
                         if(id == request.id) {
                             func(scannedTracks);
                             scanDialog->close();
                         }
                     });
}

template <typename Func>
void loadPlaylistTracks(Fooyin::MusicLibrary* library, const QList<QUrl>& files, Func&& func)
{
    auto* scanDialog = new Fooyin::ElapsedProgressDialog(Fooyin::PlaylistInteractor::tr("Loading playlist…"),
                                                         Fooyin::PlaylistInteractor::tr("Abort"), 0, 100,
                                                         Fooyin::Utils::getMainWindow());
    scanDialog->setAttribute(Qt::WA_DeleteOnClose);
    scanDialog->setModal(true);
    scanDialog->setMinimumDuration(500ms);

    const Fooyin::ScanRequest request = library->loadPlaylist(files);
    scanDialog->startTimer();

    QObject::connect(library, &Fooyin::MusicLibrary::scanProgress, scanDialog,
                     [scanDialog, request](const Fooyin::ScanProgress& progress) {
                         if(progress.id != request.id) {
                             return;
                         }

                         if(scanDialog->wasCancelled()) {
                             request.cancel();
                             scanDialog->close();
                         }

                         scanDialog->setValue(progress.percentage());

                         if(!progress.file.isEmpty()) {
                             scanDialog->setText(Fooyin::PlaylistInteractor::tr("Current file") + ":\n"_L1
                                                 + progress.file);
                         }
                     });

    QObject::connect(library, &Fooyin::MusicLibrary::tracksScanned, scanDialog,
                     [scanDialog, request, func](int id, const Fooyin::TrackList& scannedTracks) {
                         if(id == request.id) {
                             func(scannedTracks);
                             scanDialog->close();
                         }
                     });
}
} // namespace

namespace Fooyin {
PlaylistInteractor::PlaylistInteractor(PlaylistHandler* handler, PlaylistController* controller, MusicLibrary* library,
                                       QObject* parent)
    : QObject{parent}
    , m_handler{handler}
    , m_controller{controller}
    , m_library{library}
{ }

PlaylistHandler* PlaylistInteractor::handler() const
{
    return m_handler;
}

PlaylistController* PlaylistInteractor::playlistController() const
{
    return m_controller;
}

MusicLibrary* PlaylistInteractor::library() const
{
    return m_library;
}

PlayerController* PlaylistInteractor::playerController() const
{
    return m_controller->playerController();
}

void PlaylistInteractor::filesToPlaylist(const QList<QUrl>& urls, const UId& id) const
{
    if(urls.empty()) {
        return;
    }

    if(id.isValid()) {
        scanFiles(m_library, urls, [this, id](const TrackList& scannedTracks) {
            if(auto* playlist = m_handler->playlistById(id)) {
                m_handler->appendToPlaylist(playlist->id(), scannedTracks);
                m_controller->changeCurrentPlaylist(playlist);
            }
        });
    }
    else {
        scanFiles(m_library, urls, [this](const TrackList& scannedTracks) {
            const QString playlistName = Track::findCommonField(scannedTracks);
            if(auto* playlist = m_handler->createNewPlaylist(playlistName, scannedTracks)) {
                m_controller->changeCurrentPlaylist(playlist);
            }
        });
    }
}

void PlaylistInteractor::filesToCurrentPlaylist(const QList<QUrl>& urls) const
{
    if(urls.empty()) {
        return;
    }

    scanFiles(m_library, urls, [this](const TrackList& scannedTracks) {
        if(auto* playlist = m_controller->currentPlaylist()) {
            m_handler->appendToPlaylist(playlist->id(), scannedTracks);
        }
    });
}

void PlaylistInteractor::filesToCurrentPlaylistReplace(const QList<QUrl>& urls, bool play) const
{
    if(urls.empty()) {
        return;
    }

    scanFiles(m_library, urls, [this, play](const TrackList& scannedTracks) {
        if(auto* playlist = m_controller->currentPlaylist()) {
            m_handler->replacePlaylistTracks(playlist->id(), scannedTracks);
            playlist->changeCurrentIndex(0);
            if(play) {
                m_handler->startPlayback(playlist);
            }
        }
    });
}

void PlaylistInteractor::filesToNewPlaylist(const QString& playlistName, const QList<QUrl>& urls, bool play) const
{
    if(urls.empty()) {
        return;
    }

    auto handleScanResult = [this, playlistName, play](const TrackList& scannedTracks) {
        Playlist* playlist = m_handler->playlistByName(playlistName);
        if(playlist) {
            const int indexToPlay = playlist->trackCount();
            m_handler->appendToPlaylist(playlist->id(), scannedTracks);
            playlist->changeCurrentIndex(indexToPlay);
        }
        else {
            playlist = m_handler->createPlaylist(playlistName, scannedTracks);
        }

        if(playlist) {
            m_controller->changeCurrentPlaylist(playlist);
            if(play) {
                m_handler->startPlayback(playlist);
            }
        }
    };

    scanFiles(m_library, urls, handleScanResult);
}

void PlaylistInteractor::filesToNewPlaylistReplace(const QString& playlistName, const QList<QUrl>& urls,
                                                   bool play) const
{
    if(urls.empty()) {
        return;
    }

    auto handleScanResult = [this, playlistName, play](const TrackList& scannedTracks) {
        if(auto* playlist = m_handler->createPlaylist(playlistName, scannedTracks)) {
            m_controller->changeCurrentPlaylist(playlist);
            if(play) {
                m_handler->startPlayback(playlist);
            }
        }
    };

    scanFiles(m_library, urls, handleScanResult);
}

void PlaylistInteractor::filesToActivePlaylist(const QList<QUrl>& urls) const
{
    if(!m_handler->activePlaylist()) {
        return;
    }

    if(urls.empty()) {
        return;
    }

    scanFiles(m_library, urls, [this](const TrackList& scannedTracks) {
        if(auto* playlist = m_handler->activePlaylist()) {
            m_handler->appendToPlaylist(playlist->id(), scannedTracks);
        }
    });
}

void PlaylistInteractor::filesToTracks(const QList<QUrl>& urls, const std::function<void(const TrackList&)>& func) const
{
    if(urls.empty()) {
        return;
    }

    scanFiles(m_library, urls, func);
}

void PlaylistInteractor::loadPlaylist(const QList<QPair<QString, QUrl>>& playlistData, bool play) const
{
    if(playlistData.empty()) {
        return;
    }

    for(const QPair<QString, QUrl>& item : playlistData) {
        auto [name, url]      = item;
        auto handleScanResult = [this, name, play](const TrackList& scannedTracks) {
            Playlist* playlist = m_handler->playlistByName(name);
            if(playlist) {
                const int indexToPlay = playlist->trackCount();
                m_handler->appendToPlaylist(playlist->id(), scannedTracks);
                playlist->changeCurrentIndex(indexToPlay);
            }
            else {
                playlist = m_handler->createPlaylist(name, scannedTracks);
            }

            if(playlist) {
                m_controller->changeCurrentPlaylist(playlist);
                if(play) {
                    m_handler->startPlayback(playlist);
                }
            }
        };
        loadPlaylistTracks(m_library, {url}, handleScanResult);
    }
}

void PlaylistInteractor::trackMimeToPlaylist(const QByteArray& data, const UId& id)
{
    const TrackList tracks = Gui::tracksFromMimeData(m_library, data);
    if(tracks.empty()) {
        return;
    }

    if(id.isValid()) {
        m_handler->appendToPlaylist(id, tracks);
    }
    else {
        const QString playlistName = Track::findCommonField(tracks);
        if(auto* playlist = m_handler->createPlaylist(playlistName, tracks)) {
            m_controller->changeCurrentPlaylist(playlist);
        }
    }
}
} // namespace Fooyin
