#include "TorrentEngine.h"
#include "DownloadDatabase.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QVector>
#include <limits>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/load_torrent.hpp>

namespace {
QString makeId(int n) { return QStringLiteral("torrent-%1").arg(n); }
constexpr double kWholeFileAvailability = 1.0;
}

TorrentEngine::TorrentEngine(QObject *parent) : QObject(parent) {
    database_ = std::make_unique<DownloadDatabase>();
    database_->open();

    resumeDirectory_ =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/torrents";
    QDir().mkpath(resumeDirectory_);

    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::error | lt::alert_category::status |
        lt::alert_category::storage);
    settings.set_int(lt::settings_pack::active_downloads, maxActiveDownloads_);
    settings.set_int(lt::settings_pack::active_seeds, maxActiveDownloads_);
    settings.set_bool(lt::settings_pack::enable_dht, true);

    loadSettings();
    settings.set_int(lt::settings_pack::download_rate_limit, static_cast<int>(qMin<qint64>(bandwidthLimit_, std::numeric_limits<int>::max())));
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
    torrents_.insert(id, TorrentEntry{id, handle, false, false, false, false, false});
    PersistedDownload d;
    d.id = id; d.type = QStringLiteral("torrent"); d.source = magnet;
    d.destination = savePath; d.filename = QStringLiteral("Resolving magnet…");
    d.status = QStringLiteral("Resolving");
    database_->save(d);
    emit torrentAdded(id, d.filename);
    scheduleTorrents();
    return id;
}

QString TorrentEngine::addTorrentFile(const QString &path, const QString &savePath) {
    if (!QFileInfo::exists(path)) {
        emit torrentError({}, QStringLiteral("Torrent file does not exist."));
        return {};
    }

    lt::error_code ec;
    lt::load_torrent_limits limits;
    auto params = lt::load_torrent_file(path.toStdString(), ec, limits);
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
    PersistedDownload d;
    d.id = id; d.type = QStringLiteral("torrent"); d.source = path;
    d.destination = savePath; d.filename = name.isEmpty() ? QStringLiteral("Torrent") : name;
    d.status = QStringLiteral("Queued");
    database_->save(d);
    emit torrentAdded(id, d.filename);
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
    if (database_) {
        auto rows = database_->loadHistory();
        for (const auto &d : rows) if (d.id == id) {
            auto copy = d; copy.status = QStringLiteral("Paused"); copy.speed = 0;
            database_->save(copy); break;
        }
    }
    scheduleTorrents();
}

void TorrentEngine::resume(const QString &id) {
    auto it = torrents_.find(id);
    if (it == torrents_.end() || !it->handle.is_valid()) return;
    it->userPaused = false;
    it->availabilityOverride = true;
    it->scheduled = true;
    if (database_) {
        auto rows = database_->loadHistory();
        for (const auto &d : rows) if (d.id == id) {
            auto copy = d; copy.status = QStringLiteral("Queued");
            database_->save(copy); break;
        }
    }
    scheduleTorrents();
}

void TorrentEngine::remove(const QString &id, bool deleteFiles) {
    auto it = torrents_.find(id);
    if (it != torrents_.end()) {
        if (it->handle.is_valid())
            session_->remove_torrent(
                it->handle,
                deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{});
        torrents_.erase(it);
    } else if (deleteFiles && database_) {
        for (const auto &d : database_->loadHistory()) {
            if (d.id != id || d.type != QStringLiteral("torrent")) continue;
            if (!d.destination.isEmpty() && !d.filename.isEmpty()) {
                const QString root = QDir(d.destination).filePath(d.filename);
                if (QFileInfo(root).isFile()) QFile::remove(root);
                else if (QFileInfo(root).isDir()) QDir(root).removeRecursively();
            }
            break;
        }
    }

    QFile::remove(resumeDirectory_ + "/" + id + ".resume");
    if (database_) database_->remove(id);
    emit torrentRemoved(id);
    scheduleTorrents();
}

void TorrentEngine::saveOneResume(const QString &id, const lt::torrent_handle &handle) {
    if (!handle.is_valid()) return;
    handle.save_resume_data(lt::torrent_handle::save_info_dict);
}

void TorrentEngine::saveResumeData() {
    for (auto it = torrents_.cbegin(); it != torrents_.cend(); ++it)
        saveOneResume(it.key(), it->handle);
}

bool TorrentEngine::wholeFileAvailable(const lt::torrent_status &status) const {
    // libtorrent's distributed_copies is the number of complete copies of the
    // torrent currently represented across the connected swarm. A value >= 1
    // means every piece exists somewhere in the swarm, although one copy may
    // be distributed across multiple peers.
    return status.distributed_copies >= kWholeFileAvailability;
}

