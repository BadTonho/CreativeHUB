#include "expandable_search_bar.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QEvent>
#include <QKeyEvent>

namespace creative_suite::hub {

QIcon ExpandableSearchBar::createSearchIcon(const QColor& color) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    p.drawEllipse(QPointF(10.0, 10.0), 5.5, 5.5);
    p.drawLine(QPointF(14.5, 14.5), QPointF(19.0, 19.0));

    return QIcon(pixmap);
}

QIcon ExpandableSearchBar::createCloseIcon(const QColor& color) {
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    p.drawLine(QPointF(5, 5), QPointF(13, 13));
    p.drawLine(QPointF(13, 5), QPointF(5, 13));

    return QIcon(pixmap);
}

ExpandableSearchBar::ExpandableSearchBar(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void ExpandableSearchBar::setupUi() {
    setFixedHeight(36);
    setFixedWidth(kCollapsedWidth);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search button (icon trigger)
    m_searchButton = new QPushButton(this);
    m_searchButton->setFixedSize(36, 36);
    m_searchButton->setCursor(Qt::PointingHandCursor);
    m_searchButton->setIcon(createSearchIcon(QColor(0xa0, 0xa0, 0xb0)));
    m_searchButton->setIconSize(QSize(20, 20));
    m_searchButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: #222228;"
        "   border: 1px solid #383842;"
        "   border-radius: 18px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2e2e36;"
        "   border-color: #4c4c58;"
        "}"
    ));
    connect(m_searchButton, &QPushButton::clicked, this, [this]() {
        if (!m_expanded) {
            expand();
        } else if (m_lineEdit->text().isEmpty()) {
            collapse();
        } else {
            m_lineEdit->setFocus();
        }
    });
    layout->addWidget(m_searchButton);

    // Text field (initially hidden/compact)
    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setPlaceholderText(QStringLiteral("Buscar aplicativos..."));
    m_lineEdit->setVisible(false);
    m_lineEdit->installEventFilter(this);
    m_lineEdit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "   background-color: transparent;"
        "   color: #ffffff;"
        "   border: none;"
        "   font-size: 12px;"
        "   padding: 0px 8px;"
        "}"
    ));
    connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_closeButton->setVisible(!text.isEmpty());
        emit searchTextChanged(text);
    });
    layout->addWidget(m_lineEdit, 1);

    // Close / clear button
    m_closeButton = new QPushButton(this);
    m_closeButton->setFixedSize(28, 28);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setIcon(createCloseIcon(QColor(0x90, 0x90, 0xa0)));
    m_closeButton->setIconSize(QSize(14, 14));
    m_closeButton->setVisible(false);
    m_closeButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background: transparent;"
        "   border: none;"
        "   border-radius: 14px;"
        "   margin-right: 4px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #343440;"
        "}"
    ));
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        m_lineEdit->clear();
        collapse();
    });
    layout->addWidget(m_closeButton);

    // Animation setup
    m_animation = new QPropertyAnimation(this, "animatedWidth", this);
    m_animation->setDuration(240);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
}

void ExpandableSearchBar::setAnimatedWidth(int w) {
    setFixedWidth(w);
}

QString ExpandableSearchBar::text() const {
    return m_lineEdit ? m_lineEdit->text() : QString();
}

void ExpandableSearchBar::expand() {
    if (m_expanded) {
        return;
    }
    m_expanded = true;

    // Transition search button to inner prefix style
    m_searchButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background: transparent;"
        "   border: none;"
        "   border-top-left-radius: 18px;"
        "   border-bottom-left-radius: 18px;"
        "   margin-left: 4px;"
        "}"
    ));

    setStyleSheet(QStringLiteral(
        "ExpandableSearchBar {"
        "   background-color: #222228;"
        "   border: 1px solid %1;"
        "   border-radius: 18px;"
        "}"
    ).arg(HubPalette::accentPrimary.name()));

    m_lineEdit->setVisible(true);

    m_animation->stop();
    m_animation->setStartValue(width());
    m_animation->setEndValue(kExpandedWidth);
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        if (m_expanded) {
            m_lineEdit->setFocus();
        }
    });
    m_animation->start();
}

void ExpandableSearchBar::collapse() {
    if (!m_expanded) {
        return;
    }
    m_expanded = false;

    m_lineEdit->clear();
    m_closeButton->setVisible(false);

    m_animation->stop();
    m_animation->setStartValue(width());
    m_animation->setEndValue(kCollapsedWidth);
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_expanded) {
            m_lineEdit->setVisible(false);
            setStyleSheet(QStringLiteral("ExpandableSearchBar { background: transparent; border: none; }"));
            m_searchButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #222228;"
                "   border: 1px solid #383842;"
                "   border-radius: 18px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #2e2e36;"
                "   border-color: #4c4c58;"
                "}"
            ));
        }
    });
    m_animation->start();
}

void ExpandableSearchBar::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
}

bool ExpandableSearchBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_lineEdit) {
        if (event->type() == QEvent::FocusOut) {
            if (m_lineEdit->text().trimmed().isEmpty()) {
                collapse();
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                collapse();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace creative_suite::hub
