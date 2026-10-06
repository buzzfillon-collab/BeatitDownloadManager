#include "TorrentEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>

namespace {
QString makeId(int n) { return QStringLiteral("torrent-%1").arg(n); }
}

TorrentEngine::TorrentEngine(QObject *parent) : QObject(parent) {
    resumeDirectory_ =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/torrents";
    QDir().mkpath(resumeDirectory_);

    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::error | lt::alert_category::status |
        lt::alert_category::storage | lt::alert_category::progress);
    settings.set_int(lt::settings_pack::active_downloads, maxActiveDownloads_);
    settings.set_int(lt::settings_pack::active_seeds, maxActiveDownloads_);
    settings.set_bool(lt::settings_pack::enable_dht, true);

    session_ = std::make_unique<lt::session>(settings);
    alertTimer_.setInterval(250);
    connect(&alertTimer_, &QTimer::timeout, this, &TorrentEngine::pollAlerts);
    alertTimer_.start();

    QTimer::singleShot(0, this, &TorrentEngine::restoreResumeData);
}

TorrentEngine::~TorrentEngine() {
    saveResumeData();
    for (int i = 0; i < 20; ++i) {
        QThread::msleep(50);
        pollAlerts();
    }
    alertTimer_.stop();
    if (session_) session_->pause();
}

QString TorrentEngine::addMagnet(const QString &magnet, const QString &savePath) {
    lt::error_code ec;
    auto params = lt::parse_magnet_uri(magnet.toStdString(), ec);
    if (ec) {
        emit torrentError({}, QStringLiteral("Invalid magnet link: %1")
            .arg(QString::fromStdString(ec.message())));
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
        emit torrentError({}, QStringLiteral("Unable to add torrent: %1")
            .arg(QString::fromStdString(ec.message())));
        return {};
    }

    const QString id = makeId(nextId_++);
    torrents_.insert(id, TorrentEntry{id, handle, false, false, true});
    emit torrentAdded(id, QStringLiteral("Resolving magnet…"));
    scheduleTorrents();
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
        emit torrentError({}, QStringLiteral("Unable to read torrent: %1")
            .arg(QString::fromStdString(ec.message())));
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
        emit torrentError({}, QStringLiteral("Unable to add torrent: %1")
            .arg(QString::fromStdString(ec.message())));
        return {};
    }

    const QString id = makeId(nextId_++);
    torrents_.insert(id, TorrentEntry{id, handle, false, false, true});
    const QString name = QString::fromStdString(handle.status().name);
    emit torrentAdded(id, name.isEmpty() ? QStringLiteral("Torrent") : name);
    scheduleTorrents();
    return id;
}

void TorrentEngine::pause(const QString &id) {
    auto it = torrents_.find(id);
    if (it == torrents_.end() || !it->handle.is_valid()) return;
    it->userPaused = true;
    it->scheduled = false;
    it->handle.pause();
    saveOneResume(id, it->handle);
    scheduleTorrents();
}

void TorrentEngine::resume(const QString &id) {
    auto it = torrents_.find(id);
    if (it == torrents_.end() || !it->handle.is_valid()) return;
    it->userPaused = false;
    it->scheduled = true;
    scheduleTorrents();
}

void TorrentEngine::remove(const QString &id, bool deleteFiles) {
    auto it = torrents_.find(id);
    if (it == torrents_.end()) return;

    if (it->handle.is_valid())
        session_->remove_torrent(
            it->handle,
            deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{});

    QFile::remove(resumeDirectory_ + "/" + id + ".resume");
    torrents_.erase(it);
    scheduleTorrents();
}

void TorrentEngine::saveOneResume(const QString &id, const lt::torrent_handle &handle) {
    if (!handle.is_valid()) return;
    handle.save_resume_data(lt::torrent_handle::save_info_dict);
}

void TorrentEngine::saveResumeData() {
    for (auto it = torrents_.cbegin(); it != torrents_.cend(); ++it)
        saveOneResume(it.key(), it->handle);
    if (session_) session_->save_state();
}

void TorrentEngine::restoreResumeData() {
    QDir dir(resumeDirectory_);
    const auto files = dir.entryList(QStringList() << "*.resume", QDir::Files);

    for (const QString &file : files) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = f.readAll();
        f.close();

        lt::error_code ec;
        auto atp = lt::read_resume_data(
            lt::span<char const>(data.constData(), data.size()), ec);
        if (ec) continue;

        atp.flags |= lt::torrent_flags::paused;
        auto handle = session_->add_torrent(std::move(atp), ec);
        if (ec) continue;

        QString id = QFileInfo(file).completeBaseName();
        if (!id.startsWith(QStringLiteral("torrent-")))
            id = makeId(nextId_++);
        bool ok = false;
        const int n = id.mid(QStringLiteral("torrent-").size()).toInt(&ok);
        if (ok) nextId_ = qMax(nextId_, n + 1);
        if (torrents_.contains(id))
            id = makeId(nextId_++);
        torrents_.insert(id, TorrentEntry{id, handle, false, false, false});
        const QString name = QString::fromStdString(handle.status().name);
        emit torrentAdded(id, name.isEmpty() ? QStringLiteral("Torrent") : name);
    }

    scheduleTorrents();
}

void TorrentEngine::scheduleTorrents() {
    int active = 0;

    for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
        if (it->userPaused || it->handle.status().is_finished) {
            if (it->scheduled && it->handle.is_valid()) it->handle.pause();
            it->scheduled = false;
        } else if (it->scheduled && !it->handle.status().paused) {
            ++active;
        }
    }

    for (auto it = torrents_.begin(); it != torrents_.end() && active < maxActiveDownloads_; ++it) {
        if (it->userPaused || it->handle.status().is_finished || it->scheduled) continue;
        it->scheduled = true;
        it->handle.resume();
        ++active;
    }
}

void TorrentEngine::pollAlerts() {
    if (!session_) return;

    std::vector<lt::alert*> alerts;
    session_->pop_alerts(&alerts);

    for (auto *alert : alerts) {
        if (auto *resume = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
                if (it->handle == resume->handle) {
                    const QString file = resumeDirectory_ + "/" + it.key() + ".resume";
                    const auto bytes = lt::write_resume_data_buf(resume->params);
                    QFile f(file);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        f.write(bytes.data(), static_cast<qint64>(bytes.size()));
                        f.close();
                    }
                    break;
                }
            }
        }

        if (auto *error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
                if (it->handle == error->handle) {
                    emit torrentError(it.key(),
                        QString::fromStdString(error->error.message()));
                    break;
                }
            }
        }

        if (auto *metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
                if (it->handle == metadata->handle) {
                    const QString name = QString::fromStdString(it->handle.status().name);
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
            entry.scheduled = false;
            emit torrentCompleted(it.key());
            scheduleTorrents();
        }
    }
}
