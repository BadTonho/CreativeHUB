#include "backups_page.h"
#include "../../model/backup_manager.h"
#include "../../model/storage_manager.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>

namespace creative_suite::hub {

BackupsPage::BackupsPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    connect(&BackupManager::instance(), &BackupManager::backupCreated,
            this, &BackupsPage::refreshBackupsList);
}

void BackupsPage::setupUi() {
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
    auto* titleLabel = new QLabel(QStringLiteral("Cofre de Backups"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Histórico completo de snapshots e cópias de segurança geradas para seus projetos da suíte criativa."), scrollContainer);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888888; background: transparent; margin-bottom: 4px;"));
    mainLayout->addWidget(subLabel);

    // Card 1: Configuration & Overview
    auto* configCard = new QFrame(scrollContainer);
    configCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 16px 20px;"
        "}"
    ));
    auto* configLayout = new QVBoxLayout(configCard);
    configLayout->setSpacing(12);

    auto* sectionTitle = new QLabel(QStringLiteral("DIRETÓRIO DO COFRE DE BACKUPS"), configCard);
    sectionTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent;"));
    configLayout->addWidget(sectionTitle);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);

    m_pathEdit = new QLineEdit(configCard);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setText(BackupManager::instance().backupDirectory());
    m_pathEdit->setStyleSheet(HubStyle::searchInputStyle());
    pathRow->addWidget(m_pathEdit, 1);

    auto* changeFolderBtn = new QPushButton(QStringLiteral("Alterar Pasta"), configCard);
    changeFolderBtn->setFixedHeight(36);
    changeFolderBtn->setCursor(Qt::PointingHandCursor);
    changeFolderBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(changeFolderBtn, &QPushButton::clicked, this, &BackupsPage::onBrowseBackupFolder);
    pathRow->addWidget(changeFolderBtn);

    auto* openFolderBtn = new QPushButton(QStringLiteral("Abrir Pasta"), configCard);
    openFolderBtn->setFixedHeight(36);
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    openFolderBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openFolderBtn, &QPushButton::clicked, this, &BackupsPage::onOpenBackupFolder);
    pathRow->addWidget(openFolderBtn);

    configLayout->addLayout(pathRow);

    m_statsLabel = new QLabel(configCard);
    m_statsLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 600; background: transparent;"));
    configLayout->addWidget(m_statsLabel);

    mainLayout->addWidget(configCard);

    // Card 2: Backups List
    auto* listCard = new QFrame(scrollContainer);
    listCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 16px 20px;"
        "}"
    ));
    auto* listCardLayout = new QVBoxLayout(listCard);
    listCardLayout->setSpacing(12);

    auto* listHeader = new QLabel(QStringLiteral("BACKUPS SALVOS RECENTEMENTE"), listCard);
    listHeader->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent;"));
    listCardLayout->addWidget(listHeader);

    m_listLayout = new QVBoxLayout();
    m_listLayout->setSpacing(8);

    // Empty state
    m_emptyWidget = new QWidget(listCard);
    auto* emptyLayout = new QVBoxLayout(m_emptyWidget);
    emptyLayout->setContentsMargins(10, 30, 10, 30);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyLabel = new QLabel(QStringLiteral("Nenhum backup encontrado no cofre.\nVocê pode criar cópias instantâneas a qualquer momento na aba Projetos."), m_emptyWidget);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(QStringLiteral("color: #555555; font-size: 13px; line-height: 1.5; background: transparent;"));
    emptyLayout->addWidget(emptyLabel);

    m_listLayout->addWidget(m_emptyWidget);
    listCardLayout->addLayout(m_listLayout);

    mainLayout->addWidget(listCard);

    mainLayout->addStretch();
    scrollArea->setWidget(scrollContainer);
    rootLayout->addWidget(scrollArea);

    refreshBackupsList();
}

void BackupsPage::refreshBackupsList() {
    const int count = BackupManager::instance().totalBackupsCount();
    const qint64 bytes = BackupManager::instance().totalBackupsSize();

    if (m_statsLabel) {
        m_statsLabel->setText(QStringLiteral("%1 cópias de segurança salvas • %2 ocupados no disco")
            .arg(QString::number(count), StorageManager::formatBytes(bytes)));
    }
    if (m_pathEdit) {
        m_pathEdit->setText(BackupManager::instance().backupDirectory());
    }

    // Clear list items (except m_emptyWidget)
    while (auto* item = m_listLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            if (w == m_emptyWidget) {
                // keep reference
            } else {
                delete w;
            }
        }
        delete item;
    }

    const auto backups = BackupManager::instance().listBackups();
    if (backups.empty()) {
        m_listLayout->addWidget(m_emptyWidget);
        m_emptyWidget->show();
    } else {
        m_emptyWidget->hide();
        for (const auto& b : backups) {
            auto* row = new QFrame();
            row->setStyleSheet(QStringLiteral(
                "QFrame { background-color: #1a1a1a; border: 1px solid #292929; border-radius: 8px; padding: 10px 14px; }"
            ));
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 4, 4, 4);
            rowLayout->setSpacing(12);

            auto* col = new QVBoxLayout();
            col->setSpacing(3);

            auto* title = new QLabel(b.projectName, row);
            title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700; background: transparent; border: none;"));
            col->addWidget(title);

            auto* fileLabel = new QLabel(QFileInfo(b.backupFilePath).fileName(), row);
            fileLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px; background: transparent; border: none;"));
            col->addWidget(fileLabel);

            rowLayout->addLayout(col, 1);

            auto* dateLabel = new QLabel(b.timestamp.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")), row);
            dateLabel->setStyleSheet(QStringLiteral("color: #666666; font-size: 11px; background: transparent; border: none;"));
            rowLayout->addWidget(dateLabel);

            auto* sizeLabel = new QLabel(StorageManager::formatBytes(b.fileSizeBytes), row);
            sizeLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 12px; font-weight: 600; background: transparent; border: none;"));
            rowLayout->addWidget(sizeLabel);

            auto* openBtn = new QPushButton(QStringLiteral("Ver Arquivo"), row);
            openBtn->setFixedHeight(28);
            openBtn->setCursor(Qt::PointingHandCursor);
            openBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
            connect(openBtn, &QPushButton::clicked, this, [backupPath = b.backupFilePath]() {
                const QString dirPath = QFileInfo(backupPath).absolutePath();
                QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
            });
            rowLayout->addWidget(openBtn);

            m_listLayout->addWidget(row);
        }
    }
}

void BackupsPage::onBrowseBackupFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Selecionar pasta do cofre de backups"),
        m_pathEdit ? m_pathEdit->text() : BackupManager::instance().backupDirectory()
    );
    if (!dir.isEmpty()) {
        BackupManager::instance().setBackupDirectory(dir);
        refreshBackupsList();
    }
}

void BackupsPage::onOpenBackupFolder() {
    BackupManager::instance().openBackupDirectory();
}

} // namespace creative_suite::hub
