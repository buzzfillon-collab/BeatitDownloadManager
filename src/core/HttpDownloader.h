#pragma once
#include <QObject>
#include <QString>
#include <atomic>

class HttpDownloader final : public QObject {
    Q_OBJECT
public:
    explicit HttpDownloader(QObject *parent = nullptr);
    ~HttpDownloader() override;
    void start(const QString &url, const QString &destination);
    void pause();
    void cancel();
    void setSegments(int count);
    void setBandwidthLimit(qint64 bytesPerSecond);
    void setExpectedSha256(const QString &sha256);
    void setProxy(const QString &host, int port, int type);
    bool isCancelRequested() const noexcept;
    qint64 bandwidthLimit() const noexcept { return bandwidthLimit_.load(); }
    int segmentCount() const noexcept { return segmentCount_.load(); }
    const QString &proxyHost() const noexcept { return proxyHost_; }
    int proxyPort() const noexcept { return proxyPort_; }
    int proxyType() const noexcept { return proxyType_; }
signals:
    void probing();
    void started(const QString &filename, qint64 totalBytes, bool resumable);
    void progress(qint64 downloadedBytes, qint64 totalBytes, qint64 bytesPerSecond);
    void paused(qint64 downloadedBytes);
    void completed(const QString &path);
    void failed(const QString &message);
    void cancelled();
public:
    static bool verifySha256(const QString &path, const QString &expected, QString *actual = nullptr);
private:
    void run(const QString &url, const QString &destination);
    static QString filenameFromUrl(const QString &url);
    static QString humanCurlError(int code);
    std::atomic_bool pauseRequested_{false};
    std::atomic_bool cancelRequested_{false};
    std::atomic_int segmentCount_{4};
    std::atomic<qint64> bandwidthLimit_{0};
    QString expectedSha256_;
    QString proxyHost_;
    int proxyPort_ = 0;
    int proxyType_ = 0;
};