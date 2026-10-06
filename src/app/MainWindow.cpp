#include "MainWindow.h"
#include "../core/DownloadManager.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace {
QString formatBytes(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    double value = static_cast<double>(bytes);
    const QStringList units{QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
    int i = -1;
    do { value /= 1024.0; ++i; } while (value >= 1024.0 && i + 1 < units.size());
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value >= 100 ? 0 : 1).arg(units[i]);
}

QString formatSpeed(qint64 bytes) {
    return bytes <= 0 ? QStringLiteral("—") : formatBytes(bytes) + QStringLiteral("/s");
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      urlEdit_(new QLineEdit(this)),
      addButton_(new QPushButton(QStringLiteral("＋ Add"), this)),
      pauseButton_(new QPushButton(QStringLiteral("Pause"), this)),
      cancelButton_(new QPushButton(QStringLiteral("Cancel"), this)),
      openButton_(new QPushButton(QStringLiteral("Open"), this)),
      downloadsTable_(new QTableWidget(this)),
      statusLabel_(new QLabel(QStringLiteral("Ready"), this)),
      downloadManager_(new DownloadManager(this)) {

    setWindowTitle(QStringLiteral("Beatit"));
    setMinimumSize(1050, 650);
    resize(1180, 720);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("Beatit"), central);
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle = new QLabel(QStringLiteral("Fast, free, open-source downloading."), central);
    subtitle->setObjectName(QStringLiteral("subtitle"));

    auto *input = new QHBoxLayout;
    input->setSpacing(10);
    urlEdit_->setPlaceholderText(QStringLiteral("Paste a download URL…"));
    urlEdit_->setClearButtonEnabled(true);
    input->addWidget(urlEdit_, 1);
    input->addWidget(addButton_);

    downloadsTable_->setColumnCount(5);
    downloadsTable_->setHorizontalHeaderLabels(
        {QStringLiteral("FILE"), QStringLiteral("STATUS"), QStringLiteral("PROGRESS"),
         QStringLiteral("SPEED"), QStringLiteral("URL")});
    downloadsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    downloadsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    downloadsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    downloadsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    downloadsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    downloadsTable_->setShowGrid(false);
    downloadsTable_->verticalHeader()->setVisible(false);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(pauseButton_);
    actions->addWidget(cancelButton_);
    actions->addWidget(openButton_);
    actions->addStretch();
    actions->addWidget(statusLabel_);

    root->addWidget(title);
    root->addWidget(subtitle);
    root->addLayout(input);
    root->addWidget(downloadsTable_, 1);
    root->addLayout(actions);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background:#0d1117; color:#e6edf3; }"
        "QLabel#title { font-size:34px; font-weight:700; color:#ffffff; }"
        "QLabel#subtitle { font-size:14px; color:#8b949e; margin-top:-8px; }"
        "QLineEdit { background:#161b22; border:1px solid #30363d; border-radius:12px; padding:12px 14px; color:#f0f6fc; font-size:14px; }"
        "QLineEdit:focus { border:1px solid #58a6ff; }"
        "QPushButton { background:#21262d; border:1px solid #30363d; border-radius:10px; padding:10px 16px; color:#f0f6fc; font-weight:600; }"
        "QPushButton:hover { background:#30363d; }"
        "QPushButton#primary { background:#238636; border-color:#2ea043; }"
        "QTableWidget { background:#11161d; border:1px solid #21262d; border-radius:14px; gridline-color:transparent; color:#e6edf3; font-size:13px; }"
        "QTableWidget::item { padding:10px; border-bottom:1px solid #1d232c; }"
        "QTableWidget::item:selected { background:#1f3b5b; color:#ffffff; }"
        "QHeaderView::section { background:#161b22; border:none; padding:9px; color:#8b949e; font-size:11px; font-weight:700; }"
    ));

    addButton_->setObjectName(QStringLiteral("primary"));

    connect(addButton_, &QPushButton::clicked, this, &MainWindow::addDownload);
    connect(urlEdit_, &QLineEdit::returnPressed, this, &MainWindow::addDownload);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::pauseSelected);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelSelected);
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openSelected);

    connect(downloadManager_, &DownloadManager::taskAdded, this,
        [this](const QString &id, const QString &url) {
            const int row = downloadsTable_->rowCount();
            downloadsTable_->insertRow(row);
            rows_[id] = row;
            const QString name = QUrl(url).fileName().isEmpty() ? QStringLiteral("download") : QUrl(url).fileName();
            auto *file = new QTableWidgetItem(name);
            file->setData(Qt::UserRole, id);
            downloadsTable_->setItem(row, 0, file);
            downloadsTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("Queued")));
            downloadsTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0%")));
            downloadsTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
            downloadsTable_->setItem(row, 4, new QTableWidgetItem(url));
        });

    connect(downloadManager_, &DownloadManager::taskStarted, this,
        [this](const QString &id, const QString &filename, qint64 total) {
            const int row = rows_.value(id, -1);
            if (row < 0) return;
            downloadsTable_->item(row, 0)->setText(filename);
            downloadsTable_->item(row, 1)->setText(QStringLiteral("Downloading"));
            downloadsTable_->item(row, 2)->setText(total > 0 ? QStringLiteral("0%") : QStringLiteral("Live"));
        });

    connect(downloadManager_, &DownloadManager::taskProgress, this,
        [this](const QString &id, qint64 done, qint64 total, qint64 speed) {
            const int row = rows_.value(id, -1);
            if (row < 0) return;
            const QString progress = total > 0
                ? QStringLiteral("%1%").arg((done * 100) / total)
                : formatBytes(done);
            downloadsTable_->item(row, 2)->setText(progress);
            downloadsTable_->item(row, 3)->setText(formatSpeed(speed));
            statusLabel_->setText(QStringLiteral("%1 downloaded").arg(formatBytes(done)));
        });

    connect(downloadManager_, &DownloadManager::taskPaused, this,
        [this](const QString &id, qint64 done) {
            setStatus(id, QStringLiteral("Paused"));
            statusLabel_->setText(QStringLiteral("Paused at %1").arg(formatBytes(done)));
        });

    connect(downloadManager_, &DownloadManager::taskCompleted, this,
        [this](const QString &id, const QString &path) {
            paths_[id] = path;
            setStatus(id, QStringLiteral("Completed"));
            statusLabel_->setText(QStringLiteral("Download complete"));
        });

    connect(downloadManager_, &DownloadManager::taskFailed, this,
        [this](const QString &id, const QString &error) {
            setStatus(id, QStringLiteral("Failed"));
            statusLabel_->setText(error);
            QMessageBox::warning(this, QStringLiteral("Beatit"), error);
        });

    connect(downloadManager_, &DownloadManager::taskCancelled, this,
        [this](const QString &id) { setStatus(id, QStringLiteral("Cancelled")); });
}

