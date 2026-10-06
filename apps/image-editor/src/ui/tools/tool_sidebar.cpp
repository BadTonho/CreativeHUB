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

QIcon bucketFillToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath bucket;
    bucket.moveTo(7, 9);
    bucket.lineTo(13, 5);
    bucket.lineTo(25, 17);
    bucket.lineTo(19, 23);
    bucket.closeSubpath();
    painter.setPen(QPen(QColor(28, 33, 40), 1.5));
    painter.setBrush(QColor(210, 220, 233));
    painter.drawPath(bucket);
    painter.setPen(QPen(QColor(246, 180, 76), 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(18, 22), QPointF(15, 26));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(84, 183, 231));
    painter.drawEllipse(QRectF(21, 23, 5, 6));
    painter.end();
    return QIcon(icon);
}

QIcon shapesToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(230, 210, 130), 2.2));
    painter.drawLine(QPointF(4, 26), QPointF(27, 5));
    painter.setBrush(QColor(80, 155, 210, 70));
    painter.drawRect(QRectF(5, 6, 13, 12));
    painter.setBrush(QColor(225, 125, 170, 70));
    painter.drawEllipse(QRectF(16, 15, 12, 12));
    return QIcon(icon);
}

QIcon selectToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath pointer;
    pointer.moveTo(6.0, 3.0);
    pointer.lineTo(6.0, 25.0);
    pointer.lineTo(11.6, 19.8);
    pointer.lineTo(16.2, 28.0);
    pointer.lineTo(20.0, 26.0);
    pointer.lineTo(15.5, 17.8);
    pointer.lineTo(24.5, 17.8);
    pointer.closeSubpath();
    painter.setPen(QPen(QColor(31, 38, 48), 1.8, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(232, 239, 248));
    painter.drawPath(pointer);
    return QIcon(icon);
}

QIcon areaSelectionToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(102, 194, 244), 2.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(5.0, 6.0, 22.0, 19.0));
    painter.setPen(QPen(QColor(237, 242, 248), 2.0, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(11, 12), QPointF(21, 12));
    painter.drawLine(QPointF(11, 17), QPointF(18, 17));
    return QIcon(icon);
}

