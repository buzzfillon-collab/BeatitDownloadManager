#include "BrowserBridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>
namespace { constexpr auto kServerName = "BeatitBrowserBridge"; constexpr auto kMaxMessageBytes = 1024 * 1024; }
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
    if (socket->bytesAvailable() > kMaxMessageBytes) { socket->disconnectFromServer(); return; }
    const auto document = QJsonDocument::fromJson(socket->readAll());
    if (!document.isObject()) {
        socket->write(QJsonDocument(QJsonObject{{"ok", false}, {"error", "invalid-json"}}).toJson(QJsonDocument::Compact));
        socket->disconnectFromServer(); return;
    }
    processMessage(socket, document.object());
}
void BrowserBridge::processMessage(QLocalSocket *socket, const QJsonObject &message) {
    const QString url = message.value(QStringLiteral("url")).toString().trimmed();
    const QString title = message.value(QStringLiteral("title")).toString().trimmed();
    const QString kind = message.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        socket->write(QJsonDocument(QJsonObject{{"ok", false}, {"error", "invalid-url"}}).toJson(QJsonDocument::Compact));
        socket->disconnectFromServer(); return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = recentCaptures_.value(url, 0);
    if (last > 0 && now - last < 15000) {
        socket->write(QJsonDocument(QJsonObject{{"ok", true}, {"duplicate", true}})
                          .toJson(QJsonDocument::Compact));
        socket->disconnectFromServer();
        return;
    }
    recentCaptures_[url] = now;
    emit captureRequested(url, title, kind);
    socket->write(QJsonDocument(QJsonObject{{"ok", true}}).toJson(QJsonDocument::Compact));
    socket->disconnectFromServer();
}