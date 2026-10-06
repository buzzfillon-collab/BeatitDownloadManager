#include "HttpDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <algorithm>
#include <curl/curl.h>

namespace {
struct CurlContext {
    HttpDownloader *owner{};
    QFile *file{};
    qint64 written{};
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    qint64 lastBytes{};
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *ctx = static_cast<CurlContext *>(userdata);
    const qint64 bytes = static_cast<qint64>(size * nmemb);
    if (!ctx->file || !ctx->file->isOpen()) return 0;

    const qint64 written = ctx->file->write(ptr, bytes);
    if (written > 0) {
        ctx->written += written;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - ctx->lastReport).count();
        if (elapsed >= 500) {
            const qint64 delta = ctx->written - ctx->lastBytes;
            const qint64 speed = elapsed > 0 ? (delta * 1000) / elapsed : 0;
            emit ctx->owner->progress(ctx->written, 0, speed);
            ctx->lastReport = now;
            ctx->lastBytes = ctx->written;
        }
    }
    return written > 0 ? static_cast<size_t>(written) : 0;
}

int progressCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto *ctx = static_cast<CurlContext *>(clientp);
    return ctx->owner->property("cancelRequested").toBool() ? 1 : 0;
}
}

HttpDownloader::HttpDownloader(QObject *parent) : QObject(parent) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    setProperty("cancelRequested", false);
}

HttpDownloader::~HttpDownloader() {
    curl_global_cleanup();
}

QString HttpDownloader::filenameFromUrl(const QString &url) {
    QString name = QUrl(url).fileName();
    if (name.isEmpty()) name = QStringLiteral("download");
    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    return name;
}

QString HttpDownloader::humanCurlError(int code) {
    return QString::fromUtf8(curl_easy_strerror(static_cast<CURLcode>(code)));
}

void HttpDownloader::start(const QString &url, const QString &destination) {
    pauseRequested_ = false;
    cancelRequested_ = false;
    setProperty("cancelRequested", false);
    run(url, destination);
}

void HttpDownloader::pause() {
    pauseRequested_ = true;
    setProperty("cancelRequested", true);
}

void HttpDownloader::cancel() {
    cancelRequested_ = true;
    setProperty("cancelRequested", true);
}

void HttpDownloader::run(const QString &url, const QString &destination) {
    emit probing();

    QDir dir(destination);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit failed(QStringLiteral("Unable to create download directory."));
        return;
    }

    const QString finalPath = dir.filePath(filenameFromUrl(url));
    const QString partPath = finalPath + QStringLiteral(".part");
    qint64 existing = QFileInfo::exists(partPath) ? QFileInfo(partPath).size() : 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        emit failed(QStringLiteral("Unable to initialize libcurl."));
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BeatitDownloadManager/0.1 beta");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (existing > 0) {
        const QByteArray range = QStringLiteral("%1-").arg(existing).toUtf8();
        curl_easy_setopt(curl, CURLOPT_RANGE, range.constData());
    }

    QFile file(partPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        curl_easy_cleanup(curl);
        emit failed(QStringLiteral("Unable to open the partial file for writing."));
        return;
    }

    CurlContext ctx{this, &file, existing};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    emit started(QFileInfo(finalPath).fileName(), existing, existing > 0);

    const CURLcode result = curl_easy_perform(curl);
    file.flush();
    file.close();

    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_off_t contentLength = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
    curl_easy_cleanup(curl);

    if (cancelRequested_) {
        emit cancelled();
        return;
    }
    if (pauseRequested_) {
        emit paused(QFileInfo(partPath).size());
        return;
    }
    if (result != CURLE_OK) {
        emit failed(QStringLiteral("Download failed: %1").arg(humanCurlError(result)));
        return;
    }
    if (responseCode >= 400) {
        emit failed(QStringLiteral("Server returned HTTP %1.").arg(responseCode));
        return;
    }

    const qint64 actual = QFileInfo(partPath).size();
    if (contentLength >= 0 && existing == 0 && actual != contentLength) {
        emit failed(QStringLiteral("Download ended early: received %1 of %2 bytes.")
                        .arg(actual).arg(static_cast<qint64>(contentLength)));
        return;
    }

    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath)) {
        emit failed(QStringLiteral("A file with the same name already exists and could not be replaced."));
        return;
    }
    if (!QFile::rename(partPath, finalPath)) {
        emit failed(QStringLiteral("Unable to finalize the downloaded file."));
        return;
    }

    emit progress(actual, actual, 0);
    emit completed(finalPath);
}