int MainWindow::selectedRow() const {
    const auto ranges = downloadsTable_->selectedRanges();
    return ranges.isEmpty() ? -1 : ranges.first().topRow();
}

QString MainWindow::selectedId() const {
    const int row = selectedRow();
    if (row < 0 || !downloadsTable_->item(row, 0)) return {};
    return downloadsTable_->item(row, 0)->data(Qt::UserRole).toString();
}

void MainWindow::setStatus(const QString &id, const QString &status) {
    const int row = rows_.value(id, -1);
    if (row >= 0 && downloadsTable_->item(row, 1))
        downloadsTable_->item(row, 1)->setText(status);
}

void MainWindow::addDownload() {
    const QString url = urlEdit_->text().trimmed();
    const QUrl parsed(url);
    if (!parsed.isValid() ||
        (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        QMessageBox::warning(this, QStringLiteral("Invalid URL"),
                             QStringLiteral("Enter a valid HTTP or HTTPS URL."));
        return;
    }

    const QString destination =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    downloadManager_->addUrl(url, destination);
    urlEdit_->clear();
    statusLabel_->setText(QStringLiteral("Starting download…"));
}

void MainWindow::pauseSelected() {
    const QString id = selectedId();
    if (!id.isEmpty()) downloadManager_->pause(id);
}

void MainWindow::cancelSelected() {
    const QString id = selectedId();
    if (!id.isEmpty()) downloadManager_->cancel(id);
}

void MainWindow::openSelected() {
    const QString id = selectedId();
    const QString path = paths_.value(id);
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
