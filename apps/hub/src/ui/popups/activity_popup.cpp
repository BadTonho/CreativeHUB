#include "activity_popup.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QDateTime>
#include <QPainter>

namespace creative_suite::hub {

namespace {

QString formatTimestamp(const QDateTime& dt) {
    if (!dt.isValid()) return QString();
    const auto now = QDateTime::currentDateTime();
    const qint64 secs = dt.secsTo(now);

    if (secs < 60) return QStringLiteral("Agora há pouco");
    if (secs < 3600) return QStringLiteral("há %1 min").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("há %1 h").arg(secs / 3600);
    return dt.toString(QStringLiteral("dd/MM/yyyy HH:mm"));
}

QString categoryBadgeText(const QString& category) {
    if (category == QStringLiteral("backup")) return QStringLiteral("BKP");
    if (category == QStringLiteral("project")) return QStringLiteral("PRJ");
    if (category == QStringLiteral("cache")) return QStringLiteral("CCH");
    if (category == QStringLiteral("update")) return QStringLiteral("UPD");
    return QStringLiteral("SYS");
}

} // namespace

ActivityPopup::ActivityPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(360);
    setFixedHeight(420);

    setupUi();

    connect(&ActivityManager::instance(), &ActivityManager::activitiesChanged,
            this, &ActivityPopup::refreshActivities);
}

void ActivityPopup::setupUi() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* container = new QFrame(this);
    container->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #141414;"
        "   border: 1px solid #282828;"
        "   border-radius: 10px;"
        "}"
    ));

    auto* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    // Header bar
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("ATIVIDADES"), container);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 1.2px; background: transparent; border: none;"
    ));
    headerLayout->addWidget(titleLabel);

    m_countBadge = new QLabel(QStringLiteral("0"), container);
    m_countBadge->setStyleSheet(QStringLiteral(
        "background-color: #222222;"
        "color: #ffffff;"
        "border: 1px solid #333333;"
        "border-radius: 9px;"
        "font-size: 10px;"
        "font-weight: 700;"
        "padding: 1px 6px;"
    ));
    headerLayout->addWidget(m_countBadge);

    headerLayout->addStretch();

    auto* markReadBtn = new QPushButton(QStringLiteral("Marcar lidas"), container);
    markReadBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background: transparent;"
        "   color: #888888;"
        "   border: none;"
        "   font-size: 10px;"
        "   font-weight: 600;"
        "   padding: 2px 6px;"
        "}"
        "QPushButton:hover {"
        "   color: #ffffff;"
        "}"
    ));
    markReadBtn->setCursor(Qt::PointingHandCursor);
    connect(markReadBtn, &QPushButton::clicked, this, &ActivityPopup::onMarkAllRead);
    headerLayout->addWidget(markReadBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("Limpar"), container);
    clearBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background: transparent;"
        "   color: #888888;"
        "   border: none;"
        "   font-size: 10px;"
        "   font-weight: 600;"
        "   padding: 2px 6px;"
        "}"
        "QPushButton:hover {"
        "   color: #ffffff;"
        "}"
    ));
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, &ActivityPopup::onClearAll);
    headerLayout->addWidget(clearBtn);

    mainLayout->addLayout(headerLayout);

    // Scroll area for activities
    m_scrollArea = new QScrollArea(container);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: #161616; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #333333; min-height: 20px; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background: #555555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet(QStringLiteral("background: transparent;"));
    m_listLayout = new QVBoxLayout(scrollContent);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(6);

    // Empty state widget
    m_emptyWidget = new QWidget(scrollContent);
    auto* emptyLayout = new QVBoxLayout(m_emptyWidget);
    emptyLayout->setContentsMargins(10, 40, 10, 40);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyLabel = new QLabel(QStringLiteral("Nenhuma atividade recente registrada."), m_emptyWidget);
    emptyLabel->setStyleSheet(QStringLiteral("color: #555555; font-size: 12px; background: transparent; border: none;"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);
    m_listLayout->addWidget(m_emptyWidget);

    m_listLayout->addStretch();
    m_scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(m_scrollArea, 1);

    outerLayout->addWidget(container);

    refreshActivities();
}

QWidget* ActivityPopup::createActivityCard(const ActivityItem& item) {
    auto* card = new QFrame();
    const QString borderCol = item.isRead ? QStringLiteral("#222222") : QStringLiteral("#3a3a3a");
    const QString bgCol = item.isRead ? QStringLiteral("#181818") : QStringLiteral("#1e1e1e");

    card->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 6px;"
        "   padding: 2px;"
        "}"
    ).arg(bgCol, borderCol));

    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // Category badge
    auto* badge = new QLabel(categoryBadgeText(item.category), card);
    badge->setFixedSize(32, 24);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(QStringLiteral(
        "background-color: #262626;"
        "color: #ffffff;"
        "border: 1px solid #383838;"
        "border-radius: 4px;"
        "font-size: 9px;"
        "font-weight: 800;"
        "letter-spacing: 0.5px;"
    ));
    layout->addWidget(badge);

    // Text details
    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);

    auto* titleLabel = new QLabel(item.title, card);
    titleLabel->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 12px; font-weight: 700; background: transparent; border: none;"
    ));
    topRow->addWidget(titleLabel);
    topRow->addStretch();

    auto* timeLabel = new QLabel(formatTimestamp(item.timestamp), card);
    timeLabel->setStyleSheet(QStringLiteral(
        "color: #666666; font-size: 10px; background: transparent; border: none;"
    ));
    topRow->addWidget(timeLabel);

    textLayout->addLayout(topRow);

    auto* descLabel = new QLabel(item.description, card);
    descLabel->setStyleSheet(QStringLiteral(
        "color: #999999; font-size: 11px; background: transparent; border: none;"
    ));
    descLabel->setWordWrap(true);
    textLayout->addWidget(descLabel);

    layout->addLayout(textLayout, 1);

    return card;
}

void ActivityPopup::refreshActivities() {
    // Clear existing cards (except empty widget and stretch)
    QLayoutItem* child;
    while ((child = m_listLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            if (child->widget() == m_emptyWidget) {
                // keep reference
            } else {
                delete child->widget();
            }
        }
        delete child;
    }

    const auto& list = ActivityManager::instance().activities();
    const int unread = ActivityManager::instance().unreadCount();

    if (m_countBadge) {
        m_countBadge->setText(QString::number(unread > 0 ? unread : static_cast<int>(list.size())));
        m_countBadge->setStyleSheet(QStringLiteral(
            "background-color: %1;"
            "color: #ffffff;"
            "border: 1px solid #333333;"
            "border-radius: 9px;"
            "font-size: 10px;"
            "font-weight: 700;"
            "padding: 1px 6px;"
        ).arg(unread > 0 ? QStringLiteral("#333333") : QStringLiteral("#222222")));
    }

    if (list.empty()) {
        m_listLayout->addWidget(m_emptyWidget);
        m_emptyWidget->show();
    } else {
        m_emptyWidget->hide();
        for (const auto& item : list) {
            m_listLayout->addWidget(createActivityCard(item));
        }
    }

    m_listLayout->addStretch();
}

void ActivityPopup::showAt(const QPoint& globalPos) {
    move(globalPos.x() - width() + 36, globalPos.y() + 6);
    refreshActivities();
    show();
    raise();
    activateWindow();
}

void ActivityPopup::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
}

void ActivityPopup::onMarkAllRead() {
    ActivityManager::instance().markAllAsRead();
}

void ActivityPopup::onClearAll() {
    ActivityManager::instance().clear();
}

} // namespace creative_suite::hub
