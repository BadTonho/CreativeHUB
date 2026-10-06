#include "app_details_modal.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"
#include "../cards/app_status_badge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEasingCurve>
#include <algorithm>

namespace creative_suite::hub {

namespace {

QPixmap createFallbackIcon(const QString& id) {
    constexpr int size = 72;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor iconBg(0x00, 0x7a, 0xff);
    QString initials = QStringLiteral("CS");

    if (id == QStringLiteral("video-editor")) {
        iconBg = QColor(0x99, 0x45, 0xff);
        initials = QStringLiteral("Ve");
    } else if (id == QStringLiteral("image-editor")) {
        iconBg = QColor(0x00, 0x84, 0xff);
        initials = QStringLiteral("Ie");
    } else if (id == QStringLiteral("motion-editor")) {
        iconBg = QColor(0xec, 0x3b, 0x83);
        initials = QStringLiteral("Mo");
    }

    painter.setBrush(iconBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, size, size), 16, 16);

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
    setFocusPolicy(Qt::StrongFocus);
    setVisible(false);

    setupUi();
}

void AppDetailsModal::setupUi() {
    m_cardFrame = new QFrame(this);
    m_cardFrame->setObjectName(QStringLiteral("ModalCardFrame"));
    m_cardFrame->setStyleSheet(QString(R"(
        #ModalCardFrame {
            background-color: %1;
            border: 1px solid #363644;
            border-radius: 14px;
        }
    )").arg(HubPalette::cardBackground.name()));

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
    m_taglineLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #9a9aa8;"));
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
        "   background-color: rgba(255, 255, 255, 0.08);"
        "   border: 1px solid rgba(255, 255, 255, 0.12);"
        "   border-radius: 14px;"
        "   color: #b0b0c0;"
        "   font-size: 13px;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 60, 60, 0.3);"
        "   border-color: rgba(255, 80, 80, 0.5);"
        "   color: #ffffff;"
        "}"
    ));
    connect(m_closeIconButton, &QPushButton::clicked, this, &AppDetailsModal::closeWithAnimation);
    topHeaderLayout->addWidget(m_closeIconButton, 0, Qt::AlignTop);

    cardLayout->addLayout(topHeaderLayout);

    // Separator
    auto* separator = new QFrame(m_cardFrame);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("background-color: #2e2e3a; max-height: 1px; border: none;"));
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
    aboutTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787888; letter-spacing: 0.5px;"));
    contentLayout->addWidget(aboutTitle);

    m_descText = new QLabel(scrollContent);
    m_descText->setWordWrap(true);
    m_descText->setStyleSheet(QStringLiteral("font-size: 13px; color: #d0d0dc; line-height: 1.5;"));
    contentLayout->addWidget(m_descText);

    // Features section
    auto* featTitle = new QLabel(QStringLiteral("PRINCIPAIS RECURSOS"), scrollContent);
    featTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787888; letter-spacing: 0.5px; margin-top: 4px;"));
    contentLayout->addWidget(featTitle);

    m_featuresLayout = new QVBoxLayout();
    m_featuresLayout->setSpacing(8);
    contentLayout->addLayout(m_featuresLayout);

    // Technical specifications
    auto* techTitle = new QLabel(QStringLiteral("INFORMAÇÕES TÉCNICAS"), scrollContent);
    techTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787888; letter-spacing: 0.5px; margin-top: 4px;"));
    contentLayout->addWidget(techTitle);

    auto* techCard = new QFrame(scrollContent);
    techCard->setStyleSheet(QStringLiteral(
        "background-color: #24242c;"
        "border: 1px solid #32323c;"
        "border-radius: 8px;"
        "padding: 10px 14px;"
    ));
    auto* techLayout = new QVBoxLayout(techCard);
    techLayout->setSpacing(6);

    m_formatLabel = new QLabel(techCard);
    m_formatLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0bc;"));
    techLayout->addWidget(m_formatLabel);

    m_exeLabel = new QLabel(techCard);
    m_exeLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0bc;"));
    techLayout->addWidget(m_exeLabel);

    contentLayout->addWidget(techCard);
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
    const int cardW = std::clamp(width() - 80, 480, 620);
    const int cardH = std::clamp(height() - 60, 420, 520);
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

