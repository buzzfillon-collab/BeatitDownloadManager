#include "MainWindow.h"
#include "../core/DownloadManager.h"
#include "../core/TorrentEngine.h"
#include "../browser/BrowserBridge.h"
#include "../browser/BrowserInstaller.h"
#include "../core/YtDlpManager.h"
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QSettings>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QFrame>
#include <QToolButton>
#include <QSizePolicy>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>

namespace {
QString formatBytes(qint64 b){if(b<1024)return QStringLiteral("%1 B").arg(b);double v=b;const QStringList u{"KB","MB","GB","TB"};int i=-1;do{v/=1024.0;++i;}while(v>=1024.0&&i+1<u.size());return QStringLiteral("%1 %2").arg(v,0,'f',v>=100?0:1).arg(u[i]);}
QString formatSpeed(qint64 b){return b<=0?QStringLiteral("—"):formatBytes(b)+QStringLiteral("/s");}
void tintRow(QTableWidget *table, int row, const QColor &tone) {
    for (int col = 0; col < table->columnCount(); ++col)
        if (auto *item = table->item(row, col)) item->setBackground(tone);
}
void setProgress(QTableWidget *table,int row,int percent,bool torrent){
    if(row<0||row>=table->rowCount())return;
    auto *bar=qobject_cast<QProgressBar*>(table->cellWidget(row,2));
    if(!bar){bar=new QProgressBar(table);bar->setRange(0,100);bar->setTextVisible(true);table->setCellWidget(row,2,bar);}
    bar->setValue(qBound(0,percent,100));
    bar->setFormat(QStringLiteral("%1%").arg(qBound(0,percent,100)));
    bar->setStyleSheet(QStringLiteral("QProgressBar{background:#202632;border:0;border-radius:5px;text-align:center;color:#dce3ef;min-width:130px;max-width:190px;min-height:10px;max-height:10px;font-size:9px;}QProgressBar::chunk{background:%1;border-radius:5px;}").arg(torrent?QStringLiteral("#62d89b"):QStringLiteral("#67a9ff")));
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
pauseButton_(new QPushButton(QStringLiteral("Pause"),this)),resumeButton_(new QPushButton(QStringLiteral("Resume"),this)),cancelButton_(new QPushButton(QStringLiteral("Cancel"),this)),
removeButton_(new QPushButton(QStringLiteral("Remove"),this)),
openButton_(new QPushButton(QStringLiteral("Open"),this)), recheckButton_(new QPushButton(QStringLiteral("Recheck"),this)),downloadsTable_(new QTableWidget(this)),
statusLabel_(new QLabel(QStringLiteral("Ready"),this)),downloadManager_(new DownloadManager(this)),
torrentEngine_(new TorrentEngine(this)),browserBridge_(new BrowserBridge(this)),ytDlpManager_(new YtDlpManager(this)),trayIcon_(new QSystemTrayIcon(this)),trayMenu_(new QMenu(this)){
    setWindowTitle("Beatit");setWindowIcon(beatitIcon());setMinimumSize(1050,650);resize(1180,720);

    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    downloadManager_->setHttpConnections(settings.value(QStringLiteral("http/connections"), 8).toInt());

    auto *root=new QWidget(this);
    auto *mainLayout=new QVBoxLayout(root);
    mainLayout->setContentsMargins(0,0,0,0);mainLayout->setSpacing(0);

    auto *toolbar=new QFrame(root);toolbar->setObjectName("toolbar");
    auto *toolbarLayout=new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(18,12,18,12);toolbarLayout->setSpacing(10);

    auto *traffic=new QLabel(QStringLiteral("●  ●  ●"),toolbar);
    traffic->setObjectName("traffic");
    toolbarLayout->addWidget(traffic);

    auto *brand=new QLabel(QStringLiteral("Beatit"),toolbar);
    brand->setObjectName("toolbarBrand");toolbarLayout->addWidget(brand);
    toolbarLayout->addSpacing(14);

    urlEdit_->setPlaceholderText("Paste URL or magnet link…");urlEdit_->setClearButtonEnabled(true);
    urlEdit_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbarLayout->addWidget(urlEdit_,1);toolbarLayout->addWidget(addButton_);
    auto *fileButton=new QPushButton(QStringLiteral("＋ Torrent"),toolbar);toolbarLayout->addWidget(fileButton);
    auto *settingsButton=new QPushButton(QStringLiteral("⚙"),toolbar);
    settingsButton->setToolTip(QStringLiteral("Settings"));
    settingsButton->setFixedWidth(42);
    toolbarLayout->addWidget(settingsButton);
    mainLayout->addWidget(toolbar);

    auto *body=new QHBoxLayout;
    body->setContentsMargins(14,14,14,16);body->setSpacing(14);

    auto *sidebar=new QFrame(root);sidebar->setObjectName("sidebar");sidebar->setFixedWidth(178);
    auto *side=new QVBoxLayout(sidebar);side->setContentsMargins(12,18,12,12);side->setSpacing(6);

    auto *logo=new QLabel(QStringLiteral("B"),sidebar);logo->setObjectName("logo");logo->setAlignment(Qt::AlignCenter);
    side->addWidget(logo,0,Qt::AlignHCenter);
    auto *sideTitle=new QLabel(QStringLiteral("DOWNLOADS"),sidebar);sideTitle->setObjectName("sideTitle");side->addWidget(sideTitle);
    auto *allButton=new QPushButton(QStringLiteral("  All Downloads"),sidebar);allButton->setObjectName("navActive");
    auto *activeButton=new QPushButton(QStringLiteral("  Active"),sidebar);auto *completedButton=new QPushButton(QStringLiteral("  Completed"),sidebar);
    auto *torrentNav=new QPushButton(QStringLiteral("  BitTorrent"),sidebar);
    for(auto *b:{allButton,activeButton,completedButton,torrentNav}){b->setFlat(true);b->setMinimumHeight(36);b->setCursor(Qt::PointingHandCursor);side->addWidget(b);}
    side->addStretch();
    auto *sideHint=new QLabel(QStringLiteral("HTTP  •  BitTorrent\nUnified history"),sidebar);sideHint->setObjectName("sideHint");side->addWidget(sideHint);
    body->addWidget(sidebar);

    auto *content=new QWidget(root);auto *r=new QVBoxLayout(content);r->setContentsMargins(8,4,0,0);r->setSpacing(12);
    auto *heading=new QHBoxLayout;
    auto *t=new QLabel("Downloads",content);t->setObjectName("title");
    auto *s=new QLabel("Fast, free, open-source downloading.",content);s->setObjectName("subtitle");
    heading->addWidget(t);heading->addSpacing(12);heading->addWidget(s);heading->addStretch();r->addLayout(heading);

    downloadsTable_->setColumnCount(6);downloadsTable_->setHorizontalHeaderLabels({"FILE","STATUS","PROGRESS","SPEED","SOURCE","LAST ACTIVITY"});
    downloadsTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(5,QHeaderView::ResizeToContents);
    downloadsTable_->setSortingEnabled(true);downloadsTable_->horizontalHeader()->setSortIndicator(5,Qt::DescendingOrder);downloadsTable_->sortItems(5,Qt::DescendingOrder);
    downloadsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);downloadsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    downloadsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    downloadsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);downloadsTable_->setShowGrid(false);downloadsTable_->verticalHeader()->setVisible(false);
    downloadsTable_->verticalHeader()->setDefaultSectionSize(58);

