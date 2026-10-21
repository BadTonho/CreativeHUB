#include "storage_page.h"
#include <QCoreApplication>
#include "../../model/storage_manager.h"
#include "../../model/activity_manager.h"
#include "../../diagnostics/hub_logger.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace creative_suite::hub {

namespace {

QString friendlyDirName(const QString& path) {
    if (path.contains(QStringLiteral("video-editor"))) return QCoreApplication::translate("StoragePage", "Video Editor cache (previews and proxies)");
    if (path.contains(QStringLiteral("image-editor"))) return QCoreApplication::translate("StoragePage", "Image Editor cache (layers and buffers)");
    if (path.contains(QStringLiteral("motion-editor"))) return QCoreApplication::translate("StoragePage", "Motion Studio cache (rendering and effects)");
    if (path.contains(QStringLiteral("creative-suite"))) return QCoreApplication::translate("StoragePage", "Suite cache");
    return QCoreApplication::translate("StoragePage", "Local data cache");
}

} // namespace

StoragePage::StoragePage(QWidget* parent)
    : StoragePage(StorageManager::suiteCachePaths(), parent)
{}

StoragePage::StoragePage(QStringList cachePaths, QWidget* parent)
    : QWidget(parent)
    , m_cachePaths(std::move(cachePaths))
{
    setupUi();
}

void StoragePage::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* mainLayout = new QVBoxLayout(scrollContainer);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(16);

    // Title & Subtitle
    auto* titleLabel = new QLabel(QCoreApplication::translate("StoragePage", "Storage & Cache"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QCoreApplication::translate("StoragePage", "Manage disk space used by the Creative Suite and safely clear temporary caches."), scrollContainer);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888888; background: transparent; margin-bottom: 4px;"));
    mainLayout->addWidget(subLabel);

    // Card 1: Total Cache Overview
    auto* overviewCard = new QFrame(scrollContainer);
    overviewCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 16px 20px;"
        "}"
    ));
    auto* overviewLayout = new QVBoxLayout(overviewCard);
    overviewLayout->setSpacing(14);

    auto* sectionTitle = new QLabel(QCoreApplication::translate("StoragePage", "CACHE USAGE"), overviewCard);
    sectionTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent;"));
    overviewLayout->addWidget(sectionTitle);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(12);

    auto* labelDesc = new QLabel(QCoreApplication::translate("StoragePage", "Total size:"), overviewCard);
    labelDesc->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 13px; background: transparent;"));
    sizeRow->addWidget(labelDesc);

    m_totalSizeLabel = new QLabel(QCoreApplication::translate("StoragePage", "Calculating..."), overviewCard);
    m_totalSizeLabel->setObjectName(QStringLiteral("storageTotalSizeLabel"));
    m_totalSizeLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: 800; background: transparent;"));
    sizeRow->addWidget(m_totalSizeLabel);
    sizeRow->addStretch();
    overviewLayout->addLayout(sizeRow);

    auto* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);

    auto* clearBtn = new QPushButton(QCoreApplication::translate("StoragePage", "Safely Clear Cache"), overviewCard);
    clearBtn->setFixedHeight(36);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(clearBtn, &QPushButton::clicked, this, &StoragePage::onClearCache);
    actionsRow->addWidget(clearBtn);

    auto* openFolderBtn = new QPushButton(QCoreApplication::translate("StoragePage", "Open Cache Folder"), overviewCard);
    openFolderBtn->setFixedHeight(36);
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    openFolderBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openFolderBtn, &QPushButton::clicked, this, &StoragePage::onOpenCacheFolder);
    actionsRow->addWidget(openFolderBtn);

    actionsRow->addStretch();
    overviewLayout->addLayout(actionsRow);

    m_statusNote = new QLabel(overviewCard);
    m_statusNote->setVisible(false);
    m_statusNote->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 600; background: transparent;"));
    overviewLayout->addWidget(m_statusNote);

    mainLayout->addWidget(overviewCard);

    // Card 2: Directory Breakdown
    auto* breakdownCard = new QFrame(scrollContainer);
    breakdownCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 16px 20px;"
        "}"
    ));
    auto* breakdownCardLayout = new QVBoxLayout(breakdownCard);
    breakdownCardLayout->setSpacing(12);

    auto* breakdownHeader = new QLabel(QCoreApplication::translate("StoragePage", "SUITE CACHE DIRECTORIES"), breakdownCard);
    breakdownHeader->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent;"));
    breakdownCardLayout->addWidget(breakdownHeader);

    m_breakdownLayout = new QVBoxLayout();
    m_breakdownLayout->setSpacing(8);
    breakdownCardLayout->addLayout(m_breakdownLayout);

    mainLayout->addWidget(breakdownCard);

    // Card 3: Notice & Tips
    auto* tipCard = new QFrame(scrollContainer);
    tipCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #131313;"
        "   border: 1px solid #222222;"
        "   border-radius: 10px;"
        "   padding: 14px 18px;"
        "}"
    ));
    auto* tipLayout = new QVBoxLayout(tipCard);
    tipLayout->setSpacing(6);

    auto* tipTitle = new QLabel(QCoreApplication::translate("StoragePage", "HOW THE CACHE WORKS"), tipCard);
    tipTitle->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 800; color: #666666; letter-spacing: 1.0px; background: transparent;"));
    tipLayout->addWidget(tipTitle);

    auto* tipText = new QLabel(
        QCoreApplication::translate("StoragePage", "Clearing the cache removes only temporary render files, audio waveforms, and previews. Saved projects, original media, and settings remain untouched."),
        tipCard
    );
    tipText->setWordWrap(true);
    tipText->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px; line-height: 1.4; background: transparent;"));
    tipLayout->addWidget(tipText);

    mainLayout->addWidget(tipCard);

    mainLayout->addStretch();
    scrollArea->setWidget(scrollContainer);
    rootLayout->addWidget(scrollArea);

    refreshStorageInfo();
}

