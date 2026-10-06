#include "HttpDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <future>
#include <curl/curl.h>
#include <mutex>

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
};
size_t segmentWrite(char *ptr,size_t size,size_t nmemb,void *userdata) {
    auto *ctx=static_cast<SegmentContext*>(userdata);
    const qint64 bytes=static_cast<qint64>(size*nmemb);
    const qint64 n=ctx->file->write(ptr,bytes);
    if(n<=0) return 0;
    ctx->written+=n;
    return static_cast<size_t>(n);
}
int segmentProgress(void *clientp,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<SegmentContext*>(clientp)->owner->isCancelRequested()?1:0;
}
SegmentResult fetchSegment(HttpDownloader *owner,const QString &url,const QString &path,qint64 first,qint64 last) {
    SegmentResult out;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){out.error="Unable to create segment";return out;}
    CURL *curl=curl_easy_init();
    if(!curl){out.error="libcurl initialization failed";return out;}
    SegmentContext ctx{owner,&file,last-first+1,0,first};
    const QByteArray range=QStringLiteral("%1-%2").arg(first).arg(last).toUtf8();
    curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
    curl_easy_setopt(curl,CURLOPT_RANGE,range.constData());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
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

void HttpDownloader::setSegments(int count) { segmentCount_=qBound(1,count,16); }

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
        QFile::remove(finalPath);
        if(QFile::rename(partPath,finalPath)){emit started(QFileInfo(finalPath).fileName(),total,true);emit progress(total,total,0);emit completed(finalPath);return;}
        existing=0;
    }

    const int segments=qMax(1,segmentCount_.load());
    if(total>0&&ranges&&segments>1&&existing==0) {
        emit started(QFileInfo(finalPath).fileName(),total,true);
        QVector<QString> files; QVector<QPair<qint64,qint64>> bounds;
        const qint64 chunk=(total+segments-1)/segments;
        for(int i=0;i<segments;i++){
            const qint64 first=i*chunk;
            if(first>=total)break;
            const qint64 last=qMin(total-1,first+chunk-1);
            const QString sp=partPath+QStringLiteral(".%1").arg(i);
            files.push_back(sp);bounds.push_back({first,last});
        }
        QVector<std::future<SegmentResult>> jobs;
        for(int i=0;i<bounds.size();i++)
            jobs.push_back(std::async(std::launch::async,[&,i]{return fetchSegment(this,url,files[i],bounds[i].first,bounds[i].second);}));
        qint64 done=0;
        bool segmentFailed=false; QString error;
        for(int i=0;i<jobs.size();i++){
            const auto result=jobs[i].get();
            if(result.cancelled){for(const auto &f:files)QFile::remove(f);if(pauseRequested_){emit paused(done);}else emit cancelled();return;}
            if(!result.ok){segmentFailed=true;error=result.error;break;}
            done+=result.bytes;emit progress(done,total,0);
        }
        if(segmentFailed){for(const auto &f:files)QFile::remove(f);emit failed(error);return;}
        QFile out(partPath);
        if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)){for(const auto &f:files)QFile::remove(f);emit failed("Unable to assemble segmented download.");return;}
        for(const auto &f:files){QFile in(f);if(!in.open(QIODevice::ReadOnly)||out.write(in.readAll())<0){out.close();for(const auto &x:files)QFile::remove(x);emit failed("Unable to assemble segmented download.");return;}in.close();QFile::remove(f);}
        out.close();
        if(!QFile::rename(partPath,finalPath)){emit failed("Unable to finalize the downloaded file.");return;}
        emit progress(total,total,0);emit completed(finalPath);return;
    }

    auto perform=[&](bool resume,qint64 offset)->CURLcode{
        CURL *curl=curl_easy_init();if(!curl)return CURLE_FAILED_INIT;
        QFile file(partPath);
        if(!file.open(resume?(QIODevice::WriteOnly|QIODevice::Append):(QIODevice::WriteOnly|QIODevice::Truncate))){curl_easy_cleanup(curl);return CURLE_WRITE_ERROR;}
        CurlContext ctx{this,&file,0,total,offset};
        curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
        curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
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
    if(!QFile::rename(partPath,finalPath)){emit failed("Unable to finalize the downloaded file.");return;}
    emit progress(actual,total>0?total:actual,0);emit completed(finalPath);
}
