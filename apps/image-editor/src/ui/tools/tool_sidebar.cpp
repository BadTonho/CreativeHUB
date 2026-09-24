#include "tool_sidebar.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace image_editor {
namespace {

constexpr int kCollapsedSidebarWidth = 56;
constexpr int kExpandedSidebarWidth = 132;

QIcon paintToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen handle(QColor(205, 218, 231), 5.0, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(handle);
    painter.drawLine(QPointF(25.0, 6.0), QPointF(11.0, 20.0));

    QPainterPath tip;
    tip.moveTo(10.0, 18.0);
    tip.lineTo(15.0, 23.0);
    tip.lineTo(9.0, 28.0);
    tip.lineTo(4.0, 29.0);
    tip.lineTo(5.0, 24.0);
    tip.closeSubpath();
    painter.setPen(QPen(QColor(31, 38, 48), 1.0));
    painter.setBrush(QColor(52, 177, 224));
    painter.drawPath(tip);

    painter.setPen(QPen(QColor(247, 194, 83), 2.0, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(QPointF(21.0, 5.0), QPointF(27.0, 11.0));
    painter.end();
    return QIcon(icon);
}

} // namespace

ToolSidebar::ToolSidebar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imageEditorToolSidebar"));
    setFixedWidth(kCollapsedSidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 10, 4, 10);
    layout->setSpacing(10);

    paint_button_ = new QToolButton(this);
    paint_button_->setObjectName(QStringLiteral("paintToolButton"));
    paint_button_->setToolTip(QStringLiteral("Paint"));
    paint_button_->setAccessibleName(QStringLiteral("Paint tool"));
    paint_button_->setIcon(paintToolIcon());
    paint_button_->setIconSize(QSize(24, 24));
    paint_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    paint_button_->setCheckable(true);
    paint_button_->setFixedSize(40, 40);
    layout->addWidget(paint_button_, 0, Qt::AlignHCenter);

    brush_options_ = new QWidget(this);
    brush_options_->setObjectName(QStringLiteral("paintBrushOptions"));
    auto* options_layout = new QVBoxLayout(brush_options_);
    options_layout->setContentsMargins(0, 4, 0, 0);
    options_layout->setSpacing(6);

    auto* color_label = new QLabel(QStringLiteral("Color"), brush_options_);
    options_layout->addWidget(color_label);
    color_button_ = new QPushButton(QStringLiteral("Choose..."), brush_options_);
    color_button_->setObjectName(QStringLiteral("paintBrushColorButton"));
    color_button_->setIconSize(QSize(20, 20));
    options_layout->addWidget(color_button_);

    auto* diameter_label = new QLabel(QStringLiteral("Size (px)"), brush_options_);
    options_layout->addWidget(diameter_label);
    diameter_spin_ = new QSpinBox(brush_options_);
    diameter_spin_->setObjectName(QStringLiteral("paintBrushSizeSpinBox"));
    diameter_spin_->setRange(1, 512);
    diameter_spin_->setValue(12);
    diameter_spin_->setSuffix(QStringLiteral(" px"));
    options_layout->addWidget(diameter_spin_);

    brush_options_->hide();
    layout->addWidget(brush_options_);
    layout->addStretch(1);

    connect(paint_button_, &QToolButton::toggled, this, [this](bool active) {
        updateControls();
        emit paintToolToggled(active);
    });
    connect(diameter_spin_, &QSpinBox::valueChanged,
            this, &ToolSidebar::brushDiameterChanged);
    connect(color_button_, &QPushButton::clicked, this, [this]() {
        const QColor selected = QColorDialog::getColor(
            brush_color_, this, QStringLiteral("Brush Color"),
            QColorDialog::ShowAlphaChannel);
        if (!selected.isValid() || selected == brush_color_) return;
        brush_color_ = selected;
        updateColorButton();
        emit brushColorChanged(brush_color_);
    });

    updateColorButton();
    updateControls();
}

void ToolSidebar::setDocumentAvailable(bool available) {
    document_available_ = available;
    if (!document_available_) setPaintToolActive(false);
    updateControls();
}

void ToolSidebar::setPaintToolActive(bool active) {
    const QSignalBlocker blocker(paint_button_);
    paint_button_->setChecked(active && document_available_);
    updateControls();
}

bool ToolSidebar::paintToolActive() const noexcept {
    return paint_button_->isChecked();
}

QColor ToolSidebar::brushColor() const {
    return brush_color_;
}

int ToolSidebar::brushDiameter() const {
    return diameter_spin_->value();
}

void ToolSidebar::updateControls() {
    paint_button_->setEnabled(document_available_);
    const bool options_enabled = document_available_ && paint_button_->isChecked();
    setFixedWidth(options_enabled ? kExpandedSidebarWidth : kCollapsedSidebarWidth);
    brush_options_->setVisible(options_enabled);
    brush_options_->setEnabled(options_enabled);
}

void ToolSidebar::updateColorButton() {
    QPixmap swatch(20, 20);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    constexpr int tile = 5;
    for (int y = 0; y < swatch.height(); y += tile) {
        for (int x = 0; x < swatch.width(); x += tile) {
            const bool dark = ((x / tile) + (y / tile)) % 2 != 0;
            painter.fillRect(QRect(x, y, tile, tile),
                             dark ? QColor(150, 154, 160) : QColor(220, 222, 225));
        }
    }
    painter.fillRect(swatch.rect(), brush_color_);
    painter.setPen(QPen(QColor(35, 38, 43), 1.0));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    color_button_->setIcon(QIcon(swatch));
}

} // namespace image_editor