void AppDetailsModal::showApp(const AppInfo& app, const QRect& originRect) {
    m_isClosing = false;
    m_app = app;
    m_originRect = originRect;

    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    updateVisuals();

    const QRect target = targetCardRect();
    QRect start = originRect;
    if (!start.isValid() || start.isEmpty()) {
        const int startW = static_cast<int>(target.width() * 0.75);
        const int startH = static_cast<int>(target.height() * 0.75);
        const int startX = target.x() + (target.width() - startW) / 2;
        const int startY = target.y() + (target.height() - startH) / 2;
        start = QRect(startX, startY, startW, startH);
    }

    setCardGeometry(start);
    setBackdropOpacity(0.0);

    setVisible(true);
    raise();
    setFocus();

    if (m_geometryAnim && m_geometryAnim->state() == QAbstractAnimation::Running) {
        m_geometryAnim->stop();
    }
    if (m_opacityAnim && m_opacityAnim->state() == QAbstractAnimation::Running) {
        m_opacityAnim->stop();
    }

    m_geometryAnim = new QPropertyAnimation(this, "cardGeometry", this);
    m_geometryAnim->setDuration(260);
    m_geometryAnim->setStartValue(start);
    m_geometryAnim->setEndValue(target);
    m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnim = new QPropertyAnimation(this, "backdropOpacity", this);
    m_opacityAnim->setDuration(240);
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

    QRect end = m_originRect;
    if (!end.isValid() || end.isEmpty()) {
        const QRect current = cardGeometry();
        const int endW = static_cast<int>(current.width() * 0.75);
        const int endH = static_cast<int>(current.height() * 0.75);
        const int endX = current.x() + (current.width() - endW) / 2;
        const int endY = current.y() + (current.height() - endH) / 2;
        end = QRect(endX, endY, endW, endH);
    }

    if (m_geometryAnim && m_geometryAnim->state() == QAbstractAnimation::Running) {
        m_geometryAnim->stop();
    }
    if (m_opacityAnim && m_opacityAnim->state() == QAbstractAnimation::Running) {
        m_opacityAnim->stop();
    }

    m_geometryAnim = new QPropertyAnimation(this, "cardGeometry", this);
    m_geometryAnim->setDuration(220);
    m_geometryAnim->setStartValue(cardGeometry());
    m_geometryAnim->setEndValue(end);
    m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnim = new QPropertyAnimation(this, "backdropOpacity", this);
    m_opacityAnim->setDuration(200);
    m_opacityAnim->setStartValue(m_backdropOpacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_geometryAnim, &QPropertyAnimation::finished, this, [this]() {
        setVisible(false);
        m_isClosing = false;
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

    // Action button text and style
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

    // Clear previous features
    QLayoutItem* item = nullptr;
    while ((item = m_featuresLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        if (item->layout()) {
            QLayoutItem* subItem = nullptr;
            while ((subItem = item->layout()->takeAt(0)) != nullptr) {
                delete subItem->widget();
                delete subItem;
            }
            delete item->layout();
        }
        delete item;
    }

    // Add features
    for (const auto& feat : m_app.features()) {
        auto* featRow = new QHBoxLayout();
        featRow->setSpacing(10);

        auto* check = new QLabel(QStringLiteral("✓"), m_cardFrame);
        check->setStyleSheet(QStringLiteral("color: %1; font-weight: bold; font-size: 13px;").arg(HubPalette::accentPrimary.name()));
        featRow->addWidget(check);

        auto* label = new QLabel(feat, m_cardFrame);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("font-size: 12px; color: #c4c4d2;"));
        featRow->addWidget(label, 1);

        m_featuresLayout->addLayout(featRow);
    }

    if (!m_app.projectFormat().isEmpty()) {
        m_formatLabel->setText(QStringLiteral("<b>Formato nativo de projeto:</b> %1").arg(m_app.projectFormat()));
        m_formatLabel->setVisible(true);
    } else {
        m_formatLabel->setVisible(false);
    }

    m_exeLabel->setText(QStringLiteral("<b>Arquivo executável:</b> %1").arg(m_app.executableName()));
}

void AppDetailsModal::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Translucent dark background overlay
    const int alpha = static_cast<int>(m_backdropOpacity * 170.0);
    painter.fillRect(rect(), QColor(10, 10, 14, std::clamp(alpha, 0, 255)));
}

void AppDetailsModal::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Clicking outside the popup card closes it!
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
