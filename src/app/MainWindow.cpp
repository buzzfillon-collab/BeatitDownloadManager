#include "MainWindow.h"
#include "../core/DownloadManager.h"
#include "../core/TorrentEngine.h"
#include "../browser/BrowserBridge.h"
#include "../browser/BrowserInstaller.h"
#include "../core/YtDlpManager.h"
#include "../core/Scheduler.h"
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QClipboard>
#include <QCryptographicHash>
#include <QtEndian>
#include <QApplication>
#include <QDesktopServices>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDrag>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
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
#include <QPlainTextEdit>
#include <QGridLayout>
#include <QFrame>
#include <QToolButton>
#include <QSizePolicy>
#include <array>
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
#include <QTimeEdit>
#include <QTabWidget>
#include <QCalendarWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QSet>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <functional>
#include <tuple>
#include <memory>

namespace {
QString formatBytes(qint64 b){if(b<1024)return QStringLiteral("%1 B").arg(b);double v=b;const QStringList u{"KB","MB","GB","TB"};int i=-1;do{v/=1024.0;++i;}while(v>=1024.0&&i+1<u.size());return QStringLiteral("%1 %2").arg(v,0,'f',v>=100?0:1).arg(u[i]);}
QString formatSpeed(qint64 b){return b<=0?QStringLiteral("—"):formatBytes(b)+QStringLiteral("/s");}
QString categoryForUrl(const QString &url) {
    const QUrl parsed(url);
    const QString host = parsed.host().toLower();
    const QString ext = QFileInfo(parsed.path()).suffix().toLower();
    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    const QStringList custom = settings.value(QStringLiteral("categories/customNames")).toStringList();
    const QStringList allHosts = settings.value(QStringLiteral("categories/hostRules")).toStringList();
    for (const QString &entry : allHosts) {
        const int separator = entry.indexOf('=');
        if (separator <= 0) continue;
        const QString ruleHost = entry.left(separator).trimmed().toLower();
        const QString category = entry.mid(separator + 1).trimmed();
        if (!ruleHost.isEmpty() && !category.isEmpty() &&
            (host == ruleHost || host.endsWith(QStringLiteral(".") + ruleHost)) &&
            (custom.contains(category) || QStringList{QStringLiteral("Video"),QStringLiteral("Music"),QStringLiteral("Documents"),QStringLiteral("Programs"),QStringLiteral("Other")}.contains(category)))
            return category;
    }
    const QHash<QString, QString> defaults{
        {QStringLiteral("Video"), QStringLiteral("mp4,mkv,webm,avi,mov,m4v,mpeg,mpg,ts,m3u8")},
        {QStringLiteral("Music"), QStringLiteral("mp3,m4a,aac,flac,wav,ogg,opus,wma")},
        {QStringLiteral("Documents"), QStringLiteral("pdf,doc,docx,xls,xlsx,ppt,pptx,txt,rtf,odt,ods,csv")},
        {QStringLiteral("Programs"), QStringLiteral("exe,msi,msix,appx,zip,7z,rar,iso,dmg,deb,rpm")}
    };
    QStringList order{QStringLiteral("Video"), QStringLiteral("Music"), QStringLiteral("Documents"), QStringLiteral("Programs")};
    for (const QString &name : custom) if (!order.contains(name) && name != QStringLiteral("Other")) order.append(name);
    for (const QString &name : order) {
        const QString rule = settings.value(QStringLiteral("categoryRules/%1").arg(name), defaults.value(name)).toString();
        for (QString token : rule.split(',', Qt::SkipEmptyParts)) {
            token = token.trimmed().toLower();
            if (token.startsWith('.')) token.remove(0, 1);
            if (!token.isEmpty() && token == ext) return name;
        }
    }
    return QStringLiteral("Other");
}
QString categoryDestination(const QString &category) {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    return settings.value(QStringLiteral("categories/%1").arg(category), QDir(root).filePath(category)).toString();
}

QString lightThemeOverrides() {
    return QStringLiteral(R"(
        QMainWindow{background:#f3f8fb;color:#142534;}
        QFrame#toolbar{background:#ffffff;border-bottom:1px solid #d7e6eb;}
        QLabel#toolbarBrand,QLabel#title{color:#142534;}
        QLabel#subtitle,QLabel#sideTitle,QLabel#sideHint{color:#607987;}
        QFrame#sidebar{background:#ffffff;border:1px solid #d7e6eb;}
        QPushButton{background:#ffffff;border:1px solid #c9dce3;color:#203b4a;}
        QPushButton:hover{background:#e9f7fa;border-color:#80cdd6;}
        QLineEdit{background:#ffffff;border:1px solid #c3d9e1;color:#142534;}
        QTableWidget{background:#ffffff;border:1px solid #d7e6eb;color:#142534;}
        QTableWidget::item{border-bottom:1px solid #e3edf0;}
        QTableWidget::item:selected{background:#d9f5f4;color:#103d48;}
        QHeaderView::section{background:#edf5f7;border-bottom:1px solid #d7e6eb;color:#58717e;}
        QProgressBar{background:#dce9ee;color:#203b4a;}
    )");
}
class DownloadTable final : public QTableWidget {
public:
    explicit DownloadTable(QWidget *parent=nullptr):QTableWidget(parent){}
    std::function<QString()> dragPath;
protected:
    void startDrag(Qt::DropActions supportedActions) override {
        const QString path=dragPath?dragPath():QString();
        if(path.isEmpty()||!QFileInfo::exists(path)){QTableWidget::startDrag(supportedActions);return;}
        auto *mime=new QMimeData();
        mime->setUrls({QUrl::fromLocalFile(path)});
        QDrag drag(this);drag.setMimeData(mime);drag.exec(Qt::CopyAction);
    }
};
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
    bar->setStyleSheet(QStringLiteral("QProgressBar{background:#1b2b3a;border:0;border-radius:6px;text-align:center;color:#dceef5;min-width:130px;max-width:190px;min-height:12px;max-height:12px;font-size:9px;}QProgressBar::chunk{background:%1;border-radius:5px;}").arg(torrent?QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #34d399,stop:0.55 #2dd4bf,stop:1 #a3e635)"):QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22d3ee,stop:0.48 #60a5fa,stop:1 #a78bfa)")));
}
void accentRow(QTableWidget *table, int row, bool torrent) {
    tintRow(table, row, QColor(torrent ? "#102f2d" : "#102b3a"));
    if (auto *file = table->item(row, 0))
        file->setForeground(QColor(torrent ? "#63e6be" : "#55d6e8"));
    if (auto *status = table->item(row, 1))
        status->setForeground(QColor(torrent ? "#62d89b" : "#67a9ff"));
}
QIcon beatitIcon(){QPixmap x(64,64);x.fill(Qt::transparent);QPainter p(&x);p.setRenderHint(QPainter::Antialiasing);p.setBrush(QColor("#7c5cff"));p.setPen(Qt::NoPen);p.drawRoundedRect(4,4,56,56,16,16);p.setPen(QPen(Qt::white,7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawLine(20,18,20,46);p.drawLine(20,18,39,18);p.drawLine(20,32,35,32);p.drawLine(20,46,41,46);return QIcon(x);}
}

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),
urlEdit_(new QLineEdit(this)),addButton_(new QPushButton(QStringLiteral("＋ Add"),this)),
pauseButton_(new QPushButton(QStringLiteral("Pause"),this)),resumeButton_(new QPushButton(QStringLiteral("Resume"),this)),cancelButton_(new QPushButton(QStringLiteral("Cancel"),this)),
removeButton_(new QPushButton(QStringLiteral("Remove"),this)),
openButton_(new QPushButton(QStringLiteral("Open"),this)), recheckButton_(new QPushButton(QStringLiteral("Recheck"),this)),downloadsTable_(new DownloadTable(this)),
statusLabel_(new QLabel(QStringLiteral("Ready"),this)),downloadManager_(new DownloadManager(this)),torrentEngine_(new TorrentEngine(this)),scheduler_(new Scheduler(this)),
browserBridge_(new BrowserBridge(this)),ytDlpManager_(new YtDlpManager(this)),trayIcon_(new QSystemTrayIcon(this)),trayMenu_(new QMenu(this)){
    setWindowTitle("Beatit");setWindowIcon(beatitIcon());setMinimumSize(1050,650);resize(1180,720);
    setAcceptDrops(true);
    downloadsTable_->setDragEnabled(true);
    static_cast<DownloadTable*>(downloadsTable_)->dragPath=[this] {
        const QString id=selectedId();
        if(id.isEmpty())return QString();
        if(id.startsWith(QStringLiteral("torrent-")))return torrentEngine_->torrentSavePath(id);
        return paths_.value(id);
    };

    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    downloadManager_->setHttpConnections(settings.value(QStringLiteral("http/connections"), 8).toInt());
    const qint64 bandwidth = settings.value(QStringLiteral("bandwidth/limit"), 0).toLongLong();
    downloadManager_->setBandwidthLimit(bandwidth);
    downloadManager_->setProxy(settings.value(QStringLiteral("proxy/host")).toString(),
                               settings.value(QStringLiteral("proxy/port"), 0).toInt(),
                               settings.value(QStringLiteral("proxy/type"), 0).toInt());
    torrentEngine_->setBandwidthLimit(bandwidth);
    connect(scheduler_, &Scheduler::scheduleStateChanged, this, [this](bool allowed) {
        downloadManager_->setSchedulerAllowed(allowed);
        torrentEngine_->setSchedulerAllowed(allowed);
        statusLabel_->setText(allowed ? QStringLiteral("Scheduler window open") : QStringLiteral("Scheduler window closed"));
    });
    scheduler_->setEnabled(scheduler_->enabled());
    auto *siteGrabberSchedulePoll = new QTimer(this);
    siteGrabberSchedulePoll->setInterval(30000);
    connect(siteGrabberSchedulePoll, &QTimer::timeout, this, [this] { runScheduledSiteGrabber(); });
    siteGrabberSchedulePoll->start();
    auto *syncPoll = new QTimer(this); syncPoll->setInterval(30000);
    connect(syncPoll,&QTimer::timeout,this,[this]{runSyncChecks();}); syncPoll->start();
    QTimer::singleShot(2500, this, [this] { runScheduledSiteGrabber(); runSyncChecks(); });

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
    auto *fileButton=new QPushButton(QStringLiteral("＋ Torrent"),toolbar);fileButton->setObjectName("toolbarTorrentButton");toolbarLayout->addWidget(fileButton);
    auto *grabButton=new QPushButton(QStringLiteral("Grab links"),toolbar);grabButton->setObjectName("toolbarGrabLinksButton");toolbarLayout->addWidget(grabButton);
    auto *queuesButton=new QPushButton(QStringLiteral("Queues"),toolbar);queuesButton->setObjectName("toolbarQueuesButton");
    toolbarLayout->addWidget(queuesButton);
    auto *updateButton=new QPushButton(QStringLiteral("Update"),toolbar);updateButton->setObjectName("toolbarUpdateButton");toolbarLayout->addWidget(updateButton);
    auto *syncButton=new QPushButton(QStringLiteral("Sync"),toolbar);syncButton->setObjectName("toolbarSyncButton");toolbarLayout->addWidget(syncButton);
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
        QMainWindow{background:#0a111b;color:#edf7fb;}
        QFrame#toolbar{background:#111c2b;border-bottom:1px solid #203547;}
        QLabel#traffic{color:#ff5f57;font-size:12px;letter-spacing:2px;}
        QLabel#toolbarBrand{font-size:15px;font-weight:700;color:#edf7fb;}
        QLabel#title{font-size:28px;font-weight:700;color:#f5f7fb;}
        QLabel#subtitle{font-size:13px;color:#8ba5b5;}
        QFrame#sidebar{background:#0e1926;border:1px solid #1d3445;border-radius:14px;}
        QLabel#logo{background:#0faeae;color:white;border-radius:12px;font-size:22px;font-weight:800;min-width:42px;max-width:42px;min-height:42px;max-height:42px;margin-bottom:12px;}
        QLabel#sideTitle{color:#6c8798;font-size:10px;font-weight:800;padding:6px 8px;}
        QLabel#sideHint{color:#626b7c;font-size:11px;padding:8px;}
        QPushButton#navActive{background:#123943;color:#9af0ee;border:1px solid #1d5a65;border-radius:9px;text-align:left;padding-left:8px;font-weight:700;}
        QPushButton{background:#142232;border:1px solid #263d50;border-radius:9px;padding:9px 14px;color:#e5f2f8;font-weight:600;}
        QPushButton:hover{background:#1c3143;border-color:#376174;}
        QPushButton#primary{background:#0faeae;border-color:#34d4cf;color:#061b25;}
        QLineEdit{background:#0b1521;border:1px solid #294253;border-radius:10px;padding:10px 13px;color:#edf7fb;font-size:13px;}
        QLineEdit:focus{border:1px solid #22d3ee;}
        QTableWidget{background:#0e1926;border:1px solid #1d3445;border-radius:13px;gridline-color:transparent;color:#e2f0f7;font-size:12px;outline:0;}
        QTableWidget::item{padding:8px;border-bottom:1px solid #192d3c;}
        QTableWidget::item:selected{background:#173b49;color:white;}
        QHeaderView::section{background:#142333;border:none;border-bottom:1px solid #203547;padding:9px;color:#7f9bab;font-size:10px;font-weight:800;}
        QProgressBar{background:#202632;border:0;border-radius:5px;text-align:center;color:#dce3ef;min-width:130px;max-width:190px;min-height:10px;max-height:10px;font-size:9px;}
        QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22d3ee,stop:0.48 #60a5fa,stop:1 #a78bfa);border-radius:6px;}
    )");
    {
        QSettings appearance(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
        if (appearance.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString() == QStringLiteral("light"))
            setStyleSheet(styleSheet() + lightThemeOverrides());
        const QStringList columns = appearance.value(QStringLiteral("appearance/columns")).toStringList();
        if (columns.size() == downloadsTable_->columnCount())
            for (int col=0; col<columns.size(); ++col) downloadsTable_->setColumnHidden(col, columns.at(col) != QStringLiteral("1"));
        if (auto *b=findChild<QPushButton*>(QStringLiteral("toolbarTorrentButton"))) b->setVisible(appearance.value(QStringLiteral("appearance/toolbarTorrent"),true).toBool());
        if (auto *b=findChild<QPushButton*>(QStringLiteral("toolbarGrabLinksButton"))) b->setVisible(appearance.value(QStringLiteral("appearance/toolbarGrabLinks"),true).toBool());
        if (auto *b=findChild<QPushButton*>(QStringLiteral("toolbarQueuesButton"))) b->setVisible(appearance.value(QStringLiteral("appearance/toolbarQueues"),true).toBool());
    }

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
                startYtDlpDownload(url, youtube, kind, ytDlpPendingAudioOnly_);
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

    connect(queuesButton, &QPushButton::clicked, this, &MainWindow::showQueueManager);
    connect(grabButton, &QPushButton::clicked, this, &MainWindow::showLinkExtractor);
    connect(addButton_,&QPushButton::clicked,this,&MainWindow::addDownload);
    connect(urlEdit_,&QLineEdit::returnPressed,this,&MainWindow::addDownload);
    connect(pauseButton_,&QPushButton::clicked,this,&MainWindow::pauseSelected);
    connect(resumeButton_,&QPushButton::clicked,this,&MainWindow::resumeSelected);
    connect(cancelButton_,&QPushButton::clicked,this,&MainWindow::cancelSelected);
    connect(removeButton_,&QPushButton::clicked,this,&MainWindow::removeSelected);
    connect(openButton_,&QPushButton::clicked,this,&MainWindow::openSelected);
    connect(recheckButton_,&QPushButton::clicked,this,&MainWindow::recheckSelected);
    connect(downloadsTable_, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { showSelectedDetails(); });
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
        if (!torrent) {
            menu.addAction(QStringLiteral("Set / verify SHA-256…"), this, &MainWindow::configureChecksumSelected);
        }
        if (!torrent && !completed)
            menu.addAction(QStringLiteral("Cancel"), this, &MainWindow::cancelSelected);
        menu.addAction(QStringLiteral("Remove from history…"), this, &MainWindow::removeSelected);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Properties…"), this, &MainWindow::showSelectedProperties);
        menu.addAction(QStringLiteral("Download details…"), this, &MainWindow::showSelectedDetails);
        menu.addAction(QStringLiteral("Open"), this, &MainWindow::openSelected);
        menu.exec(downloadsTable_->viewport()->mapToGlobal(pos));
    });
    connect(settingsButton,&QPushButton::clicked,this,&MainWindow::showSettings);
    connect(updateButton,&QPushButton::clicked,this,&MainWindow::checkForUpdates);
    connect(syncButton,&QPushButton::clicked,this,&MainWindow::showSyncManager);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        QSettings cfg(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
        if (!cfg.value(QStringLiteral("clipboard/monitor"), false).toBool()) return;
        const QString text = QApplication::clipboard()->text().trimmed();
        static QString lastPrompted;
        if (text.isEmpty() || text == lastPrompted || text.size() > 8192) return;
        const QUrl url(text);
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) || url.host().isEmpty()) return;
        lastPrompted = text;
        if (QMessageBox::question(this, QStringLiteral("Add copied URL?"),
                QStringLiteral("Add this URL to Beatit downloads?\n\n%1").arg(text.left(500)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            urlEdit_->setText(text);
            addDownload();
            showFromTray();
        }
    });
    connect(downloadManager_, &DownloadManager::taskCompleted, this, [this](const QString &, const QString &path) {
        QSettings cfg(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
        const QString scanner = cfg.value(QStringLiteral("antivirus/program")).toString().trimmed();
        if (!scanner.isEmpty() && QFileInfo(scanner).isFile() && QFileInfo(path).isFile()) {
            QStringList args = QProcess::splitCommand(cfg.value(QStringLiteral("antivirus/arguments"), QStringLiteral("\"{file}\"")).toString());
            bool hasFile = false;
            for (QString &arg : args) {
                if (arg.contains(QStringLiteral("{file}"))) { arg.replace(QStringLiteral("{file}"), path); hasFile = true; }
                if (arg.contains(QStringLiteral("{filename}"))) arg.replace(QStringLiteral("{filename}"), QFileInfo(path).fileName());
            }
            if (!hasFile) args.append(path);
            auto *process = new QProcess(this);
            connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
                [this,process](int code,QProcess::ExitStatus state) {
                    statusLabel_->setText(state == QProcess::NormalExit && code == 0
                        ? QStringLiteral("Antivirus scan process completed successfully")
                        : QStringLiteral("Antivirus scan returned an error; inspect the scanner"));
                    process->deleteLater();
                });
            connect(process, &QProcess::errorOccurred, this, [this,process](QProcess::ProcessError) {
                statusLabel_->setText(QStringLiteral("Could not start antivirus scanner"));
                process->deleteLater();
            });
            QTimer::singleShot(10*60*1000, process, [this,process] {
                if(process->state()!=QProcess::NotRunning) {
                    process->kill();
                    statusLabel_->setText(QStringLiteral("Antivirus scan timed out after 10 minutes"));
                }
            });
            process->start(scanner, args);
        }
        if (cfg.value(QStringLiteral("power/shutdownOnComplete"), false).toBool()) {
            QTimer::singleShot(2500, this, [this] {
                bool any = false, allFinished = true;
                for (int row = 0; row < downloadsTable_->rowCount(); ++row) {
                    const QString state = downloadsTable_->item(row,1) ? downloadsTable_->item(row,1)->text().toLower() : QString();
                    if (state.isEmpty()) continue;
                    any = true;
                    const bool terminal = state.contains(QStringLiteral("completed")) || state.contains(QStringLiteral("failed")) ||
                        state.contains(QStringLiteral("cancelled")) || state.contains(QStringLiteral("seeding")) ||
                        state.contains(QStringLiteral("finished"));
                    if (!terminal) { allFinished = false; break; }
                }
                if (any && allFinished && QMessageBox::question(this, QStringLiteral("Downloads finished"),
                    QStringLiteral("All downloads have finished. Shut down Windows in 60 seconds?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                    QProcess::startDetached(QStringLiteral("shutdown.exe"), {QStringLiteral("/s"),QStringLiteral("/t"),QStringLiteral("60")});
                    statusLabel_->setText(QStringLiteral("Shutdown scheduled; run shutdown.exe /a to cancel"));
                }
            });
        }
    });
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
        downloadedBytes_[id] = done;
        totalBytes_[id] = total;
        currentDownloadSpeed_[id] = 0;
        currentUploadSpeed_[id] = 0;
        startedAt_[id] = QDateTime::currentSecsSinceEpoch();
        accentRow(downloadsTable_, row, false);
    });
    connect(downloadManager_,&DownloadManager::taskStarted,this,[this](const QString&id,const QString&f,qint64 total){
        const int row=rowForId(id);if(row<0)return;downloadsTable_->item(row,0)->setText(f);
        downloadsTable_->item(row,1)->setText("Downloading");downloadsTable_->item(row,2)->setText(total>0?"0%":"Live");
        totalBytes_[id] = total;
        startedAt_[id] = QDateTime::currentSecsSinceEpoch();
    });
    connect(downloadManager_,&DownloadManager::taskProgress,this,[this](const QString&id,qint64 done,qint64 total,qint64 speed){
        const int row=rowForId(id);if(row<0)return;
        setProgress(downloadsTable_,row,total>0?static_cast<int>(done*100/total):0,false);
        downloadedBytes_[id] = done;
        if (total >= 0) totalBytes_[id] = total;
        currentDownloadSpeed_[id] = speed;
        if (!startedAt_.contains(id)) startedAt_[id] = QDateTime::currentSecsSinceEpoch();
        downloadsTable_->item(row,3)->setText(formatSpeed(speed));statusLabel_->setText(QStringLiteral("%1 downloaded").arg(formatBytes(done)));
        accentRow(downloadsTable_, row, false);
    });
    connect(downloadManager_,&DownloadManager::taskPaused,this,[this](const QString&id,qint64 done){setStatus(id,"Paused");statusLabel_->setText(QStringLiteral("Paused at %1").arg(formatBytes(done)));});
    connect(downloadManager_,&DownloadManager::taskCompleted,this,[this](const QString&id,const QString&path){paths_[id]=path;setStatus(id,"Completed"); if(const int row=rowForId(id);row>=0){auto *date=downloadsTable_->item(row,5);if(date){const auto ts=QDateTime::currentSecsSinceEpoch();date->setText(QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));date->setData(Qt::UserRole,ts);}} trayIcon_->showMessage("Beatit","Download complete"); QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit")); const int action=settings.value(QStringLiteral("completion/action"),0).toInt(); if(action==1) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); else if(action==2) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));});
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
        downloadsTable_->setItem(row,5,date);
        downloadedBytes_[id] = done;
        totalBytes_[id] = total;
        currentDownloadSpeed_[id] = 0;
        currentUploadSpeed_[id] = 0;
        startedAt_[id] = QDateTime::currentSecsSinceEpoch();
        accentRow(downloadsTable_,row,true);
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
    connect(torrentEngine_,&TorrentEngine::torrentProgress,this,[this](const QString&id,int progress,qint64 done,qint64 total,qint64 down,qint64 up,int peers){
        const int row=rowForId(id);if(row<0)return;downloadsTable_->item(row,1)->setText(QStringLiteral("Torrent • %1 peers").arg(peers));
        setProgress(downloadsTable_,row,progress,true);downloadsTable_->item(row,3)->setText(formatSpeed(down));
        downloadedBytes_[id] = done;
        totalBytes_[id] = total;
        currentDownloadSpeed_[id] = down;
        currentUploadSpeed_[id] = up;
        if (!startedAt_.contains(id)) startedAt_[id] = QDateTime::currentSecsSinceEpoch();
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
void MainWindow::startYtDlpDownload(const QString &url, bool youtube, const QString &kind, bool audioOnly) {
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
    ytDlpPendingAudioOnly_ = audioOnly;
    ytDlpPendingArgs_ = {
        QStringLiteral("--no-playlist"), QStringLiteral("--newline"),
        QStringLiteral("--continue"),
        QStringLiteral("--retries"), QStringLiteral("10"),
        QStringLiteral("--fragment-retries"), QStringLiteral("10"),
        QStringLiteral("--file-access-retries"), QStringLiteral("3"),
        QStringLiteral("--concurrent-fragments"), QStringLiteral("4"),
        QStringLiteral("-o"), output
    };
    if (audioOnly) {
        ytDlpPendingArgs_ << QStringLiteral("-x")
                          << QStringLiteral("--audio-format") << QStringLiteral("mp3")
                          << QStringLiteral("--audio-quality") << QStringLiteral("0");
    }
    if (!ytDlpFormat_.isEmpty() && !audioOnly)
        ytDlpPendingArgs_ << QStringLiteral("-f") << ytDlpFormat_;
    if (youtube || kind == QStringLiteral("hls") || kind == QStringLiteral("dash"))
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
            this, [this, youtube, audioOnly](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(ytDlpProcess_->readAll()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;

        ytDlpProcess_->deleteLater();
        ytDlpProcess_ = nullptr;

        if (success) {
            ytDlpRetryAfterUpdate_ = false;
            ytDlpFormat_.clear();
            statusLabel_->setText(QStringLiteral("Stream download completed"));
            trayIcon_->showMessage(QStringLiteral("Beatit"),
                                   audioOnly ? QStringLiteral("MP3 download complete")
                                   : (youtube ? QStringLiteral("YouTube download complete")
                                              : QStringLiteral("Stream download complete")));
            return;
        }

        if (!ytDlpRetryAfterUpdate_) {
            ytDlpRetryAfterUpdate_ = true;
            statusLabel_->setText(QStringLiteral("yt-dlp failed — updating and retrying once…"));
            ytDlpManager_->updateNow();
            return;
        }

        ytDlpRetryAfterUpdate_ = false;
        ytDlpFormat_.clear();
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
        ytDlpFormat_.clear();
        statusLabel_->setText(message);
        trayIcon_->showMessage(QStringLiteral("Beatit"), message, QSystemTrayIcon::Warning);
    });

    ytDlpProcess_->start();
    statusLabel_->setText(audioOnly ? QStringLiteral("MP3 conversion started")
                                  : (youtube ? QStringLiteral("YouTube download started")
                                  : (kind == QStringLiteral("hls")
                                      ? QStringLiteral("HLS download started")
                                      : (kind == QStringLiteral("dash")
                                          ? QStringLiteral("DASH download started")
                                          : QStringLiteral("Stream download started")))));
}

void MainWindow::chooseVideoFormat(const QString &url, bool youtube, const QString &kind) {
    if (ytDlpProcess_) {
        statusLabel_->setText(QStringLiteral("A stream download is already running"));
        return;
    }

    const QString executable = ytDlpManager_->executablePath();
    if (executable.isEmpty()) {
        startYtDlpDownload(url, youtube, kind, false);
        return;
    }

    auto *probe = new QProcess(this);
    probe->setProgram(executable);
    probe->setArguments({
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--skip-download"),
        url
    });
    const QString toolsDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools"));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), toolsDir + QStringLiteral(";") + env.value(QStringLiteral("PATH")));
    probe->setProcessEnvironment(env);
    probe->setProcessChannelMode(QProcess::SeparateChannels);

    connect(probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, probe, url, youtube, kind](int code, QProcess::ExitStatus state) {
        const QByteArray raw = probe->readAllStandardOutput();
        const bool ok = state == QProcess::NormalExit && code == 0;
        probe->deleteLater();

        if (!ok) {
            statusLabel_->setText(QStringLiteral("Could not inspect available video formats"));
            startYtDlpDownload(url, youtube, kind, false);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            statusLabel_->setText(QStringLiteral("Could not parse video formats"));
            startYtDlpDownload(url, youtube, kind, false);
            return;
        }

        const QJsonObject root = doc.object();
        const QString titleText = root.value(QStringLiteral("title")).toString();
        struct Choice { QString label; QString selector; int height; };
        QList<Choice> choices;
        QSet<QString> selectors;
        choices.push_back({QStringLiteral("Best available quality"), QStringLiteral("bestvideo*+bestaudio/best"), INT_MAX});
        selectors.insert(choices.first().selector);

        const QJsonArray formats = root.value(QStringLiteral("formats")).toArray();
        for (const auto &value : formats) {
            const QJsonObject f = value.toObject();
            const QString id = f.value(QStringLiteral("format_id")).toString();
            const QString vcodec = f.value(QStringLiteral("vcodec")).toString();
            if (id.isEmpty() || vcodec.isEmpty() || vcodec == QStringLiteral("none")) continue;
            const int height = f.value(QStringLiteral("height")).toInt(0);
            if (height <= 0) continue;
            const QString ext = f.value(QStringLiteral("ext")).toString();
            const QString resolution = f.value(QStringLiteral("resolution")).toString(
                height > 0 ? QStringLiteral("%1p").arg(height) : QStringLiteral("video"));
            const double fps = f.value(QStringLiteral("fps")).toDouble(0);
            const QString acodec = f.value(QStringLiteral("acodec")).toString();
            const qint64 bytes = f.value(QStringLiteral("filesize")).toVariant().toLongLong();
            const qint64 approx = f.value(QStringLiteral("filesize_approx")).toVariant().toLongLong();
            const qint64 displayBytes = bytes > 0 ? bytes : approx;
            QString label = QStringLiteral("%1  •  %2").arg(resolution, ext.isEmpty() ? QStringLiteral("format") : ext);
            if (fps > 0) label += QStringLiteral("  •  %1 fps").arg(fps, 0, 'f', fps == static_cast<int>(fps) ? 0 : 1);
            if (displayBytes > 0) label += QStringLiteral("  •  %1").arg(formatBytes(displayBytes));
            if (!acodec.isEmpty() && acodec != QStringLiteral("none")) label += QStringLiteral("  •  audio");
            const QString selector = acodec.isEmpty() || acodec == QStringLiteral("none")
                ? id + QStringLiteral("+bestaudio/best") : id;
            if (selectors.contains(selector)) continue;
            selectors.insert(selector);
            choices.push_back({label, selector, height});
        }

        std::sort(choices.begin() + 1, choices.end(), [](const Choice &a, const Choice &b) {
            if (a.height != b.height) return a.height > b.height;
            return a.label < b.label;
        });

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Choose video quality"));
        dialog.resize(620, 520);
        auto *layout = new QVBoxLayout(&dialog);
        auto *heading = new QLabel(titleText.isEmpty() ? QStringLiteral("Video download") : titleText, &dialog);
        heading->setWordWrap(true);
        heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
        layout->addWidget(heading);
        auto *list = new QListWidget(&dialog);
        for (const auto &choice : choices) {
            auto *item = new QListWidgetItem(choice.label, list);
            item->setData(Qt::UserRole, choice.selector);
        }
        list->setCurrentRow(0);
        layout->addWidget(list, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem *) {
            dialog.accept();
        });

        if (dialog.exec() != QDialog::Accepted || !list->currentItem()) return;
        ytDlpFormat_ = list->currentItem()->data(Qt::UserRole).toString();
        startYtDlpDownload(url, youtube, kind, false);
    });
    connect(probe, &QProcess::errorOccurred, this, [this, probe, url, youtube, kind](QProcess::ProcessError) {
        probe->deleteLater();
        statusLabel_->setText(QStringLiteral("Video format inspection failed"));
        startYtDlpDownload(url, youtube, kind, false);
    });
    probe->start();
    statusLabel_->setText(QStringLiteral("Inspecting available video formats…"));
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
    const bool dash = path.endsWith(QStringLiteral(".mpd"));

    if (youtube || hls || dash || kind == QStringLiteral("youtube") || kind == QStringLiteral("hls") ||
        kind == QStringLiteral("dash") || kind == QStringLiteral("video") || kind == QStringLiteral("audio")) {
        const QString streamKind = hls || kind == QStringLiteral("hls") ? QStringLiteral("hls")
            : (dash || kind == QStringLiteral("dash") ? QStringLiteral("dash") : kind);
        if (kind == QStringLiteral("audio")) {
            startYtDlpDownload(url, youtube || kind == QStringLiteral("youtube"), streamKind, true);
        } else {
            chooseVideoFormat(url, youtube || kind == QStringLiteral("youtube"), streamKind);
        }
        return;
    }

    if (parsed.isValid() && (parsed.scheme() == QStringLiteral("http") ||
                             parsed.scheme() == QStringLiteral("https"))) {
        const QString category = categoryForUrl(url);
        const QString destination = categoryDestination(category);
        QDir().mkpath(destination);
        downloadManager_->addUrl(url, destination, category);
        statusLabel_->setText(QStringLiteral("Browser download queued — %1").arg(category));
    }
}

void MainWindow::showSelectedProperties() {
    const QString id = selectedId();
    if (id.isEmpty()) return;
    if (id.startsWith(QStringLiteral("torrent-"))) {
        QMessageBox::information(this, QStringLiteral("Properties"), QStringLiteral("Torrent properties are managed by the BitTorrent engine."));
        return;
    }
    const PersistedDownload stored = downloadManager_->downloadInfo(id);
    if (stored.id.isEmpty() || stored.type != QStringLiteral("http")) {
        QMessageBox::warning(this, QStringLiteral("Properties"), QStringLiteral("Download properties are unavailable."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Download Properties"));
    dialog.resize(620, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *source = new QLineEdit(stored.source, &dialog);
    auto *filename = new QLineEdit(stored.filename, &dialog);
    auto *destination = new QLineEdit(stored.destination, &dialog);
    auto *browse = new QPushButton(QStringLiteral("Browse…"), &dialog);
    auto *destinationRow = new QWidget(&dialog);
    auto *destinationLayout = new QHBoxLayout(destinationRow);
    destinationLayout->setContentsMargins(0,0,0,0);
    destinationLayout->addWidget(destination,1);
    destinationLayout->addWidget(browse);
    auto *category = new QLineEdit(stored.category.isEmpty() ? QStringLiteral("Other") : stored.category, &dialog);
    auto *description = new QLineEdit(stored.description, &dialog);
    auto *connections = new QSpinBox(&dialog);
    connections->setRange(1,8);
    connections->setValue(stored.connectionCount > 0 ? stored.connectionCount : downloadManager_->httpConnections());
    auto *sha = new QLineEdit(stored.sha256, &dialog);
    auto *userAgent = new QLineEdit(stored.userAgent, &dialog);
    userAgent->setPlaceholderText(QStringLiteral("Default if empty"));
    auto *proxyMode = new QComboBox(&dialog);
    proxyMode->addItems({QStringLiteral("Use global settings"), QStringLiteral("No proxy"), QStringLiteral("HTTP proxy"), QStringLiteral("SOCKS5 hostname")});
    proxyMode->setCurrentIndex(stored.proxyType < 0 ? 0 : stored.proxyType + 1);
    auto *proxyHost = new QLineEdit(stored.proxyHost, &dialog);
    auto *proxyPort = new QSpinBox(&dialog);
    proxyPort->setRange(0,65535);
    proxyPort->setValue(stored.proxyPort);
    sha->setPlaceholderText(QStringLiteral("Optional 64-character SHA-256"));
    form->addRow(QStringLiteral("Source URL"), source);
    form->addRow(QStringLiteral("Filename"), filename);
    form->addRow(QStringLiteral("Save to folder"), destinationRow);
    form->addRow(QStringLiteral("Category"), category);
    form->addRow(QStringLiteral("Description"), description);
    form->addRow(QStringLiteral("HTTP connections"), connections);
    form->addRow(QStringLiteral("Expected SHA-256"), sha);
    form->addRow(QStringLiteral("User-Agent"), userAgent);
    form->addRow(QStringLiteral("Proxy mode"), proxyMode);
    form->addRow(QStringLiteral("Proxy host"), proxyHost);
    form->addRow(QStringLiteral("Proxy port"), proxyPort);
    layout->addLayout(form);
    const QString status = downloadsTable_->item(rowForId(id),1) ? downloadsTable_->item(rowForId(id),1)->text().toLower() : QString();
    const bool active = status.contains(QStringLiteral("downloading")) || status.contains(QStringLiteral("resolving")) || status.contains(QStringLiteral("starting"));
    source->setEnabled(!active);
    filename->setEnabled(!active);
    destination->setEnabled(!active);
    browse->setEnabled(!active);
    if (active) {
        auto *note = new QLabel(QStringLiteral("Source URL, filename and destination are locked while this download is active. Other changes apply to future retries/resumes."), &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    connect(browse, &QPushButton::clicked, &dialog, [&dialog,destination] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Choose destination"), destination->text());
        if (!path.isEmpty()) destination->setText(path);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog,this,stored,source,filename,destination,category,description,connections,sha,userAgent,proxyMode,proxyHost,proxyPort] {
        PersistedDownload updated = stored;
        updated.source = source->text().trimmed();
        updated.filename = filename->text().trimmed();
        updated.destination = destination->text().trimmed();
        updated.category = category->text().trimmed();
        updated.description = description->text().trimmed();
        updated.connectionCount = connections->value();
        updated.sha256 = sha->text().trimmed();
        updated.userAgent = userAgent->text().trimmed();
        updated.proxyType = proxyMode->currentIndex() == 0 ? -1 : proxyMode->currentIndex() - 1;
        updated.proxyHost = proxyHost->text().trimmed();
        updated.proxyPort = proxyPort->value();
        if (!downloadManager_->updateProperties(updated)) {
            QMessageBox::warning(&dialog, QStringLiteral("Properties not saved"),
                QStringLiteral("Check the filename, destination and SHA-256 value. Active downloads cannot change filename or destination."));
            return;
        }
        const int row = rowForId(stored.id);
        if (row >= 0 && downloadsTable_->item(row, 0)) downloadsTable_->item(row, 0)->setText(updated.filename);
        paths_[stored.id] = updated.destination;
        statusLabel_->setText(QStringLiteral("Download properties saved"));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::showLinkExtractor() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Download All / Site Grabber"));
    dialog.resize(900, 720);
    auto *root = new QVBoxLayout(&dialog);
    auto *urlRow = new QHBoxLayout();
    auto *pageUrl = new QLineEdit(&dialog);
    pageUrl->setPlaceholderText(QStringLiteral("https://example.com/page or paste HTML below"));
    auto *fetch = new QPushButton(QStringLiteral("Extract links"), &dialog);
    auto *cancelScan = new QPushButton(QStringLiteral("Cancel scan"), &dialog);
    cancelScan->setEnabled(false);
    urlRow->addWidget(pageUrl,1); urlRow->addWidget(fetch); urlRow->addWidget(cancelScan);
    root->addLayout(urlRow);

    auto *htmlInput = new QPlainTextEdit(&dialog);
    htmlInput->setPlaceholderText(QStringLiteral("Optional: paste HTML or selected page text here to extract links without fetching a page."));
    htmlInput->setMaximumHeight(85);
    root->addWidget(htmlInput);
    auto *options = new QGridLayout();
    auto *siteGrabber = new QCheckBox(QStringLiteral("Crawl same-host HTML pages"), &dialog);
    auto *maxPages = new QSpinBox(&dialog); maxPages->setRange(1,500); maxPages->setValue(20);
    auto *maxDepth = new QSpinBox(&dialog); maxDepth->setRange(0,20); maxDepth->setValue(3);
    auto *includePattern = new QLineEdit(&dialog); includePattern->setPlaceholderText(QStringLiteral("Optional regex: include matching URLs"));
    auto *excludePattern = new QLineEdit(&dialog); excludePattern->setPlaceholderText(QStringLiteral("Optional regex: exclude matching URLs"));
    auto *extensions = new QLineEdit(&dialog); extensions->setPlaceholderText(QStringLiteral("Optional extensions: pdf,zip,mp4"));
    QSettings grabberSettings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    auto *schedule = new QCheckBox(QStringLiteral("Schedule recurring background scans"), &dialog);
    schedule->setChecked(grabberSettings.value(QStringLiteral("siteGrabber/scheduleEnabled"), false).toBool());
    auto *scheduleHours = new QSpinBox(&dialog); scheduleHours->setRange(1,168); scheduleHours->setValue(grabberSettings.value(QStringLiteral("siteGrabber/scheduleHours"),24).toInt());
    if (schedule->isChecked()) {
        const QJsonDocument savedSchedule=QJsonDocument::fromJson(grabberSettings.value(QStringLiteral("siteGrabber/scheduleConfig")).toByteArray());
        if(savedSchedule.isObject()) {
            const QJsonObject saved=savedSchedule.object();
            pageUrl->setText(saved.value(QStringLiteral("url")).toString());
            htmlInput->setPlainText(saved.value(QStringLiteral("html")).toString());
            siteGrabber->setChecked(saved.value(QStringLiteral("siteGrabber")).toBool());
            maxPages->setValue(qBound(1,saved.value(QStringLiteral("maxPages")).toInt(20),500));
            maxDepth->setValue(qBound(0,saved.value(QStringLiteral("maxDepth")).toInt(3),20));
            includePattern->setText(saved.value(QStringLiteral("include")).toString());
            excludePattern->setText(saved.value(QStringLiteral("exclude")).toString());
            extensions->setText(saved.value(QStringLiteral("extensions")).toString());
        }
    }
    options->addWidget(siteGrabber,0,0,1,2);
    options->addWidget(new QLabel(QStringLiteral("Page limit"),&dialog),1,0); options->addWidget(maxPages,1,1);
    options->addWidget(new QLabel(QStringLiteral("Depth limit"),&dialog),1,2); options->addWidget(maxDepth,1,3);
    options->addWidget(new QLabel(QStringLiteral("Include URL regex"),&dialog),2,0); options->addWidget(includePattern,2,1,1,3);
    options->addWidget(new QLabel(QStringLiteral("Exclude URL regex"),&dialog),3,0); options->addWidget(excludePattern,3,1,1,3);
    options->addWidget(new QLabel(QStringLiteral("File extensions"),&dialog),4,0); options->addWidget(extensions,4,1,1,3);
    options->addWidget(schedule,5,0,1,2); options->addWidget(new QLabel(QStringLiteral("Every hours"),&dialog),5,2); options->addWidget(scheduleHours,5,3);
    root->addLayout(options);
    auto *projectRow = new QHBoxLayout();
    auto *saveProject = new QPushButton(QStringLiteral("Save project…"),&dialog);
    auto *loadProject = new QPushButton(QStringLiteral("Load project…"),&dialog);
    projectRow->addWidget(saveProject); projectRow->addWidget(loadProject); projectRow->addStretch();
    root->addLayout(projectRow);
    auto *status = new QLabel(QStringLiteral("Paste a page URL or HTML. Crawling is bounded by page and depth limits."), &dialog);
    status->setWordWrap(true); root->addWidget(status);
    auto *links = new QListWidget(&dialog);
    links->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(links,1);
    auto *actions = new QHBoxLayout();
    auto *selectAll = new QPushButton(QStringLiteral("Select all"), &dialog);
    auto *selectNone = new QPushButton(QStringLiteral("Select none"), &dialog);
    auto *downloadSelected = new QPushButton(QStringLiteral("Download selected"), &dialog);
    auto *previewZip = new QPushButton(QStringLiteral("Preview remote ZIP"), &dialog);
    auto *downloadAll = new QPushButton(QStringLiteral("Download All"), &dialog);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    actions->addWidget(selectAll); actions->addWidget(selectNone); actions->addStretch();
    actions->addWidget(previewZip); actions->addWidget(downloadSelected); actions->addWidget(downloadAll); actions->addWidget(close);
    root->addLayout(actions);

    auto *network = new QNetworkAccessManager(&dialog);
    QSet<QString> seenPages, seenLinks;
    QStringList pendingPages;
    QHash<QString,int> pageDepth;
    QSet<QNetworkReply*> activeReplies;
    int pageCount = 0;
    QString baseHost;
    bool cancelled = false;
    auto crawlFn = std::make_shared<std::function<void(QUrl)>>();
    std::weak_ptr<std::function<void(QUrl)>> weakCrawl = crawlFn;
    auto acceptedByPatterns = [includePattern,excludePattern](const QString &url) {
        if (!includePattern->text().trimmed().isEmpty()) {
            const QRegularExpression re(includePattern->text().trimmed(), QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid() || !re.match(url).hasMatch()) return false;
        }
        if (!excludePattern->text().trimmed().isEmpty()) {
            const QRegularExpression re(excludePattern->text().trimmed(), QRegularExpression::CaseInsensitiveOption);
            if (re.isValid() && re.match(url).hasMatch()) return false;
        }
        return true;
    };
    auto acceptedByFilters = [acceptedByPatterns,extensions](const QString &url) {
        if (!acceptedByPatterns(url)) return false;
        const QString filter = extensions->text().trimmed().toLower();
        if (filter.isEmpty()) return true;
        const QString ext = QFileInfo(QUrl(url).path()).suffix().toLower();
        QStringList allowed;
        for (QString token : filter.split(',', Qt::SkipEmptyParts)) {
            token = token.trimmed(); if (token.startsWith('.')) token.remove(0,1);
            if (!token.isEmpty()) allowed.append(token);
        }
        return !allowed.isEmpty() && allowed.contains(ext);
    };
    auto addHtmlLinks = [&,acceptedByPatterns,acceptedByFilters](const QString &html, const QUrl &page, int depth) {
        static const QRegularExpression href(QStringLiteral(R"re(href\s*=\s*["']([^"']+)["'])re"), QRegularExpression::CaseInsensitiveOption);
        auto it = href.globalMatch(html);
        while (it.hasNext()) {
            const auto match = it.next();
            QUrl target = page.resolved(QUrl(match.captured(1).trimmed()));
            target.setFragment(QString());
            const QString scheme = target.scheme().toLower();
            if (!target.isValid() || target.host().isEmpty() ||
                (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) continue;
            const QString key = target.toString(QUrl::FullyEncoded);
            if (!acceptedByPatterns(key)) continue;
            if (acceptedByFilters(key) && !seenLinks.contains(key)) {
                seenLinks.insert(key);
                auto *item = new QListWidgetItem(key, links);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            }
            const QString path = target.path().toLower();
            const bool htmlPage = path.isEmpty() || path.endsWith(QStringLiteral(".html")) ||
                path.endsWith(QStringLiteral(".htm")) || path.endsWith(QStringLiteral(".php")) ||
                path.endsWith(QStringLiteral(".asp")) || path.endsWith(QStringLiteral(".aspx"));
            if (siteGrabber->isChecked() && depth < maxDepth->value() &&
                target.host().compare(baseHost, Qt::CaseInsensitive) == 0 && htmlPage &&
                !seenPages.contains(key) && pageCount + pendingPages.size() < maxPages->value()) {
                pendingPages.append(key);
                pageDepth.insert(key, depth + 1);
            }
        }
        static const QRegularExpression rawUrl(QStringLiteral(R"re(https?://[^\s"'<>]+)re"), QRegularExpression::CaseInsensitiveOption);
        auto raw = rawUrl.globalMatch(html);
        while (raw.hasNext()) {
            QString value = raw.next().captured(0);
            while (!value.isEmpty() && QStringLiteral(".,;:!?) ]}").contains(value.back())) value.chop(1);
            QUrl target(value);
            target.setFragment(QString());
            const QString scheme = target.scheme().toLower();
            if (!target.isValid() || target.host().isEmpty() ||
                (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) continue;
            const QString key = target.toString(QUrl::FullyEncoded);
            if (!acceptedByPatterns(key)) continue;
            if (acceptedByFilters(key) && !seenLinks.contains(key)) {
                seenLinks.insert(key);
                auto *item = new QListWidgetItem(key, links);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            }
            const QString path = target.path().toLower();
            const bool htmlPage = path.isEmpty() || path.endsWith(QStringLiteral(".html")) ||
                path.endsWith(QStringLiteral(".htm")) || path.endsWith(QStringLiteral(".php")) ||
                path.endsWith(QStringLiteral(".asp")) || path.endsWith(QStringLiteral(".aspx"));
            if (siteGrabber->isChecked() && depth < maxDepth->value() &&
                target.host().compare(baseHost, Qt::CaseInsensitive) == 0 && htmlPage &&
                !seenPages.contains(key) && pageCount + pendingPages.size() < maxPages->value()) {
                pendingPages.append(key);
                pageDepth.insert(key, depth + 1);
            }
        }
    };
    *crawlFn = [&,weakCrawl](QUrl page) {
        if (cancelled) return;
        page.setFragment(QString());
        const QString normalized = page.toString(QUrl::FullyEncoded);
        if (seenPages.contains(normalized) || pageCount >= maxPages->value()) {
            if (pendingPages.isEmpty()) { status->setText(QStringLiteral("Extracted %1 unique links from %2 pages.").arg(links->count()).arg(pageCount)); fetch->setEnabled(true); cancelScan->setEnabled(false); }
            return;
        }
        seenPages.insert(normalized);
        ++pageCount;
        status->setText(QStringLiteral("Fetching page %1 of %2 (depth %3): %4").arg(pageCount).arg(maxPages->value()).arg(pageDepth.value(normalized,0)).arg(page.host()));
        QNetworkRequest request(page);
        request.setRawHeader("User-Agent", "BeatitDownloadManager/0.1");
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = network->get(request);
        activeReplies.insert(reply);
        connect(reply, &QNetworkReply::finished, &dialog, [&,reply,page,weakCrawl] {
            activeReplies.remove(reply);
            const QByteArray html = reply->readAll();
            const auto netError = reply->error();
            const QString finalHost = reply->url().host();
            reply->deleteLater();
            const bool sameHost = finalHost.compare(baseHost, Qt::CaseInsensitive) == 0;
            if (!cancelled && netError == QNetworkReply::NoError && (!siteGrabber->isChecked() || sameHost))
                addHtmlLinks(QString::fromUtf8(html), page, pageDepth.value(page.toString(QUrl::FullyEncoded),0));
            if (cancelled) return;
            if (!pendingPages.isEmpty() && pageCount < maxPages->value()) {
                const QUrl next(pendingPages.takeFirst());
                if (auto nextFetch = weakCrawl.lock()) (*nextFetch)(next);
            } else {
                status->setText(QStringLiteral("Extracted %1 unique links from %2 pages%3.")
                    .arg(links->count()).arg(pageCount).arg(netError == QNetworkReply::NoError ? QString() : QStringLiteral(" (some pages failed)")));
                fetch->setEnabled(true); cancelScan->setEnabled(false);
            }
        });
    };
    auto runExtraction = [&] {
        cancelled = false;
        if (!htmlInput->toPlainText().trimmed().isEmpty()) {
            const QUrl base(pageUrl->text().trimmed());
            const QUrl safeBase = base.isValid() && !base.host().isEmpty() ? base : QUrl(QStringLiteral("https://paste.invalid/"));
            links->clear(); seenPages.clear(); seenLinks.clear(); pendingPages.clear(); pageDepth.clear(); pageCount = 1;
            baseHost = safeBase.host();
            addHtmlLinks(htmlInput->toPlainText(), safeBase, 0);
            status->setText(QStringLiteral("Extracted %1 unique links from pasted HTML/text.").arg(links->count()));
            return;
        }
        const QUrl url(pageUrl->text().trimmed());
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            QMessageBox::warning(&dialog, QStringLiteral("Invalid URL"), QStringLiteral("Enter a valid HTTP/HTTPS URL or paste HTML."));
            return;
        }
        links->clear(); seenPages.clear(); seenLinks.clear(); pendingPages.clear(); pageDepth.clear(); pageCount = 0;
        baseHost = url.host(); pageDepth.insert(url.toString(QUrl::FullyEncoded),0);
        fetch->setEnabled(false); cancelScan->setEnabled(true);
        (*crawlFn)(url);
    };
    connect(fetch, &QPushButton::clicked, &dialog, runExtraction);
    connect(cancelScan, &QPushButton::clicked, &dialog, [&] {
        cancelled = true; pendingPages.clear();
        for (QNetworkReply *reply : activeReplies) if (reply) reply->abort();
        activeReplies.clear(); fetch->setEnabled(true); cancelScan->setEnabled(false);
        status->setText(QStringLiteral("Scan cancelled. Already extracted links are retained."));
    });
    connect(selectAll, &QPushButton::clicked, &dialog, [links] { for (int i=0;i<links->count();++i) links->item(i)->setCheckState(Qt::Checked); });
    connect(selectNone, &QPushButton::clicked, &dialog, [links] { for (int i=0;i<links->count();++i) links->item(i)->setCheckState(Qt::Unchecked); });
    auto enqueue = [this,links,status](bool all) {
        int count = 0;
        for (int i=0;i<links->count();++i) {
            auto *item = links->item(i);
            if (!all && item->checkState() != Qt::Checked) continue;
            const QString url = item->text();
            const QString category = categoryForUrl(url);
            const QString destination = categoryDestination(category);
            QDir().mkpath(destination);
            downloadManager_->addUrl(url,destination,category);
            ++count;
        }
        status->setText(QStringLiteral("Queued %1 downloads.").arg(count));
    };
    connect(previewZip, &QPushButton::clicked, &dialog, [this,links,pageUrl] {
        QString target;
        for(int i=0;i<links->count();++i) if(links->item(i)->checkState()==Qt::Checked && QUrl(links->item(i)->text()).path().endsWith(QStringLiteral(".zip"),Qt::CaseInsensitive)) {target=links->item(i)->text();break;}
        if(target.isEmpty() && QUrl(pageUrl->text().trimmed()).path().endsWith(QStringLiteral(".zip"),Qt::CaseInsensitive)) target=pageUrl->text().trimmed();
        if(target.isEmpty()) {QMessageBox::information(this,QStringLiteral("ZIP preview"),QStringLiteral("Select a checked .zip link or enter a direct ZIP URL."));return;}
        previewRemoteZip(target);
    });
    connect(downloadSelected, &QPushButton::clicked, &dialog, [enqueue] { enqueue(false); });
    connect(downloadAll, &QPushButton::clicked, &dialog, [enqueue] { enqueue(true); });
    auto saveValues = [&] {
        QJsonObject obj;
        obj.insert(QStringLiteral("url"),pageUrl->text());
        obj.insert(QStringLiteral("html"),htmlInput->toPlainText());
        obj.insert(QStringLiteral("siteGrabber"),siteGrabber->isChecked());
        obj.insert(QStringLiteral("maxPages"),maxPages->value());
        obj.insert(QStringLiteral("maxDepth"),maxDepth->value());
        obj.insert(QStringLiteral("include"),includePattern->text());
        obj.insert(QStringLiteral("exclude"),excludePattern->text());
        obj.insert(QStringLiteral("extensions"),extensions->text());
        obj.insert(QStringLiteral("schedule"),schedule->isChecked());
        obj.insert(QStringLiteral("scheduleHours"),scheduleHours->value());
        return obj;
    };
    auto persistSchedule = [&] {
        grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleEnabled"), schedule->isChecked());
        grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleHours"), scheduleHours->value());
        grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"), QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));
        if (schedule->isChecked())
            grabberSettings.setValue(QStringLiteral("siteGrabber/nextRun"), QDateTime::currentSecsSinceEpoch() + scheduleHours->value()*3600);
        else
            grabberSettings.remove(QStringLiteral("siteGrabber/nextRun"));
    };
    connect(schedule,&QCheckBox::toggled,&dialog,[&](bool){persistSchedule();});
    connect(scheduleHours,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){if(schedule->isChecked())persistSchedule();});
    connect(pageUrl,&QLineEdit::textChanged,&dialog,[&](const QString&){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(htmlInput,&QPlainTextEdit::textChanged,&dialog,[&]{if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(siteGrabber,&QCheckBox::toggled,&dialog,[&](bool){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(maxPages,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(maxDepth,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(includePattern,&QLineEdit::textChanged,&dialog,[&](const QString&){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(excludePattern,&QLineEdit::textChanged,&dialog,[&](const QString&){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(extensions,&QLineEdit::textChanged,&dialog,[&](const QString&){if(schedule->isChecked())grabberSettings.setValue(QStringLiteral("siteGrabber/scheduleConfig"),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));});
    connect(saveProject,&QPushButton::clicked,&dialog,[&] {
        bool ok=false;
        const QString name=QInputDialog::getText(&dialog,QStringLiteral("Save Site Grabber project"),QStringLiteral("Project name"),QLineEdit::Normal,QString(),&ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QSettings cfg(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        cfg.setValue(QStringLiteral("siteGrabber/projects/%1").arg(name),QJsonDocument(saveValues()).toJson(QJsonDocument::Compact));
        status->setText(QStringLiteral("Saved project: %1").arg(name));
    });
    connect(loadProject,&QPushButton::clicked,&dialog,[&] {
        QSettings cfg(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        cfg.beginGroup(QStringLiteral("siteGrabber/projects"));
        const QStringList names=cfg.childGroups();
        cfg.endGroup();
        if (names.isEmpty()) { QMessageBox::information(&dialog,QStringLiteral("No saved projects"),QStringLiteral("Save a Site Grabber project first.")); return; }
        bool ok=false;
        const QString name=QInputDialog::getItem(&dialog,QStringLiteral("Load Site Grabber project"),QStringLiteral("Project"),names,0,false,&ok);
        if (!ok || name.isEmpty()) return;
        const auto doc=QJsonDocument::fromJson(cfg.value(QStringLiteral("siteGrabber/projects/%1").arg(name)).toByteArray());
        if (!doc.isObject()) { QMessageBox::warning(&dialog,QStringLiteral("Invalid project"),QStringLiteral("The saved project could not be read.")); return; }
        const auto obj=doc.object();
        pageUrl->setText(obj.value(QStringLiteral("url")).toString());
        htmlInput->setPlainText(obj.value(QStringLiteral("html")).toString());
        siteGrabber->setChecked(obj.value(QStringLiteral("siteGrabber")).toBool());
        maxPages->setValue(qBound(1,obj.value(QStringLiteral("maxPages")).toInt(20),500));
        maxDepth->setValue(qBound(0,obj.value(QStringLiteral("maxDepth")).toInt(3),20));
        includePattern->setText(obj.value(QStringLiteral("include")).toString());
        excludePattern->setText(obj.value(QStringLiteral("exclude")).toString());
        extensions->setText(obj.value(QStringLiteral("extensions")).toString());
        schedule->setChecked(obj.value(QStringLiteral("schedule")).toBool());
        scheduleHours->setValue(qBound(1,obj.value(QStringLiteral("scheduleHours")).toInt(24),168));
        status->setText(QStringLiteral("Loaded project: %1").arg(name));
    });
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.exec();
}


void MainWindow::previewRemoteZip(const QString &url) {
    const QUrl parsed(url);
    if(!parsed.isValid()||parsed.host().isEmpty()||(parsed.scheme()!=QStringLiteral("http")&&parsed.scheme()!=QStringLiteral("https"))) {
        QMessageBox::warning(this,QStringLiteral("ZIP preview"),QStringLiteral("Only direct HTTP/HTTPS ZIP URLs are supported."));return;
    }
    auto *network=new QNetworkAccessManager(this);
    auto fail=[this,network](const QString &message) {
        QMessageBox::warning(this,QStringLiteral("ZIP preview unavailable"),message);
        network->deleteLater();
    };
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent","BeatitDownloadManager/0.1");
    request.setRawHeader("Range","bytes=-65557");
    request.setTransferTimeout(20000);
    QNetworkReply *tail=network->get(request);
    connect(tail,&QNetworkReply::downloadProgress,tail,[tail](qint64 received,qint64){if(received>2*1024*1024)tail->abort();});
    connect(tail,&QNetworkReply::finished,this,[this,network,tail,parsed,fail] {
        const QByteArray data=tail->readAll();
        const int status=tail->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString range=QString::fromLatin1(tail->rawHeader("Content-Range"));
        tail->deleteLater();
        if(status!=206 || data.size()>2*1024*1024) {fail(QStringLiteral("The server did not honor a bounded HTTP Range request. No full ZIP download was started."));return;}
        static const QRegularExpression contentRange(QStringLiteral(R"re(^bytes\s+(\d+)-(\d+)/(\d+)$)re"),QRegularExpression::CaseInsensitiveOption);
        const auto rangeMatch=contentRange.match(range);
        if(!rangeMatch.hasMatch()) {fail(QStringLiteral("The server returned an invalid Content-Range header."));return;}
        const quint64 totalSize=rangeMatch.captured(3).toULongLong();
        int eocd=-1;
        for(int pos=data.size()-22;pos>=qMax(0,data.size()-65557);--pos) {
            if(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()+pos))==0x06054b50u) {eocd=pos;break;}
        }
        if(eocd<0 || eocd+22>data.size()) {fail(QStringLiteral("Could not find the ZIP end-of-central-directory record. The archive may be unsupported or malformed."));return;}
        const auto *e=reinterpret_cast<const uchar*>(data.constData()+eocd);
        const quint16 entries=qFromLittleEndian<quint16>(e+10);
        const quint32 cdSize=qFromLittleEndian<quint32>(e+12);
        const quint32 cdOffset=qFromLittleEndian<quint32>(e+16);
        if(entries==0xffff || cdSize==0xffffffffu || cdOffset==0xffffffffu) {fail(QStringLiteral("ZIP64 archives are not supported by the remote preview yet."));return;}
        if(entries>50000 || cdSize>16*1024*1024 || quint64(cdOffset)+cdSize>totalSize || cdSize==0) {fail(QStringLiteral("The archive directory exceeds the safe preview limits or is malformed."));return;}
        QNetworkRequest directoryRequest(parsed);
        directoryRequest.setRawHeader("User-Agent","BeatitDownloadManager/0.1");
        directoryRequest.setRawHeader("Range",QByteArray("bytes=")+QByteArray::number(cdOffset)+"-"+QByteArray::number(quint64(cdOffset)+cdSize-1));
        directoryRequest.setTransferTimeout(20000);
        QNetworkReply *directory=network->get(directoryRequest);
        connect(directory,&QNetworkReply::downloadProgress,directory,[directory](qint64 received,qint64){if(received>16*1024*1024)directory->abort();});
        connect(directory,&QNetworkReply::finished,this,[this,network,directory,entries,cdSize] {
            const QByteArray bytes=directory->readAll();
            const int status=directory->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            directory->deleteLater();
            if(status!=206 || quint64(bytes.size())!=cdSize) {
                QMessageBox::warning(this,QStringLiteral("ZIP preview unavailable"),QStringLiteral("The server did not return the complete central directory using HTTP Range requests."));
                network->deleteLater();return;
            }
            QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Remote ZIP contents"));dialog.resize(700,520);
            auto *layout=new QVBoxLayout(&dialog);
            auto *summary=new QLabel(&dialog);summary->setWordWrap(true);layout->addWidget(summary);
            auto *list=new QListWidget(&dialog);layout->addWidget(list,1);
            quint64 totalExpanded=0;int unsafe=0;int suspicious=0;int parsedEntries=0;int pos=0;
            while(pos+46<=bytes.size() && parsedEntries<entries) {
                const auto *entry=reinterpret_cast<const uchar*>(bytes.constData()+pos);
                if(qFromLittleEndian<quint32>(entry)!=0x02014b50u) break;
                const quint32 compressed=qFromLittleEndian<quint32>(entry+20);
                const quint32 expanded=qFromLittleEndian<quint32>(entry+24);
                const quint16 nameLen=qFromLittleEndian<quint16>(entry+28);
                const quint16 extraLen=qFromLittleEndian<quint16>(entry+30);
                const quint16 commentLen=qFromLittleEndian<quint16>(entry+32);
                const quint16 flags=qFromLittleEndian<quint16>(entry+8);
                if(pos+46+nameLen+extraLen+commentLen>bytes.size()) break;
                const QByteArray rawName=bytes.mid(pos+46,nameLen);
                QString name=QString::fromUtf8(rawName);
                if(!(flags&0x0800) && name.contains(QChar::ReplacementCharacter)) name=QString::fromLocal8Bit(rawName);
                QString normalized=name;normalized.replace('\\','/');
                const bool bad=normalized.startsWith('/') || QRegularExpression(QStringLiteral(R"re(^[A-Za-z]:)re")).match(normalized).hasMatch() ||
                    normalized.split('/',Qt::KeepEmptyParts).contains(QStringLiteral(".."));
                if(bad) ++unsafe;
                if(compressed==0 ? expanded>0 : quint64(expanded)>quint64(compressed)*1000) ++suspicious;
                totalExpanded+=expanded;
                auto *item=new QListWidgetItem(QStringLiteral("%1    %2").arg(bad?QStringLiteral("⚠ unsafe path"):QStringLiteral("•")).arg(name),list);
                item->setToolTip(QStringLiteral("Compressed: %1 bytes; expanded: %2 bytes").arg(compressed).arg(expanded));
                if(bad) item->setForeground(QColor(QStringLiteral("#d94a4a")));
                ++parsedEntries;pos+=46+nameLen+extraLen+commentLen;
            }
            summary->setText(QStringLiteral("%1 entries shown; %2 total expanded bytes. Unsafe paths: %3. Extreme compression ratios: %4. This is a preview only; no extraction occurred.")
                .arg(parsedEntries).arg(totalExpanded).arg(unsafe).arg(suspicious));
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            dialog.exec();network->deleteLater();
        });
    });
}

void MainWindow::runScheduledSiteGrabber() {
    if (siteGrabberScheduleRunning_) return;
    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    if (!settings.value(QStringLiteral("siteGrabber/scheduleEnabled"), false).toBool()) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (settings.value(QStringLiteral("siteGrabber/nextRun"), 0).toLongLong() > now) return;
    const QJsonDocument doc = QJsonDocument::fromJson(settings.value(QStringLiteral("siteGrabber/scheduleConfig")).toByteArray());
    if (!doc.isObject()) {
        settings.setValue(QStringLiteral("siteGrabber/nextRun"), now + settings.value(QStringLiteral("siteGrabber/scheduleHours"),24).toInt()*3600);
        return;
    }
    const QJsonObject cfg = doc.object();
    const QUrl startUrl(cfg.value(QStringLiteral("url")).toString().trimmed());
    if (!startUrl.isValid() || startUrl.host().isEmpty() ||
        (startUrl.scheme() != QStringLiteral("http") && startUrl.scheme() != QStringLiteral("https")) ||
        !cfg.value(QStringLiteral("html")).toString().trimmed().isEmpty()) {
        settings.setValue(QStringLiteral("siteGrabber/nextRun"), now + settings.value(QStringLiteral("siteGrabber/scheduleHours"),24).toInt()*3600);
        return;
    }
    siteGrabberScheduleRunning_ = true;
    const QString baseHost = startUrl.host();
    const int maxPages = qBound(1,cfg.value(QStringLiteral("maxPages")).toInt(20),500);
    const int maxDepth = qBound(0,cfg.value(QStringLiteral("maxDepth")).toInt(3),20);
    const bool doCrawl = cfg.value(QStringLiteral("siteGrabber")).toBool();
    const QString include = cfg.value(QStringLiteral("include")).toString().trimmed();
    const QString exclude = cfg.value(QStringLiteral("exclude")).toString().trimmed();
    const QString extensionFilter = cfg.value(QStringLiteral("extensions")).toString().trimmed().toLower();
    auto *network = new QNetworkAccessManager(this);
    auto seenPages = std::make_shared<QSet<QString>>();
    auto seenLinks = std::make_shared<QSet<QString>>();
    auto pending = std::make_shared<QStringList>();
    auto depths = std::make_shared<QHash<QString,int>>();
    auto links = std::make_shared<QStringList>();
    auto count = std::make_shared<int>(0);
    auto finished = std::make_shared<bool>(false);
    auto finish = [this,network,links,finished]() {
        if (*finished) return;
        *finished = true;
        for (const QString &url : *links) {
            const QString category = categoryForUrl(url);
            const QString destination = categoryDestination(category);
            QDir().mkpath(destination);
            downloadManager_->addUrl(url,destination,category);
        }
        QSettings cfg(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        const int hours = qBound(1,cfg.value(QStringLiteral("siteGrabber/scheduleHours"),24).toInt(),168);
        cfg.setValue(QStringLiteral("siteGrabber/nextRun"),QDateTime::currentSecsSinceEpoch()+hours*3600);
        siteGrabberScheduleRunning_ = false;
        statusLabel_->setText(QStringLiteral("Scheduled Site Grabber queued %1 links").arg(links->size()));
        network->deleteLater();
    };
    auto crawl = std::make_shared<std::function<void(QUrl)>>();
    std::weak_ptr<std::function<void(QUrl)>> weakCrawl = crawl;
    *crawl = [this,network,baseHost,maxPages,maxDepth,doCrawl,include,exclude,extensionFilter,seenPages,seenLinks,pending,depths,links,count,finish,weakCrawl](QUrl page) {
        page.setFragment(QString());
        const QString key = page.toString(QUrl::FullyEncoded);
        if (seenPages->contains(key) || *count >= maxPages) {
            if (pending->isEmpty()) finish();
            return;
        }
        seenPages->insert(key); ++(*count);
        QNetworkRequest request(page);
        request.setRawHeader("User-Agent","BeatitDownloadManager/0.1");
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = network->get(request);
        connect(reply,&QNetworkReply::finished,this,[this,reply,page,baseHost,maxPages,maxDepth,doCrawl,include,exclude,extensionFilter,seenPages,seenLinks,pending,depths,links,count,finish,weakCrawl] {
            const QByteArray html=reply->readAll();
            const bool success=reply->error()==QNetworkReply::NoError;
            const QString finalHost=reply->url().host();
            reply->deleteLater();
            if (success && finalHost.compare(baseHost,Qt::CaseInsensitive)==0) {
                static const QRegularExpression href(QStringLiteral(R"re(href\s*=\s*["']([^"']+)["'])re"),QRegularExpression::CaseInsensitiveOption);
                auto it=href.globalMatch(QString::fromUtf8(html));
                while(it.hasNext()) {
                    QUrl target=page.resolved(QUrl(it.next().captured(1).trimmed())); target.setFragment(QString());
                    if(!target.isValid()||target.host().isEmpty()||(target.scheme()!=QStringLiteral("http")&&target.scheme()!=QStringLiteral("https"))) continue;
                    const QString url=target.toString(QUrl::FullyEncoded);
                    if(!include.isEmpty()){const QRegularExpression re(include,QRegularExpression::CaseInsensitiveOption);if(!re.isValid()||!re.match(url).hasMatch())continue;}
                    if(!exclude.isEmpty()){const QRegularExpression re(exclude,QRegularExpression::CaseInsensitiveOption);if(re.isValid()&&re.match(url).hasMatch())continue;}
                    const bool allowedFileType=[&] {
                        if(extensionFilter.isEmpty())return true;
                        QStringList exts;for(QString e:extensionFilter.split(',',Qt::SkipEmptyParts)){e=e.trimmed();if(e.startsWith('.'))e.remove(0,1);if(!e.isEmpty())exts.append(e);}
                        return !exts.isEmpty()&&exts.contains(QFileInfo(target.path()).suffix().toLower());
                    }();
                    if(allowedFileType&&!seenLinks->contains(url)){seenLinks->insert(url);links->append(url);}
                    const QString path=target.path().toLower();
                    const bool htmlPage=path.isEmpty()||path.endsWith(QStringLiteral(".html"))||path.endsWith(QStringLiteral(".htm"))||path.endsWith(QStringLiteral(".php"))||path.endsWith(QStringLiteral(".aspx"));
                    const int depth=depths->value(page.toString(QUrl::FullyEncoded),0);
                    if(doCrawl && target.host().compare(baseHost,Qt::CaseInsensitive)==0 && htmlPage && depth<maxDepth && !seenPages->contains(url) && *count+pending->size()<maxPages){pending->append(url);depths->insert(url,depth+1);}
                }
            }
            if(!pending->isEmpty()&&*count<maxPages){const QUrl next(pending->takeFirst());if(auto fn=weakCrawl.lock())(*fn)(next);}
            else finish();
        });
    };
    depths->insert(startUrl.toString(QUrl::FullyEncoded),0);
    (*crawl)(startUrl);
}

void MainWindow::showSelectedDetails(){
    const QString id = selectedId();
    if (id.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Download details"));
    dialog.resize(560, 390);
    auto *root = new QVBoxLayout(&dialog);
    auto *title = new QLabel(&dialog);
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;"));
    title->setWordWrap(true);
    root->addWidget(title);

    auto *form = new QFormLayout();
    auto *status = new QLabel(&dialog);
    auto *size = new QLabel(&dialog);
    auto *location = new QLabel(&dialog);
    auto *down = new QLabel(&dialog);
    auto *avg = new QLabel(&dialog);
    auto *up = new QLabel(&dialog);
    auto *eta = new QLabel(&dialog);
    auto *source = new QLabel(&dialog);
    source->setWordWrap(true);
    form->addRow(QStringLiteral("Status"), status);
    form->addRow(QStringLiteral("Size"), size);
    form->addRow(QStringLiteral("Location"), location);
    form->addRow(QStringLiteral("Current download"), down);
    form->addRow(QStringLiteral("Average download"), avg);
    form->addRow(QStringLiteral("Current upload"), up);
    form->addRow(QStringLiteral("ETA"), eta);
    form->addRow(QStringLiteral("Source"), source);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    const bool torrent = id.startsWith(QStringLiteral("torrent-"));
    QTimer timer(&dialog);
    auto refresh = [this, id, torrent, title, status, size, location, down, avg, up, eta, source]() {
        const int row = rowForId(id);
        if (row < 0) return;
        const QString name = downloadsTable_->item(row, 0) ? downloadsTable_->item(row, 0)->text()
                                                            : QStringLiteral("Download");
        const QString st = downloadsTable_->item(row, 1) ? downloadsTable_->item(row, 1)->text()
                                                          : QStringLiteral("Unknown");
        const qint64 done = downloadedBytes_.value(id, 0);
        const qint64 total = totalBytes_.value(id, 0);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 elapsed = qMax<qint64>(1, now - startedAt_.value(id, now));
        const qint64 average = done > 0 ? done / elapsed : 0;
        const qint64 current = currentDownloadSpeed_.value(id, 0);
        const qint64 upload = currentUploadSpeed_.value(id, 0);

        title->setText(name);
        status->setText(st);
        size->setText(total > 0 ? QStringLiteral("%1 / %2").arg(formatBytes(done), formatBytes(total))
                                : formatBytes(done));
        QString path;
        if (torrent) {
            path = torrentEngine_->torrentSavePath(id);
        } else if (paths_.contains(id)) {
            path = paths_.value(id);
        } else {
            path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
        location->setText(path.isEmpty() ? QStringLiteral("Unavailable") : QDir::toNativeSeparators(path));
        down->setText(formatSpeed(current));
        avg->setText(formatSpeed(average));
        up->setText(torrent ? formatSpeed(upload) : QStringLiteral("N/A"));
        source->setText(downloadsTable_->item(row, 4) ? downloadsTable_->item(row, 4)->text()
                                                       : QStringLiteral("—"));

        if (total > 0 && current > 0 && total > done) {
            const qint64 seconds = (total - done) / current;
            eta->setText(seconds >= 86400
                ? QStringLiteral("%1d %2h").arg(seconds / 86400).arg((seconds / 3600) % 24)
                : seconds >= 3600
                    ? QStringLiteral("%1h %2m").arg(seconds / 3600).arg((seconds / 60) % 60)
                    : QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60));
        } else {
            eta->setText(total > 0 && done >= total ? QStringLiteral("Complete") : QStringLiteral("—"));
        }
    };
    refresh();
    connect(&timer, &QTimer::timeout, &dialog, refresh);
    timer.start(500);
    dialog.exec();
}

void MainWindow::setupTray(){trayIcon_->setIcon(windowIcon());trayIcon_->setToolTip("Beatit Download Manager");trayMenu_->addAction("Show Beatit",this,&MainWindow::showFromTray);trayMenu_->addSeparator();trayMenu_->addAction("Exit",this,&MainWindow::exitFromTray);trayIcon_->setContextMenu(trayMenu_);connect(trayIcon_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::DoubleClick||r==QSystemTrayIcon::Trigger)showFromTray();});trayIcon_->show();}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasUrls() || event->mimeData()->hasText()) event->acceptProposedAction();
    else event->ignore();
}
void MainWindow::dropEvent(QDropEvent *event) {
    bool handled=false;
    if(event->mimeData()->hasUrls()) {
        for(const QUrl &item:event->mimeData()->urls()) {
            if(item.isLocalFile()) {
                const QString path=item.toLocalFile();
                if(path.endsWith(QStringLiteral(".torrent"),Qt::CaseInsensitive)) {
                    torrentEngine_->addTorrentFile(path,QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
                    handled=true;
                } else if(path.endsWith(QStringLiteral(".txt"),Qt::CaseInsensitive) && QFileInfo(path).isFile()) {
                    QFile file(path);
                    if(file.open(QIODevice::ReadOnly|QIODevice::Text)) {
                        const QStringList lines=QString::fromUtf8(file.readAll()).split(QRegularExpression(QStringLiteral("[\\r\\n]+")),Qt::SkipEmptyParts);
                        for(QString line:lines) {
                            line=line.trimmed();
                            const QUrl url(line);
                            if(url.isValid() && (url.scheme()==QStringLiteral("http")||url.scheme()==QStringLiteral("https"))) {
                                urlEdit_->setText(line);addDownload();handled=true;
                            }
                        }
                    }
                }
            } else if(item.isValid() && (item.scheme()==QStringLiteral("http")||item.scheme()==QStringLiteral("https")||item.scheme()==QStringLiteral("magnet"))) {
                handleExternalCommand({item.toString()});handled=true;
            }
        }
    } else if(event->mimeData()->hasText()) {
        const QString text=event->mimeData()->text().trimmed();
        const QUrl url(text);
        if(url.isValid() && (url.scheme()==QStringLiteral("http")||url.scheme()==QStringLiteral("https")||url.scheme()==QStringLiteral("magnet"))) {
            handleExternalCommand({text});handled=true;
        }
    }
    if(handled) event->acceptProposedAction();
    else {event->ignore();statusLabel_->setText(QStringLiteral("Drop HTTP/HTTPS URLs, magnet links, .torrent files, or a text file containing URLs."));}
}

void MainWindow::closeEvent(QCloseEvent*e){if(!reallyQuit_&&trayIcon_->isVisible()){hide();trayIcon_->showMessage("Beatit","Beatit is still running in the system tray.",QSystemTrayIcon::Information,2500);e->ignore();return;}e->accept();}
void MainWindow::showFromTray(){showNormal();raise();activateWindow();}
void MainWindow::exitFromTray(){reallyQuit_=true;close();}
int MainWindow::selectedRow()const{auto r=downloadsTable_->selectedRanges();return r.isEmpty()?-1:r.first().topRow();}
QString MainWindow::selectedId()const{const int row=selectedRow();return row<0||!downloadsTable_->item(row,0)?QString():downloadsTable_->item(row,0)->data(Qt::UserRole).toString();}
void MainWindow::setStatus(const QString&id,const QString&status){const int row=rowForId(id);if(row>=0)downloadsTable_->item(row,1)->setText(status);}
int MainWindow::rowForId(const QString&id)const{for(int row=0;row<downloadsTable_->rowCount();++row)if(downloadsTable_->item(row,0)&&downloadsTable_->item(row,0)->data(Qt::UserRole).toString()==id)return row;return -1;}
void MainWindow::handleExternalCommand(const QStringList &arguments) {
    for (const QString &argument : arguments) {
        const QString value = argument.trimmed();
        if (value.isEmpty() || value == QStringLiteral("--hidden")) continue;
        if (value.startsWith(QStringLiteral("magnet:?")) ||
            value.startsWith(QStringLiteral("http://")) ||
            value.startsWith(QStringLiteral("https://"))) {
            urlEdit_->setText(value);
            addDownload();
            showFromTray();
            return;
        }
        if (value.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) && QFileInfo::exists(value)) {
            torrentEngine_->addTorrentFile(value, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
            showFromTray();
            return;
        }
    }
}

void MainWindow::addDownload(){
    const QString url=urlEdit_->text().trimmed();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(url.startsWith("magnet:?")){torrentEngine_->addMagnet(url,root);urlEdit_->clear();statusLabel_->setText("Adding torrent…");return;}
    const QUrl parsed(url);
    if(!parsed.isValid()||(parsed.scheme()!="http"&&parsed.scheme()!="https")){statusLabel_->setText("Only HTTP/HTTPS URLs and magnet links are supported");return;}

    const QString category = categoryForUrl(url);
    const QString destination = categoryDestination(category);
    QDir().mkpath(destination);
    downloadManager_->addUrl(url,destination,category);
    urlEdit_->clear();statusLabel_->setText(QStringLiteral("Queued — %1").arg(category));
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

void MainWindow::openSelected(){
    const QString id = selectedId();
    if (id.isEmpty()) return;

    QString path = paths_.value(id);
    if (id.startsWith(QStringLiteral("torrent-"))) {
        path = torrentEngine_->torrentSavePath(id);
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
    }

    if (path.isEmpty()) {
        const int row = selectedRow();
        const QString filename = row >= 0 && downloadsTable_->item(row, 0)
            ? downloadsTable_->item(row, 0)->text() : QString();
        if (!filename.isEmpty())
            path = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(filename);
    }

    if (path.isEmpty()) {
        statusLabel_->setText(QStringLiteral("File location is unavailable"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void MainWindow::recheckSelected(){
    const QString id=selectedId();
    if (!id.startsWith(QStringLiteral("torrent-"))) { statusLabel_->setText(QStringLiteral("Select a torrent first")); return; }
    torrentEngine_->forceRecheck(id);
    statusLabel_->setText(QStringLiteral("Rechecking torrent files…"));
}
void MainWindow::configureChecksumSelected(){
    const QString id = selectedId();
    if (id.isEmpty() || id.startsWith(QStringLiteral("torrent-"))) return;
    bool ok = false;
    const QString hash = QInputDialog::getText(
        this, QStringLiteral("SHA-256 checksum"),
        QStringLiteral("Expected SHA-256 (64 hexadecimal characters):"),
        QLineEdit::Normal, QString(), &ok).trimmed().toLower();
    if (!ok) return;
    if (!downloadManager_->setExpectedSha256(id, hash)) {
        QMessageBox::warning(this, QStringLiteral("Invalid checksum"),
                             QStringLiteral("Enter exactly 64 hexadecimal characters."));
        return;
    }
    const QString status = downloadsTable_->item(selectedRow(), 1)
        ? downloadsTable_->item(selectedRow(), 1)->text().toLower() : QString();
    if (status.contains(QStringLiteral("completed"))) {
        QString message;
        const bool verified = downloadManager_->verifyChecksum(id, &message);
        setStatus(id, verified ? QStringLiteral("Completed (verified)") : QStringLiteral("Failed — checksum mismatch"));
        QMessageBox::information(this, QStringLiteral("SHA-256 verification"), message);
    } else {
        statusLabel_->setText(QStringLiteral("SHA-256 configured; it will be verified after download."));
    }
}


void MainWindow::checkForUpdates() {
    auto *network = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/buzzfillon-collab/BeatitDownloadManager/releases/latest")));
    request.setRawHeader("User-Agent","BeatitDownloadManager-Updater/1.0");
    request.setRawHeader("Accept","application/vnd.github+json");
    request.setTransferTimeout(20000);
    QNetworkReply *reply=network->get(request);
    connect(reply,&QNetworkReply::finished,this,[this,network,reply] {
        const QByteArray body=reply->readAll();
        const auto error=reply->error();
        reply->deleteLater();
        if(error!=QNetworkReply::NoError) {
            QMessageBox::warning(this,QStringLiteral("Update check failed"),QStringLiteral("Could not contact GitHub Releases: %1").arg(reply->errorString()));
            network->deleteLater(); return;
        }
        const QJsonDocument doc=QJsonDocument::fromJson(body);
        if(!doc.isObject()) { QMessageBox::warning(this,QStringLiteral("Update check failed"),QStringLiteral("GitHub returned invalid release metadata."));network->deleteLater();return; }
        const QJsonObject release=doc.object();
        const QString tag=release.value(QStringLiteral("tag_name")).toString().trimmed();
        if(tag.isEmpty()) { QMessageBox::information(this,QStringLiteral("No release"),QStringLiteral("No published stable release is available yet."));network->deleteLater();return; }
        if(tag==QStringLiteral("v0.1.0-beta1")) { QMessageBox::information(this,QStringLiteral("Beatit is up to date"),QStringLiteral("You are running the latest published version (%1).").arg(tag));network->deleteLater();return; }
        QString installerUrl,checksumsUrl;
        const QJsonArray assets=release.value(QStringLiteral("assets")).toArray();
        for(const QJsonValue &value:assets) {
            const QJsonObject asset=value.toObject();
            const QString name=asset.value(QStringLiteral("name")).toString();
            if(name==QStringLiteral("BeatitDownloadManager-win64-setup.exe")) installerUrl=asset.value(QStringLiteral("browser_download_url")).toString();
            else if(name==QStringLiteral("SHA256SUMS.txt")) checksumsUrl=asset.value(QStringLiteral("browser_download_url")).toString();
        }
        if(installerUrl.isEmpty()||checksumsUrl.isEmpty()) {
            QMessageBox::warning(this,QStringLiteral("Update unavailable"),QStringLiteral("Release %1 does not contain both the Windows installer and SHA256SUMS.txt.").arg(tag));
            network->deleteLater();return;
        }
        if(QMessageBox::question(this,QStringLiteral("Beatit update available"),QStringLiteral("Version %1 is available. Download and verify the installer?").arg(tag),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) {network->deleteLater();return;}
        auto downloadAsset=std::make_shared<std::function<void(QString,std::function<void(QByteArray,QString)>)>>();
        *downloadAsset=[network,this](QString url,std::function<void(QByteArray,QString)> done) {
            QNetworkRequest req{QUrl(url)}; req.setRawHeader("User-Agent","BeatitDownloadManager-Updater/1.0"); req.setTransferTimeout(60000);
            QNetworkReply *assetReply=network->get(req);
            connect(assetReply,&QNetworkReply::finished,this,[assetReply,done] {
                const QByteArray data=assetReply->readAll();
                const QString error=assetReply->error()==QNetworkReply::NoError?QString():assetReply->errorString();
                assetReply->deleteLater(); done(data,error);
            });
        };
        (*downloadAsset)(installerUrl,[this,network,downloadAsset,checksumsUrl,tag](QByteArray installer,QString error) {
            if(!error.isEmpty()||installer.isEmpty()) {QMessageBox::warning(this,QStringLiteral("Update download failed"),error.isEmpty()?QStringLiteral("The installer was empty."):error);network->deleteLater();return;}
            (*downloadAsset)(checksumsUrl,[this,network,tag,installer](QByteArray manifest,QString checksumError) {
                if(!checksumError.isEmpty()) {QMessageBox::warning(this,QStringLiteral("Update verification failed"),checksumError);network->deleteLater();return;}
                const QRegularExpression line(QStringLiteral(R"re(^([A-Fa-f0-9]{64})\s+BeatitDownloadManager-win64-setup\.exe\s*$)re"),QRegularExpression::MultilineOption);
                const auto match=line.match(QString::fromUtf8(manifest));
                if(!match.hasMatch()) {QMessageBox::warning(this,QStringLiteral("Update verification failed"),QStringLiteral("The release checksum manifest has no valid installer entry."));network->deleteLater();return;}
                const QByteArray expected=match.captured(1).toLatin1().toLower();
                const QByteArray actual=QCryptographicHash::hash(installer,QCryptographicHash::Sha256).toHex().toLower();
                if(expected!=actual) {QMessageBox::critical(this,QStringLiteral("Update rejected"),QStringLiteral("SHA-256 verification failed. The installer will not be executed."));network->deleteLater();return;}
                const QString dir=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("BeatitUpdater"));
                if(!QDir().mkpath(dir)) {QMessageBox::warning(this,QStringLiteral("Update failed"),QStringLiteral("Could not create the temporary update folder."));network->deleteLater();return;}
                const QString path=QDir(dir).filePath(QStringLiteral("BeatitDownloadManager-win64-setup.exe"));
                QFile file(path);
                if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(installer)!=installer.size()||!file.flush()) {
                    file.close();QMessageBox::warning(this,QStringLiteral("Update failed"),QStringLiteral("Could not save the verified installer."));network->deleteLater();return;
                }
                file.close();
                if(QMessageBox::question(this,QStringLiteral("Installer verified"),QStringLiteral("SHA-256 verified for %1. Launch the installer and close Beatit so it can update the application?").arg(tag),
                    QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes) {
                    if(QProcess::startDetached(path,{})) {network->deleteLater();QApplication::quit();return;}
                    QMessageBox::warning(this,QStringLiteral("Update failed"),QStringLiteral("Windows could not launch the installer."));
                }
                network->deleteLater();
            });
        });
    });
}

void MainWindow::showSettings(){
    QSettings settings(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Beatit Settings"));
    dialog.resize(720, 620);
    auto *root = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    root->addWidget(tabs, 1);

    auto *general = new QWidget(&dialog);
    auto *g = new QFormLayout(general);
    auto *yt = new QComboBox(general);
    yt->addItems({QStringLiteral("Nightly (recommended)"), QStringLiteral("Stable")});
    yt->setCurrentIndex(ytDlpManager_->channel() == YtDlpManager::Channel::Stable ? 1 : 0);
    g->addRow(QStringLiteral("yt-dlp channel"), yt);
    auto *connections = new QSpinBox(general);
    connections->setRange(1, 8);
    connections->setValue(downloadManager_->httpConnections());
    g->addRow(QStringLiteral("HTTP connections / download"), connections);
    auto *bandwidth = new QSpinBox(general);
    bandwidth->setRange(0, 1024 * 1024);
    bandwidth->setSuffix(QStringLiteral(" KiB/s (0 = unlimited)"));
    bandwidth->setValue(static_cast<int>(settings.value(QStringLiteral("bandwidth/limit"), 0).toLongLong() / 1024));
    g->addRow(QStringLiteral("Global download limit"), bandwidth);
    auto *proxyType = new QComboBox(general);
    proxyType->addItems({QStringLiteral("No proxy"), QStringLiteral("HTTP proxy"), QStringLiteral("SOCKS5 hostname")});
    proxyType->setCurrentIndex(settings.value(QStringLiteral("proxy/type"), 0).toInt());
    auto *proxyHost = new QLineEdit(settings.value(QStringLiteral("proxy/host")).toString(), general);
    auto *proxyPort = new QSpinBox(general);
    proxyPort->setRange(0, 65535);
    proxyPort->setValue(settings.value(QStringLiteral("proxy/port"), 0).toInt());
    g->addRow(QStringLiteral("Proxy / SOCKS"), proxyType);
    g->addRow(QStringLiteral("Proxy host"), proxyHost);
    g->addRow(QStringLiteral("Proxy port"), proxyPort);
    auto *completion = new QComboBox(general);
    completion->addItems({QStringLiteral("Do nothing"), QStringLiteral("Open downloaded file"), QStringLiteral("Open containing folder")});
    completion->setCurrentIndex(qBound(0, settings.value(QStringLiteral("completion/action"), 0).toInt(), 2));
    g->addRow(QStringLiteral("After download completes"), completion);
    auto *clipboardMonitor = new QCheckBox(QStringLiteral("Ask before adding copied HTTP/HTTPS URLs"), general);
    clipboardMonitor->setChecked(settings.value(QStringLiteral("clipboard/monitor"), false).toBool());
    g->addRow(QStringLiteral("Clipboard monitoring"), clipboardMonitor);
    auto *shutdownOnComplete = new QCheckBox(QStringLiteral("Offer shutdown when all downloads finish"), general);
    shutdownOnComplete->setChecked(settings.value(QStringLiteral("power/shutdownOnComplete"), false).toBool());
    g->addRow(QStringLiteral("Queue completion"), shutdownOnComplete);
    auto *antivirusPath = new QLineEdit(settings.value(QStringLiteral("antivirus/program")).toString(), general);
    auto *antivirusArgs = new QLineEdit(settings.value(QStringLiteral("antivirus/arguments"), QStringLiteral("\"{file}\"")).toString(), general);
    auto *avBrowse = new QPushButton(QStringLiteral("Browse…"), general);
    auto *avRow = new QWidget(general);
    auto *avLayout = new QHBoxLayout(avRow); avLayout->setContentsMargins(0,0,0,0);
    avLayout->addWidget(antivirusPath,1); avLayout->addWidget(avBrowse);
    connect(avBrowse, &QPushButton::clicked, &dialog, [antivirusPath, &dialog] {
        const QString chosen = QFileDialog::getOpenFileName(&dialog, QStringLiteral("Choose antivirus executable"));
        if (!chosen.isEmpty()) antivirusPath->setText(chosen);
    });
    g->addRow(QStringLiteral("Antivirus executable"), avRow);
    antivirusArgs->setPlaceholderText(QStringLiteral("Arguments; use {file} for the completed file path"));
    g->addRow(QStringLiteral("Antivirus arguments"), antivirusArgs);
    auto *customCategoriesEdit = new QLineEdit(settings.value(QStringLiteral("categories/customNames")).toStringList().join(QStringLiteral(", ")), general);
    customCategoriesEdit->setPlaceholderText(QStringLiteral("Comma-separated category names"));
    g->addRow(QStringLiteral("Custom categories"), customCategoriesEdit);
    auto *hostRulesEdit = new QLineEdit(settings.value(QStringLiteral("categories/hostRules")).toStringList().join(QStringLiteral("; ")), general);
    hostRulesEdit->setPlaceholderText(QStringLiteral("host.example=Video; downloads.example=Programs"));
    g->addRow(QStringLiteral("Host-specific routing"), hostRulesEdit);

    QHash<QString, QLineEdit*> categoryPaths;
    QHash<QString, QLineEdit*> categoryRules;
    const QString downloadRoot = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QStringList categoryNames{QStringLiteral("Video"), QStringLiteral("Music"), QStringLiteral("Documents"), QStringLiteral("Programs"), QStringLiteral("Other")};
    for (QString name : settings.value(QStringLiteral("categories/customNames")).toStringList()) {
        name = name.trimmed();
        if (!name.isEmpty() && !categoryNames.contains(name) && name.compare(QStringLiteral("Other"), Qt::CaseInsensitive) != 0)
            categoryNames.append(name);
    }
    for (const QString &category : categoryNames) {
        auto *pathEdit = new QLineEdit(settings.value(QStringLiteral("categories/%1").arg(category),
            QDir(downloadRoot).filePath(category)).toString(), general);
        auto *browse = new QPushButton(QStringLiteral("Browse…"), general);
        auto *pathRow = new QWidget(general);
        auto *pathLayout = new QHBoxLayout(pathRow);
        pathLayout->setContentsMargins(0,0,0,0);
        pathLayout->addWidget(pathEdit, 1);
        pathLayout->addWidget(browse);
        connect(browse, &QPushButton::clicked, &dialog, [pathEdit, &dialog] {
            const QString chosen = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Choose category folder"), pathEdit->text());
            if (!chosen.isEmpty()) pathEdit->setText(chosen);
        });
        categoryPaths.insert(category, pathEdit);
        g->addRow(QStringLiteral("%1 folder").arg(category), pathRow); 
        if (category != QStringLiteral("Other")) {
            const QHash<QString, QString> defaults{
                {QStringLiteral("Video"), QStringLiteral("mp4,mkv,webm,avi,mov,m4v,mpeg,mpg,ts,m3u8")},
                {QStringLiteral("Music"), QStringLiteral("mp3,m4a,aac,flac,wav,ogg,opus,wma")},
                {QStringLiteral("Documents"), QStringLiteral("pdf,doc,docx,xls,xlsx,ppt,pptx,txt,rtf,odt,ods,csv")},
                {QStringLiteral("Programs"), QStringLiteral("exe,msi,msix,appx,zip,7z,rar,iso,dmg,deb,rpm")}
            };
            auto *rule = new QLineEdit(settings.value(QStringLiteral("categoryRules/%1").arg(category), defaults.value(category)).toString(), general);
            rule->setPlaceholderText(QStringLiteral("Comma-separated extensions, e.g. pdf,docx"));
            categoryRules.insert(category, rule);
            g->addRow(QStringLiteral("%1 extensions").arg(category), rule);
        }
    }
    auto *note = new QLabel(QStringLiteral("HTTP/HTTPS only. Magnet links and .torrent files use the BitTorrent engine. The bandwidth limit applies to HTTP and BitTorrent downloads."), general);
    note->setWordWrap(true);
    g->addRow(QString(), note);
    tabs->addTab(general, QStringLiteral("General"));

    auto *appearancePage = new QWidget(&dialog);
    auto *appearanceForm = new QFormLayout(appearancePage);
    auto *theme = new QComboBox(appearancePage);
    theme->addItems({QStringLiteral("Dark"),QStringLiteral("Light")});
    theme->setCurrentIndex(settings.value(QStringLiteral("appearance/theme"),QStringLiteral("dark")).toString()==QStringLiteral("light")?1:0);
    appearanceForm->addRow(QStringLiteral("Theme"),theme);
    const QStringList columnNames{QStringLiteral("FILE"),QStringLiteral("STATUS"),QStringLiteral("PROGRESS"),QStringLiteral("SPEED"),QStringLiteral("SOURCE"),QStringLiteral("LAST ACTIVITY")};
    const QStringList savedColumns=settings.value(QStringLiteral("appearance/columns"),QStringList{QStringLiteral("1"),QStringLiteral("1"),QStringLiteral("1"),QStringLiteral("1"),QStringLiteral("1"),QStringLiteral("1")}).toStringList();
    QHash<int,QCheckBox*> columnChecks;
    for(int col=0;col<columnNames.size();++col){auto *check=new QCheckBox(columnNames.at(col),appearancePage);check->setChecked(col>=savedColumns.size()||savedColumns.at(col)==QStringLiteral("1"));columnChecks.insert(col,check);appearanceForm->addRow(QStringLiteral("Show column"),check);}
    auto *showTorrentButton=new QCheckBox(QStringLiteral("Show Torrent button"),appearancePage);
    auto *showGrabButton=new QCheckBox(QStringLiteral("Show Grab links button"),appearancePage);
    auto *showQueuesButton=new QCheckBox(QStringLiteral("Show Queues button"),appearancePage);
    showTorrentButton->setChecked(settings.value(QStringLiteral("appearance/toolbarTorrent"),true).toBool());
    showGrabButton->setChecked(settings.value(QStringLiteral("appearance/toolbarGrabLinks"),true).toBool());
    showQueuesButton->setChecked(settings.value(QStringLiteral("appearance/toolbarQueues"),true).toBool());
    appearanceForm->addRow(QStringLiteral("Toolbar"),showTorrentButton);
    appearanceForm->addRow(QString(),showGrabButton);
    appearanceForm->addRow(QString(),showQueuesButton);
    tabs->addTab(appearancePage,QStringLiteral("Appearance"));

    auto *schedulePage = new QWidget(&dialog);
    auto *sl = new QVBoxLayout(schedulePage);
    auto *enabled = new QCheckBox(QStringLiteral("Enable weekly download scheduler"), schedulePage);
    enabled->setChecked(scheduler_->enabled());
    sl->addWidget(enabled);
    auto *calendar = new QCalendarWidget(schedulePage);
    calendar->setGridVisible(true);
    sl->addWidget(calendar);
    auto *hint = new QLabel(QStringLiteral("Select a date to edit that day of the week. The schedule repeats weekly. Start = allowed time; end = stop time. Overnight windows are supported. If no days are enabled, downloads remain unrestricted."), schedulePage);
    hint->setWordWrap(true);
    sl->addWidget(hint);
    auto *row = new QHBoxLayout;
    auto *dayLabel = new QLabel(schedulePage);
    auto *dayEnabled = new QCheckBox(QStringLiteral("Allowed"), schedulePage);
    auto *start = new QTimeEdit(schedulePage); start->setDisplayFormat(QStringLiteral("HH:mm"));
    auto *endTime = new QTimeEdit(schedulePage); endTime->setDisplayFormat(QStringLiteral("HH:mm"));
    row->addWidget(dayLabel); row->addWidget(dayEnabled); row->addWidget(new QLabel(QStringLiteral("Start"), schedulePage)); row->addWidget(start);
    row->addWidget(new QLabel(QStringLiteral("End"), schedulePage)); row->addWidget(endTime); row->addStretch();
    sl->addLayout(row);

    std::array<bool, 7> scheduleEnabled{};
    std::array<int, 7> scheduleStart{};
    std::array<int, 7> scheduleEnd{};
    for (int day = 1; day <= 7; ++day) {
        scheduleEnabled[day - 1] = scheduler_->dayEnabled(day);
        scheduleStart[day - 1] = scheduler_->startMinute(day);
        scheduleEnd[day - 1] = scheduler_->endMinute(day);
    }

    auto loadDay = [calendar, dayLabel, dayEnabled, start, endTime, &scheduleEnabled, &scheduleStart, &scheduleEnd]() {
        const int day = calendar->selectedDate().dayOfWeek();
        static const QStringList names{QStringLiteral("Monday"),QStringLiteral("Tuesday"),QStringLiteral("Wednesday"),
                                       QStringLiteral("Thursday"),QStringLiteral("Friday"),QStringLiteral("Saturday"),QStringLiteral("Sunday")};
        dayLabel->setText(names[day - 1]);
        dayEnabled->setChecked(scheduleEnabled[day - 1]);
        start->setTime(QTime::fromMSecsSinceStartOfDay(scheduleStart[day - 1] * 60000));
        endTime->setTime(QTime::fromMSecsSinceStartOfDay(scheduleEnd[day - 1] * 60000));
    };
    auto saveDay = [calendar, dayEnabled, start, endTime, &scheduleEnabled, &scheduleStart, &scheduleEnd]() {
        const int day = calendar->selectedDate().dayOfWeek() - 1;
        scheduleEnabled[day] = dayEnabled->isChecked();
        scheduleStart[day] = start->time().hour() * 60 + start->time().minute();
        scheduleEnd[day] = endTime->time().hour() * 60 + endTime->time().minute();
    };
    connect(calendar, &QCalendarWidget::selectionChanged, &dialog, loadDay);
    connect(dayEnabled, &QCheckBox::toggled, &dialog, [saveDay](bool){ saveDay(); });
    connect(start, &QTimeEdit::timeChanged, &dialog, [saveDay](const QTime&){ saveDay(); });
    connect(endTime, &QTimeEdit::timeChanged, &dialog, [saveDay](const QTime&){ saveDay(); });
    loadDay();
    tabs->addTab(schedulePage, QStringLiteral("Scheduler / Calendar"));

    auto *torrentPage = new QWidget(&dialog);
    auto *tl = new QFormLayout(torrentPage);
    const QStringList modes{QStringLiteral("Stop after ratio"),QStringLiteral("Stop after time"),QStringLiteral("Seed forever"),QStringLiteral("Stop immediately")};
    auto *mode = new QComboBox(torrentPage); mode->addItems(modes); mode->setCurrentIndex(torrentEngine_->seedingPolicyMode());
    auto *ratio = new QDoubleSpinBox(torrentPage); ratio->setRange(0.1,100.0); ratio->setDecimals(1); ratio->setValue(torrentEngine_->seedingRatio());
    auto *minutes = new QSpinBox(torrentPage); minutes->setRange(1,100000); minutes->setValue(torrentEngine_->seedingMinutes());
    tl->addRow(QStringLiteral("Seeding policy"), mode);
    tl->addRow(QStringLiteral("Upload/download ratio"), ratio);
    tl->addRow(QStringLiteral("Seeding minutes"), minutes);
    tabs->addTab(torrentPage, QStringLiteral("BitTorrent"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    const auto newChannel = yt->currentIndex() == 1 ? YtDlpManager::Channel::Stable : YtDlpManager::Channel::Nightly;
    const bool channelChanged = newChannel != ytDlpManager_->channel();
    ytDlpManager_->setChannel(newChannel);
    if (channelChanged) ytDlpManager_->updateNow();

    downloadManager_->setHttpConnections(connections->value());
    settings.setValue(QStringLiteral("http/connections"), connections->value());
    const qint64 limit = static_cast<qint64>(bandwidth->value()) * 1024;
    settings.setValue(QStringLiteral("bandwidth/limit"), limit);
    downloadManager_->setBandwidthLimit(limit);
    settings.setValue(QStringLiteral("proxy/type"), proxyType->currentIndex());
    settings.setValue(QStringLiteral("completion/action"), completion->currentIndex());
    settings.setValue(QStringLiteral("appearance/theme"),theme->currentIndex()==1?QStringLiteral("light"):QStringLiteral("dark"));
    QStringList columnValues;
    for(int col=0;col<6;++col) columnValues.append(columnChecks.value(col)->isChecked()?QStringLiteral("1"):QStringLiteral("0"));
    settings.setValue(QStringLiteral("appearance/columns"),columnValues);
    settings.setValue(QStringLiteral("appearance/toolbarTorrent"),showTorrentButton->isChecked());
    settings.setValue(QStringLiteral("appearance/toolbarGrabLinks"),showGrabButton->isChecked());
    settings.setValue(QStringLiteral("appearance/toolbarQueues"),showQueuesButton->isChecked());
    for(int col=0;col<6;++col) downloadsTable_->setColumnHidden(col,!columnChecks.value(col)->isChecked());
    setStyleSheet(styleSheet().remove(lightThemeOverrides()));
    if(theme->currentIndex()==1) setStyleSheet(styleSheet()+lightThemeOverrides());
    if(auto *b=findChild<QPushButton*>(QStringLiteral("toolbarTorrentButton"))) b->setVisible(showTorrentButton->isChecked());
    if(auto *b=findChild<QPushButton*>(QStringLiteral("toolbarGrabLinksButton"))) b->setVisible(showGrabButton->isChecked());
    if(auto *b=findChild<QPushButton*>(QStringLiteral("toolbarQueuesButton"))) b->setVisible(showQueuesButton->isChecked());
    settings.setValue(QStringLiteral("clipboard/monitor"), clipboardMonitor->isChecked());
    settings.setValue(QStringLiteral("power/shutdownOnComplete"), shutdownOnComplete->isChecked());
    settings.setValue(QStringLiteral("antivirus/program"), antivirusPath->text().trimmed());
    settings.setValue(QStringLiteral("antivirus/arguments"), antivirusArgs->text().trimmed());
    QStringList customNames;
    for (QString name : customCategoriesEdit->text().split(',', Qt::SkipEmptyParts)) {
        name = name.trimmed();
        if (!name.isEmpty() && name.size() <= 40 && !name.contains('/') && !name.contains('\\') &&
            !QStringList{QStringLiteral("Video"),QStringLiteral("Music"),QStringLiteral("Documents"),QStringLiteral("Programs"),QStringLiteral("Other")}.contains(name, Qt::CaseInsensitive) &&
            !customNames.contains(name, Qt::CaseInsensitive)) customNames.append(name);
    }
    settings.setValue(QStringLiteral("categories/customNames"), customNames);
    QStringList hostRules;
    for (QString rule : hostRulesEdit->text().split(';', Qt::SkipEmptyParts)) {
        rule = rule.trimmed();
        const int separator = rule.indexOf('=');
        if (separator > 0 && !rule.left(separator).trimmed().contains('/') &&
            QRegularExpression(QStringLiteral(R"re(^[a-zA-Z0-9.-]+$)re")).match(rule.left(separator).trimmed()).hasMatch() &&
            !rule.mid(separator + 1).trimmed().isEmpty()) hostRules.append(rule);
    }
    settings.setValue(QStringLiteral("categories/hostRules"), hostRules);
    for (auto it = categoryRules.cbegin(); it != categoryRules.cend(); ++it)
        settings.setValue(QStringLiteral("categoryRules/%1").arg(it.key()), it.value()->text().trimmed().toLower());
    for (auto it = categoryPaths.cbegin(); it != categoryPaths.cend(); ++it) {
        const QString path = it.value()->text().trimmed();
        if (!path.isEmpty()) {
            QDir().mkpath(path);
            settings.setValue(QStringLiteral("categories/%1").arg(it.key()), QDir::cleanPath(path));
        }
    }
    settings.setValue(QStringLiteral("proxy/host"), proxyHost->text().trimmed());
    settings.setValue(QStringLiteral("proxy/port"), proxyPort->value());
    downloadManager_->setProxy(proxyHost->text().trimmed(), proxyPort->value(), proxyType->currentIndex());
    torrentEngine_->setBandwidthLimit(limit);
    for (int day = 1; day <= 7; ++day)
        scheduler_->setDay(day, scheduleEnabled[day - 1], scheduleStart[day - 1], scheduleEnd[day - 1]);
    scheduler_->setEnabled(enabled->isChecked());
    torrentEngine_->setSeedingPolicy(mode->currentIndex(), ratio->value(), minutes->value());

    statusLabel_->setText(QStringLiteral("Settings saved"));
}



void MainWindow::showSyncManager() {
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Periodic synchronization queues")); dialog.resize(720,480);
    auto *layout=new QVBoxLayout(&dialog);
    auto *list=new QListWidget(&dialog); layout->addWidget(list,1);
    auto refresh=[list] {
        list->clear(); QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        const QJsonArray jobs=QJsonDocument::fromJson(settings.value(QStringLiteral("sync/jobs")).toByteArray()).array();
        for(const QJsonValue &value:jobs) {
            const QJsonObject job=value.toObject();
            auto *item=new QListWidgetItem(QStringLiteral("%1 — every %2 hour(s)").arg(job.value(QStringLiteral("url")).toString()).arg(job.value(QStringLiteral("hours")).toInt(24)),list);
            item->setData(Qt::UserRole,job.value(QStringLiteral("id")).toString());
        }
    };
    auto *form=new QFormLayout();
    auto *url=new QLineEdit(&dialog); url->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    auto *hours=new QSpinBox(&dialog); hours->setRange(1,720); hours->setValue(24);
    form->addRow(QStringLiteral("HTTP/HTTPS file URL"),url); form->addRow(QStringLiteral("Check interval (hours)"),hours); layout->addLayout(form);
    auto *buttons=new QHBoxLayout(); auto *add=new QPushButton(QStringLiteral("Add sync job"),&dialog); auto *remove=new QPushButton(QStringLiteral("Remove selected"),&dialog);
    auto *checkNow=new QPushButton(QStringLiteral("Check now"),&dialog); auto *close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    buttons->addWidget(add);buttons->addWidget(remove);buttons->addWidget(checkNow);buttons->addStretch();buttons->addWidget(close);layout->addLayout(buttons);
    connect(add,&QPushButton::clicked,&dialog,[&] {
        const QUrl parsed(url->text().trimmed());
        if(!parsed.isValid()||parsed.host().isEmpty()||(parsed.scheme()!=QStringLiteral("http")&&parsed.scheme()!=QStringLiteral("https"))) {
            QMessageBox::warning(&dialog,QStringLiteral("Invalid URL"),QStringLiteral("Only direct HTTP/HTTPS file URLs can be synchronized."));return;
        }
        QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        QJsonArray jobs=QJsonDocument::fromJson(settings.value(QStringLiteral("sync/jobs")).toByteArray()).array();
        for(const QJsonValue &v:jobs) if(v.toObject().value(QStringLiteral("url")).toString()==parsed.toString(QUrl::FullyEncoded)) {
            QMessageBox::information(&dialog,QStringLiteral("Already configured"),QStringLiteral("That URL already has a synchronization job."));return;
        }
        QJsonObject job;job.insert(QStringLiteral("id"),QUuid::createUuid().toString(QUuid::WithoutBraces));job.insert(QStringLiteral("url"),parsed.toString(QUrl::FullyEncoded));
        job.insert(QStringLiteral("hours"),hours->value());job.insert(QStringLiteral("lastValidator"),QString());job.insert(QStringLiteral("nextCheck"),QDateTime::currentSecsSinceEpoch()+30);
        jobs.append(job);settings.setValue(QStringLiteral("sync/jobs"),QJsonDocument(jobs).toJson(QJsonDocument::Compact));url->clear();refresh();
    });
    connect(remove,&QPushButton::clicked,&dialog,[&] {
        if(!list->currentItem())return;const QString id=list->currentItem()->data(Qt::UserRole).toString();
        QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit"));QJsonArray old=QJsonDocument::fromJson(settings.value(QStringLiteral("sync/jobs")).toByteArray()).array(),next;
        for(const QJsonValue &v:old)if(v.toObject().value(QStringLiteral("id")).toString()!=id)next.append(v);
        settings.setValue(QStringLiteral("sync/jobs"),QJsonDocument(next).toJson(QJsonDocument::Compact));refresh();
    });
    connect(checkNow,&QPushButton::clicked,&dialog,[this]{QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit"));QJsonArray jobs=QJsonDocument::fromJson(settings.value(QStringLiteral("sync/jobs")).toByteArray()).array();for(int i=0;i<jobs.size();++i){QJsonObject o=jobs.at(i).toObject();o.insert(QStringLiteral("nextCheck"),0);jobs.replace(i,o);}settings.setValue(QStringLiteral("sync/jobs"),QJsonDocument(jobs).toJson(QJsonDocument::Compact));runSyncChecks();});
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);refresh();dialog.exec();
}

void MainWindow::runSyncChecks() {
    if(syncCheckRunning_)return;
    QSettings settings(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
    auto jobs=std::make_shared<QJsonArray>(QJsonDocument::fromJson(settings.value(QStringLiteral("sync/jobs")).toByteArray()).array());
    const qint64 now=QDateTime::currentSecsSinceEpoch();
    QStringList due;
    for(int i=0;i<jobs->size();++i) {
        QJsonObject job=jobs->at(i).toObject();
        if(job.value(QStringLiteral("nextCheck")).toVariant().toLongLong()>now)continue;
        const QString id=job.value(QStringLiteral("id")).toString();
        if(id.isEmpty())continue;
        due.append(id);
        job.insert(QStringLiteral("nextCheck"),now+qBound(1,job.value(QStringLiteral("hours")).toInt(24),720)*3600);
        jobs->replace(i,job);
    }
    if(due.isEmpty())return;
    settings.setValue(QStringLiteral("sync/jobs"),QJsonDocument(*jobs).toJson(QJsonDocument::Compact));
    syncCheckRunning_=true;
    auto *network=new QNetworkAccessManager(this);
    auto remaining=std::make_shared<int>(due.size());
    auto processResult=[this,network,remaining](const QString &id,bool ok,const QString &etag,const QString &modified,qint64 size) {
        QSettings cfg(QStringLiteral("Beatit"),QStringLiteral("Beatit"));
        QJsonArray current=QJsonDocument::fromJson(cfg.value(QStringLiteral("sync/jobs")).toByteArray()).array();
        for(int i=0;i<current.size();++i) {
            QJsonObject job=current.at(i).toObject();
            if(job.value(QStringLiteral("id")).toString()!=id)continue;
            if(ok) {
                const QString validator=(etag.isEmpty()&&modified.isEmpty()&&size<=0)?QString():QStringLiteral("%1|%2|%3").arg(etag,modified).arg(size);
                const QString previous=job.value(QStringLiteral("lastValidator")).toString();
                if(!validator.isEmpty())job.insert(QStringLiteral("lastValidator"),validator);
                if(!previous.isEmpty()&&!validator.isEmpty()&&previous!=validator) {
                    const QString url=job.value(QStringLiteral("url")).toString();
                    const QString category=categoryForUrl(url);const QString destination=categoryDestination(category);
                    QDir().mkpath(destination);downloadManager_->addUrl(url,destination,category);
                    statusLabel_->setText(QStringLiteral("Sync detected a changed file and queued it: %1").arg(QUrl(url).fileName()));
                } else if(previous.isEmpty()&&!validator.isEmpty()) {
                    statusLabel_->setText(QStringLiteral("Sync baseline recorded for %1").arg(QUrl(job.value(QStringLiteral("url")).toString()).fileName()));
                } else if(validator.isEmpty()) {
                    statusLabel_->setText(QStringLiteral("Sync skipped: server supplied no ETag, Last-Modified, or usable size validator."));
                }
            } else {
                statusLabel_->setText(QStringLiteral("Sync check failed for %1").arg(QUrl(job.value(QStringLiteral("url")).toString()).host()));
            }
            current.replace(i,job);break;
        }
        cfg.setValue(QStringLiteral("sync/jobs"),QJsonDocument(current).toJson(QJsonDocument::Compact));
        --(*remaining);
        if(*remaining<=0){syncCheckRunning_=false;network->deleteLater();}
    };
    auto readMetadata=[](QNetworkReply *reply) {
        const QString etag=reply->rawHeader("ETag").trimmed();
        const QString modified=reply->header(QNetworkRequest::LastModifiedHeader).toDateTime().toUTC().toString(Qt::ISODate);
        qint64 size=reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        static const QRegularExpression totalFromRange(QStringLiteral(R"re(/(\d+)\s*$)re"));
        const auto match=totalFromRange.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
        if(match.hasMatch())size=match.captured(1).toLongLong();
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok=reply->error()==QNetworkReply::NoError&&status>=200&&status<400;
        return std::tuple<bool,QString,QString,qint64>{ok,etag,modified,size};
    };
    for(const QString &id:due) {
        QJsonObject job;
        for(const QJsonValue &v:*jobs)if(v.toObject().value(QStringLiteral("id")).toString()==id){job=v.toObject();break;}
        const QUrl url(job.value(QStringLiteral("url")).toString());
        QNetworkRequest request(url);request.setRawHeader("User-Agent","BeatitDownloadManager/0.1");request.setTransferTimeout(20000);
        QNetworkReply *reply=network->head(request);
        connect(reply,&QNetworkReply::finished,this,[this,reply,network,id,processResult,readMetadata] {
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool needsFallback=reply->error()!=QNetworkReply::NoError||status==405||status==501;
            if(needsFallback) {
                const QUrl url=reply->url();
                reply->deleteLater();
                QNetworkRequest request(url);request.setRawHeader("User-Agent","BeatitDownloadManager/0.1");
                request.setRawHeader("Range","bytes=0-0");request.setTransferTimeout(20000);
                QNetworkReply *fallback=network->get(request);
                connect(fallback,&QNetworkReply::downloadProgress,fallback,[fallback](qint64 received,qint64){if(received>1024*1024)fallback->abort();});
                connect(fallback,&QNetworkReply::finished,this,[fallback,id,processResult,readMetadata] {
                    const auto [ok,etag,modified,size]=readMetadata(fallback);
                    fallback->deleteLater();processResult(id,ok,etag,modified,size);
                });
                return;
            }
            const auto [ok,etag,modified,size]=readMetadata(reply);
            reply->deleteLater();processResult(id,ok,etag,modified,size);
        });
    }
}
void MainWindow::showQueueManager() {
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Download queues")); dialog.resize(540,420);
    auto *layout=new QVBoxLayout(&dialog); auto *list=new QListWidget(&dialog);
    auto refresh=[this,list](){const QString old=list->currentItem()?list->currentItem()->data(Qt::UserRole).toString():QString();list->clear();
        for(const auto&q:downloadManager_->queues()){auto*i=new QListWidgetItem(QStringLiteral("%1 — %2 active max — %3").arg(q.name).arg(q.maxActive).arg(q.paused?"Stopped":"Running"),list);i->setData(Qt::UserRole,q.id);i->setData(Qt::UserRole+1,q.paused);if(q.id==old)list->setCurrentItem(i);}
        if(!list->currentItem()&&list->count())list->setCurrentRow(0);};
    layout->addWidget(list,1);auto*buttons=new QHBoxLayout();auto*add=new QPushButton("New queue",&dialog);auto*rename=new QPushButton("Rename",&dialog);auto*toggle=new QPushButton("Stop / Start queue",&dialog);
    buttons->addWidget(add);buttons->addWidget(rename);buttons->addWidget(toggle);layout->addLayout(buttons);
    auto*form=new QFormLayout();auto*limit=new QSpinBox(&dialog);limit->setRange(1,32);form->addRow("Maximum simultaneous downloads",limit);layout->addLayout(form);
    auto*apply=new QPushButton("Apply limit",&dialog);auto*move=new QPushButton("Move selected download here",&dialog);auto*retry=new QPushButton("Retry failed in queue",&dialog);auto*close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    auto*bottom=new QHBoxLayout();bottom->addWidget(apply);bottom->addWidget(move);bottom->addWidget(retry);bottom->addStretch();bottom->addWidget(close);layout->addLayout(bottom);
    auto queueId=[list](){return list->currentItem()?list->currentItem()->data(Qt::UserRole).toString():QString();};
    auto updateLimit=[this,limit,queueId](){for(const auto&q:downloadManager_->queues())if(q.id==queueId()){limit->setValue(q.maxActive);break;}};
    connect(list,&QListWidget::currentRowChanged,&dialog,[updateLimit](int){updateLimit();});
    connect(add,&QPushButton::clicked,&dialog,[this,&dialog,refresh](){bool ok=false;QString n=QInputDialog::getText(&dialog,"New queue","Queue name",QLineEdit::Normal,QString(),&ok);if(ok&&!downloadManager_->createQueue(n))QMessageBox::warning(&dialog,"Queue not created","Name must be unique and non-empty.");refresh();});
    connect(rename,&QPushButton::clicked,&dialog,[this,&dialog,queueId,refresh](){QString id=queueId();if(id.isEmpty())return;QString old;for(const auto&q:downloadManager_->queues())if(q.id==id)old=q.name;bool ok=false;QString n=QInputDialog::getText(&dialog,"Rename queue","Queue name",QLineEdit::Normal,old,&ok);if(ok&&!downloadManager_->renameQueue(id,n))QMessageBox::warning(&dialog,"Rename failed","Name must be unique and non-empty.");refresh();});
    connect(toggle,&QPushButton::clicked,&dialog,[this,list,queueId,refresh](){if(!list->currentItem())return;downloadManager_->setQueuePaused(queueId(),!list->currentItem()->data(Qt::UserRole+1).toBool());refresh();});
    connect(apply,&QPushButton::clicked,&dialog,[this,queueId,limit,&dialog](){if(!downloadManager_->setQueueConcurrency(queueId(),limit->value()))QMessageBox::warning(&dialog,"Queue update failed","Could not save queue limit.");});
    connect(move,&QPushButton::clicked,&dialog,[this,queueId,&dialog](){QString id=selectedId();if(id.isEmpty()||id.startsWith("torrent-")){QMessageBox::information(&dialog,"Select a download","Select an HTTP/HTTPS download in the main list first.");return;}if(!downloadManager_->moveToQueue(id,queueId()))QMessageBox::information(&dialog,"Cannot move download","Pause the download before moving it.");else statusLabel_->setText("Download moved to queue");});
    connect(retry,&QPushButton::clicked,&dialog,[this,queueId](){downloadManager_->retryFailedInQueue(queueId());statusLabel_->setText("Retrying failed downloads in queue");});
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);refresh();updateLimit();dialog.exec();
}
