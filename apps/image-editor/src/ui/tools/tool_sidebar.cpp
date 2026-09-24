#include "tool_sidebar.h"

#include <QColorDialog>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace image_editor {
namespace {

constexpr int kCollapsedSidebarWidth = 56;

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

QIcon eraserToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath eraser;
    eraser.moveTo(8, 19);
    eraser.lineTo(19, 8);
    eraser.quadTo(21, 6, 23, 8);
    eraser.lineTo(28, 13);
    eraser.quadTo(30, 15, 28, 17);
    eraser.lineTo(17, 28);
    eraser.lineTo(8, 19);
    eraser.closeSubpath();
    painter.setPen(QPen(QColor(24, 28, 34), 1.5));
    painter.setBrush(QColor(226, 105, 147));
    painter.drawPath(eraser);
    painter.setPen(QPen(QColor(242, 224, 232), 1.3));
    painter.drawLine(QPointF(9, 20), QPointF(17, 28));
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

    eraser_button_ = new QToolButton(this);
    eraser_button_->setObjectName(QStringLiteral("eraserToolButton"));
    eraser_button_->setToolTip(QStringLiteral("Eraser"));
    eraser_button_->setAccessibleName(QStringLiteral("Eraser tool"));
    eraser_button_->setIcon(eraserToolIcon());
    eraser_button_->setIconSize(QSize(24, 24));
    eraser_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    eraser_button_->setCheckable(true);
    eraser_button_->setFixedSize(40, 40);
    layout->addWidget(eraser_button_, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    color_button_ = new QToolButton(this);
    color_button_->setObjectName(QStringLiteral("paintBrushColorButton"));
    color_button_->setToolTip(QStringLiteral("Paint color"));
    color_button_->setAccessibleName(QStringLiteral("Paint color"));
    color_button_->setIconSize(QSize(24, 24));
    color_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    color_button_->setFixedSize(40, 40);
    layout->addWidget(color_button_, 0, Qt::AlignHCenter);

    connect(paint_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Paint);
        else if (active_tool_ == Tool::Paint) setActiveTool(Tool::None);
    });
    connect(eraser_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Eraser);
        else if (active_tool_ == Tool::Eraser) setActiveTool(Tool::None);
    });
    connect(color_button_, &QToolButton::clicked, this, [this]() {
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
    if (!document_available_ || !painting_allowed_) setActiveTool(Tool::None);
    updateControls();
}

void ToolSidebar::setPaintingAllowed(bool allowed) {
    painting_allowed_ = allowed;
    if (!painting_allowed_) setActiveTool(Tool::None);
    updateControls();
}

void ToolSidebar::setPaintToolActive(bool active) {
    setActiveTool(active ? Tool::Paint :
        (active_tool_ == Tool::Paint ? Tool::None : active_tool_));
}

void ToolSidebar::setEraserToolActive(bool active) {
    setActiveTool(active ? Tool::Eraser :
        (active_tool_ == Tool::Eraser ? Tool::None : active_tool_));
}

void ToolSidebar::setActiveTool(Tool tool) {
    if (!document_available_ || !painting_allowed_) tool = Tool::None;
    const bool changed = active_tool_ != tool;
    active_tool_ = tool;
    {
        const QSignalBlocker paint_blocker(paint_button_);
        const QSignalBlocker eraser_blocker(eraser_button_);
        paint_button_->setChecked(tool == Tool::Paint);
        eraser_button_->setChecked(tool == Tool::Eraser);
    }
    updateControls();
    if (changed) emit activeToolChanged(active_tool_);
}

bool ToolSidebar::paintToolActive() const noexcept {
    return active_tool_ == Tool::Paint;
}

bool ToolSidebar::eraserToolActive() const noexcept {
    return active_tool_ == Tool::Eraser;
}

QColor ToolSidebar::brushColor() const {
    return brush_color_;
}

void ToolSidebar::updateControls() {
    const bool enabled = document_available_ && painting_allowed_;
    paint_button_->setEnabled(enabled);
    eraser_button_->setEnabled(enabled);
    paint_button_->setToolTip(enabled
        ? QStringLiteral("Paint")
        : (document_available_
            ? QStringLiteral("Select or create an editable layer to paint")
            : QStringLiteral("Open an image to paint")));
    eraser_button_->setToolTip(enabled
        ? QStringLiteral("Eraser")
        : (document_available_
            ? QStringLiteral("Select or create an editable layer to erase")
            : QStringLiteral("Open an image to erase")));
    color_button_->setEnabled(true);
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
