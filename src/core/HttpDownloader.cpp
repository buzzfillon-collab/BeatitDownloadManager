#include "HttpDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <future>
#include <thread>
#include <curl/curl.h>
#include <mutex>
#include <vector>

namespace {
struct CurlContext {
    HttpDownloader *owner{};
    QFile *file{};
    qint64 written{};
    qint64 total{-1};
    qint64 base{};
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    qint64 lastBytes{};
};
size_t writeCallback(char *ptr,size_t size,size_t nmemb,void *userdata) {
    auto *ctx=static_cast<CurlContext*>(userdata);
    const qint64 bytes=static_cast<qint64>(size*nmemb);
    if(!ctx->file||!ctx->file->isOpen()) return 0;
    const qint64 n=ctx->file->write(ptr,bytes);
    if(n<=0) return 0;
    ctx->written+=n;
    const auto now=std::chrono::steady_clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-ctx->lastReport).count();
    if(elapsed>=500) {
        const qint64 delta=ctx->written-ctx->lastBytes;
        const qint64 speed=elapsed?(delta*1000)/elapsed:0;
        emit ctx->owner->progress(ctx->base+ctx->written,ctx->total,speed);
        ctx->lastReport=now; ctx->lastBytes=ctx->written;
    }
    return static_cast<size_t>(n);
}
int progressCallback(void *clientp,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<CurlContext*>(clientp)->owner->isCancelRequested()?1:0;
}
void applyProxy(CURL *curl, const HttpDownloader *owner) {
    if (!owner || owner->proxyType() == 0 || owner->proxyHost().isEmpty()) return;
    curl_easy_setopt(curl, CURLOPT_PROXY, owner->proxyHost().toUtf8().constData());
    if (owner->proxyPort() > 0)
        curl_easy_setopt(curl, CURLOPT_PROXYPORT, static_cast<long>(owner->proxyPort()));
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE,
                     owner->proxyType() == 2 ? CURLPROXY_SOCKS5_HOSTNAME : CURLPROXY_HTTP);
}

struct HeaderContext { qint64 contentRangeTotal=-1; bool acceptsRanges=false; };
size_t headerCallback(char *buffer,size_t size,size_t nitems,void *userdata) {
    auto *ctx=static_cast<HeaderContext*>(userdata);
    const QString line=QString::fromUtf8(buffer,static_cast<int>(size*nitems)).trimmed();
    static const QRegularExpression rx(QStringLiteral(R"(^Content-Range:\s*bytes\s+\d+-\d+/(\d+|\*))"),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto m=rx.match(line);
    if(m.hasMatch()&&m.captured(1)!=QStringLiteral("*")) ctx->contentRangeTotal=m.captured(1).toLongLong();
    static const QRegularExpression ar(QStringLiteral(R"(^Accept-Ranges:\s*bytes\s*$)"), QRegularExpression::CaseInsensitiveOption);
    if(ar.match(line).hasMatch()) ctx->acceptsRanges=true;
    return size*nitems;
}
}

HttpDownloader::HttpDownloader(QObject *parent):QObject(parent){
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}
HttpDownloader::~HttpDownloader() = default;
QString HttpDownloader::filenameFromUrl(const QString &url){
    QString name=QUrl(url).fileName();
    if(name.isEmpty()) name=QStringLiteral("download");
    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")),QStringLiteral("_"));
    return name;
}
QString HttpDownloader::humanCurlError(int code){return QString::fromUtf8(curl_easy_strerror(static_cast<CURLcode>(code)));}
void HttpDownloader::start(const QString &url,const QString &destination){
    pauseRequested_=false; cancelRequested_=false; run(url,destination);
}
void HttpDownloader::pause(){pauseRequested_=true;}
void HttpDownloader::cancel(){cancelRequested_=true;}
bool HttpDownloader::isCancelRequested() const noexcept{return cancelRequested_.load()||pauseRequested_.load();}

