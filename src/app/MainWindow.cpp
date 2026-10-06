#include "MainWindow.h"
#include "../core/DownloadManager.h"
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
 urlEdit_(new QLineEdit(this)), addButton_(new QPushButton("Add Download",this)),
 downloadsTable_(new QTableWidget(this)), downloadManager_(new DownloadManager(this)) {
 setWindowTitle("Beatit Download Manager");
 resize(1000,600);
 urlEdit_->setPlaceholderText("https://example.com/file.zip");
 auto *bar=new QHBoxLayout; bar->addWidget(urlEdit_,1); bar->addWidget(addButton_);
 downloadsTable_->setColumnCount(5);
 downloadsTable_->setHorizontalHeaderLabels({"File","Status","Progress","Speed","URL"});
 downloadsTable_->horizontalHeader()->setStretchLastSection(true);
 downloadsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
 downloadsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
 auto *layout=new QVBoxLayout; layout->addLayout(bar); layout->addWidget(downloadsTable_);
 auto *central=new QWidget(this); central->setLayout(layout); setCentralWidget(central);
 connect(addButton_,&QPushButton::clicked,this,&MainWindow::addDownload);
 connect(urlEdit_,&QLineEdit::returnPressed,this,&MainWindow::addDownload);
 connect(downloadManager_,&DownloadManager::taskAdded,this,[this](const QString &id,const QString &url){
  int row=downloadsTable_->rowCount(); downloadsTable_->insertRow(row);
  downloadsTable_->setItem(row,0,new QTableWidgetItem(url.section('/',-1)));
  downloadsTable_->setItem(row,1,new QTableWidgetItem("Queued"));
  downloadsTable_->setItem(row,2,new QTableWidgetItem("0%"));
  downloadsTable_->setItem(row,3,new QTableWidgetItem("-"));
  downloadsTable_->setItem(row,4,new QTableWidgetItem(url));
  downloadsTable_->item(row,0)->setData(Qt::UserRole,id);
 });
}
void MainWindow::addDownload() {
 const QString url=urlEdit_->text().trimmed();
 if(url.isEmpty()) { QMessageBox::warning(this,"Invalid URL","Enter a download URL."); return; }
 if(!url.startsWith("http://") && !url.startsWith("https://")) {
  QMessageBox::warning(this,"Unsupported URL","HTTP and HTTPS are supported by the first engine."); return;
 }
 downloadManager_->addUrl(url); urlEdit_->clear();
}
