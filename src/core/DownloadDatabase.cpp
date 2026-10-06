#include "DownloadDatabase.h"
#include <QDir>
#include <QStandardPaths>
#include <sqlite3.h>

namespace {
sqlite3 *asDb(void *p) { return static_cast<sqlite3 *>(p); }
void execSql(sqlite3 *db, const char *sql) {
    char *error = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
}
QVector<PersistedDownload> readRows(sqlite3 *db, const char *sql) {
    QVector<PersistedDownload> out;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        PersistedDownload d;
        d.id = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 0)));
        d.type = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 1)));
        d.source = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 2)));
        d.destination = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 3)));
        d.filename = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 4)));
        d.status = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 5)));
        d.totalBytes = sqlite3_column_int64(s, 6);
        d.downloadedBytes = sqlite3_column_int64(s, 7);
        d.speed = sqlite3_column_int64(s, 8);
        d.error = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 9)));
        out.push_back(std::move(d));
    }
    sqlite3_finalize(s);
    return out;
}
}

DownloadDatabase::DownloadDatabase() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    path_ = QDir(dir).filePath(QStringLiteral("beatit.db"));
}
DownloadDatabase::~DownloadDatabase() { close(); }

bool DownloadDatabase::open() {
    if (db_) return true;
    sqlite3 *db = nullptr;
    if (sqlite3_open16(path_.utf16(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    db_ = db;
    execSql(db, "PRAGMA journal_mode=WAL;");
    execSql(db, "PRAGMA synchronous=NORMAL;");
    initialize();
    return true;
}

void DownloadDatabase::initialize() {
    execSql(asDb(db_),
        "CREATE TABLE IF NOT EXISTS downloads ("
        "id TEXT PRIMARY KEY,type TEXT NOT NULL,source TEXT NOT NULL,"
        "destination TEXT NOT NULL,filename TEXT NOT NULL,status TEXT NOT NULL,"
        "total_bytes INTEGER NOT NULL DEFAULT 0,downloaded_bytes INTEGER NOT NULL DEFAULT 0,"
        "speed INTEGER NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '',"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()));");
    execSql(asDb(db_), "CREATE INDEX IF NOT EXISTS downloads_status ON downloads(status);");
}

bool DownloadDatabase::save(const PersistedDownload &d) {
    if (!open()) return false;
    const char *sql =
        "INSERT INTO downloads(id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error)"
        " VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "type=excluded.type,source=excluded.source,destination=excluded.destination,"
        "filename=excluded.filename,status=excluded.status,total_bytes=excluded.total_bytes,"
        "downloaded_bytes=excluded.downloaded_bytes,speed=excluded.speed,error=excluded.error,"
        "updated_at=unixepoch();";
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(asDb(db_), sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(s,1,d.id.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,d.type.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,d.source.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,d.destination.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,5,d.filename.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,6,d.status.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(s,7,d.totalBytes);
    sqlite3_bind_int64(s,8,d.downloadedBytes);
    sqlite3_bind_int64(s,9,d.speed);
    sqlite3_bind_text(s,10,d.error.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}

QVector<PersistedDownload> DownloadDatabase::loadActive() const {
    if (!db_) return {};
    return readRows(asDb(db_),
        "SELECT id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error "
        "FROM downloads WHERE status IN ('Queued','Paused','Downloading','Failed') "
        "ORDER BY updated_at ASC;");
}

QVector<PersistedDownload> DownloadDatabase::loadHistory() const {
    if (!db_) return {};
    return readRows(asDb(db_),
        "SELECT id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error "
        "FROM downloads ORDER BY updated_at DESC;");
}

QString DownloadDatabase::path() const { return path_; }
void DownloadDatabase::close() {
    if (db_) {
        sqlite3_close(asDb(db_));
        db_ = nullptr;
    }
}