QIcon eyedropperToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath pipette;
    pipette.moveTo(8.0, 19.0);
    pipette.lineTo(19.0, 8.0);
    pipette.lineTo(24.0, 13.0);
    pipette.lineTo(13.0, 24.0);
    pipette.closeSubpath();
    painter.setPen(QPen(QColor(31, 38, 48), 1.8, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(91, 184, 221));
    painter.drawPath(pipette);
    painter.setPen(QPen(QColor(242, 224, 232), 2.0, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(QPointF(10.0, 22.0), QPointF(5.0, 27.0));
    painter.setPen(QPen(QColor(247, 194, 83), 2.0, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(QPointF(18.0, 9.0), QPointF(23.0, 4.0));
    painter.drawLine(QPointF(22.0, 5.0), QPointF(27.0, 10.0));
    painter.end();
    return QIcon(icon);
}

QIcon textToolIcon() {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(233, 238, 245), 3.0, Qt::SolidLine,
                        Qt::SquareCap, Qt::MiterJoin));
    painter.drawLine(QPointF(6, 7), QPointF(26, 7));
    painter.drawLine(QPointF(16, 7), QPointF(16, 26));
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

    bucket_fill_button_ = new QToolButton(this);
    bucket_fill_button_->setObjectName(QStringLiteral("bucketFillToolButton"));
    bucket_fill_button_->setToolTip(QStringLiteral("Bucket Fill"));
    bucket_fill_button_->setAccessibleName(QStringLiteral("Bucket Fill tool"));
    bucket_fill_button_->setIcon(bucketFillToolIcon());
    bucket_fill_button_->setIconSize(QSize(24, 24));
    bucket_fill_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bucket_fill_button_->setCheckable(true);
    bucket_fill_button_->setFixedSize(40, 40);
    layout->addWidget(bucket_fill_button_, 0, Qt::AlignHCenter);

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

    shapes_button_ = new QToolButton(this);
    shapes_button_->setObjectName(QStringLiteral("shapesToolButton"));
    shapes_button_->setToolTip(QStringLiteral("Shapes"));
    shapes_button_->setAccessibleName(QStringLiteral("Shapes tool"));
    shapes_button_->setIcon(shapesToolIcon());
    shapes_button_->setIconSize(QSize(24, 24));
    shapes_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    shapes_button_->setCheckable(true);
    shapes_button_->setFixedSize(40, 40);
    layout->addWidget(shapes_button_, 0, Qt::AlignHCenter);

    text_button_ = new QToolButton(this);
    text_button_->setObjectName(QStringLiteral("textToolButton"));
    text_button_->setToolTip(QStringLiteral("Text"));
    text_button_->setAccessibleName(QStringLiteral("Text tool"));
    text_button_->setIcon(textToolIcon());
    text_button_->setIconSize(QSize(24, 24));
    text_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    text_button_->setCheckable(true);
    text_button_->setFixedSize(40, 40);
    layout->addWidget(text_button_, 0, Qt::AlignHCenter);

    select_shapes_button_ = new QToolButton(this);
    select_shapes_button_->setObjectName(QStringLiteral("selectShapesToolButton"));
    select_shapes_button_->setToolTip(QStringLiteral("Selection"));
    select_shapes_button_->setAccessibleName(QStringLiteral("Selection tool"));
    select_shapes_button_->setIcon(selectToolIcon());
    select_shapes_button_->setIconSize(QSize(24, 24));
    select_shapes_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    select_shapes_button_->setCheckable(true);
    select_shapes_button_->setFixedSize(40, 40);
    layout->addWidget(select_shapes_button_, 0, Qt::AlignHCenter);

    area_selection_button_ = new QToolButton(this);
    area_selection_button_->setObjectName(QStringLiteral("areaSelectionToolButton"));
    area_selection_button_->setToolTip(QStringLiteral("Area Selection"));
    area_selection_button_->setAccessibleName(QStringLiteral("Area Selection tool"));
    area_selection_button_->setIcon(areaSelectionToolIcon());
    area_selection_button_->setIconSize(QSize(24, 24));
    area_selection_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    area_selection_button_->setCheckable(true);
    area_selection_button_->setFixedSize(40, 40);
    layout->addWidget(area_selection_button_, 0, Qt::AlignHCenter);

    eyedropper_button_ = new QToolButton(this);
    eyedropper_button_->setObjectName(QStringLiteral("eyedropperToolButton"));
    eyedropper_button_->setToolTip(QStringLiteral("Eyedropper"));
    eyedropper_button_->setAccessibleName(QStringLiteral("Eyedropper tool"));
    eyedropper_button_->setIcon(eyedropperToolIcon());
    eyedropper_button_->setIconSize(QSize(24, 24));
    eyedropper_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    eyedropper_button_->setCheckable(true);
    eyedropper_button_->setFixedSize(40, 40);
    layout->addWidget(eyedropper_button_, 0, Qt::AlignHCenter);

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
    connect(bucket_fill_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::BucketFill);
        else if (active_tool_ == Tool::BucketFill) setActiveTool(Tool::None);
    });
    connect(eraser_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Eraser);
        else if (active_tool_ == Tool::Eraser) setActiveTool(Tool::None);
    });
    connect(shapes_button_, &QToolButton::clicked, this, [this]() {
        const QSignalBlocker blocker(shapes_button_);
        shapes_button_->setChecked(active_tool_ == Tool::Shapes);
        emit shapesPaletteRequested();
    });
    connect(select_shapes_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Select);
        else if (active_tool_ == Tool::Select) setActiveTool(Tool::None);
    });
    connect(area_selection_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::AreaSelect);
        else if (active_tool_ == Tool::AreaSelect) setActiveTool(Tool::None);
    });
    connect(eyedropper_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Eyedropper);
        else if (active_tool_ == Tool::Eyedropper) setActiveTool(Tool::None);
    });
    connect(text_button_, &QToolButton::toggled, this, [this](bool active) {
        if (active) setActiveTool(Tool::Text);
        else if (active_tool_ == Tool::Text) setActiveTool(Tool::None);
    });
    connect(color_button_, &QToolButton::clicked, this, [this]() {
        QColor initial_color = brush_color_;
        if (initial_color.alpha() == 0) initial_color.setAlpha(255);
        const QColor selected = QColorDialog::getColor(
            initial_color, this, QStringLiteral("Brush Color"),
            QColorDialog::ShowAlphaChannel);
        setBrushColor(selected);
    });

    updateColorButton();
    updateControls();
}

