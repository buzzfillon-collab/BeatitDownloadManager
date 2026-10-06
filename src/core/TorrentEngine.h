#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>
#include <memory>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

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

signals:
    void torrentAdded(const QString &id, const QString &name);
    void torrentProgress(const QString &id, int progress, qint64 downloaded, qint64 total,
                         qint64 downloadRate, qint64 uploadRate, int peers);
    void torrentCompleted(const QString &id);
    void torrentError(const QString &id, const QString &error);

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
    };

    void scheduleTorrents();
    void saveOneResume(const QString &id, const lt::torrent_handle &handle);

    std::unique_ptr<lt::session> session_;
    QTimer alertTimer_;
    QHash<QString, TorrentEntry> torrents_;
    int nextId_ = 1;
    int maxActiveDownloads_ = 5;
    QString resumeDirectory_;
};