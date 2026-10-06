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
    bool isCancelRequested() const noexcept;
signals:
    void probing();
    void started(const QString &filename, qint64 totalBytes, bool resumable);
    void progress(qint64 downloadedBytes, qint64 totalBytes, qint64 bytesPerSecond);
    void paused(qint64 downloadedBytes);
    void completed(const QString &path);
    void failed(const QString &message);
    void cancelled();
private:
    void run(const QString &url, const QString &destination);
    static QString filenameFromUrl(const QString &url);
    static QString humanCurlError(int code);
    std::atomic_bool pauseRequested_{false};
    std::atomic_bool cancelRequested_{false};
    std::atomic_int segmentCount_{4};
};