void ToolSidebar::setDocumentAvailable(bool available) {
    document_available_ = available;
    if (!document_available_ ||
        (!painting_allowed_ &&
         (active_tool_ == Tool::Paint || active_tool_ == Tool::BucketFill ||
          active_tool_ == Tool::Eraser))) {
        setActiveTool(Tool::None);
    }
    updateControls();
}

void ToolSidebar::setPaintingAllowed(bool allowed) {
    painting_allowed_ = allowed;
    if (!painting_allowed_ &&
        (active_tool_ == Tool::Paint || active_tool_ == Tool::BucketFill ||
         active_tool_ == Tool::Eraser)) {
        setActiveTool(Tool::None);
    }
    updateControls();
}

void ToolSidebar::setPaintToolActive(bool active) {
    setActiveTool(active ? Tool::Paint :
        (active_tool_ == Tool::Paint ? Tool::None : active_tool_));
}

void ToolSidebar::setBucketFillToolActive(bool active) {
    setActiveTool(active ? Tool::BucketFill :
        (active_tool_ == Tool::BucketFill ? Tool::None : active_tool_));
}

void ToolSidebar::setEraserToolActive(bool active) {
    setActiveTool(active ? Tool::Eraser :
        (active_tool_ == Tool::Eraser ? Tool::None : active_tool_));
}

void ToolSidebar::setShapesToolActive(bool active) {
    setActiveTool(active ? Tool::Shapes :
        (active_tool_ == Tool::Shapes ? Tool::None : active_tool_));
}

void ToolSidebar::setSelectToolActive(bool active) {
    setActiveTool(active ? Tool::Select :
        (active_tool_ == Tool::Select ? Tool::None : active_tool_));
}

void ToolSidebar::setAreaSelectionToolActive(bool active) {
    setActiveTool(active ? Tool::AreaSelect :
        (active_tool_ == Tool::AreaSelect ? Tool::None : active_tool_));
}

void ToolSidebar::setEyedropperToolActive(bool active) {
    setActiveTool(active ? Tool::Eyedropper :
        (active_tool_ == Tool::Eyedropper ? Tool::None : active_tool_));
}

void ToolSidebar::setTextToolActive(bool active) {
    setActiveTool(active ? Tool::Text :
        (active_tool_ == Tool::Text ? Tool::None : active_tool_));
}

void ToolSidebar::setActiveTool(Tool tool) {
    const bool requires_editable_layer = tool == Tool::Paint ||
        tool == Tool::BucketFill || tool == Tool::Eraser;
    if (!document_available_ || (requires_editable_layer && !painting_allowed_)) {
        tool = Tool::None;
    }
    const bool changed = active_tool_ != tool;
    active_tool_ = tool;
    {
        const QSignalBlocker paint_blocker(paint_button_);
        const QSignalBlocker bucket_fill_blocker(bucket_fill_button_);
        const QSignalBlocker eraser_blocker(eraser_button_);
        const QSignalBlocker shapes_blocker(shapes_button_);
        const QSignalBlocker text_blocker(text_button_);
        const QSignalBlocker select_shapes_blocker(select_shapes_button_);
        const QSignalBlocker area_selection_blocker(area_selection_button_);
        const QSignalBlocker eyedropper_blocker(eyedropper_button_);
        paint_button_->setChecked(tool == Tool::Paint);
        bucket_fill_button_->setChecked(tool == Tool::BucketFill);
        eraser_button_->setChecked(tool == Tool::Eraser);
        shapes_button_->setChecked(tool == Tool::Shapes);
        text_button_->setChecked(tool == Tool::Text);
        select_shapes_button_->setChecked(tool == Tool::Select);
        area_selection_button_->setChecked(tool == Tool::AreaSelect);
        eyedropper_button_->setChecked(tool == Tool::Eyedropper);
    }
    updateControls();
    if (changed) emit activeToolChanged(active_tool_);
}

