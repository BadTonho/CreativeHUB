#include "storage_page.h"
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

namespace creative_suite::hub {

namespace {

QString friendlyDirName(const QString& path) {
    if (path.contains(QStringLiteral("video-editor"))) return QStringLiteral("Cache do Video Editor (previews & proxies)");
    if (path.contains(QStringLiteral("image-editor"))) return QStringLiteral("Cache do Image Editor (camadas & buffers)");
    if (path.contains(QStringLiteral("motion-editor"))) return QStringLiteral("Cache do Motion Studio (render & efeitos)");
    if (path.contains(QStringLiteral("creative-suite"))) return QStringLiteral("Cache Principal da Suíte");
    return QStringLiteral("Cache de Dados Locais");
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
    auto* titleLabel = new QLabel(QStringLiteral("Armazenamento & Cache"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Gerencie o espaço em disco ocupado pela suíte criativa e limpe caches temporários com segurança."), scrollContainer);
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

    auto* sectionTitle = new QLabel(QStringLiteral("ESPAÇO OCUPADO EM CACHE"), overviewCard);
    sectionTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent;"));
    overviewLayout->addWidget(sectionTitle);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(12);

    auto* labelDesc = new QLabel(QStringLiteral("Tamanho total acumulado:"), overviewCard);
    labelDesc->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 13px; background: transparent;"));
    sizeRow->addWidget(labelDesc);

    m_totalSizeLabel = new QLabel(QStringLiteral("Calculando..."), overviewCard);
    m_totalSizeLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: 800; background: transparent;"));
    sizeRow->addWidget(m_totalSizeLabel);
    sizeRow->addStretch();
    overviewLayout->addLayout(sizeRow);

    auto* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);

    auto* clearBtn = new QPushButton(QStringLiteral("Limpar Cache Seguro"), overviewCard);
    clearBtn->setFixedHeight(36);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(clearBtn, &QPushButton::clicked, this, &StoragePage::onClearCache);
    actionsRow->addWidget(clearBtn);

    auto* openFolderBtn = new QPushButton(QStringLiteral("Abrir Pasta de Cache"), overviewCard);
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

    auto* breakdownHeader = new QLabel(QStringLiteral("DIRETÓRIOS DE CACHE DA SUÍTE"), breakdownCard);
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

    auto* tipTitle = new QLabel(QStringLiteral("COMO O CACHE FUNCIONA"), tipCard);
    tipTitle->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 800; color: #666666; letter-spacing: 1.0px; background: transparent;"));
    tipLayout->addWidget(tipTitle);

    auto* tipText = new QLabel(
        QStringLiteral("A limpeza de cache elimina apenas arquivos temporários de renderização, formas de onda de áudio e pré-visualizações. "
                       "Seus projetos salvos, arquivos de mídia originais e configurações permanecem 100% preservados."),
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
    const auto totalBytes = StorageManager::calculateCacheSize(m_cachePaths);
    if (m_totalSizeLabel) {
        m_totalSizeLabel->setText(StorageManager::formatBytes(totalBytes));
    }

    // Refresh breakdown list
    while (auto* item = m_breakdownLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            delete w;
        }
        delete item;
    }

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

        const qint64 dirSize = StorageManager::calculateDirectorySize(path);
        auto* sizeLabel = new QLabel(StorageManager::formatBytes(dirSize), row);
        sizeLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 12px; font-weight: 700; background: transparent; border: none;"));
        rowLayout->addWidget(sizeLabel);

        m_breakdownLayout->addWidget(row);
    }
}

void StoragePage::onClearCache() {
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(QStringLiteral("Limpar Cache da Suíte"));
    confirm.setText(QStringLiteral("Deseja realmente limpar todos os arquivos de cache e pré-visualizações temporárias da suíte criativa?"));
    confirm.setInformativeText(QStringLiteral("Seus projetos e mídias originais não serão alterados."));
    auto* okBtn = confirm.addButton(QStringLiteral("Limpar Cache"), QMessageBox::AcceptRole);
    confirm.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
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
        title = result.removed_entries > 0 ? QStringLiteral("Cache Liberado") : QStringLiteral("Cache já vazio");
        message = result.removed_entries > 0
            ? QStringLiteral("Cache limpo com sucesso. Foram liberados %1.").arg(removed)
            : QStringLiteral("O cache já estava vazio. Nenhum arquivo foi removido.");
    } else {
        title = result.removed_entries > 0 ? QStringLiteral("Limpeza parcial do cache") : QStringLiteral("Falha na limpeza do cache");
        const QString residue = result.remaining_size_known
            ? QStringLiteral("%1 permanecem em cache.").arg(remaining)
            : QStringLiteral("Pelo menos %1 permanecem em cache; não foi possível medir todos os arquivos.").arg(remaining);
        message = QStringLiteral("%1. Foram liberados %2. %3 Algumas entradas não puderam ser limpas. Feche os editores e tente novamente. Consulte o log para os detalhes.")
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
