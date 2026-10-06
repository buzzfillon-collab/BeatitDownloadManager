#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QVector>
#include <memory>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

class DownloadDatabase;

class TorrentEngine final : public QObject {
    Q_OBJECT
public:
    explicit TorrentEngine(QObject *parent = nullptr);
    ~TorrentEngine() override;

    QString addMagnet(const QString &magnet, const QString &savePath);
    QString addTorrentFile(const QString &path, const QString &savePath);
    void pause(const QString &id);
    void resume(const QString &id);
    void remove(const QString &id, bool deleteFiles = false);
    void saveResumeData();
    void forceRecheck(const QString &id);
    void setFilePriorities(const QString &id, const QVector<int> &priorities);
    QVector<QString> torrentFiles(const QString &id) const;
    QVector<int> filePriorities(const QString &id) const;
    void setSeedingPolicy(int mode, double ratio = 1.0, int minutes = 30);
    int seedingPolicyMode() const { return seedingPolicyMode_; }
    double seedingRatio() const { return seedingRatio_; }
    int seedingMinutes() const { return seedingMinutes_; }

signals:
    void torrentAdded(const QString &id, const QString &name);
    void torrentHistoryRestored(const QString &id, const QString &name, const QString &source,
                                 const QString &status, qint64 downloaded, qint64 total, qint64 updatedAt);
    void torrentRemoved(const QString &id);
    void torrentProgress(const QString &id, int progress, qint64 downloaded, qint64 total,
                         qint64 downloadRate, qint64 uploadRate, int peers);
    void torrentCompleted(const QString &id);
    void torrentError(const QString &id, const QString &error);
    void torrentAvailabilityQuestion(const QString &id, const QString &name, double distributedCopies, int peers);
    void torrentStatusChanged(const QString &id, const QString &status);
    void torrentHealthChanged(const QString &id, bool trackerAnnouncing, bool dhtAnnouncing,
                              int knownPeers, int connectCandidates);
    void torrentStalled(const QString &id, int seconds);

private slots:
    void pollAlerts();
    void restoreResumeData();

private:
    struct TorrentEntry {
        QString id;
        lt::torrent_handle handle;
        bool completedNotified = false;
        bool userPaused = false;
        bool scheduled = false;
        bool availabilityPrompted = false;
        bool availabilityOverride = false;
        bool availabilityWaiting = false;
        bool stalledNotified = false;
        bool seedStopped = false;
        qint64 lastProgressBytes = 0;
        qint64 lastProgressTime = 0;
        qint64 seedStartTime = 0;
    };

    void scheduleTorrents();
    void saveOneResume(const QString &id, const lt::torrent_handle &handle);
    void persistStatus(const QString &id, const lt::torrent_status &status);
    bool wholeFileAvailable(const lt::torrent_status &status) const;
    QString stateText(const TorrentEntry &entry, const lt::torrent_status &status) const;
    bool shouldStopSeeding(const lt::torrent_status &status) const;
    void persistSettings();
    void loadSettings();

    std::unique_ptr<lt::session> session_;
    QTimer alertTimer_;
    QHash<QString, TorrentEntry> torrents_;
    int nextId_ = 1;
    int maxActiveDownloads_ = 5;
    QString resumeDirectory_;
    std::unique_ptr<DownloadDatabase> database_;
    qint64 lastResumeSave_ = 0;
    int seedingPolicyMode_ = 0; // 0=ratio, 1=time, 2=forever, 3=stop immediately
    double seedingRatio_ = 1.0;
    int seedingMinutes_ = 30;
};