bool ToolSidebar::paintToolActive() const noexcept {
    return active_tool_ == Tool::Paint;
}

bool ToolSidebar::bucketFillToolActive() const noexcept {
    return active_tool_ == Tool::BucketFill;
}

bool ToolSidebar::eraserToolActive() const noexcept {
    return active_tool_ == Tool::Eraser;
}

bool ToolSidebar::shapesToolActive() const noexcept {
    return active_tool_ == Tool::Shapes;
}

bool ToolSidebar::textToolActive() const noexcept {
    return active_tool_ == Tool::Text;
}

bool ToolSidebar::selectToolActive() const noexcept {
    return active_tool_ == Tool::Select;
}

bool ToolSidebar::areaSelectionToolActive() const noexcept {
    return active_tool_ == Tool::AreaSelect;
}

bool ToolSidebar::eyedropperToolActive() const noexcept {
    return active_tool_ == Tool::Eyedropper;
}

QColor ToolSidebar::brushColor() const {
    return brush_color_;
}

void ToolSidebar::setBrushColor(const QColor& color) {
    if (!color.isValid() || color == brush_color_) return;
    brush_color_ = color;
    updateColorButton();
    emit brushColorChanged(brush_color_);
}

void ToolSidebar::updateControls() {
    const bool editable_layer_available = document_available_ && painting_allowed_;
    paint_button_->setEnabled(editable_layer_available);
    bucket_fill_button_->setEnabled(editable_layer_available);
    eraser_button_->setEnabled(editable_layer_available);
    shapes_button_->setEnabled(document_available_);
    text_button_->setEnabled(document_available_);
    select_shapes_button_->setEnabled(document_available_);
    area_selection_button_->setEnabled(document_available_);
    eyedropper_button_->setEnabled(document_available_);
    paint_button_->setToolTip(editable_layer_available
        ? QStringLiteral("Paint")
        : (document_available_
            ? QStringLiteral("Select or create an editable layer to paint")
            : QStringLiteral("Open an image to paint")));
    bucket_fill_button_->setToolTip(editable_layer_available
        ? QStringLiteral("Bucket Fill")
        : (document_available_
            ? QStringLiteral("Select or create an editable layer to fill")
            : QStringLiteral("Open an image to use Bucket Fill")));
    eraser_button_->setToolTip(editable_layer_available
        ? QStringLiteral("Eraser")
        : (document_available_
            ? QStringLiteral("Select or create an editable layer to erase")
            : QStringLiteral("Open an image to erase")));
    select_shapes_button_->setToolTip(document_available_
        ? QStringLiteral("Selection")
        : QStringLiteral("Open an image to use Selection"));
    area_selection_button_->setToolTip(document_available_
        ? QStringLiteral("Area Selection")
        : QStringLiteral("Open an image to use Area Selection"));
    shapes_button_->setToolTip(document_available_
        ? QStringLiteral("Shapes")
        : QStringLiteral("Open an image to use Shapes"));
    text_button_->setToolTip(document_available_
        ? QStringLiteral("Text")
        : QStringLiteral("Open an image to add text"));
    eyedropper_button_->setToolTip(document_available_
        ? QStringLiteral("Eyedropper")
        : QStringLiteral("Open an image to sample a color"));
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
