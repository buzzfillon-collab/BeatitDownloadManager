#include <cstdio>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
static bool readMessage(QByteArray &payload) {
    quint32 length = 0;
    if (fread(&length, sizeof(length), 1, stdin) != 1) return false;
    if (length == 0 || length > 1024 * 1024) return false;
    payload.resize(static_cast<int>(length));
    return fread(payload.data(), 1, length, stdin) == length;
}
static void writeMessage(const QByteArray &payload) {
    const quint32 length = static_cast<quint32>(payload.size());
    fwrite(&length, sizeof(length), 1, stdout);
    fwrite(payload.constData(), 1, payload.size(), stdout);
    fflush(stdout);
}
int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QCoreApplication app(argc, argv);
    QByteArray payload;
    if (!readMessage(payload)) return 1;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        writeMessage(QJsonDocument(QJsonObject{{"ok", false}, {"error", "invalid-json"}}).toJson(QJsonDocument::Compact));
        return 1;
    }
    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("BeatitBrowserBridge"));
    if (!socket.waitForConnected(750)) {
        const QString appPath = QCoreApplication::applicationDirPath() + QStringLiteral("/BeatitDownloadManager.exe");
        if (QFileInfo::exists(appPath)) {
            QProcess::startDetached(appPath, {QStringLiteral("--hidden")});
            socket.connectToServer(QStringLiteral("BeatitBrowserBridge"));
            socket.waitForConnected(2500);
        }
    }
    if (!socket.isOpen()) {
        writeMessage(QJsonDocument(QJsonObject{{"ok", false}, {"error", "beatit-not-running"}}).toJson(QJsonDocument::Compact));
        return 1;
    }
    // Local sockets are streams, so delimit the JSON request explicitly.
    QByteArray request = payload;
    request.append('\n');
    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(1000)) {
        writeMessage(QJsonDocument(QJsonObject{{"ok", false}, {"error", "beatit-write-failed"}}).toJson(QJsonDocument::Compact));
        return 1;
    }

    QByteArray response;
    while (!response.contains('\n') && response.size() <= 1024 * 1024) {
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(2500)) break;
        response.append(socket.readAll());
    }
    const qsizetype newline = response.indexOf('\n');
    if (newline < 0) {
        writeMessage(QJsonDocument(QJsonObject{{"ok", false}, {"error", "beatit-timeout"}}).toJson(QJsonDocument::Compact));
        return 1;
    }
    writeMessage(response.left(newline).trimmed());
    return 0;
}