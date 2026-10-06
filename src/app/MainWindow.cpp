#include "MainWindow.h"
#include "../core/DownloadManager.h"
#include "../core/TorrentEngine.h"
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDateTime>
#include <QMessageBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QString formatBytes(qint64 b){if(b<1024)return QStringLiteral("%1 B").arg(b);double v=b;const QStringList u{"KB","MB","GB","TB"};int i=-1;do{v/=1024.0;++i;}while(v>=1024.0&&i+1<u.size());return QStringLiteral("%1 %2").arg(v,0,'f',v>=100?0:1).arg(u[i]);}
QString formatSpeed(qint64 b){return b<=0?QStringLiteral("—"):formatBytes(b)+QStringLiteral("/s");}
void tintRow(QTableWidget *table, int row, const QColor &tone) {
    for (int col = 0; col < table->columnCount(); ++col)
        if (auto *item = table->item(row, col)) item->setBackground(tone);
}
void accentRow(QTableWidget *table, int row, bool torrent) {
    tintRow(table, row, QColor(torrent ? "#10271e" : "#102238"));
    if (auto *file = table->item(row, 0))
        file->setForeground(QColor(torrent ? "#62d89b" : "#67a9ff"));
    if (auto *status = table->item(row, 1))
        status->setForeground(QColor(torrent ? "#62d89b" : "#67a9ff"));
}
QIcon beatitIcon(){QPixmap x(64,64);x.fill(Qt::transparent);QPainter p(&x);p.setRenderHint(QPainter::Antialiasing);p.setBrush(QColor("#7c5cff"));p.setPen(Qt::NoPen);p.drawRoundedRect(4,4,56,56,16,16);p.setPen(QPen(Qt::white,7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawLine(20,18,20,46);p.drawLine(20,18,39,18);p.drawLine(20,32,35,32);p.drawLine(20,46,41,46);return QIcon(x);}
}

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),
urlEdit_(new QLineEdit(this)),addButton_(new QPushButton(QStringLiteral("＋ Add"),this)),
pauseButton_(new QPushButton(QStringLiteral("Pause"),this)),cancelButton_(new QPushButton(QStringLiteral("Cancel"),this)),
removeButton_(new QPushButton(QStringLiteral("Remove"),this)),
openButton_(new QPushButton(QStringLiteral("Open"),this)),downloadsTable_(new QTableWidget(this)),
statusLabel_(new QLabel(QStringLiteral("Ready"),this)),downloadManager_(new DownloadManager(this)),
torrentEngine_(new TorrentEngine(this)),trayIcon_(new QSystemTrayIcon(this)),trayMenu_(new QMenu(this)){
    setWindowTitle("Beatit");setWindowIcon(beatitIcon());setMinimumSize(1050,650);resize(1180,720);
    auto*c=new QWidget(this);auto*r=new QVBoxLayout(c);r->setContentsMargins(28,24,28,20);r->setSpacing(14);
    auto*t=new QLabel("Beatit",c);t->setObjectName("title");auto*s=new QLabel("Fast, free, open-source downloading.",c);s->setObjectName("subtitle");
    auto*in=new QHBoxLayout;
    urlEdit_->setPlaceholderText("Paste URL or magnet link…");urlEdit_->setClearButtonEnabled(true);
    in->addWidget(urlEdit_,1);in->addWidget(addButton_);
    auto*fileButton=new QPushButton(QStringLiteral("＋ Torrent"),c);in->addWidget(fileButton);
    downloadsTable_->setColumnCount(6);downloadsTable_->setHorizontalHeaderLabels({"FILE","STATUS","PROGRESS","SPEED","SOURCE","LAST ACTIVITY"});
    downloadsTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);downloadsTable_->horizontalHeader()->setSectionResizeMode(5,QHeaderView::ResizeToContents);downloadsTable_->setSortingEnabled(true);downloadsTable_->horizontalHeader()->setSortIndicator(5,Qt::DescendingOrder);downloadsTable_->sortItems(5,Qt::DescendingOrder);
    downloadsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);downloadsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    downloadsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);downloadsTable_->setShowGrid(false);downloadsTable_->verticalHeader()->setVisible(false);
    auto*a=new QHBoxLayout;a->addWidget(pauseButton_);a->addWidget(cancelButton_);a->addWidget(removeButton_);a->addWidget(openButton_);a->addStretch();a->addWidget(statusLabel_);
    r->addWidget(t);r->addWidget(s);r->addLayout(in);r->addWidget(downloadsTable_,1);r->addLayout(a);setCentralWidget(c);
    setStyleSheet("QMainWindow{background:#0d1117;color:#e6edf3;} QLabel#title{font-size:34px;font-weight:700;color:white;} QLabel#subtitle{font-size:14px;color:#8b949e;margin-top:-8px;} QLineEdit{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:12px 14px;color:#f0f6fc;font-size:14px;} QLineEdit:focus{border:1px solid #7c5cff;} QPushButton{background:#21262d;border:1px solid #30363d;border-radius:10px;padding:10px 16px;color:#f0f6fc;font-weight:600;} QPushButton:hover{background:#30363d;} QPushButton#primary{background:#7c5cff;border-color:#8d72ff;} QTableWidget{background:#11161d;border:1px solid #21262d;border-radius:14px;gridline-color:transparent;color:#e6edf3;font-size:13px;} QTableWidget::item{padding:10px;border-bottom:1px solid #1d232c;} QTableWidget::item:selected{background:#2d2452;color:white;} QHeaderView::section{background:#161b22;border:none;padding:9px;color:#8b949e;font-size:11px;font-weight:700;}");
    addButton_->setObjectName("primary");setupTray();

    connect(addButton_,&QPushButton::clicked,this,&MainWindow::addDownload);
    connect(urlEdit_,&QLineEdit::returnPressed,this,&MainWindow::addDownload);
    connect(pauseButton_,&QPushButton::clicked,this,&MainWindow::pauseSelected);
    connect(cancelButton_,&QPushButton::clicked,this,&MainWindow::cancelSelected);
    connect(removeButton_,&QPushButton::clicked,this,&MainWindow::removeSelected);
    connect(openButton_,&QPushButton::clicked,this,&MainWindow::openSelected);
    connect(fileButton,&QPushButton::clicked,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,"Open torrent",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),"Torrent files (*.torrent)");
        if(!p.isEmpty()) torrentEngine_->addTorrentFile(p,QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    });

    connect(downloadManager_,&DownloadManager::taskAdded,this,[this](const QString&id,const QString&url){
        const int row=downloadsTable_->rowCount();downloadsTable_->insertRow(row);
        auto*f=new QTableWidgetItem(QUrl(url).fileName().isEmpty()?"download":QUrl(url).fileName());f->setData(Qt::UserRole,id);
        downloadsTable_->setItem(row,0,f);downloadsTable_->setItem(row,1,new QTableWidgetItem("Queued"));
        downloadsTable_->setItem(row,2,new QTableWidgetItem("0%"));downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));
        downloadsTable_->setItem(row,4,new QTableWidgetItem(url));
        auto *date=new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))); date->setData(Qt::UserRole,QDateTime::currentSecsSinceEpoch()); downloadsTable_->setItem(row,5,date);
        accentRow(downloadsTable_, row, false);
    });
    connect(downloadManager_,&DownloadManager::taskRestored,this,[this](const QString&id,const QString&url,const QString&filename,const QString&status,qint64 done,qint64 total,qint64 updatedAt){
        const int row=downloadsTable_->rowCount();downloadsTable_->insertRow(row);
        auto*f=new QTableWidgetItem(filename);f->setData(Qt::UserRole,id);downloadsTable_->setItem(row,0,f);
        downloadsTable_->setItem(row,1,new QTableWidgetItem(status));downloadsTable_->setItem(row,2,new QTableWidgetItem(total>0?QStringLiteral("%1%").arg(done*100/total):formatBytes(done)));
        downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));downloadsTable_->setItem(row,4,new QTableWidgetItem(url));
        const auto ts=updatedAt>0?updatedAt:QDateTime::currentSecsSinceEpoch(); auto *date=new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))); date->setData(Qt::UserRole,ts); downloadsTable_->setItem(row,5,date);
        accentRow(downloadsTable_, row, false);
    });
    connect(downloadManager_,&DownloadManager::taskStarted,this,[this](const QString&id,const QString&f,qint64 total){
        const int row=rowForId(id);if(row<0)return;downloadsTable_->item(row,0)->setText(f);
        downloadsTable_->item(row,1)->setText("Downloading");downloadsTable_->item(row,2)->setText(total>0?"0%":"Live");
    });
    connect(downloadManager_,&DownloadManager::taskProgress,this,[this](const QString&id,qint64 done,qint64 total,qint64 speed){
        const int row=rowForId(id);if(row<0)return;
        downloadsTable_->item(row,2)->setText(total>0?QStringLiteral("%1%").arg(done*100/total):formatBytes(done));
        downloadsTable_->item(row,3)->setText(formatSpeed(speed));statusLabel_->setText(QStringLiteral("%1 downloaded").arg(formatBytes(done)));
        accentRow(downloadsTable_, row, true);
    });
    connect(downloadManager_,&DownloadManager::taskPaused,this,[this](const QString&id,qint64 done){setStatus(id,"Paused");statusLabel_->setText(QStringLiteral("Paused at %1").arg(formatBytes(done)));});
    connect(downloadManager_,&DownloadManager::taskCompleted,this,[this](const QString&id,const QString&path){paths_[id]=path;setStatus(id,"Completed"); if(const int row=rowForId(id);row>=0){auto *date=downloadsTable_->item(row,5);if(date){const auto ts=QDateTime::currentSecsSinceEpoch();date->setText(QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));date->setData(Qt::UserRole,ts);}}trayIcon_->showMessage("Beatit","Download complete");});
    connect(downloadManager_,&DownloadManager::taskFailed,this,[this](const QString&id,const QString&e){setStatus(id,"Failed");trayIcon_->showMessage("Beatit",e,QSystemTrayIcon::Warning);});
    connect(downloadManager_,&DownloadManager::taskCancelled,this,[this](const QString&id){setStatus(id,"Cancelled");});
    connect(downloadManager_,&DownloadManager::taskRemoved,this,[this](const QString&id){ const int row=rowForId(id); if(row<0)return; downloadsTable_->removeRow(row); statusLabel_->setText("Removed"); });

    connect(torrentEngine_,&TorrentEngine::torrentHistoryRestored,this,[this](const QString&id,const QString&name,const QString&source,const QString&status,qint64 done,qint64 total,qint64 updatedAt){
        const int existingRow=rowForId(id);
        const int row=existingRow>=0?existingRow:downloadsTable_->rowCount();
        if(existingRow<0) downloadsTable_->insertRow(row);
        auto*f=downloadsTable_->item(row,0);
        if(!f){f=new QTableWidgetItem;downloadsTable_->setItem(row,0,f);}
        f->setText(name.isEmpty()?QStringLiteral("Torrent"):name);f->setData(Qt::UserRole,id);
        downloadsTable_->setItem(row,1,new QTableWidgetItem(status));
        downloadsTable_->setItem(row,2,new QTableWidgetItem(total>0?QStringLiteral("%1%").arg(done*100/total):QStringLiteral("0%")));
        downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));
        downloadsTable_->setItem(row,4,new QTableWidgetItem(source.isEmpty()?QStringLiteral("BitTorrent"):source));
        const auto ts=updatedAt>0?updatedAt:QDateTime::currentSecsSinceEpoch();
        auto*date=new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));date->setData(Qt::UserRole,ts);
        downloadsTable_->setItem(row,5,date);accentRow(downloadsTable_,row,true);
    });

    connect(torrentEngine_,&TorrentEngine::torrentRemoved,this,[this](const QString&id){
        const int row=rowForId(id); if(row>=0) downloadsTable_->removeRow(row);
    });

    connect(torrentEngine_,&TorrentEngine::torrentAdded,this,[this](const QString&id,const QString&name){
        const int existingRow=rowForId(id);
        if(existingRow>=0){
            downloadsTable_->item(existingRow,0)->setText(name);
            accentRow(downloadsTable_, existingRow, true);
            return;
        }
        const int row=downloadsTable_->rowCount();downloadsTable_->insertRow(row);
        auto*f=new QTableWidgetItem(name);f->setData(Qt::UserRole,id);downloadsTable_->setItem(row,0,f);
        downloadsTable_->setItem(row,1,new QTableWidgetItem("Torrent"));downloadsTable_->setItem(row,2,new QTableWidgetItem("0%"));
        downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));downloadsTable_->setItem(row,4,new QTableWidgetItem("BitTorrent"));
        auto *date=new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))); date->setData(Qt::UserRole,QDateTime::currentSecsSinceEpoch()); downloadsTable_->setItem(row,5,date);accentRow(downloadsTable_,row,true);
    });
    connect(torrentEngine_,&TorrentEngine::torrentProgress,this,[this](const QString&id,int progress,qint64 done,qint64,qint64 down,qint64,int peers){
        const int row=rowForId(id);if(row<0)return;downloadsTable_->item(row,1)->setText(QStringLiteral("Torrent • %1 peers").arg(peers));
        downloadsTable_->item(row,2)->setText(QStringLiteral("%1%").arg(progress));downloadsTable_->item(row,3)->setText(formatSpeed(down));
        statusLabel_->setText(QStringLiteral("%1 downloaded").arg(formatBytes(done)));
    });
    connect(torrentEngine_,&TorrentEngine::torrentCompleted,this,[this](const QString&id){
        setStatus(id,"Completed");
        const int row=rowForId(id);
        if(row>=0){
            const auto ts=QDateTime::currentSecsSinceEpoch();
            auto *date=downloadsTable_->item(row,5);
            if(date){date->setText(QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));date->setData(Qt::UserRole,ts);}
            accentRow(downloadsTable_,row,true);
        }
        trayIcon_->showMessage("Beatit","Torrent completed");
    });
    connect(torrentEngine_,&TorrentEngine::torrentError,this,[this](const QString&,const QString&e){trayIcon_->showMessage("Beatit",e,QSystemTrayIcon::Warning);});
}
void MainWindow::setupTray(){trayIcon_->setIcon(windowIcon());trayIcon_->setToolTip("Beatit Download Manager");trayMenu_->addAction("Show Beatit",this,&MainWindow::showFromTray);trayMenu_->addSeparator();trayMenu_->addAction("Exit",this,&MainWindow::exitFromTray);trayIcon_->setContextMenu(trayMenu_);connect(trayIcon_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::DoubleClick||r==QSystemTrayIcon::Trigger)showFromTray();});trayIcon_->show();}
void MainWindow::closeEvent(QCloseEvent*e){if(!reallyQuit_&&trayIcon_->isVisible()){hide();trayIcon_->showMessage("Beatit","Beatit is still running in the system tray.",QSystemTrayIcon::Information,2500);e->ignore();return;}e->accept();}
void MainWindow::showFromTray(){showNormal();raise();activateWindow();}
void MainWindow::exitFromTray(){reallyQuit_=true;close();}
int MainWindow::selectedRow()const{auto r=downloadsTable_->selectedRanges();return r.isEmpty()?-1:r.first().topRow();}
QString MainWindow::selectedId()const{const int row=selectedRow();return row<0||!downloadsTable_->item(row,0)?QString():downloadsTable_->item(row,0)->data(Qt::UserRole).toString();}
void MainWindow::setStatus(const QString&id,const QString&status){const int row=rowForId(id);if(row>=0)downloadsTable_->item(row,1)->setText(status);}
int MainWindow::rowForId(const QString&id)const{for(int row=0;row<downloadsTable_->rowCount();++row)if(downloadsTable_->item(row,0)&&downloadsTable_->item(row,0)->data(Qt::UserRole).toString()==id)return row;return -1;}
void MainWindow::addDownload(){
    const QString url=urlEdit_->text().trimmed();
    if(url.startsWith("magnet:?")){torrentEngine_->addMagnet(url,QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));urlEdit_->clear();statusLabel_->setText("Adding torrent…");return;}
    const QUrl parsed(url);
    if(!parsed.isValid()||(parsed.scheme()!="http"&&parsed.scheme()!="https")){statusLabel_->setText("Invalid URL");return;}
    downloadManager_->addUrl(url,QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));urlEdit_->clear();statusLabel_->setText("Queued…");
}
void MainWindow::pauseSelected(){const QString id=selectedId();if(id.startsWith("torrent-"))torrentEngine_->pause(id);else if(!id.isEmpty())downloadManager_->pause(id);}
void MainWindow::cancelSelected(){const QString id=selectedId();if(id.startsWith("torrent-"))torrentEngine_->remove(id);else if(!id.isEmpty())downloadManager_->cancel(id);}
void MainWindow::removeSelected(){
    const QString id=selectedId();
    if(id.isEmpty()) return;

    QCheckBox *deleteFile = new QCheckBox(QStringLiteral("Also delete the downloaded file from the SSD"), this);
    deleteFile->setChecked(false);

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Remove download"));
    box.setText(QStringLiteral("Remove this download from Beatit history?"));
    box.setInformativeText(QStringLiteral("The downloaded file will be kept unless you tick the box below."));
    box.setIcon(QMessageBox::Question);
    box.setCheckBox(deleteFile);
    box.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Cancel);
    if(box.exec()!=QMessageBox::Ok) return;

    const bool eraseFile=deleteFile->isChecked();
    if(id.startsWith(QStringLiteral("torrent-"))) torrentEngine_->remove(id, eraseFile);
    else downloadManager_->remove(id, eraseFile);
}

void MainWindow::openSelected(){const QString id=selectedId();const QString path=paths_.value(id);if(!path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