    auto *a=new QHBoxLayout;a->setSpacing(8);a->addWidget(pauseButton_);a->addWidget(resumeButton_);a->addWidget(cancelButton_);a->addWidget(removeButton_);a->addWidget(openButton_);a->addWidget(recheckButton_);a->addStretch();a->addWidget(statusLabel_);
    r->addWidget(downloadsTable_,1);r->addLayout(a);body->addWidget(content,1);
    mainLayout->addLayout(body,1);setCentralWidget(root);

    setStyleSheet(R"(
        QMainWindow{background:#0b0d12;color:#f5f7fb;}
        QFrame#toolbar{background:#151820;border-bottom:1px solid #252a35;}
        QLabel#traffic{color:#ff5f57;font-size:12px;letter-spacing:2px;}
        QLabel#toolbarBrand{font-size:15px;font-weight:700;color:#f5f7fb;}
        QLabel#title{font-size:28px;font-weight:700;color:#f5f7fb;}
        QLabel#subtitle{font-size:13px;color:#8b92a3;}
        QFrame#sidebar{background:#10131a;border:1px solid #202530;border-radius:14px;}
        QLabel#logo{background:#7c5cff;color:white;border-radius:12px;font-size:22px;font-weight:800;min-width:42px;max-width:42px;min-height:42px;max-height:42px;margin-bottom:12px;}
        QLabel#sideTitle{color:#626b7c;font-size:10px;font-weight:800;padding:6px 8px;}
        QLabel#sideHint{color:#626b7c;font-size:11px;padding:8px;}
        QPushButton#navActive{background:#27203f;color:#bdafff;border:1px solid #3b2e61;border-radius:9px;text-align:left;padding-left:8px;font-weight:700;}
        QPushButton{background:#1a1f29;border:1px solid #2a303c;border-radius:9px;padding:9px 14px;color:#e9edf5;font-weight:600;}
        QPushButton:hover{background:#252b37;border-color:#3a4250;}
        QPushButton#primary{background:#7c5cff;border-color:#8d72ff;color:white;}
        QLineEdit{background:#0f1218;border:1px solid #2b303b;border-radius:10px;padding:10px 13px;color:#f0f3f8;font-size:13px;}
        QLineEdit:focus{border:1px solid #7c5cff;}
        QTableWidget{background:#10141b;border:1px solid #242a35;border-radius:13px;gridline-color:transparent;color:#e6eaf2;font-size:12px;outline:0;}
        QTableWidget::item{padding:8px;border-bottom:1px solid #1e232d;}
        QTableWidget::item:selected{background:#29233e;color:white;}
        QHeaderView::section{background:#171b24;border:none;border-bottom:1px solid #252b36;padding:9px;color:#7f8798;font-size:10px;font-weight:800;}
        QProgressBar{background:#202632;border:0;border-radius:5px;text-align:center;color:#dce3ef;min-width:130px;max-width:190px;min-height:10px;max-height:10px;font-size:9px;}
        QProgressBar::chunk{background:#67a9ff;border-radius:5px;}
    )");

    addButton_->setObjectName("primary");setupTray();
    connect(allButton, &QPushButton::clicked, this, [this] { filterDownloads(QStringLiteral("all")); });
    connect(activeButton, &QPushButton::clicked, this, [this] { filterDownloads(QStringLiteral("active")); });
    connect(completedButton, &QPushButton::clicked, this, [this] { filterDownloads(QStringLiteral("completed")); });
    connect(torrentNav, &QPushButton::clicked, this, [this] { filterDownloads(QStringLiteral("torrent")); });
    connect(ytDlpManager_, &YtDlpManager::updateStarted, this, [this] {
        if (ytDlpRetryAfterUpdate_)
            statusLabel_->setText(QStringLiteral("Updating yt-dlp…"));
    });
    connect(ytDlpManager_, &YtDlpManager::updateFinished, this,
            [this](bool success, const QString &message) {
        if (ytDlpRetryAfterUpdate_) {
            if (success) {
                statusLabel_->setText(QStringLiteral("yt-dlp updated — retrying stream download…"));
                const bool youtube = ytDlpPendingKind_ == QStringLiteral("youtube");
                const QString url = ytDlpPendingUrl_;
                const QString kind = ytDlpPendingKind_;
                startYtDlpDownload(url, youtube, kind);
            } else {
                ytDlpRetryAfterUpdate_ = false;
                statusLabel_->setText(QStringLiteral("yt-dlp update failed: %1").arg(message));
                trayIcon_->showMessage(QStringLiteral("Beatit"),
                    QStringLiteral("yt-dlp update failed: %1").arg(message),
                    QSystemTrayIcon::Warning);
            }
            return;
        }
        if (!message.isEmpty())
            statusLabel_->setText(QStringLiteral("yt-dlp: %1").arg(message));
    });

    connect(addButton_,&QPushButton::clicked,this,&MainWindow::addDownload);
    connect(urlEdit_,&QLineEdit::returnPressed,this,&MainWindow::addDownload);
    connect(pauseButton_,&QPushButton::clicked,this,&MainWindow::pauseSelected);
    connect(resumeButton_,&QPushButton::clicked,this,&MainWindow::resumeSelected);
    connect(cancelButton_,&QPushButton::clicked,this,&MainWindow::cancelSelected);
    connect(removeButton_,&QPushButton::clicked,this,&MainWindow::removeSelected);
    connect(openButton_,&QPushButton::clicked,this,&MainWindow::openSelected);
    connect(recheckButton_,&QPushButton::clicked,this,&MainWindow::recheckSelected);
    connect(downloadsTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = downloadsTable_->rowAt(pos.y());
        if (row < 0) return;
        downloadsTable_->selectRow(row);
        const QString id = selectedId();
        if (id.isEmpty()) return;

        const QString status = downloadsTable_->item(row, 1)
            ? downloadsTable_->item(row, 1)->text().toLower() : QString();
        const bool torrent = id.startsWith(QStringLiteral("torrent-"));
        const bool paused = status.contains(QStringLiteral("paused"));
        const bool active = status.contains(QStringLiteral("downloading")) ||
                            status.contains(QStringLiteral("queued")) ||
                            status.contains(QStringLiteral("resolving")) ||
                            status.contains(QStringLiteral("checking")) ||
                            status.contains(QStringLiteral("waiting"));
        const bool completed = status.contains(QStringLiteral("completed")) ||
                               status.contains(QStringLiteral("finished")) ||
                               status.contains(QStringLiteral("seeding"));

        QMenu menu(this);
        if (paused || (!completed && !active))
            menu.addAction(QStringLiteral("Resume"), this, &MainWindow::resumeSelected);
        if (active || status.contains(QStringLiteral("stalled")))
            menu.addAction(QStringLiteral("Pause"), this, &MainWindow::pauseSelected);
        if (torrent)
            menu.addAction(QStringLiteral("Recheck files"), this, &MainWindow::recheckSelected);
        if (!torrent && !completed)
            menu.addAction(QStringLiteral("Cancel"), this, &MainWindow::cancelSelected);
        menu.addAction(QStringLiteral("Remove from history…"), this, &MainWindow::removeSelected);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Open"), this, &MainWindow::openSelected);
        menu.exec(downloadsTable_->viewport()->mapToGlobal(pos));
    });
    connect(settingsButton,&QPushButton::clicked,this,&MainWindow::showSettings);
    connect(fileButton,&QPushButton::clicked,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,"Open torrent",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),"Torrent files (*.torrent)");
        if(!p.isEmpty()) torrentEngine_->addTorrentFile(p,QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    });

    connect(downloadManager_,&DownloadManager::taskAdded,this,[this](const QString&id,const QString&url){
        const int row=downloadsTable_->rowCount();downloadsTable_->insertRow(row);
        auto*f=new QTableWidgetItem(QUrl(url).fileName().isEmpty()?"download":QUrl(url).fileName());f->setData(Qt::UserRole,id);
        downloadsTable_->setItem(row,0,f);downloadsTable_->setItem(row,1,new QTableWidgetItem("Queued"));
        setProgress(downloadsTable_,row,0,false);downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));
        downloadsTable_->setItem(row,4,new QTableWidgetItem(url));
        auto *date=new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))); date->setData(Qt::UserRole,QDateTime::currentSecsSinceEpoch()); downloadsTable_->setItem(row,5,date);
        accentRow(downloadsTable_, row, false);
    });
    connect(downloadManager_,&DownloadManager::taskRestored,this,[this](const QString&id,const QString&url,const QString&filename,const QString&status,qint64 done,qint64 total,qint64 updatedAt){
        const int row=downloadsTable_->rowCount();downloadsTable_->insertRow(row);
        auto*f=new QTableWidgetItem(filename);f->setData(Qt::UserRole,id);downloadsTable_->setItem(row,0,f);
        downloadsTable_->setItem(row,1,new QTableWidgetItem(status));setProgress(downloadsTable_,row,total>0?static_cast<int>(done*100/total):0,false);
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
        setProgress(downloadsTable_,row,total>0?static_cast<int>(done*100/total):0,false);
        downloadsTable_->item(row,3)->setText(formatSpeed(speed));statusLabel_->setText(QStringLiteral("%1 downloaded").arg(formatBytes(done)));
        accentRow(downloadsTable_, row, false);
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
        setProgress(downloadsTable_,row,total>0?static_cast<int>(done*100/total):0,true);
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
        downloadsTable_->setItem(row,1,new QTableWidgetItem("Torrent"));setProgress(downloadsTable_,row,0,true);
        downloadsTable_->setItem(row,3,new QTableWidgetItem("—"));downloadsTable_->setItem(row,4,new QTableWidgetItem("BitTorrent"));
        auto *date=new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))); date->setData(Qt::UserRole,QDateTime::currentSecsSinceEpoch()); downloadsTable_->setItem(row,5,date);accentRow(downloadsTable_,row,true);
    });
    connect(torrentEngine_,&TorrentEngine::torrentProgress,this,[this](const QString&id,int progress,qint64 done,qint64,qint64 down,qint64,int peers){
        const int row=rowForId(id);if(row<0)return;downloadsTable_->item(row,1)->setText(QStringLiteral("Torrent • %1 peers").arg(peers));
        setProgress(downloadsTable_,row,progress,true);downloadsTable_->item(row,3)->setText(formatSpeed(down));
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
    connect(torrentEngine_,&TorrentEngine::torrentAvailabilityQuestion,this,
        [this](const QString&id,const QString&name,double copies,int peers){
            const QString displayName=name.isEmpty()?QStringLiteral("This torrent"):name;
            QMessageBox box(this);
            box.setWindowTitle(QStringLiteral("Torrent availability"));
            box.setIcon(QMessageBox::Warning);
            box.setText(QStringLiteral("%1 is not fully available in the current swarm.").arg(displayName));
            box.setInformativeText(
                QStringLiteral("The rarest pieces currently have less than one complete distributed copy. "
                               "Downloading may stall permanently if the missing pieces never appear. "
                               "Still download?\n\nPeers: %1\nDistributed copies: %2")
                    .arg(peers).arg(copies,0,'f',2));
            auto *yes=box.addButton(QStringLiteral("Still download"),QMessageBox::AcceptRole);
            box.addButton(QStringLiteral("No, wait"),QMessageBox::RejectRole);
            box.exec();
            if(box.clickedButton()==yes){
                torrentEngine_->resume(id);
                setStatus(id,QStringLiteral("Downloading"));
                statusLabel_->setText(QStringLiteral("Downloading despite incomplete availability"));
            } else {
                setStatus(id,QStringLiteral("Waiting — incomplete availability"));
                statusLabel_->setText(QStringLiteral("Torrent waiting for complete swarm availability"));
            }
        });
    connect(torrentEngine_,&TorrentEngine::torrentStatusChanged,this,[this](const QString&id,const QString&s){
        setStatus(id,s);
        if (s.contains(QStringLiteral("Seeding stopped"))) statusLabel_->setText(QStringLiteral("Torrent seeding policy reached"));
    });
    connect(torrentEngine_,&TorrentEngine::torrentHealthChanged,this,[this](const QString&id,bool tracker,bool dht,int known,int candidates){
        Q_UNUSED(id);
        statusLabel_->setText(QStringLiteral("Torrent health: tracker %1 • DHT %2 • known peers %3 • candidates %4")
            .arg(tracker?QStringLiteral("OK"):QStringLiteral("idle"))
            .arg(dht?QStringLiteral("OK"):QStringLiteral("idle")).arg(known).arg(candidates));
    });
    connect(torrentEngine_,&TorrentEngine::torrentStalled,this,[this](const QString&id,int seconds){
        setStatus(id,QStringLiteral("Stalled"));
        statusLabel_->setText(QStringLiteral("Torrent stalled for %1 min").arg(seconds/60));
        trayIcon_->showMessage(QStringLiteral("Beatit"),QStringLiteral("Torrent stalled: %1").arg(id),QSystemTrayIcon::Warning);
    });
    connect(torrentEngine_,&TorrentEngine::torrentError,this,[this](const QString&,const QString&e){trayIcon_->showMessage("Beatit",e,QSystemTrayIcon::Warning);});

    QString browserInstallError;
    const bool browserInstalled = BrowserInstaller::install(&browserInstallError);
    if (browserBridge_->start()) {
        connect(browserBridge_, &BrowserBridge::captureRequested,
                this, &MainWindow::handleBrowserCapture);
        statusLabel_->setText(browserInstalled
            ? QStringLiteral("Browser integration ready")
            : QStringLiteral("Browser bridge ready; native host registration failed"));
    } else {
        statusLabel_->setText(QStringLiteral("Browser integration unavailable"));
    }
    if (!browserInstalled && !browserInstallError.isEmpty())
        trayIcon_->showMessage(QStringLiteral("Beatit browser integration"), browserInstallError,
                               QSystemTrayIcon::Warning);

    ytDlpManager_->updateIfDue();
}
void MainWindow::startYtDlpDownload(const QString &url, bool youtube, const QString &kind) {
    if (ytDlpProcess_) {
        statusLabel_->setText(QStringLiteral("A stream download is already running"));
        return;
    }

    const QString executable = ytDlpManager_->executablePath();
    if (executable.isEmpty()) {
        trayIcon_->showMessage(QStringLiteral("Beatit"),
            QStringLiteral("yt-dlp.exe was not found. Put it in Beatit's tools folder."),
            QSystemTrayIcon::Warning);
        statusLabel_->setText(QStringLiteral("yt-dlp missing"));
        return;
    }

    const QString output = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        .filePath(QStringLiteral("%(title)s.%(ext)s"));

    ytDlpPendingUrl_ = url;
    ytDlpPendingKind_ = kind;
    ytDlpPendingArgs_ = {
        QStringLiteral("--no-playlist"), QStringLiteral("--newline"),
        QStringLiteral("-o"), output
    };
    if (youtube || kind == QStringLiteral("hls"))
        ytDlpPendingArgs_ << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
    ytDlpPendingArgs_ << url;

    ytDlpProcess_ = new QProcess(this);
    ytDlpProcess_->setProgram(executable);
    ytDlpProcess_->setArguments(ytDlpPendingArgs_);
    ytDlpProcess_->setProcessChannelMode(QProcess::MergedChannels);

    const QString toolsDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools"));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pathSeparator = QStringLiteral(";");
    env.insert(QStringLiteral("PATH"), toolsDir + pathSeparator + env.value(QStringLiteral("PATH")));
    ytDlpProcess_->setProcessEnvironment(env);

    connect(ytDlpProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, youtube](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(ytDlpProcess_->readAll()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;

        ytDlpProcess_->deleteLater();
        ytDlpProcess_ = nullptr;

        if (success) {
            ytDlpRetryAfterUpdate_ = false;
            statusLabel_->setText(QStringLiteral("Stream download completed"));
            trayIcon_->showMessage(QStringLiteral("Beatit"),
                                   youtube ? QStringLiteral("YouTube download complete")
                                           : QStringLiteral("Stream download complete"));
            return;
        }

        if (!ytDlpRetryAfterUpdate_) {
            ytDlpRetryAfterUpdate_ = true;
            statusLabel_->setText(QStringLiteral("yt-dlp failed — updating and retrying once…"));
            ytDlpManager_->updateNow();
            return;
        }

        ytDlpRetryAfterUpdate_ = false;
        QString message = QStringLiteral("yt-dlp download failed");
        if (!output.isEmpty()) {
            const QStringList lines = output.split(
                QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            if (!lines.isEmpty())
                message = lines.constLast();
        }
        statusLabel_->setText(message);
        trayIcon_->showMessage(QStringLiteral("Beatit"), message, QSystemTrayIcon::Warning);
    });

    connect(ytDlpProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        if (!ytDlpProcess_) return;
        const QString message = ytDlpProcess_->errorString();
        ytDlpProcess_->deleteLater();
        ytDlpProcess_ = nullptr;

        if (!ytDlpRetryAfterUpdate_) {
            ytDlpRetryAfterUpdate_ = true;
            statusLabel_->setText(QStringLiteral("yt-dlp could not start — updating and retrying once…"));
            ytDlpManager_->updateNow();
            return;
        }

        ytDlpRetryAfterUpdate_ = false;
        statusLabel_->setText(message);
        trayIcon_->showMessage(QStringLiteral("Beatit"), message, QSystemTrayIcon::Warning);
    });

    ytDlpProcess_->start();
    statusLabel_->setText(youtube ? QStringLiteral("YouTube download started")
                                  : (kind == QStringLiteral("hls")
                                      ? QStringLiteral("HLS download started")
                                      : QStringLiteral("Stream download started")));
}

void MainWindow::handleBrowserCapture(const QString &url, const QString &title, const QString &kind) {
    Q_UNUSED(title);
    const QUrl parsed(url);
    const QString host = parsed.host().toLower();
    const QString path = parsed.path().toLower();

    const bool youtube = host == QStringLiteral("youtube.com") ||
                         host.endsWith(QStringLiteral(".youtube.com")) ||
                         host == QStringLiteral("youtu.be");
    const bool hls = path.contains(QStringLiteral(".m3u8"));

    if (youtube || hls || kind == QStringLiteral("youtube") || kind == QStringLiteral("hls")) {
        startYtDlpDownload(url, youtube || kind == QStringLiteral("youtube"),
                           hls || kind == QStringLiteral("hls") ? QStringLiteral("hls") : kind);
        return;
    }

    if (parsed.isValid() && (parsed.scheme() == QStringLiteral("http") ||
                             parsed.scheme() == QStringLiteral("https"))) {
        downloadManager_->addUrl(url, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        statusLabel_->setText(QStringLiteral("Browser download queued"));
    }
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
void MainWindow::resumeSelected(){
    const QString id=selectedId();
    if(id.isEmpty()) return;
    if(id.startsWith(QStringLiteral("torrent-"))) torrentEngine_->resume(id);
    else downloadManager_->resume(id);
    setStatus(id, QStringLiteral("Queued"));
    statusLabel_->setText(QStringLiteral("Resumed"));
}
void MainWindow::filterDownloads(const QString &filter){
    for(int row=0; row<downloadsTable_->rowCount(); ++row){
        const QString id = downloadsTable_->item(row,0)
            ? downloadsTable_->item(row,0)->data(Qt::UserRole).toString() : QString();
        const QString status = downloadsTable_->item(row,1)
            ? downloadsTable_->item(row,1)->text().toLower() : QString();
        bool visible = true;
        if(filter == QStringLiteral("active"))
            visible = !status.contains(QStringLiteral("completed")) &&
                      !status.contains(QStringLiteral("cancelled")) &&
                      !status.contains(QStringLiteral("failed")) &&
                      !status.contains(QStringLiteral("seeding stopped"));
        else if(filter == QStringLiteral("completed"))
            visible = status.contains(QStringLiteral("completed")) ||
                      status.contains(QStringLiteral("finished")) ||
                      status.contains(QStringLiteral("seeding"));
        else if(filter == QStringLiteral("torrent"))
            visible = id.startsWith(QStringLiteral("torrent-"));
        downloadsTable_->setRowHidden(row, !visible);
    }
    statusLabel_->setText(filter == QStringLiteral("all") ? QStringLiteral("All downloads")
                         : filter == QStringLiteral("active") ? QStringLiteral("Active downloads")
                         : filter == QStringLiteral("completed") ? QStringLiteral("Completed downloads")
                         : QStringLiteral("BitTorrent downloads"));
}
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
void MainWindow::recheckSelected(){
    const QString id=selectedId();
    if (!id.startsWith(QStringLiteral("torrent-"))) { statusLabel_->setText(QStringLiteral("Select a torrent first")); return; }
    torrentEngine_->forceRecheck(id);
    statusLabel_->setText(QStringLiteral("Rechecking torrent files…"));
}
void MainWindow::showSettings(){
    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    bool ok=false;

    const QStringList ytChannels{QStringLiteral("Nightly (recommended)"),
                                  QStringLiteral("Stable")};
    const int ytCurrent = ytDlpManager_->channel() == YtDlpManager::Channel::Stable ? 1 : 0;
    const QString ytChoice = QInputDialog::getItem(
        this, QStringLiteral("yt-dlp release channel"),
        QStringLiteral("Channel:"), ytChannels, ytCurrent, false, &ok);
    if (!ok) return;
    const auto newChannel = ytChannels.indexOf(ytChoice) == 1
        ? YtDlpManager::Channel::Stable : YtDlpManager::Channel::Nightly;
    const bool channelChanged = newChannel != ytDlpManager_->channel();
    ytDlpManager_->setChannel(newChannel);

    if (channelChanged ||
        QMessageBox::question(this, QStringLiteral("yt-dlp updates"),
            QStringLiteral("Check yt-dlp for updates now?")) == QMessageBox::Yes) {
        ytDlpManager_->updateNow();
        statusLabel_->setText(QStringLiteral("Checking yt-dlp updates…"));
    }
    const int current=qBound(1,downloadManager_->httpConnections(),8);
    const int value=QInputDialog::getInt(this,QStringLiteral("Beatit Settings"),
        QStringLiteral("HTTP connections per download:"),current,1,8,1,&ok);
    if(!ok) return;
    downloadManager_->setHttpConnections(value);
    settings.setValue(QStringLiteral("http/connections"),value);

    const QStringList modes{QStringLiteral("Stop after 1.0× upload/download ratio"),
                             QStringLiteral("Stop after 30 minutes seeding"),
                             QStringLiteral("Seed forever"),
                             QStringLiteral("Stop immediately after completion")};
    const int currentMode=torrentEngine_->seedingPolicyMode();
    const QString mode=QInputDialog::getItem(this,QStringLiteral("Torrent seeding policy"),
        QStringLiteral("After a torrent finishes:"),modes,currentMode,false,&ok);
    if(ok){
        const int selected=modes.indexOf(mode);
        double ratio=torrentEngine_->seedingRatio();
        int minutes=torrentEngine_->seedingMinutes();
        if(selected==0){
            ratio=QInputDialog::getDouble(this,QStringLiteral("Seeding ratio"),
                QStringLiteral("Upload/download ratio:"),ratio,0.1,100.0,1,&ok);
            if(!ok) return;
        } else if(selected==1){
            minutes=QInputDialog::getInt(this,QStringLiteral("Seeding time"),
                QStringLiteral("Minutes to seed:"),minutes,1,100000,1,&ok);
            if(!ok) return;
        }
        torrentEngine_->setSeedingPolicy(selected,ratio,minutes);
    }

    const QString id=selectedId();
    if(id.startsWith(QStringLiteral("torrent-"))){
        const auto files=torrentEngine_->torrentFiles(id);
        const auto existingPriorities=torrentEngine_->filePriorities(id);
        if(!files.isEmpty()){
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Selective torrent download"));
            dialog.resize(620,420);
            auto *layout=new QVBoxLayout(&dialog);
            auto *hint=new QLabel(QStringLiteral("Uncheck files you do not want to download."),&dialog);
            layout->addWidget(hint);
            auto *list=new QListWidget(&dialog);
            for(int i=0;i<files.size();++i){
                auto *item=new QListWidgetItem(files[i],list);
                item->setCheckState(i < existingPriorities.size() && existingPriorities[i] == 0 ? Qt::Unchecked : Qt::Checked);
            }
            layout->addWidget(list,1);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
            layout->addWidget(buttons);
            connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if(dialog.exec()==QDialog::Accepted){
                QVector<int> selectedPriorities;
                selectedPriorities.reserve(list->count());
                for(int i=0;i<list->count();++i)
                    selectedPriorities.push_back(list->item(i)->checkState()==Qt::Checked?4:0);
                torrentEngine_->setFilePriorities(id,selectedPriorities);
                statusLabel_->setText(QStringLiteral("Torrent file selection updated"));
            }
        }
    }
    statusLabel_->setText(QStringLiteral("Settings saved"));
}
