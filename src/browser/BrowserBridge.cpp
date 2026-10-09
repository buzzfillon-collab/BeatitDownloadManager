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

    // QLocalSocket is a byte stream: one write on the client is not guaranteed
    // to arrive as one readyRead event. Use a newline delimiter rather than
    // parsing arbitrary partial chunks as complete JSON.
    const qsizetype newline = pending.indexOf('\n');
    if (newline < 0) {
        socket->setProperty(kPendingProperty, pending);
        return;
    }

    const QByteArray message = pending.left(newline).trimmed();
    const auto document = QJsonDocument::fromJson(message);
    if (!document.isObject()) {
        sendJsonLine(socket, QJsonObject{{"ok", false}, {"error", "invalid-json"}});
        socket->disconnectFromServer();
        return;
    }
    socket->setProperty(kPendingProperty, QVariant());
    processMessage(socket, document.object());
}
void BrowserBridge::processMessage(QLocalSocket *socket, const QJsonObject &message) {
    const QString url = message.value(QStringLiteral("url")).toString().trimmed();
    const QString title = message.value(QStringLiteral("title")).toString().trimmed();
    const QString kind = message.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        sendJsonLine(socket, QJsonObject{{"ok", false}, {"error", "invalid-url"}});
        socket->disconnectFromServer(); return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = recentCaptures_.value(url, 0);
    if (last > 0 && now - last < 15000) {
        sendJsonLine(socket, QJsonObject{{"ok", true}, {"duplicate", true}});
        socket->disconnectFromServer();
        return;
    }
    recentCaptures_[url] = now;
    emit captureRequested(url, title, kind);
    sendJsonLine(socket, QJsonObject{{"ok", true}});
    socket->disconnectFromServer();
}