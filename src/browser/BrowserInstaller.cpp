#include "BrowserInstaller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#ifdef Q_OS_WIN
namespace {
constexpr auto kHostName = "com.beatit.download_manager";
constexpr auto kExtensionId = "mhmokbkgppciedcjabjlhkbhipnkfkpb";
constexpr auto kFirefoxExtensionId = "beatit@example.org";
QString manifestDirectory() { return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("native-messaging-hosts")); }
QString nativeHostPath() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("BeatitBrowserHost.exe")); }
}
bool BrowserInstaller::writeManifest(const QString &path, const QString &hostPath, bool firefox, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject manifest{{QStringLiteral("name"),QString::fromLatin1(kHostName)},
                            {QStringLiteral("description"),QStringLiteral("Beatit Download Manager browser integration")},
                            {QStringLiteral("path"),hostPath},{QStringLiteral("type"),QStringLiteral("stdio")}};
    if (firefox) manifest.insert(QStringLiteral("allowed_extensions"), QJsonArray{QString::fromLatin1(kFirefoxExtensionId)});
    else manifest.insert(QStringLiteral("allowed_origins"), QJsonArray{QStringLiteral("chrome-extension://%1/").arg(QString::fromLatin1(kExtensionId))});
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if(error)*error=QStringLiteral("Cannot write native host manifest: %1").arg(file.errorString()); return false; }
    if (file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0) { if(error)*error=QStringLiteral("Cannot write native host manifest"); return false; }
    return true;
}
bool BrowserInstaller::registerHost(const QString &key, const QString &manifestPath) {
    QSettings registry(key, QSettings::NativeFormat);
    registry.setValue(QStringLiteral("."), manifestPath); registry.sync();
    return registry.status() == QSettings::NoError;
}
bool BrowserInstaller::install(QString *error) {
    const QString hostPath=nativeHostPath();
    if(!QFileInfo::exists(hostPath)){if(error)*error="BeatitBrowserHost.exe was not found beside BeatitDownloadManager.exe";return false;}
    const QString dir=manifestDirectory();
    const QString chromiumManifest=QDir(dir).filePath("com.beatit.download_manager.json");
    const QString firefoxManifest=QDir(dir).filePath("com.beatit.download_manager.firefox.json");
    if(!writeManifest(chromiumManifest,hostPath,false,error)||!writeManifest(firefoxManifest,hostPath,true,error)) return false;
    const QStringList chromiumKeys{
        "HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\com.beatit.download_manager",
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.beatit.download_manager",
        "HKEY_CURRENT_USER\\Software\\Chromium\\NativeMessagingHosts\\com.beatit.download_manager",
        "HKEY_CURRENT_USER\\Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\com.beatit.download_manager",
        "HKEY_CURRENT_USER\\Software\\Vivaldi\\NativeMessagingHosts\\com.beatit.download_manager"};
    for(const QString &key:chromiumKeys) registerHost(key,chromiumManifest);
    const QString firefoxKey="HKEY_CURRENT_USER\\Software\\Mozilla\\NativeMessagingHosts\\com.beatit.download_manager";
    if(!registerHost(firefoxKey,firefoxManifest)){if(error)*error="Could not register the Firefox native messaging host";return false;}
    return true;
}
QString BrowserInstaller::status(){QString error;return install(&error)?"Installed":error;}
#else
bool BrowserInstaller::install(QString *error){if(error)*error="Browser native messaging registration is currently Windows-only";return false;}
QString BrowserInstaller::status(){return "Unsupported platform";}
#endif
