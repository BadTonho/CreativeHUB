#include "app_details_modal.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEasingCurve>
#include <QComboBox>
#include <QTextBrowser>
#include <algorithm>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    constexpr int size = 72;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(QColor(0x20, 0x20, 0x20));
    painter.setPen(QColor(0x33, 0x33, 0x33));
    painter.drawRoundedRect(QRect(0, 0, size, size), 16, 16);

    QString initials = QStringLiteral("CS");
    if (id == QStringLiteral("video-editor")) {
        initials = QStringLiteral("V");
    } else if (id == QStringLiteral("image-editor")) {
        initials = QStringLiteral("I");
    } else if (id == QStringLiteral("motion-editor")) {
        initials = QStringLiteral("M");
    }

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(24);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initials);

    return pixmap;
}

} // namespace

QPixmap AppDetailsModal::getAppIcon(const AppInfo& app) {
    if (!app.iconPath().isEmpty() && QFile::exists(app.iconPath())) {
        return QPixmap(app.iconPath()).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return createFallbackIcon(app.id());
}

AppDetailsModal::AppDetailsModal(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::StrongFocus);
    setVisible(false);

    setupUi();
}

void AppDetailsModal::setupUi() {
    m_cardFrame = new QFrame(this);
    m_cardFrame->setObjectName(QStringLiteral("ModalCardFrame"));
    m_cardFrame->setStyleSheet(QStringLiteral(
        "#ModalCardFrame {"
        "   background-color: #141414;"
        "   border: 1px solid #282828;"
        "   border-radius: 14px;"
        "}"
    ));

    auto* cardLayout = new QVBoxLayout(m_cardFrame);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(16);

    // Top Header: Icon + Titles + Action Button + Close Button
    auto* topHeaderLayout = new QHBoxLayout();
    topHeaderLayout->setSpacing(16);

    m_iconLabel = new QLabel(m_cardFrame);
    m_iconLabel->setFixedSize(72, 72);
    topHeaderLayout->addWidget(m_iconLabel);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    m_nameLabel = new QLabel(m_cardFrame);
    m_nameLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 800; color: #ffffff;"));
    titleCol->addWidget(m_nameLabel);

    m_taglineLabel = new QLabel(m_cardFrame);
    m_taglineLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #8e8e9e;"));
    titleCol->addWidget(m_taglineLabel);

    topHeaderLayout->addLayout(titleCol, 1);

    m_actionButton = new QPushButton(m_cardFrame);
    m_actionButton->setFixedSize(110, 36);
    connect(m_actionButton, &QPushButton::clicked, this, [this]() {
        emit actionRequested(m_app.id());
        closeWithAnimation();
    });
    topHeaderLayout->addWidget(m_actionButton, 0, Qt::AlignVCenter);

    m_closeIconButton = new QPushButton(QStringLiteral("✕"), m_cardFrame);
    m_closeIconButton->setFixedSize(28, 28);
    m_closeIconButton->setCursor(Qt::PointingHandCursor);
    m_closeIconButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: #1e1e1e;"
        "   border: 1px solid #2e2e2e;"
        "   border-radius: 14px;"
        "   color: #888888;"
        "   font-size: 13px;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2a2a2a;"
        "   color: #ffffff;"
        "   border-color: #444444;"
        "}"
    ));
    connect(m_closeIconButton, &QPushButton::clicked, this, &AppDetailsModal::closeWithAnimation);
    topHeaderLayout->addWidget(m_closeIconButton, 0, Qt::AlignTop);

    cardLayout->addLayout(topHeaderLayout);

    // Separator
    auto* separator = new QFrame(m_cardFrame);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("background-color: #242424; max-height: 1px; border: none;"));
    cardLayout->addWidget(separator);

    // Scrollable details section
    auto* scrollArea = new QScrollArea(m_cardFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContent = new QWidget(scrollArea);
    scrollContent->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 4, 0, 4);
    contentLayout->setSpacing(14);

    // About section
    auto* aboutTitle = new QLabel(QStringLiteral("SOBRE O APLICATIVO"), scrollContent);
    aboutTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px;"));
    contentLayout->addWidget(aboutTitle);

    m_descText = new QLabel(scrollContent);
    m_descText->setWordWrap(true);
    m_descText->setStyleSheet(QStringLiteral("font-size: 13px; color: #d0d0d0; line-height: 1.5;"));
    contentLayout->addWidget(m_descText);

    // Features section
    auto* featTitle = new QLabel(QStringLiteral("PRINCIPAIS RECURSOS"), scrollContent);
    featTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px; margin-top: 4px;"));
    contentLayout->addWidget(featTitle);

    m_featuresLayout = new QVBoxLayout();
    m_featuresLayout->setSpacing(8);
    contentLayout->addLayout(m_featuresLayout);

    // Technical specifications
    auto* techTitle = new QLabel(QStringLiteral("INFORMAÇÕES TÉCNICAS"), scrollContent);
    techTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px; margin-top: 4px;"));
    contentLayout->addWidget(techTitle);

    auto* techCard = new QFrame(scrollContent);
    techCard->setStyleSheet(QStringLiteral(
        "background-color: #1a1a1a;"
        "border: 1px solid #282828;"
        "border-radius: 8px;"
        "padding: 10px 14px;"
    ));
    auto* techLayout = new QVBoxLayout(techCard);
    techLayout->setSpacing(6);

    m_formatLabel = new QLabel(techCard);
    m_formatLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0b0;"));
    techLayout->addWidget(m_formatLabel);

    m_exeLabel = new QLabel(techCard);
    m_exeLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0b0;"));
    techLayout->addWidget(m_exeLabel);

    contentLayout->addWidget(techCard);

    // Changelog section
    auto* changelogHeaderRow = new QHBoxLayout();
    changelogHeaderRow->setContentsMargins(0, 4, 0, 0);

    auto* changelogTitle = new QLabel(QStringLiteral("NOTAS DE VERSÃO"), scrollContent);
    changelogTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #707070; letter-spacing: 0.5px;"));
    changelogHeaderRow->addWidget(changelogTitle);
    changelogHeaderRow->addStretch();

    m_versionBadge = new QLabel(scrollContent);
    m_versionBadge->setStyleSheet(QStringLiteral(
        "background-color: #222222; color: #cccccc; font-size: 11px; font-weight: 600; "
        "border-radius: 6px; padding: 2px 8px;"
    ));
    m_versionBadge->setVisible(false);
    changelogHeaderRow->addWidget(m_versionBadge);

    m_versionCombo = new QComboBox(scrollContent);
    m_versionCombo->setCursor(Qt::PointingHandCursor);
    m_versionCombo->setFixedHeight(26);
    m_versionCombo->setStyleSheet(QStringLiteral(
        "QComboBox {"
        "   background-color: #1a1a1a;"
        "   border: 1px solid #2e2e2e;"
        "   border-radius: 6px;"
        "   color: #ffffff;"
        "   font-size: 11px;"
        "   font-weight: 600;"
        "   padding: 2px 10px 2px 8px;"
        "}"
        "QComboBox:hover {"
        "   border-color: #444444;"
        "}"
        "QComboBox::drop-down {"
        "   border: none;"
        "   width: 16px;"
        "}"
        "QComboBox QAbstractItemView {"
        "   background-color: #161616;"
        "   border: 1px solid #2e2e2e;"
        "   selection-background-color: #333333;"
        "   color: #ffffff;"
        "   font-size: 11px;"
        "}"
    ));
    m_versionCombo->setVisible(false);
    connect(m_versionCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && m_versionCombo) {
            const QString ver = m_versionCombo->itemData(index).toString();
            loadChangelogForVersion(ver);
        }
    });
    changelogHeaderRow->addWidget(m_versionCombo);

    contentLayout->addLayout(changelogHeaderRow);

    m_changelogCard = new QFrame(scrollContent);
    m_changelogCard->setObjectName(QStringLiteral("ChangelogCard"));
    m_changelogCard->setStyleSheet(QStringLiteral(
        "#ChangelogCard {"
        "   background-color: #1a1a1a;"
        "   border: 1px solid #282828;"
        "   border-radius: 8px;"
        "   padding: 10px 14px;"
        "}"
    ));
    auto* changelogLayout = new QVBoxLayout(m_changelogCard);
    changelogLayout->setContentsMargins(10, 10, 10, 10);
    changelogLayout->setSpacing(6);

    m_changelogBrowser = new QTextBrowser(m_changelogCard);
    m_changelogBrowser->setOpenExternalLinks(true);
    m_changelogBrowser->setReadOnly(true);
    m_changelogBrowser->setMinimumHeight(130);
    m_changelogBrowser->document()->setDocumentMargin(2);
    m_changelogBrowser->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "   background: transparent;"
        "   border: none;"
        "   color: #d0d0d0;"
        "   font-size: 12px;"
        "   line-height: 1.4;"
        "   selection-background-color: #333333;"
        "}"
    ));
    changelogLayout->addWidget(m_changelogBrowser);

    m_emptyChangelogLabel = new QLabel(m_changelogCard);
    m_emptyChangelogLabel->setWordWrap(true);
    m_emptyChangelogLabel->setStyleSheet(QStringLiteral(
        "color: #707070; font-size: 12px; font-style: italic; background: transparent;"
    ));
    changelogLayout->addWidget(m_emptyChangelogLabel);

    contentLayout->addWidget(m_changelogCard);
    contentLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    cardLayout->addWidget(scrollArea, 1);

    // Bottom close button
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();

    m_closeBottomButton = new QPushButton(QStringLiteral("Fechar"), m_cardFrame);
    m_closeBottomButton->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(m_closeBottomButton, &QPushButton::clicked, this, &AppDetailsModal::closeWithAnimation);
    bottomRow->addWidget(m_closeBottomButton);

    cardLayout->addLayout(bottomRow);
}