QString TorrentEngine::stateText(const TorrentEntry &entry, const lt::torrent_status &status) const {
    if (status.errc) return QStringLiteral("Error: %1").arg(QString::fromStdString(status.errc.message()));
    if (entry.userPaused) return QStringLiteral("Paused");
    if (status.state == lt::torrent_status::checking_resume_data ||
        status.state == lt::torrent_status::checking_files) return QStringLiteral("Checking files");
    if (!status.has_metadata) return QStringLiteral("Resolving metadata");
    if (status.is_seeding) return entry.seedStopped ? QStringLiteral("Seeding stopped") : QStringLiteral("Seeding");
    if (entry.availabilityWaiting) return QStringLiteral("Waiting — incomplete availability");
    if (entry.stalledNotified) return QStringLiteral("Stalled");
    if (status.state == lt::torrent_status::downloading) return QStringLiteral("Downloading");
    if (status.is_finished) return QStringLiteral("Finished");
    return QStringLiteral("Queued");
}

bool TorrentEngine::shouldStopSeeding(const lt::torrent_status &status) const {
    if (!status.is_seeding) return false;
    if (seedingPolicyMode_ == 3) return true;
    if (seedingPolicyMode_ == 2) return false;
    if (seedingPolicyMode_ == 1)
        return status.seeding_duration.count() >= seedingMinutes_ * 60;
    if (status.total_done <= 0) return false;
    return static_cast<double>(status.all_time_upload) / static_cast<double>(status.total_done) >= seedingRatio_;
}
void TorrentEngine::loadSettings() {
    QSettings s(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    bandwidthLimit_ = qMax<qint64>(0, s.value(QStringLiteral("bandwidth/limit"), 0).toLongLong());
    seedingPolicyMode_ = qBound(0, s.value(QStringLiteral("torrent/seedingMode"), 0).toInt(), 3);
    seedingRatio_ = qMax(0.1, s.value(QStringLiteral("torrent/seedingRatio"), 1.0).toDouble());
    seedingMinutes_ = qMax(1, s.value(QStringLiteral("torrent/seedingMinutes"), 30).toInt());
}

void TorrentEngine::persistSettings() {
    QSettings s(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    s.setValue(QStringLiteral("bandwidth/limit"), bandwidthLimit_);
    s.setValue(QStringLiteral("torrent/seedingMode"), seedingPolicyMode_);
    s.setValue(QStringLiteral("torrent/seedingRatio"), seedingRatio_);
    s.setValue(QStringLiteral("torrent/seedingMinutes"), seedingMinutes_);
}

void TorrentEngine::applyBandwidthLimit() {
    if (!session_) return;
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::download_rate_limit,
                     static_cast<int>(qMin<qint64>(bandwidthLimit_, std::numeric_limits<int>::max())));
    session_->apply_settings(settings);
}

void TorrentEngine::setBandwidthLimit(qint64 bytesPerSecond) {
    bandwidthLimit_ = qMax<qint64>(0, bytesPerSecond);
    QSettings s(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    s.setValue(QStringLiteral("bandwidth/limit"), bandwidthLimit_);
    applyBandwidthLimit();
}

void TorrentEngine::setSchedulerAllowed(bool allowed) {
    if (schedulerAllowed_ == allowed) return;
    schedulerAllowed_ = allowed;
    scheduleTorrents();
}

void TorrentEngine::setSeedingPolicy(int mode, double ratio, int minutes) {
    seedingPolicyMode_ = qBound(0, mode, 3);
    seedingRatio_ = qMax(0.1, ratio);
    seedingMinutes_ = qMax(1, minutes);
    persistSettings();
    for (auto it = torrents_.begin(); it != torrents_.end(); ++it)
        if (it->seedStopped && it->handle.is_valid()) {
            it->seedStopped = false;
            it->userPaused = false;
            it->handle.resume();
        }
    scheduleTorrents();
}

void TorrentEngine::forceRecheck(const QString &id) {
    auto it = torrents_.find(id);
    if (it == torrents_.end() || !it->handle.is_valid()) return;
    it->stalledNotified = false;
    it->availabilityWaiting = false;
    it->availabilityPrompted = false;
    it->availabilityOverride = false;
    it->seedStopped = false;
    it->lastProgressBytes = 0;
    it->lastProgressTime = QDateTime::currentSecsSinceEpoch();
    it->handle.force_recheck();
    it->scheduled = false;
    emit torrentStatusChanged(id, QStringLiteral("Checking files"));
}

void TorrentEngine::setFilePriorities(const QString &id, const QVector<int> &priorities) {
    auto it = torrents_.find(id);
    if (it == torrents_.end() || !it->handle.is_valid()) return;
    if (!it->handle.torrent_file()) return;
    std::vector<lt::download_priority_t> p;
    p.reserve(priorities.size());
    for (int v : priorities) p.push_back(lt::download_priority_t(qBound(0, v, 7)));
    it->handle.prioritize_files(p);
    saveOneResume(id, it->handle);
    scheduleTorrents();
}

QVector<QString> TorrentEngine::torrentFiles(const QString &id) const {
    QVector<QString> out;
    const auto it = torrents_.constFind(id);
    if (it == torrents_.constEnd() || !it->handle.is_valid()) return out;
    auto ti = it->handle.torrent_file();
    if (!ti) return out;
    const auto &layout = ti->layout();
    lt::filenames names(layout, it->handle.get_renamed_files());
    for (int i = 0; i < names.num_files(); ++i)
        out.push_back(QString::fromStdString(names.file_path(lt::file_index_t(i))));
    return out;
}

QVector<int> TorrentEngine::filePriorities(const QString &id) const {
    QVector<int> out;
    const auto it = torrents_.constFind(id);
    if (it == torrents_.constEnd() || !it->handle.is_valid()) return out;
    for (auto p : it->handle.get_file_priorities()) out.push_back(static_cast<int>(static_cast<lt::download_priority_t::underlying_type>(p)));
    return out;
}

QString TorrentEngine::torrentSavePath(const QString &id) const {
    const auto it = torrents_.constFind(id);
    if (it == torrents_.constEnd() || !it->handle.is_valid()) return {};
    const auto status = it->handle.status();
    return QString::fromStdString(status.save_path);
}

void TorrentEngine::persistStatus(const QString &id, const lt::torrent_status &status) {
    if (!database_) return;
    for (const auto &d : database_->loadHistory()) {
        if (d.id != id) continue;
        auto copy = d;
        copy.downloadedBytes = status.total_done;
        copy.totalBytes = status.total_wanted;
        copy.speed = status.download_rate;
        const auto torrentIt = torrents_.constFind(id);
        const bool userPaused = torrentIt != torrents_.constEnd() && torrentIt->userPaused;
        copy.status = torrentIt != torrents_.constEnd()
            ? stateText(torrentIt.value(), status)
            : (status.is_finished ? QStringLiteral("Completed") : QStringLiteral("Downloading"));
        database_->save(copy);
        return;
    }
}

void TorrentEngine::restoreResumeData() {
    if (database_) {
        for (const auto &d : database_->loadHistory()) {
            if (d.type == QStringLiteral("torrent"))
                emit torrentHistoryRestored(d.id, d.filename, d.source, d.status,
                                            d.downloadedBytes, d.totalBytes, d.updatedAt);
        }
    }

    const auto history = database_ ? database_->loadHistory() : QVector<PersistedDownload>{};
    for (const auto &d : history) {
        if (d.type != QStringLiteral("torrent")) continue;
        bool ok = false;
        const int n = d.id.mid(QStringLiteral("torrent-").size()).toInt(&ok);
        if (ok) nextId_ = qMax(nextId_, n + 1);
    }

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
        bool wasPaused = false;
        for (const auto &d : history) if (d.id == id) {
            wasPaused = d.status == QStringLiteral("Paused");
            break;
        }
        torrents_.insert(id, TorrentEntry{id, handle, false, wasPaused, false, false, false});
        const auto restoredStatus = handle.status();
        const QString name = QString::fromStdString(restoredStatus.name);
        const QString displayName = name.isEmpty() ? QStringLiteral("Torrent") : name;
        if (database_) {
            bool found = false;
            for (const auto &d : history) if (d.id == id) {
                auto copy = d;
                copy.filename = displayName;
                copy.status = restoredStatus.is_finished ? QStringLiteral("Completed")
                                                          : (wasPaused ? QStringLiteral("Paused") : QStringLiteral("Queued"));
                copy.downloadedBytes = restoredStatus.total_done;
                copy.totalBytes = restoredStatus.total_wanted;
                database_->save(copy); found = true; break;
            }
            if (!found) {
                PersistedDownload d; d.id=id; d.type=QStringLiteral("torrent");
                d.source=QStringLiteral("resume://") + id; d.destination=QString();
                d.filename=displayName; d.status=restoredStatus.is_finished ? QStringLiteral("Completed") : QStringLiteral("Queued");
                d.downloadedBytes = restoredStatus.total_done; d.totalBytes = restoredStatus.total_wanted;
                database_->save(d);
            }
        }
        emit torrentAdded(id, displayName);
    }

    // If resume data is missing/corrupt, rebuild the torrent from its persisted
    // source instead of leaving a dead history entry. This covers both .torrent
    // files and magnets and preserves the original torrent ID.
    for (const auto &d : history) {
        if (d.type != QStringLiteral("torrent") || torrents_.contains(d.id))
            continue;

        lt::error_code ec;
        lt::add_torrent_params params;
        bool usable = false;

        if (d.source.startsWith(QStringLiteral("magnet:?"))) {
            params = lt::parse_magnet_uri(d.source.toStdString(), ec);
            usable = !ec;
        } else if (!d.source.isEmpty() && QFileInfo::exists(d.source)) {
            lt::load_torrent_limits limits;
            params = lt::load_torrent_file(d.source.toStdString(), ec, limits);
            usable = !ec;
        }

        if (!usable) continue;

        params.save_path = d.destination.toStdString();
        params.flags &= ~lt::torrent_flags::paused;
        auto handle = session_->add_torrent(std::move(params), ec);
        if (ec) continue;

        const bool paused = d.status == QStringLiteral("Paused");
        torrents_.insert(d.id, TorrentEntry{d.id, handle, false, paused, false, false, false});
        const auto status = handle.status();
        const QString name = QString::fromStdString(status.name);
        if (!name.isEmpty()) {
            auto copy = d;
            copy.filename = name;
            copy.downloadedBytes = status.total_done;
            copy.totalBytes = status.total_wanted;
            copy.status = status.is_finished ? QStringLiteral("Completed")
                                             : (paused ? QStringLiteral("Paused")
                                                       : QStringLiteral("Queued"));
            database_->save(copy);
            emit torrentAdded(d.id, name);
        } else {
            emit torrentAdded(d.id, d.filename);
        }
    }

    scheduleTorrents();
}

void TorrentEngine::scheduleTorrents() {
    if (!schedulerAllowed_) {
        for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
            if (!it->handle.is_valid() || it->userPaused || it->seedStopped) continue;
            if (!(it->handle.flags() & lt::torrent_flags::paused)) {
                it->schedulerPaused = true;
                it->scheduled = false;
                it->handle.pause();
            }
        }
        return;
    }
    for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
        if (it->schedulerPaused && it->handle.is_valid() && !it->userPaused && !it->seedStopped) {
            it->schedulerPaused = false;
            it->handle.resume();
        }
    }

    int activeDownloads = 0;
    for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
        if (!it->handle.is_valid()) continue;
        const auto status = it->handle.status();
        if (it->userPaused || it->seedStopped) {
            it->scheduled = false;
            if (!(it->handle.flags() & lt::torrent_flags::paused)) it->handle.pause();
            continue;
        }
        if (it->availabilityWaiting && !it->availabilityOverride) {
            if (status.has_metadata && status.list_peers > 0 && wholeFileAvailable(status)) {
                it->availabilityWaiting = false;
                it->availabilityPrompted = false;
                it->scheduled = false;
                it->handle.unset_flags(lt::torrent_flags::upload_mode);
                emit torrentStatusChanged(it.key(), QStringLiteral("Availability complete — starting"));
            } else {
                it->scheduled = true;
                it->handle.set_flags(lt::torrent_flags::upload_mode);
                it->handle.resume();
                continue;
            }
        }
        if (!it->availabilityOverride && status.has_metadata && status.num_peers > 0 &&
            !status.is_finished && !wholeFileAvailable(status)) {
            it->availabilityWaiting = true;
            it->scheduled = true;
            it->handle.set_flags(lt::torrent_flags::upload_mode);
            it->handle.resume();
            if (!it->availabilityPrompted) {                it->availabilityPrompted = true;
                emit torrentAvailabilityQuestion(it.key(), QString::fromStdString(status.name),
                    status.distributed_copies, status.num_peers);
            }
            continue;
        }
        if (!status.is_finished && it->scheduled && !(it->handle.flags() & lt::torrent_flags::paused))
            ++activeDownloads;
        if (status.is_finished && !status.is_seeding && !(it->handle.flags() & lt::torrent_flags::paused))
            it->scheduled = true;
    }
    for (auto it = torrents_.begin(); it != torrents_.end() && activeDownloads < maxActiveDownloads_; ++it) {
        if (it->userPaused || it->seedStopped || !it->handle.is_valid() || it->scheduled) continue;
        const auto status = it->handle.status();
        if (status.is_seeding) { it->scheduled = true; it->handle.resume(); continue; }
        if (status.is_finished) { it->scheduled = true; it->handle.resume(); ++activeDownloads; continue; }
        if (!it->availabilityOverride && status.has_metadata && status.num_peers > 0 &&
            !wholeFileAvailable(status)) continue;
        it->scheduled = true;
        it->handle.resume();
        ++activeDownloads;
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
                        f.write(bytes.data(), static_cast<qint64>(bytes.size())); f.close();
                    }
                    break;
                }
            }
        }
        if (auto *error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) if (it->handle == error->handle) {
                it->stalledNotified = false;
                emit torrentError(it.key(), QString::fromStdString(error->error.message()));
                emit torrentStatusChanged(it.key(), QStringLiteral("Error: %1").arg(QString::fromStdString(error->error.message())));
                break;
            }
        }
        if (auto *metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) if (it->handle == metadata->handle) {
                const QString name = QString::fromStdString(it->handle.status().name);
                if (!name.isEmpty()) emit torrentAdded(it.key(), name);
                break;
            }
        }
        if (auto *fileError = lt::alert_cast<lt::file_error_alert>(alert)) {
            for (auto it = torrents_.begin(); it != torrents_.end(); ++it) if (it->handle == fileError->handle) {
                emit torrentError(it.key(), QString::fromStdString(fileError->error.message()));
                break;
            }
        }
    }

    static qint64 lastDatabaseWrite = 0;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool writeDatabase = (now != lastDatabaseWrite);
    if (writeDatabase) lastDatabaseWrite = now;
    if (now - lastResumeSave_ >= 30) { saveResumeData(); lastResumeSave_ = now; }

    for (auto it = torrents_.begin(); it != torrents_.end(); ++it) {
        auto &entry = it.value();
        if (!entry.handle.is_valid()) continue;
        auto status = entry.handle.status();

        if (!entry.userPaused && !entry.seedStopped && !entry.availabilityOverride &&
            status.has_metadata && status.list_peers > 0 && !status.is_finished &&
            !wholeFileAvailable(status)) {
            entry.availabilityWaiting = true;
            entry.scheduled = true;
            entry.handle.set_flags(lt::torrent_flags::upload_mode);
            entry.handle.resume();
            if (!entry.availabilityPrompted) {
                entry.availabilityPrompted = true;
                emit torrentAvailabilityQuestion(it.key(), QString::fromStdString(status.name),
                    status.distributed_copies, status.num_peers);
            }
        }

        if (entry.availabilityWaiting && !entry.availabilityOverride &&
            status.has_metadata && status.list_peers > 0 && wholeFileAvailable(status)) {
            entry.availabilityWaiting = false;
            entry.availabilityPrompted = false;
            entry.scheduled = false;
            entry.handle.unset_flags(lt::torrent_flags::upload_mode);
            entry.handle.resume();
            emit torrentStatusChanged(it.key(), QStringLiteral("Availability complete — starting"));
            scheduleTorrents();
            status = entry.handle.status();
        }

        const qint64 downloaded = status.total_done;
        if (entry.lastProgressTime == 0) {
            entry.lastProgressTime = now;
            entry.lastProgressBytes = downloaded;
        } else if (downloaded > entry.lastProgressBytes) {
            entry.lastProgressBytes = downloaded;
            entry.lastProgressTime = now;
            entry.stalledNotified = false;
        } else if (!entry.userPaused && !entry.seedStopped && status.state == lt::torrent_status::downloading &&
                   status.num_peers > 0 && now - entry.lastProgressTime >= 300) {
            if (!entry.stalledNotified) {
                entry.stalledNotified = true;
                emit torrentStalled(it.key(), static_cast<int>(now - entry.lastProgressTime));
            }
        }

        if (status.is_seeding && entry.seedStartTime == 0) entry.seedStartTime = now;
        if (status.is_seeding && shouldStopSeeding(status) && !entry.seedStopped) {
            entry.seedStopped = true;
            entry.scheduled = false;
            entry.handle.pause();
            emit torrentStatusChanged(it.key(), QStringLiteral("Seeding stopped by policy"));
        }

        emit torrentProgress(it.key(), static_cast<int>(status.progress_ppm / 10000),
            status.total_done, status.total_wanted, status.download_rate,
            status.upload_rate, status.num_peers);
        emit torrentHealthChanged(it.key(), status.announcing_to_trackers, status.announcing_to_dht,
                                 status.list_peers, status.connect_candidates);
        emit torrentStatusChanged(it.key(), stateText(entry, status));

        if (writeDatabase) persistStatus(it.key(), status);

        if (status.is_finished && !entry.completedNotified) {
            entry.completedNotified = true;
            emit torrentCompleted(it.key());
        }
    }
    scheduleTorrents();
}