#pragma once
#include <QMainWindow>
class QLineEdit;
class QPushButton;
class QTableWidget;
class DownloadManager;
class MainWindow final : public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(QWidget *parent=nullptr);
private slots:
 void addDownload();
private:
 QLineEdit *urlEdit_;
 QPushButton *addButton_;
 QTableWidget *downloadsTable_;
 DownloadManager *downloadManager_;
};
