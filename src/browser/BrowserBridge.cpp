#include "BrowserBridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>
#include <QVariant>
namespace {
constexpr auto kServerName = "BeatitBrowserBridge";
constexpr auto kMaxMessageBytes = 1024 * 1024;
constexpr auto kPendingProperty = "beatit.pending-message";

void sendJsonLine(QLocalSocket *socket, const QJsonObject &object) {
    if (!socket) return;
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    socket->write(bytes);
    socket->flush();
}
}
BrowserBridge::BrowserBridge(QObject *parent) : QObject(parent), server_(new QLocalServer(this)) {
    connect(server_, &QLocalServer::newConnection, this, &BrowserBridge::acceptConnection);
}
bool BrowserBridge::start() {
    const QString name = QString::fromLatin1(kServerName);
    if (server_->listen(name)) return true;
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(150)) return false;
    QLocalServer::removeServer(name);
    return server_->listen(name);
}
void BrowserBridge::acceptConnection() {
    while (auto *socket = server_->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, &BrowserBridge::readSocket);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}
void BrowserBridge::readSocket() {
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) return;
    QByteArray pending = socket->property(kPendingProperty).toByteArray();
    pending.append(socket->readAll());
    if (pending.size() > kMaxMessageBytes) {
        sendJsonLine(socket, QJsonObject{{"ok", false}, {"error", "message-too-large"}});
        socket->disconnectFromServer();
        return;
    }
    const qsizetype newline = pending.indexOf('\n');
    if (newline < 0) {
        socket->setProperty(kPendingProperty, pending);
        return;
    }
    const auto document = QJsonDocument::fromJson(pending.left(newline).trimmed());
    if (!document.isObject()) {
        sendJsonLine(socket, QJsonObject{{"ok", false}, {"error", "invalid-json"}});
        socket->disconnectFromServer();
        return;
    }
    socket->setProperty(kPendingProperty, QVariant());
    processMessage(socket, document.object());
}
void BrowserBridge::processMessage(QLocalSocket *socket, const QJsonObject &message) {
    const QString action = message.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("status")) {
        sendJsonLine(socket, QJsonObject{{"ok", true}, {"status", QStringLiteral("connected")}});
        socket->disconnectFromServer();
        return;
    }
    const QString url = message.value(QStringLiteral("url")).toString().trimmed();
    const QString title = message.value(QStringLiteral("title")).toString().trimmed();
    const QString kind = message.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
    QString pageUrl = message.value(QStringLiteral("pageUrl")).toString().trimmed();
    QString userAgent = message.value(QStringLiteral("userAgent")).toString().trimmed();
    if (pageUrl.size() > 8192 || !(QUrl(pageUrl).scheme() == QStringLiteral("http") || QUrl(pageUrl).scheme() == QStringLiteral("https")))
        pageUrl.clear();
    if (userAgent.size() > 1024) userAgent.clear();
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        sendJsonLine(socket, QJsonObject{{"ok", false}, {"error", "invalid-url"}});
        socket->disconnectFromServer();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = recentCaptures_.value(url, 0);
    if (last > 0 && now - last < 15000) {
        sendJsonLine(socket, QJsonObject{{"ok", true}, {"duplicate", true}});
        socket->disconnectFromServer();
        return;
    }
    recentCaptures_[url] = now;
    emit captureRequested(url, title, kind, pageUrl, userAgent);
    sendJsonLine(socket, QJsonObject{{"ok", true}});
    socket->disconnectFromServer();
}