#include "TorrentEngine.h"
#include <QDir>
#include <QFileInfo>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/settings_pack.hpp>

namespace { QString makeId(int n) { return QStringLiteral("torrent-%1").arg(n); } }

TorrentEngine::TorrentEngine(QObject *parent) : QObject(parent) {
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::error | lt::alert_category::status |
        lt::alert_category::storage | lt::alert_category::progress);
    settings.set_int(lt::settings_pack::active_downloads, 5);
    settings.set_int(lt::settings_pack::active_seeds, 5);
    settings.set_bool(lt::settings_pack::enable_dht, true);
    session_ = std::make_unique<lt::session>(settings);
    alertTimer_.setInterval(250);
    connect(&alertTimer_, &QTimer::timeout, this, &TorrentEngine::pollAlerts);
    alertTimer_.start();
}
TorrentEngine::~TorrentEngine() {
    alertTimer_.stop();
    if (session_) session_->pause();
}

QString TorrentEngine::addMagnet(const QString &magnet, const QString &savePath) {
    lt::error_code ec;
    auto params = lt::parse_magnet_uri(magnet.toStdString(), ec);
    if (ec) {
        emit torrentError({}, QStringLiteral("Invalid magnet link: %1").arg(QString::fromStdString(ec.message())));
        return {};
    }
    QDir dir(savePath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit torrentError({}, QStringLiteral("Unable to create torrent save directory."));
        return {};
    }
    params.save_path = savePath.toStdString();
    params.flags &= ~lt::torrent_flags::paused;
    auto handle = session_->add_torrent(std::move(params), ec);
    if (ec) {
        emit torrentError({}, QStringLiteral("Unable to add torrent: %1").arg(QString::fromStdString(ec.message())));
        return {};
    }
    const QString id = makeId(nextId_++);
    torrents_.insert(id, TorrentEntry{id, handle, false});
    emit torrentAdded(id, QStringLiteral("Resolving magnet…"));
    return id;
}

QString TorrentEngine::addTorrentFile(const QString &path, const QString &savePath) {
    if (!QFileInfo::exists(path)) {
        emit torrentError({}, QStringLiteral("Torrent file does not exist."));
        return {};
    }
    lt::error_code ec;
    auto info = std::make_shared<lt::torrent_info>(path.toStdString(), ec);
    if (ec) {
        emit torrentError({}, QStringLiteral("Unable to read torrent: %1").arg(QString::fromStdString(ec.message())));
        return {};
    }
    QDir dir(savePath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit torrentError({}, QStringLiteral("Unable to create torrent save directory."));
        return {};
    }
    lt::add_torrent_params params;
    params.ti = std::move(info);
    params.save_path = savePath.toStdString();
    auto handle = session_->add_torrent(std::move(params), ec);
    if (ec) {
        emit torrentError({}, QStringLiteral("Unable to add torrent: %1").arg(QString::fromStdString(ec.message())));
        return {};
    }
    const QString id = makeId(nextId_++);
    torrents_.insert(id, TorrentEntry{id, handle, false});
    const QString name = QString::fromStdString(handle.status().name);
    emit torrentAdded(id, name.isEmpty() ? QStringLiteral("Torrent") : name);
    return id;
}

void TorrentEngine::pause(const QString &id) {
    auto it = torrents_.find(id);
    if (it != torrents_.end() && it->handle.is_valid()) it->handle.pause();
}
void TorrentEngine::resume(const QString &id) {
    auto it = torrents_.find(id);
    if (it != torrents_.end() && it->handle.is_valid()) it->handle.resume();
}
void TorrentEngine::remove(const QString &id, bool deleteFiles) {
    auto it = torrents_.find(id);
    if (it == torrents_.end()) return;
    if (it->handle.is_valid())
        session_->remove_torrent(it->handle, deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{});
    torrents_.erase(it);
}

void TorrentEngine::pollAlerts() {
    if (!session_) return;
    std::vector<lt::alert*> alerts;
    session_->pop_alerts(&alerts);
    for (auto *alert : alerts) {
        if (auto *error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
                if (it->handle == error->handle) {
                    emit torrentError(it.key(), QString::fromStdString(error->error.message()));
                    break;
                }
            }
        }
        if (auto *metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
                if (it->handle == metadata->handle) {
                    const auto status = it->handle.status();
                    const QString name = QString::fromStdString(status.name);
                    if (!name.isEmpty()) emit torrentAdded(it.key(), name);
                    break;
                }
            }
        }
    }

    for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
        auto &entry = it.value();
        if (!entry.handle.is_valid()) continue;
        const auto status = entry.handle.status();
        emit torrentProgress(it.key(), static_cast<int>(status.progress_ppm / 10000),
            status.total_done, status.total_wanted, status.download_rate,
            status.upload_rate, status.num_peers);
        if (status.is_finished && !entry.completedNotified) {
            entry.completedNotified = true;
            emit torrentCompleted(it.key());
        }
    }
}