void StoragePage::refreshStorageInfo() {
    if (m_totalSizeLabel) {
        m_totalSizeLabel->setText(QCoreApplication::translate("StoragePage", "Calculating…"));
    }

    // Refresh breakdown list
    while (auto* item = m_breakdownLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            delete w;
        }
        delete item;
    }
    m_sizeLabels.clear();

    const auto& paths = m_cachePaths;
    for (const auto& path : paths) {
        auto* row = new QFrame();
        row->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #1a1a1a; border: 1px solid #292929; border-radius: 6px; padding: 6px 12px; }"
        ));
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 4, 4, 4);

        auto* col = new QVBoxLayout();
        col->setSpacing(2);

        auto* nameLabel = new QLabel(friendlyDirName(path), row);
        nameLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700; background: transparent; border: none;"));
        col->addWidget(nameLabel);

        auto* pathLabel = new QLabel(path, row);
        pathLabel->setStyleSheet(QStringLiteral("color: #666666; font-size: 10px; background: transparent; border: none;"));
        col->addWidget(pathLabel);

        rowLayout->addLayout(col, 1);

        auto* sizeLabel = new QLabel(QCoreApplication::translate("StoragePage", "Calculating…"), row);
        sizeLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 12px; font-weight: 700; background: transparent; border: none;"));
        rowLayout->addWidget(sizeLabel);
        m_sizeLabels.push_back(sizeLabel);

        m_breakdownLayout->addWidget(row);
    }

    const auto generation = ++m_refresh_generation;
    const auto paths_snapshot = m_cachePaths;
    auto* watcher = new QFutureWatcher<QVector<qint64>>(this);
    connect(watcher, &QFutureWatcher<QVector<qint64>>::finished, this,
            [this, watcher, generation]() {
        const auto sizes = watcher->result();
        watcher->deleteLater();
        if (generation != m_refresh_generation) return;
        qint64 total = 0;
        for (qsizetype i = 0; i < sizes.size(); ++i) {
            total += sizes[i];
            if (i < m_sizeLabels.size() && m_sizeLabels[i])
                m_sizeLabels[i]->setText(StorageManager::formatBytes(sizes[i]));
        }
        if (m_totalSizeLabel) m_totalSizeLabel->setText(StorageManager::formatBytes(total));
    });
    watcher->setFuture(QtConcurrent::run([paths_snapshot]() {
        QVector<qint64> sizes;
        sizes.reserve(paths_snapshot.size());
        for (const auto& path : paths_snapshot)
            sizes.push_back(StorageManager::calculateDirectorySize(path));
        return sizes;
    }));
}

void StoragePage::onClearCache() {
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(QCoreApplication::translate("StoragePage", "Clear Suite Cache"));
    confirm.setText(QCoreApplication::translate("StoragePage", "Are you sure you want to clear all cache files and temporary previews for the Creative Suite?"));
    confirm.setInformativeText(QCoreApplication::translate("StoragePage", "Your projects and original media will not be changed."));
    auto* okBtn = confirm.addButton(QCoreApplication::translate("StoragePage", "Clear Cache"), QMessageBox::AcceptRole);
    confirm.addButton(QCoreApplication::translate("StoragePage", "Cancel"), QMessageBox::RejectRole);
    confirm.exec();

    if (confirm.clickedButton() != okBtn) {
        return;
    }

    const auto result = StorageManager::clearCachePaths(m_cachePaths);
    refreshStorageInfo();

    const QString removed = StorageManager::formatBytes(result.removed_bytes);
    const QString remaining = StorageManager::formatBytes(result.remaining_bytes);
    QString title;
    QString message;
    if (result.succeeded()) {
        title = result.removed_entries > 0 ? QCoreApplication::translate("StoragePage", "Cache Cleared") : QCoreApplication::translate("StoragePage", "Cache already empty");
        message = result.removed_entries > 0
            ? QCoreApplication::translate("StoragePage", "Cache cleared successfully. %1 freed.").arg(removed)
            : QCoreApplication::translate("StoragePage", "The cache was already empty. No files were removed.");
    } else {
        title = result.removed_entries > 0 ? QCoreApplication::translate("StoragePage", "Partial Cache Cleanup") : QCoreApplication::translate("StoragePage", "Cache Cleanup Failed");
        const QString residue = result.remaining_size_known
            ? QCoreApplication::translate("StoragePage", "%1 remain in the cache.").arg(remaining)
            : QCoreApplication::translate("StoragePage", "At least %1 remain in the cache; not all files could be measured.").arg(remaining);
        message = QCoreApplication::translate("StoragePage", "%1. %2 freed. %3 Some items could not be removed. Close the editors and try again. See the log for details.")
            .arg(title, removed, residue);
    }
    if (m_statusNote) {
        m_statusNote->setText(message);
        m_statusNote->setWordWrap(true);
        m_statusNote->setTextFormat(Qt::PlainText);
        m_statusNote->setStyleSheet(result.succeeded()
            ? QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 600; background: transparent;")
            : QStringLiteral("color: #f2b86a; font-size: 12px; font-weight: 600; background: transparent;"));
        m_statusNote->setVisible(true);
    }

    ActivityManager::instance().addActivity(title, message, QStringLiteral("cache"));
}

void StoragePage::onOpenCacheFolder() {
    const QString cachePath = m_cachePaths.isEmpty() ? StorageManager::defaultCachePath() : m_cachePaths.front();
    QDir().mkpath(cachePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(cachePath));
}

} // namespace creative_suite::hub
