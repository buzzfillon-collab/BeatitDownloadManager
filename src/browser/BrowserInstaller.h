#pragma once
#include <QString>
class BrowserInstaller final {
public:
    static bool install(QString *error = nullptr);
    static QString status();
private:
    static bool writeManifest(const QString &path, const QString &hostPath, bool firefox, QString *error);
    static bool registerHost(const QString &key, const QString &manifestPath);
};
