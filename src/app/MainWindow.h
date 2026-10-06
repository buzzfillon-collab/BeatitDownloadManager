#pragma once

#include <QHash>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class DownloadManager;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void addDownload();
    void pauseSelected();
    void cancelSelected();
    void openSelected();

private:
    int selectedRow() const;
    QString selectedId() const;
    void setStatus(const QString &id, const QString &status);

    QLineEdit *urlEdit_;
    QPushButton *addButton_;
    QPushButton *pauseButton_;
    QPushButton *cancelButton_;
    QPushButton *openButton_;
    QTableWidget *downloadsTable_;
    QLabel *statusLabel_;
    DownloadManager *downloadManager_;
    QHash<QString, QString> paths_;
    QHash<QString, int> rows_;
};