QRect AppDetailsModal::targetCardRect() const {
    const int cardW = std::clamp(width() - 80, 500, 660);
    const int cardH = std::clamp(height() - 60, 440, 580);
    const int x = (width() - cardW) / 2;
    const int y = (height() - cardH) / 2;
    return QRect(x, y, cardW, cardH);
}

QRect AppDetailsModal::cardGeometry() const noexcept {
    return m_cardFrame ? m_cardFrame->geometry() : QRect();
}

void AppDetailsModal::setCardGeometry(const QRect& rect) {
    if (m_cardFrame) {
        m_cardFrame->setGeometry(rect);
    }
}

void AppDetailsModal::setBackdropOpacity(double opacity) {
    m_backdropOpacity = opacity;
    update();
}

void AppDetailsModal::clearFeatures() {
    if (!m_featuresLayout) {
        return;
    }
    while (auto* item = m_featuresLayout->takeAt(0)) {
        if (auto* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
}

void AppDetailsModal::setChangelogBasePath(const QString& path) {
    m_changelogReader.setBasePath(path);
    if (!m_app.id().isEmpty()) {
        updateVisuals();
    }
}

void AppDetailsModal::loadChangelogForVersion(const QString& version) {
    if (!m_changelogBrowser) {
        return;
    }
    const QString md = m_changelogReader.loadChangelog(m_app.id(), version);
    if (!md.trimmed().isEmpty()) {
        m_changelogBrowser->setMarkdown(md);
        m_changelogBrowser->setVisible(true);
        if (m_emptyChangelogLabel) {
            m_emptyChangelogLabel->setVisible(false);
        }
    } else {
        m_changelogBrowser->setVisible(false);
        if (m_emptyChangelogLabel) {
            m_emptyChangelogLabel->setText(
                QStringLiteral("Não foi possível carregar as notas da versão %1.").arg(version));
            m_emptyChangelogLabel->setVisible(true);
        }
    }
}

void AppDetailsModal::showApp(const AppInfo& app, const QRect& originRect) {
    if (m_geometryAnim) {
        m_geometryAnim->stop();
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
    }

    m_isClosing = false;
    m_app = app;
    m_originRect = originRect;

    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    updateVisuals();

    const QRect target = targetCardRect();
    const QRect start(target.x(), target.y() + 8, target.width(), target.height());

    setCardGeometry(start);
    setBackdropOpacity(0.0);

    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setVisible(true);
    raise();
    setFocus();

    m_geometryAnim = new QPropertyAnimation(this, "cardGeometry", this);
    m_geometryAnim->setDuration(120);
    m_geometryAnim->setStartValue(start);
    m_geometryAnim->setEndValue(target);
    m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnim = new QPropertyAnimation(this, "backdropOpacity", this);
    m_opacityAnim->setDuration(110);
    m_opacityAnim->setStartValue(0.0);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_geometryAnim->start(QAbstractAnimation::DeleteWhenStopped);
    m_opacityAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AppDetailsModal::closeWithAnimation() {
    if (m_isClosing || !isVisible()) {
        return;
    }
    m_isClosing = true;

    // Immediately stop intercepting mouse events so parent is responsive
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    const QRect current = cardGeometry();
    const QRect end(current.x(), current.y() + 6, current.width(), current.height());

    if (m_geometryAnim) {
        m_geometryAnim->stop();
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
    }

    m_geometryAnim = new QPropertyAnimation(this, "cardGeometry", this);
    m_geometryAnim->setDuration(90);
    m_geometryAnim->setStartValue(current);
    m_geometryAnim->setEndValue(end);
    m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnim = new QPropertyAnimation(this, "backdropOpacity", this);
    m_opacityAnim->setDuration(80);
    m_opacityAnim->setStartValue(m_backdropOpacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_geometryAnim, &QPropertyAnimation::finished, this, [this]() {
        setVisible(false);
        lower();
        m_isClosing = false;
        if (parentWidget()) {
            parentWidget()->setFocus();
        }
        emit closed();
    });

    m_geometryAnim->start(QAbstractAnimation::DeleteWhenStopped);
    m_opacityAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AppDetailsModal::updateVisuals() {
    m_iconLabel->setPixmap(getAppIcon(m_app));
    m_nameLabel->setText(m_app.name());
    m_taglineLabel->setText(m_app.tagLine());
    m_descText->setText(m_app.description());

    // Action button text and style (monochrome)
    if (m_app.isInstalled()) {
        if (m_app.hasUpdate()) {
            m_actionButton->setText(QStringLiteral("Atualizar"));
            m_actionButton->setStyleSheet(HubStyle::primaryButtonStyle());
        } else {
            m_actionButton->setText(QStringLiteral("Abrir"));
            m_actionButton->setStyleSheet(HubStyle::secondaryButtonStyle());
        }
    } else {
        m_actionButton->setText(QStringLiteral("Baixar"));
        m_actionButton->setStyleSheet(HubStyle::primaryButtonStyle());
    }

    // Safely clear previous features
    clearFeatures();

    // Add features as row widgets
    for (const auto& feat : m_app.features()) {
        auto* rowWidget = new QWidget(m_cardFrame);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        auto* check = new QLabel(QStringLiteral("✓"), rowWidget);
        check->setStyleSheet(QStringLiteral("color: #ffffff; font-weight: bold; font-size: 13px;"));
        rowLayout->addWidget(check);

        auto* label = new QLabel(feat, rowWidget);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("font-size: 12px; color: #c4c4c4;"));
        rowLayout->addWidget(label, 1);

        m_featuresLayout->addWidget(rowWidget);
    }

    if (!m_app.projectFormat().isEmpty()) {
        m_formatLabel->setText(QStringLiteral("<b>Formato nativo de projeto:</b> %1").arg(m_app.projectFormat()));
        m_formatLabel->setVisible(true);
    } else {
        m_formatLabel->setVisible(false);
    }

    m_exeLabel->setText(QStringLiteral("<b>Arquivo executável:</b> %1").arg(m_app.executableName()));

    // Populate changelog section
    const QStringList versions = m_changelogReader.availableVersions(m_app.id());
    if (!versions.isEmpty()) {
        if (m_emptyChangelogLabel) {
            m_emptyChangelogLabel->setVisible(false);
        }
        if (m_changelogBrowser) {
            m_changelogBrowser->setVisible(true);
        }

        if (m_versionCombo) {
            m_versionCombo->blockSignals(true);
            m_versionCombo->clear();
            for (const auto& ver : versions) {
                m_versionCombo->addItem(QStringLiteral("v%1").arg(ver), ver);
            }
            m_versionCombo->blockSignals(false);
        }

        int selectedIdx = 0;
        const QString preferredVer = !m_app.installedVersion().isEmpty() ? m_app.installedVersion() : m_app.latestVersion();
        const int matchIdx = versions.indexOf(preferredVer);
        if (matchIdx >= 0) {
            selectedIdx = matchIdx;
        }

        if (versions.size() > 1 && m_versionCombo) {
            if (m_versionBadge) {
                m_versionBadge->setVisible(false);
            }
            m_versionCombo->setVisible(true);
            m_versionCombo->setCurrentIndex(selectedIdx);
        } else {
            if (m_versionCombo) {
                m_versionCombo->setVisible(false);
            }
            if (m_versionBadge) {
                m_versionBadge->setText(QStringLiteral("v%1").arg(versions.first()));
                m_versionBadge->setVisible(true);
            }
        }

        loadChangelogForVersion(versions.value(selectedIdx));
    } else {
        if (m_versionCombo) {
            m_versionCombo->setVisible(false);
        }
        if (m_versionBadge) {
            m_versionBadge->setVisible(false);
        }
        if (m_changelogBrowser) {
            m_changelogBrowser->setVisible(false);
        }
        if (m_emptyChangelogLabel) {
            m_emptyChangelogLabel->setText(
                QStringLiteral("Nenhuma nota de versão registrada para o %1 ainda. As notas de lançamento serão disponibilizadas aqui conforme novas versões forem lançadas.")
                .arg(m_app.name()));
            m_emptyChangelogLabel->setVisible(true);
        }
    }
}

void AppDetailsModal::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    // Translucent dark background overlay
    const int alpha = static_cast<int>(m_backdropOpacity * 170.0);
    painter.fillRect(rect(), QColor(0, 0, 0, std::clamp(alpha, 0, 255)));
}

void AppDetailsModal::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_cardFrame && !m_cardFrame->geometry().contains(event->pos())) {
            closeWithAnimation();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void AppDetailsModal::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        closeWithAnimation();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AppDetailsModal::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (isVisible() && !m_isClosing) {
        setCardGeometry(targetCardRect());
    }
}

} // namespace creative_suite::hub
