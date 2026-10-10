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
#include <condition_variable>
#include <deque>
#include <memory>
#include <algorithm>
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
    bool retryable = true;
    bool rangeUnsupported = false;
    bool splitRequested = false;
    qint64 bytes = 0;
    QString error;
};

bool retryableHttpStatus(long status) {
    return status == 408 || status == 425 || status == 429 ||
           status == 500 || status == 502 || status == 503 || status == 504;
}

void interruptibleBackoff(int milliseconds, const HttpDownloader *owner) {
    const int quantumMs = 100;
    for (int elapsed = 0; elapsed < milliseconds && !owner->isCancelRequested(); elapsed += quantumMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(qMin(quantumMs, milliseconds - elapsed)));
}
struct SegmentContext {
    HttpDownloader *owner{};
    QFile *file{};
    qint64 expected{};
    qint64 written{};
    qint64 globalBase{};
    std::atomic<qint64> *aggregate{};
    std::atomic<qint64> *lastReportBytes{};
    std::atomic<qint64> *lastReportMs{};
    std::atomic_bool *splitRequested{};
    std::atomic<qint64> *liveWritten{};
    std::atomic<qint64> *lastProgressMs{};
};
size_t segmentWrite(char *ptr,size_t size,size_t nmemb,void *userdata) {
    auto *ctx=static_cast<SegmentContext*>(userdata);
    const qint64 bytes=static_cast<qint64>(size*nmemb);
    const qint64 n=ctx->file->write(ptr,bytes);
    if(n<=0) return 0;
    ctx->written+=n;
    if (ctx->liveWritten) ctx->liveWritten->store(ctx->written);
    if (ctx->lastProgressMs) {
        ctx->lastProgressMs->store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
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
    auto *ctx=static_cast<SegmentContext*>(clientp);
    return (ctx->owner->isCancelRequested() ||
            (ctx->splitRequested && ctx->splitRequested->load())) ? 1 : 0;
}
SegmentResult fetchSegment(HttpDownloader *owner,const QString &url,const QString &path,qint64 first,qint64 last,
                           std::atomic<qint64> *aggregate, std::atomic<qint64> *lastReportBytes,
                           std::atomic<qint64> *lastReportMs, std::atomic_bool *splitRequested,
                           std::atomic<qint64> *liveWritten, std::atomic<qint64> *lastProgressMs) {
    SegmentResult out;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){out.error="Unable to create segment";return out;}
    CURL *curl=curl_easy_init();
    if(!curl){out.error="libcurl initialization failed";return out;}
    if (liveWritten) liveWritten->store(0);
    if (lastProgressMs) lastProgressMs->store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    SegmentContext ctx{owner,&file,last-first+1,0,first,aggregate,lastReportBytes,lastReportMs,
                       splitRequested,liveWritten,lastProgressMs};
    const QByteArray range=QStringLiteral("%1-%2").arg(first).arg(last).toUtf8();
    curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
    applyProxy(curl, owner);
    curl_easy_setopt(curl,CURLOPT_RANGE,range.constData());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,owner->userAgent().toUtf8().constData());
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
    // Progress counts bytes from in-flight attempts. The caller subtracts this
    // amount when a failed/paused attempt is discarded, so retries and resumes
    // report retained bytes rather than cumulative bytes ever received.
    out.bytes=ctx.written;
    if(owner->isCancelRequested()){out.cancelled=true;return out;}
    if(splitRequested && splitRequested->load()) { out.splitRequested=true; return out; }
    if(code!=CURLE_OK){
        out.error=QString::fromUtf8(curl_easy_strerror(code));
        out.retryable = code != CURLE_URL_MALFORMAT && code != CURLE_UNSUPPORTED_PROTOCOL &&
                        code != CURLE_NOT_BUILT_IN;
        return out;
    }
    if(response!=206){
        if(response==200){
            out.rangeUnsupported=true;
            out.retryable=false;
            out.error=QStringLiteral("Server did not honor HTTP Range for segmented download.");
        } else {
            out.retryable=retryableHttpStatus(response);
            out.error=QStringLiteral("HTTP %1 while downloading a segment.").arg(response);
        }
        return out;
    }
    if(ctx.written!=ctx.expected){
        out.error=QStringLiteral("Segment size mismatch.");
        out.retryable=true;
        return out;
    }
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
void HttpDownloader::setUserAgent(const QString &userAgent) {
    userAgent_ = userAgent.trimmed().isEmpty() ? QStringLiteral("BeatitDownloadManager/0.1 beta") : userAgent.trimmed();
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
        curl_easy_setopt(curl,CURLOPT_USERAGENT,userAgent_.toUtf8().constData());
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

        struct SegmentPiece { qint64 first; qint64 last; QString path; };
        struct SegmentJob {
            qint64 first{};
            qint64 last{};
            QString path;
            std::atomic_bool splitRequested{false};
            std::atomic<qint64> written{0};
            std::atomic<qint64> startedMs{0};
            std::atomic<qint64> lastProgressMs{0};
        };
        const auto nowMs=[] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        };

        std::deque<std::shared_ptr<SegmentJob>> pendingJobs;
        std::vector<SegmentPiece> completedPieces;
        std::vector<QString> adaptiveFiles;
        std::vector<std::shared_ptr<SegmentJob>> activeJobs;
        std::mutex schedulerMutex;
        std::condition_variable schedulerCv;
        std::atomic_bool workerFailed{false};
        std::atomic_bool rangeUnsupported{false};
        std::atomic<qint64> aggregateDone{0};
        std::atomic<qint64> lastReportBytes{0};
        std::atomic<qint64> lastReportMs{nowMs()};
        std::atomic<quint64> adaptiveId{0};
        std::mutex resultMutex;
        QString error;
        std::atomic_bool schedulerDone{false};

        for(int i=0;i<bounds.size();++i) {
            const qint64 first=bounds[i].first;
            const qint64 last=bounds[i].second;
            const qint64 expected=last-first+1;
            const QString path=segmentFiles[i];
            const qint64 size=QFileInfo::exists(path)?QFileInfo(path).size():0;
            if(size==expected) {
                completedPieces.push_back({first,last,path});
                aggregateDone.fetch_add(expected);
            } else {
                if(size>0) QFile::remove(path);
                auto job=std::make_shared<SegmentJob>();
                job->first=first; job->last=last; job->path=path;
                pendingJobs.push_back(job);
            }
        }
        schedulerDone=pendingJobs.empty();
        lastReportBytes.store(aggregateDone.load());

        const auto makeAdaptivePath=[&](qint64 first,qint64 last) {
            const quint64 id=adaptiveId.fetch_add(1);
            const QString path=partPath+QStringLiteral(".adaptive.%1.%2.%3")
                .arg(first).arg(last).arg(id);
            QFile::remove(path);
            adaptiveFiles.push_back(path);
            return path;
        };
        const auto makeJob=[&](qint64 first,qint64 last,const QString &path) {
            auto job=std::make_shared<SegmentJob>();
            job->first=first; job->last=last; job->path=path;
            return job;
        };

        const int workerCount=connections;
        std::vector<std::future<void>> workers;
        workers.reserve(workerCount);
        for(int worker=0;worker<workerCount;++worker) {
            workers.push_back(std::async(std::launch::async,[&,worker] {
                Q_UNUSED(worker);
                for(;;) {
                    std::shared_ptr<SegmentJob> job;
                    {
                        std::unique_lock<std::mutex> lock(schedulerMutex);
                        schedulerCv.wait(lock,[&] {
                            return isCancelRequested() || workerFailed.load() ||
                                   !pendingJobs.empty() || activeJobs.empty();
                        });
                        if(isCancelRequested() || workerFailed.load()) break;
                        if(pendingJobs.empty()) {
                            if(activeJobs.empty()) {
                                schedulerDone=true;
                                schedulerCv.notify_all();
                                break;
                            }
                            continue;
                        }
                        job=pendingJobs.front();
                        pendingJobs.pop_front();
                        job->startedMs.store(nowMs());
                        job->lastProgressMs.store(job->startedMs.load());
                        activeJobs.push_back(job);
                    }

                    SegmentResult result;
                    for(int attempt=0;attempt<5&&!isCancelRequested()&&!workerFailed.load();++attempt) {
                        job->splitRequested.store(false);
                        result=fetchSegment(this,url,job->path,job->first,job->last,
                                            &aggregateDone,&lastReportBytes,&lastReportMs,
                                            &job->splitRequested,&job->written,&job->lastProgressMs);
                        if(result.ok || result.splitRequested) break;
                        if(result.bytes > 0) {
                            const qint64 retained=aggregateDone.fetch_sub(result.bytes)-result.bytes;
                            lastReportBytes.store(retained);
                            lastReportMs.store(nowMs());
                        }
                        if(result.cancelled||!result.retryable) break;
                        if(attempt<4) interruptibleBackoff(500*(1<<attempt),this);
                    }

                    {
                        std::lock_guard<std::mutex> lock(schedulerMutex);
                        auto it=std::find(activeJobs.begin(),activeJobs.end(),job);
                        if(it!=activeJobs.end()) activeJobs.erase(it);

                        if(result.splitRequested && !isCancelRequested() && !workerFailed.load()) {
                            const qint64 received=qBound<qint64>(0,job->written.load(),job->last-job->first+1);
                            if(received>0) {
                                completedPieces.push_back({job->first,job->first+received-1,job->path});
                            } else {
                                QFile::remove(job->path);
                            }
                            const qint64 tailFirst=job->first+received;
                            if(tailFirst<=job->last) {
                                if(received==0) {
                                    const qint64 mid=job->first+(job->last-job->first)/2;
                                    pendingJobs.push_back(makeJob(job->first,mid,makeAdaptivePath(job->first,mid)));
                                    pendingJobs.push_back(makeJob(mid+1,job->last,makeAdaptivePath(mid+1,job->last)));
                                } else {
                                    const qint64 tailLength=job->last-tailFirst+1;
                                    if(tailLength>=4LL*1024*1024) {
                                        const qint64 mid=tailFirst+(job->last-tailFirst)/2;
                                        pendingJobs.push_back(makeJob(tailFirst,mid,makeAdaptivePath(tailFirst,mid)));
                                        pendingJobs.push_back(makeJob(mid+1,job->last,makeAdaptivePath(mid+1,job->last)));
                                    } else {
                                        pendingJobs.push_back(makeJob(tailFirst,job->last,makeAdaptivePath(tailFirst,job->last)));
                                    }
                                }
                            }
                        } else if(result.ok) {
                            completedPieces.push_back({job->first,job->last,job->path});
                        } else if(!result.cancelled && !isCancelRequested() && !workerFailed.load()) {
                            {
                                std::lock_guard<std::mutex> errorLock(resultMutex);
                                if(error.isEmpty()) error=result.error;
                            }
                            if(result.rangeUnsupported) rangeUnsupported.store(true);
                            workerFailed.store(true);
                        }
                        if(pendingJobs.empty() && activeJobs.empty()) schedulerDone=true;
                        schedulerCv.notify_all();
                    }
                    if(isCancelRequested() || workerFailed.load()) break;
                }
            }));
        }

        // Watch live workers and split only when one range is materially slower than
        // its peers. The prefix already written by that worker becomes a completed
        // piece; only the unreceived tail is re-queued for another available worker.
        int adaptiveSplitCount=0;
        while(!schedulerDone && !isCancelRequested() && !workerFailed.load() && connections>1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::lock_guard<std::mutex> lock(schedulerMutex);
            if(activeJobs.empty() || adaptiveSplitCount>=16) continue;
            const qint64 now=nowMs();
            qint64 fastestSpeed=0;
            for(const auto &job:activeJobs) {
                const qint64 elapsed=qMax<qint64>(1,now-job->startedMs.load());
                const qint64 speed=job->written.load()*1000/elapsed;
                fastestSpeed=qMax(fastestSpeed,speed);
            }
            std::shared_ptr<SegmentJob> candidate;
            qint64 candidateRemaining=0;
            for(const auto &job:activeJobs) {
                if(job->splitRequested.load()) continue;
                const qint64 length=job->last-job->first+1;
                const qint64 written=qBound<qint64>(0,job->written.load(),length);
                const qint64 remaining=length-written;
                const qint64 elapsed=now-job->startedMs.load();
                if(length<8LL*1024*1024 || remaining<2LL*1024*1024 || elapsed<4000) continue;
                const qint64 speed=written*1000/qMax<qint64>(1,elapsed);
                const bool stalled=now-job->lastProgressMs.load()>=2500;
                const bool muchSlower=(fastestSpeed>0 && speed*100<fastestSpeed*65);
                const bool idleWorker=(activeJobs.size()<static_cast<size_t>(connections) && elapsed>=6000);
                if(!stalled && !muchSlower && !idleWorker) continue;
                if(remaining>candidateRemaining) {
                    candidate=job;
                    candidateRemaining=remaining;
                }
            }
            if(candidate) {
                candidate->splitRequested.store(true);
                ++adaptiveSplitCount;
            }
        }
        schedulerCv.notify_all();
        for(auto &worker:workers) worker.get();

        if(isCancelRequested()) {
            for(const auto &f:adaptiveFiles) QFile::remove(f);
            if(cancelRequested_) {
                for(const auto &f:segmentFiles) QFile::remove(f);
                emit cancelled();
            } else {
                qint64 retained=0;
                for(int i=0;i<bounds.size();++i) {
                    const qint64 expected=bounds[i].second-bounds[i].first+1;
                    if(QFileInfo(segmentFiles[i]).size()==expected) retained+=expected;
                }
                emit paused(retained);
            }
            return;
        }
        if(workerFailed.load()) {
            for(const auto &f:adaptiveFiles) QFile::remove(f);
            if(rangeUnsupported.load()) {
                for(const auto &f:segmentFiles) QFile::remove(f);
                emit failed(QStringLiteral("Server stopped honoring HTTP Range requests during segmented download."));
            } else {
                emit failed(error.isEmpty()?QStringLiteral("A segmented HTTP transfer failed."):error);
            }
            return;
        }

        std::sort(completedPieces.begin(),completedPieces.end(),
                  [](const SegmentPiece &a,const SegmentPiece &b){return a.first<b.first;});
        qint64 nextExpected=0;
        for(const auto &piece:completedPieces) {
            const qint64 pieceSize=piece.last-piece.first+1;
            if(piece.first!=nextExpected || pieceSize<=0 || QFileInfo(piece.path).size()!=pieceSize) {
                for(const auto &f:adaptiveFiles) QFile::remove(f);
                emit failed(QStringLiteral("Adaptive range integrity check failed; the file was not assembled."));
                return;
            }
            nextExpected=piece.last+1;
        }
        if(nextExpected!=total) {
            for(const auto &f:adaptiveFiles) QFile::remove(f);
            emit failed(QStringLiteral("Adaptive range coverage check failed; the file was not assembled."));
            return;
        }

        emit progress(total,total,0);
        QFile out(partPath);
        if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
            for(const auto &f:segmentFiles) QFile::remove(f);
            for(const auto &f:adaptiveFiles) QFile::remove(f);
            emit failed(QStringLiteral("Unable to assemble segmented download."));
            return;
        }
        for(const auto &piece:completedPieces) {
            QFile in(piece.path);
            if(!in.open(QIODevice::ReadOnly)) {
                out.close();
                for(const auto &x:segmentFiles) QFile::remove(x);
                for(const auto &x:adaptiveFiles) QFile::remove(x);
                emit failed(QStringLiteral("Unable to assemble segmented download."));
                return;
            }
            while(!in.atEnd()) {
                const QByteArray block=in.read(1024*1024);
                if((block.isEmpty()&&!in.atEnd()) || out.write(block)!=block.size()) {
                    in.close(); out.close();
                    for(const auto &x:segmentFiles) QFile::remove(x);
                    for(const auto &x:adaptiveFiles) QFile::remove(x);
                    emit failed(QStringLiteral("Unable to assemble segmented download."));
                    return;
                }
            }
            in.close();
        }
        out.close();
        for(const auto &f:segmentFiles) QFile::remove(f);
        for(const auto &f:adaptiveFiles) QFile::remove(f);
        if (!expectedSha256_.isEmpty()) {
            QString actual;
            if (!verifySha256(partPath, expectedSha256_, &actual)) {
                QFile::remove(partPath);
                emit failed(QStringLiteral("SHA-256 checksum mismatch. Expected %1, got %2.").arg(expectedSha256_, actual));
                return;
            }
        }
        if(!QFile::rename(partPath,finalPath)){emit failed(QStringLiteral("Unable to finalize the downloaded file."));return;}
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
        curl_easy_setopt(curl,CURLOPT_USERAGENT,userAgent_.toUtf8().constData());curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
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
