#pragma once
#include <QHash>
#include <QMainWindow>
#include <QSystemTrayIcon>

class QLabel; class QLineEdit; class QPushButton; class QTableWidget; class QCloseEvent; class QCheckBox;
class QMenu; class DownloadManager; class TorrentEngine;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void addDownload();
    void pauseSelected();
    void cancelSelected();
    void removeSelected();
    void openSelected();
    void showFromTray();
    void exitFromTray();
private:
    int selectedRow() const;
    QString selectedId() const;
    void setStatus(const QString &id, const QString &status);
    void setupTray();
    int rowForId(const QString &id) const;
    QLineEdit *urlEdit_; QPushButton *addButton_; QPushButton *pauseButton_;
    QPushButton *cancelButton_; QPushButton *removeButton_; QPushButton *openButton_; QTableWidget *downloadsTable_;
    QLabel *statusLabel_; DownloadManager *downloadManager_; TorrentEngine *torrentEngine_;
    QSystemTrayIcon *trayIcon_; QMenu *trayMenu_;
    QHash<QString, QString> paths_; QHash<QString, int> rows_; bool reallyQuit_ = false;
};