namespace {
struct SegmentResult {
    bool ok = false;
    bool cancelled = false;
    qint64 bytes = 0;
    QString error;
};
struct SegmentContext {
    HttpDownloader *owner{};
    QFile *file{};
    qint64 expected{};
    qint64 written{};
    qint64 globalBase{};
    std::atomic<qint64> *aggregate{};
    std::atomic<qint64> *lastReportBytes{};
    std::atomic<qint64> *lastReportMs{};
};
size_t segmentWrite(char *ptr,size_t size,size_t nmemb,void *userdata) {
    auto *ctx=static_cast<SegmentContext*>(userdata);
    const qint64 bytes=static_cast<qint64>(size*nmemb);
    const qint64 n=ctx->file->write(ptr,bytes);
    if(n<=0) return 0;
    ctx->written+=n;
    if (ctx->aggregate) {
        const qint64 aggregateNow = ctx->aggregate->fetch_add(n) + n;
        const qint64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        qint64 lastMs = ctx->lastReportMs->load();
        if (nowMs - lastMs >= 500 && ctx->lastReportMs->compare_exchange_strong(lastMs, nowMs)) {
            const qint64 previousBytes = ctx->lastReportBytes->exchange(aggregateNow);
            const qint64 elapsed = qMax<qint64>(1, nowMs - lastMs);
            const qint64 speed = (aggregateNow - previousBytes) * 1000 / elapsed;
            emit ctx->owner->progress(aggregateNow, -1, speed);
        }
    }
    return static_cast<size_t>(n);
}
int segmentProgress(void *clientp,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<SegmentContext*>(clientp)->owner->isCancelRequested()?1:0;
}
SegmentResult fetchSegment(HttpDownloader *owner,const QString &url,const QString &path,qint64 first,qint64 last,
                           std::atomic<qint64> *aggregate, std::atomic<qint64> *lastReportBytes,
                           std::atomic<qint64> *lastReportMs) {
    SegmentResult out;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){out.error="Unable to create segment";return out;}
    CURL *curl=curl_easy_init();
    if(!curl){out.error="libcurl initialization failed";return out;}
    SegmentContext ctx{owner,&file,last-first+1,0,first,aggregate,lastReportBytes,lastReportMs};
    const QByteArray range=QStringLiteral("%1-%2").arg(first).arg(last).toUtf8();
    curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
    applyProxy(curl, owner);
    curl_easy_setopt(curl,CURLOPT_RANGE,range.constData());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    const qint64 limit = owner->bandwidthLimit();
    if (limit > 0) curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE,
                                    static_cast<curl_off_t>(qMax<qint64>(1, limit / qMax(1, owner->segmentCount()))));
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,segmentWrite);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&ctx);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,segmentProgress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&ctx);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    const CURLcode code=curl_easy_perform(curl);
    long response=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&response);
    file.flush(); file.close(); curl_easy_cleanup(curl);
    if(owner->isCancelRequested()){out.cancelled=true;return out;}
    if(code!=CURLE_OK){out.error=QString::fromUtf8(curl_easy_strerror(code));return out;}
    if(response!=206){out.error=QStringLiteral("Server did not honor HTTP Range for segmented download.");return out;}
    if(ctx.written!=ctx.expected){out.error=QStringLiteral("Segment size mismatch.");return out;}
    out.ok=true;out.bytes=ctx.written;return out;
}
}

void HttpDownloader::setSegments(int count) { segmentCount_=qBound(1,count,8); }
void HttpDownloader::setBandwidthLimit(qint64 bytesPerSecond) { bandwidthLimit_=qMax<qint64>(0, bytesPerSecond); }
void HttpDownloader::setExpectedSha256(const QString &sha256) { expectedSha256_=sha256.trimmed().toLower(); }
void HttpDownloader::setProxy(const QString &host, int port, int type) {
    proxyHost_ = host.trimmed();
    proxyPort_ = qBound(0, port, 65535);
    proxyType_ = qBound(0, type, 2);
}
bool HttpDownloader::verifySha256(const QString &path, const QString &expected, QString *actual) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return false;
    const QString got = QString::fromLatin1(hash.result().toHex()).toLower();
    if (actual) *actual = got;
    return expected.isEmpty() || got == expected.trimmed().toLower();
}

