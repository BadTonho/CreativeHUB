#include "scopes_page.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ScopesPage", text);
}

enum class ScopeMode { Waveform = 0, Parade = 1, Histogram = 2, Vectorscope = 3 };

// Scope graticule: gold level lines and labels, lane or grid lines per mode
// and an empty-frame message.
class ScopeGraph final : public QWidget {
public:
    explicit ScopeGraph(QWidget* parent) : QWidget(parent) {
        setMinimumSize(260, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        publish();
    }

    void setMode(ScopeMode mode) {
        mode_ = mode;
        publish();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::black);
        const QColor gold("#b8901c");
        constexpr double kLeft = 38.0;
        const QRectF area(kLeft, 8.0, width() - kLeft - 6.0, height() - 16.0);

        if (mode_ == ScopeMode::Vectorscope) {
            // Circular graticule with crosshair.
            const QPointF center = area.center();
            const double radius = std::min(area.width(), area.height()) / 2.0 - 4.0;
            painter.setPen(QPen(gold, 1.0));
            painter.setBrush(Qt::NoBrush);
            for (const double fraction : {0.25, 0.5, 0.75, 1.0})
                painter.drawEllipse(center, radius * fraction, radius * fraction);
            painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
            painter.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
        } else if (mode_ == ScopeMode::Histogram) {
            // Vertical grid over a baseline.
            painter.setPen(QPen(gold, 1.0));
            for (int i = 0; i <= 8; ++i) {
                const double x = area.left() + area.width() * i / 8.0;
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            }
            painter.drawLine(area.bottomLeft(), area.bottomRight());
        } else {
            // Level lines and labels, 0 to 1023.
            painter.setPen(QPen(gold, 1.0));
            for (const int level : {0, 128, 256, 384, 512, 640, 768, 896, 1023}) {
                const double y = area.bottom() - area.height() * level / 1023.0;
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                const QRectF label(0.0, y - 8.0, kLeft - 4.0, 16.0);
                painter.drawText(label, Qt::AlignRight | Qt::AlignVCenter, QString::number(level));
            }
            if (mode_ == ScopeMode::Parade) {
                // Three lanes for Red, Green and Blue.
                painter.setPen(QPen(QColor(184, 144, 28, 130), 1.0, Qt::DashLine));
                for (int lane = 1; lane < 3; ++lane) {
                    const double x = area.left() + area.width() * lane / 3.0;
                    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                }
            }
        }

        painter.setPen(QColor("#4a5260"));
        painter.drawText(area, Qt::AlignCenter, tr("No frame"));
    }

private:
    // The mode index is exposed as the "mode" property for tests.
    void publish() { setProperty("mode", static_cast<int>(mode_)); }

    ScopeMode mode_ = ScopeMode::Parade;
};

QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 28) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 26);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

}  // namespace

ScopesPage::ScopesPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorScopesPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Scopes"), this);
    title->setObjectName("colorScopesTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* mode = new QComboBox(this);
    mode->setObjectName("colorScopesMode");
    mode->addItem(tr("Waveform"));
    mode->addItem(tr("Parade"));
    mode->addItem(tr("Histogram"));
    mode->addItem(tr("Vectorscope"));
    mode->setCurrentIndex(static_cast<int>(ScopeMode::Parade));
    header->addWidget(mode);
    header->addWidget(makeButton(this, QStringLiteral("☰"),
        QStringLiteral("colorScopesSettingsButton"), tr("Scope settings")));
    header->addWidget(makeButton(this, QStringLiteral("⤢"),
        QStringLiteral("colorScopesExpandButton"), tr("Expand")));
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorScopesMoreButton"), tr("More actions")));
    root->addLayout(header);

    auto* graph = new ScopeGraph(this);
    graph->setObjectName("colorScopesGraph");
    root->addWidget(graph, 1);

    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [graph](int index) {
        graph->setMode(static_cast<ScopeMode>(index));
    });
}

void ScopesPage::restoreDefaults() {
    if (auto* mode = findChild<QComboBox*>("colorScopesMode"))
        mode->setCurrentIndex(static_cast<int>(ScopeMode::Parade));
}

}  // namespace ui
