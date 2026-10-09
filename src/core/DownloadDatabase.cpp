#include "DownloadDatabase.h"
#include <QDir>
#include <QStandardPaths>
#include <sqlite3.h>\n#include <QByteArray>

namespace {
sqlite3 *asDb(void *p) { return static_cast<sqlite3 *>(p); }
bool execSql(sqlite3 *db, const char *sql) {
    char *error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
    return rc == SQLITE_OK;
}
bool hasColumn(sqlite3 *db, const char *table, const char *column) {
    const QByteArray pragma = QByteArray("PRAGMA table_info(") + table + ");";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, pragma.constData(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (name && QByteArray(name) == column) { found = true; break; }
    }
    sqlite3_finalize(stmt);
    return found;
}
bool ensureColumn(sqlite3 *db, const char *name, const char *definition) {
    if (hasColumn(db, "downloads", name)) return true;
    const QByteArray sql = QByteArray("ALTER TABLE downloads ADD COLUMN ") + name + " " + definition + ";";
    return execSql(db, sql.constData());
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
        d.updatedAt = sqlite3_column_int64(s, 10);
        d.sha256 = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 11)));
        d.verification = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 12)));
        d.category = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 13)));
        d.description = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 14)));
        d.userAgent = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 15)));
        d.queueId = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 16)));
        d.connectionCount = sqlite3_column_int(s, 17);
        d.proxyType = sqlite3_column_int(s, 18);
        d.proxyHost = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 19)));
        d.proxyPort = sqlite3_column_int(s, 20);
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

bool DownloadDatabase::initialize() {
    sqlite3 *db = asDb(db_);
    if (!execSql(db, "BEGIN IMMEDIATE;")) return false;
    bool ok = execSql(db,
        "CREATE TABLE IF NOT EXISTS downloads ("
        "id TEXT PRIMARY KEY,type TEXT NOT NULL,source TEXT NOT NULL,"
        "destination TEXT NOT NULL,filename TEXT NOT NULL,status TEXT NOT NULL,"
        "total_bytes INTEGER NOT NULL DEFAULT 0,downloaded_bytes INTEGER NOT NULL DEFAULT 0,"
        "speed INTEGER NOT NULL DEFAULT 0,error TEXT NOT NULL DEFAULT '',"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "sha256 TEXT NOT NULL DEFAULT '',verification TEXT NOT NULL DEFAULT '');");
    // Version 3 is additive: existing downloads and history are retained.
    ok = ok && ensureColumn(db, "sha256", "TEXT NOT NULL DEFAULT ''");
    ok = ok && ensureColumn(db, "verification", "TEXT NOT NULL DEFAULT ''");
    ok = ok && ensureColumn(db, "category", "TEXT NOT NULL DEFAULT 'Other'");
    ok = ok && ensureColumn(db, "description", "TEXT NOT NULL DEFAULT ''");
    ok = ok && ensureColumn(db, "user_agent", "TEXT NOT NULL DEFAULT ''");
    ok = ok && ensureColumn(db, "queue_id", "TEXT NOT NULL DEFAULT 'main'");
    ok = ok && ensureColumn(db, "connection_count", "INTEGER NOT NULL DEFAULT 0");
    ok = ok && ensureColumn(db, "proxy_type", "INTEGER NOT NULL DEFAULT -1");
    ok = ok && ensureColumn(db, "proxy_host", "TEXT NOT NULL DEFAULT ''");
    ok = ok && ensureColumn(db, "proxy_port", "INTEGER NOT NULL DEFAULT 0");
    ok = ok && execSql(db, "CREATE INDEX IF NOT EXISTS downloads_status ON downloads(status);");
    ok = ok && execSql(db, "CREATE INDEX IF NOT EXISTS downloads_queue_status ON downloads(queue_id,status);");
    ok = ok && execSql(db, "PRAGMA user_version=3;");
    if (ok) return execSql(db, "COMMIT;");
    execSql(db, "ROLLBACK;");
    return false;
}

bool DownloadDatabase::save(const PersistedDownload &d) {
    if (!open()) return false;
    const char *sql =
        "INSERT INTO downloads(id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error,sha256,verification,category,description,user_agent,queue_id,connection_count,proxy_type,proxy_host,proxy_port)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "type=excluded.type,source=excluded.source,destination=excluded.destination,"
        "filename=excluded.filename,status=excluded.status,total_bytes=excluded.total_bytes,"
        "downloaded_bytes=excluded.downloaded_bytes,speed=excluded.speed,error=excluded.error,"
        "sha256=excluded.sha256,verification=excluded.verification,category=excluded.category,"
        "description=excluded.description,user_agent=excluded.user_agent,queue_id=excluded.queue_id,"
        "connection_count=excluded.connection_count,proxy_type=excluded.proxy_type,"
        "proxy_host=excluded.proxy_host,proxy_port=excluded.proxy_port,updated_at=unixepoch();";
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
    sqlite3_bind_text(s,11,d.sha256.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,12,d.verification.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,13,d.category.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,14,d.description.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,15,d.userAgent.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,16,d.queueId.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(s,17,d.connectionCount);
    sqlite3_bind_int(s,18,d.proxyType);
    sqlite3_bind_text(s,19,d.proxyHost.toUtf8().constData(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(s,20,d.proxyPort);
    const bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}

QVector<PersistedDownload> DownloadDatabase::loadActive() const {
    if (!db_) return {};
    return readRows(asDb(db_),
        "SELECT id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error,updated_at,sha256,verification,category,description,user_agent,queue_id,connection_count,proxy_type,proxy_host,proxy_port "
        "FROM downloads WHERE status IN ('Queued','Paused','Downloading','Failed') "
        "ORDER BY updated_at ASC;");
}

QVector<PersistedDownload> DownloadDatabase::loadHistory() const {
    if (!db_) return {};
    return readRows(asDb(db_),
        "SELECT id,type,source,destination,filename,status,total_bytes,downloaded_bytes,speed,error,updated_at,sha256,verification "
        "FROM downloads ORDER BY updated_at DESC;");
}

QString DownloadDatabase::path() const { return path_; }
void DownloadDatabase::close() {
    if (db_) {
        sqlite3_close(asDb(db_));
        db_ = nullptr;
    }
}

bool DownloadDatabase::remove(const QString &id) {
    if (!open()) return false;
    sqlite3_stmt *s = nullptr;
    const char *sql = "DELETE FROM downloads WHERE id=?;";
    if (sqlite3_prepare_v2(asDb(db_), sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(s, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}
