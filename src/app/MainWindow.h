#pragma once
#include <QHash>
#include <QStringList>
#include <QMainWindow>
#include <QSystemTrayIcon>

class QLabel; class QLineEdit; class QPushButton; class QTableWidget; class QCloseEvent; class QCheckBox;
class QMenu; class DownloadManager; class Scheduler; class TorrentEngine; class BrowserBridge; class YtDlpManager; class QProcess;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void handleExternalCommand(const QStringList &arguments);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void addDownload();
    void pauseSelected();
    void resumeSelected();
    void cancelSelected();
    void removeSelected();
    void openSelected();
    void recheckSelected();
    void showSettings();
    void checkForUpdates();
    void showFromTray();
    void exitFromTray();
    void handleBrowserCapture(const QString &url, const QString &title, const QString &kind);
    void startYtDlpDownload(const QString &url, bool youtube, const QString &kind, bool audioOnly = false);
    void chooseVideoFormat(const QString &url, bool youtube, const QString &kind);
    void filterDownloads(const QString &filter);
    void configureChecksumSelected();
    void showSelectedDetails();
    void showSelectedProperties();
    void showLinkExtractor();
    void runScheduledSiteGrabber();
    void showQueueManager();
private:
    int selectedRow() const;
    QString selectedId() const;
    void setStatus(const QString &id, const QString &status);
    void setupTray();
    int rowForId(const QString &id) const;
    QLineEdit *urlEdit_; QPushButton *addButton_; QPushButton *pauseButton_; QPushButton *resumeButton_;
    QPushButton *cancelButton_; QPushButton *removeButton_; QPushButton *openButton_; QPushButton *recheckButton_; QTableWidget *downloadsTable_;
    QLabel *statusLabel_; DownloadManager *downloadManager_; TorrentEngine *torrentEngine_; Scheduler *scheduler_;
    QSystemTrayIcon *trayIcon_; QMenu *trayMenu_;
    BrowserBridge *browserBridge_;
    YtDlpManager *ytDlpManager_;
    QProcess *ytDlpProcess_ = nullptr;
    QStringList ytDlpPendingArgs_;
    QString ytDlpPendingUrl_;
    QString ytDlpPendingKind_;
    bool ytDlpRetryAfterUpdate_ = false;
    bool ytDlpPendingAudioOnly_ = false;
    QString ytDlpFormat_;
    QHash<QString, QString> paths_; QHash<QString, int> rows_;
    QHash<QString, qint64> currentDownloadSpeed_;
    QHash<QString, qint64> currentUploadSpeed_;
    QHash<QString, qint64> downloadedBytes_;
    QHash<QString, qint64> totalBytes_;
    QHash<QString, qint64> startedAt_;
    bool reallyQuit_ = false;
    bool siteGrabberScheduleRunning_ = false;
};