void HttpDownloader::run(const QString &url,const QString &destination){
    emit probing();
    QDir dir(destination);
    if(!dir.exists()&&!dir.mkpath(".")){emit failed("Unable to create download directory.");return;}
    const QString finalPath=dir.filePath(filenameFromUrl(url));
    const QString partPath=finalPath+".part";
    qint64 existing=QFileInfo::exists(partPath)?QFileInfo(partPath).size():0;

    auto probe=[&](qint64 &total,bool &ranges)->bool{
        CURL *curl=curl_easy_init(); if(!curl)return false;
        curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
        applyProxy(curl, this);
        curl_easy_setopt(curl,CURLOPT_NOBODY,1L);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);
        curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
        HeaderContext headers;
        curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,headerCallback);
        curl_easy_setopt(curl,CURLOPT_HEADERDATA,&headers);
        const CURLcode headerCode = curl_easy_perform(curl);
        long headerResponse=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&headerResponse);
        curl_off_t headerLength=-1; curl_easy_getinfo(curl,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&headerLength);
        total=headerLength>=0?static_cast<qint64>(headerLength):-1;
        ranges=headers.acceptsRanges;
        curl_easy_cleanup(curl);
        return headerCode==CURLE_OK&&headerResponse>=200&&headerResponse<400;
    };

    qint64 total=-1; bool ranges=false;
    probe(total,ranges);
    if(existing>0&&total>=0&&existing>=total){
        if (!expectedSha256_.isEmpty()) {
            QString actual;
            if (!verifySha256(partPath, expectedSha256_, &actual)) {
                QFile::remove(partPath);
                emit failed(QStringLiteral("SHA-256 checksum mismatch. Expected %1, got %2.").arg(expectedSha256_, actual));
                return;
            }
        }
        QFile::remove(finalPath);
        if(QFile::rename(partPath,finalPath)){emit started(QFileInfo(finalPath).fileName(),total,true);emit progress(total,total,0);emit completed(finalPath);return;}
        existing=0;
    }

    const int connections=qBound(1,segmentCount_.load(),8);
    const qint64 minimumRangeSize=1024LL*1024;
    const qint64 maxRangesBySize=qMax<qint64>(1,(total+minimumRangeSize-1)/minimumRangeSize);
    // Keep the on-disk range layout stable so changing the connection setting does not
    // invalidate completed segments when a paused download is resumed.
    const int rangeCount=qMax(1,static_cast<int>(qMin<qint64>(32,maxRangesBySize)));

    QVector<QString> segmentFiles;
    QVector<QPair<qint64,qint64>> bounds;
    bool hasSegmentState=false;
    if(total>0&&ranges) {
        const qint64 chunk=(total+rangeCount-1)/rangeCount;
        for(int i=0;i<rangeCount;i++){
            const qint64 first=i*chunk;
            if(first>=total)break;
            const qint64 last=qMin(total-1,first+chunk-1);
            const QString sp=partPath+QStringLiteral(".%1").arg(i);
            segmentFiles.push_back(sp);
            bounds.push_back({first,last});
            if(QFileInfo::exists(sp)) hasSegmentState=true;
        }
    }

    if(total>0&&ranges&&existing==0&&(connections>1||hasSegmentState)) {
        emit started(QFileInfo(finalPath).fileName(),total, true);

        QVector<int> pending;
        qint64 done=0;
        for(int i=0;i<bounds.size();++i) {
            const qint64 expected=bounds[i].second-bounds[i].first+1;
            const qint64 size=QFileInfo(segmentFiles[i]).exists()?QFileInfo(segmentFiles[i]).size():0;
            if(size==expected) done+=expected;
            else {
                if(size>0) QFile::remove(segmentFiles[i]);
                pending.push_back(i);
            }
        }

        std::atomic_int nextJob{0};
        std::atomic_bool workerFailed{false};
        std::atomic_bool rangeUnsupported{false};
        std::atomic<qint64> aggregateDone{done};
        std::atomic<qint64> lastReportBytes{done};
        std::atomic<qint64> lastReportMs{
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()};
        std::mutex resultMutex;
        QString error;

        const int workerCount=qMin(connections,pending.size());
        std::vector<std::future<void>> workers;
        workers.reserve(workerCount);
        for(int worker=0;worker<workerCount;++worker) {
            workers.push_back(std::async(std::launch::async,[&,worker] {
                Q_UNUSED(worker);
                while(!workerFailed.load()&&!isCancelRequested()) {
                    const int slot=nextJob.fetch_add(1);
                    if(slot>=pending.size()) break;
                    const int index=pending[slot];
                    const auto [first,last]=bounds[index];
                    SegmentResult result;
                    for(int attempt=0;attempt<3&&!isCancelRequested();++attempt) {
                        result=fetchSegment(this,url,segmentFiles[index],first,last,
                                            &aggregateDone,&lastReportBytes,&lastReportMs);
                        if(result.ok||result.cancelled) break;
                        if(attempt<2) std::this_thread::sleep_for(std::chrono::seconds(1<<attempt));
                    }
                    if(result.cancelled) break;
                    if(!result.ok) {
                        {
                            std::lock_guard<std::mutex> lock(resultMutex);
                            if(error.isEmpty()) error=result.error;
                        }
                        if(result.error.contains(QStringLiteral("did not honor HTTP Range")))
                            rangeUnsupported.store(true);
                        workerFailed.store(true);
                        break;
                    }
                }
            }));
        }
        for(auto &worker:workers) worker.get();

        if(isCancelRequested()) {
            if(cancelRequested_) {
                for(const auto &f:segmentFiles) QFile::remove(f);
                emit cancelled();
            } else {
                emit paused(aggregateDone.load());
            }
            return;
        }
        if(workerFailed.load()) {
            if(rangeUnsupported.load()) {
                for(const auto &f:segmentFiles) QFile::remove(f);
                emit failed(QStringLiteral("Server stopped honoring HTTP Range requests during segmented download."));
            } else {
                emit failed(error.isEmpty()?QStringLiteral("A segmented HTTP transfer failed."):error);
            }
            return;
        }

        emit progress(total,total,0);
        QFile out(partPath);
        if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
            for(const auto &f:segmentFiles) QFile::remove(f);
            emit failed(QStringLiteral("Unable to assemble segmented download."));
            return;
        }
        for(const auto &f:segmentFiles) {
            QFile in(f);
            if(!in.open(QIODevice::ReadOnly)) {
                out.close();
                for(const auto &x:segmentFiles) QFile::remove(x);
                emit failed(QStringLiteral("Unable to assemble segmented download."));
                return;
            }
            while(!in.atEnd()) {
                const QByteArray block=in.read(1024*1024);
                if(block.isEmpty()&&!in.atEnd() || out.write(block)!=block.size()) {
                    in.close(); out.close();
                    for(const auto &x:segmentFiles) QFile::remove(x);
                    emit failed(QStringLiteral("Unable to assemble segmented download."));
                    return;
                }
            }
            in.close();
            QFile::remove(f);
        }
        out.close();
        if (!expectedSha256_.isEmpty()) {
            QString actual;
            if (!verifySha256(partPath, expectedSha256_, &actual)) {
                QFile::remove(partPath);
                emit failed(QStringLiteral("SHA-256 checksum mismatch. Expected %1, got %2.").arg(expectedSha256_, actual));
                return;
            }
        }
        if(!QFile::rename(partPath,finalPath)){emit failed("Unable to finalize the downloaded file.");return;}
        emit completed(finalPath);
        return;
    }

    auto perform=[&](bool resume,qint64 offset)->CURLcode{
        CURL *curl=curl_easy_init();if(!curl)return CURLE_FAILED_INIT;
        QFile file(partPath);
        if(!file.open(resume?(QIODevice::WriteOnly|QIODevice::Append):(QIODevice::WriteOnly|QIODevice::Truncate))){curl_easy_cleanup(curl);return CURLE_WRITE_ERROR;}
        CurlContext ctx{this,&file,0,total,offset};
        curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
        applyProxy(curl, this);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
        curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
        const qint64 limit = bandwidthLimit_.load();
        if (limit > 0) curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(limit));
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCallback);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&ctx);
        curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progressCallback);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&ctx);curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
        if(resume&&offset>0){const QByteArray range=QStringLiteral("%1-").arg(offset).toUtf8();curl_easy_setopt(curl,CURLOPT_RANGE,range.constData());}
        const CURLcode result=curl_easy_perform(curl);long response=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&response);
        curl_off_t length=-1;curl_easy_getinfo(curl,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&length);
        if(ctx.total<0&&length>=0)ctx.total=resume?offset+length:length;
        file.flush();file.close();
        if(resume&&response==200&&result==CURLE_OK){curl_easy_cleanup(curl);QFile::remove(partPath);return CURLE_RANGE_ERROR;}
        if(result==CURLE_OK&&response>=400){curl_easy_cleanup(curl);return CURLE_HTTP_RETURNED_ERROR;}
        curl_easy_cleanup(curl);return result;
    };

    emit started(QFileInfo(finalPath).fileName(),total,existing>0);
    CURLcode result=perform(existing>0,existing);
    if(result==CURLE_RANGE_ERROR){existing=0;result=perform(false,0);}
    if(cancelRequested_){emit cancelled();return;}
    if(pauseRequested_){emit paused(QFileInfo(partPath).size());return;}
    if(result!=CURLE_OK){emit failed(humanCurlError(result));return;}
    const qint64 actual=QFileInfo(partPath).size();
    if(QFileInfo::exists(finalPath)&&!QFile::remove(finalPath)){emit failed("A file with the same name already exists.");return;}
    if (!expectedSha256_.isEmpty()) {
        QString actualHash;
        if (!verifySha256(partPath, expectedSha256_, &actualHash)) {
            QFile::remove(partPath);
            emit failed(QStringLiteral("SHA-256 checksum mismatch. Expected %1, got %2.").arg(expectedSha256_, actualHash));
            return;
        }
    }
    if(!QFile::rename(partPath,finalPath)){emit failed("Unable to finalize the downloaded file.");return;}
    emit progress(actual,total>0?total:actual,0);emit completed(finalPath);
}
