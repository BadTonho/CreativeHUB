#include "shape_palette.h"

#include <QButtonGroup>
#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPixmap>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace image_editor {

namespace {

QIcon shapePaletteIcon(ImageShapeKind kind) {
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (kind == ImageShapeKind::Line) {
        painter.setPen(QPen(QColor(238, 196, 88), 3.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(5.0, 26.0), QPointF(27.0, 5.0));
    } else if (kind == ImageShapeKind::Rectangle) {
        painter.setPen(QPen(QColor(31, 38, 48), 1.6));
        painter.setBrush(QColor(80, 155, 210, 120));
        painter.drawRect(QRectF(5.0, 6.0, 22.0, 20.0));
    } else {
        painter.setPen(QPen(QColor(31, 38, 48), 1.6));
        painter.setBrush(QColor(225, 125, 170, 120));
        painter.drawEllipse(QRectF(5.0, 6.0, 22.0, 20.0));
    }
    return QIcon(icon);
}

} // namespace

ShapePalette::ShapePalette(QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                         Qt::WindowCloseButtonHint) {
    setObjectName(QStringLiteral("shapePaletteWindow"));
    setWindowTitle(QStringLiteral("Shapes"));
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    button_group_ = new QButtonGroup(this);
    button_group_->setObjectName(QStringLiteral("shapePaletteButtonGroup"));
    button_group_->setExclusive(true);

    const auto addShapeButton = [this, layout](ImageShapeKind kind,
                                               const QString& text,
                                               const QString& object_name) {
        auto* button = new QToolButton(this);
        button->setObjectName(object_name);
        button->setAccessibleName(text + QStringLiteral(" shape"));
        button->setIcon(shapePaletteIcon(kind));
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        const QString article = kind == ImageShapeKind::Ellipse
            ? QStringLiteral("an") : QStringLiteral("a");
        button->setToolTip(QStringLiteral("Draw %1 %2").arg(article, text.toLower()));
        button->setCheckable(true);
        button->setFixedSize(36, 36);
        button_group_->addButton(button, static_cast<int>(kind));
        buttons_.append(button);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, kind]() { emit shapeKindSelected(static_cast<int>(kind)); });
    };
    addShapeButton(ImageShapeKind::Line, QStringLiteral("Line"),
                   QStringLiteral("shapePaletteLineButton"));
    addShapeButton(ImageShapeKind::Rectangle, QStringLiteral("Rectangle"),
                   QStringLiteral("shapePaletteRectangleButton"));
    addShapeButton(ImageShapeKind::Ellipse, QStringLiteral("Ellipse"),
                   QStringLiteral("shapePaletteEllipseButton"));

    adjustSize();
}

void ShapePalette::setDocumentAvailable(bool available) {
    for (auto* button : buttons_) {
        if (button != nullptr) button->setEnabled(available);
    }
}

void ShapePalette::setSelectedKind(ImageShapeKind kind) {
    if (auto* selected = button_group_->button(static_cast<int>(kind)); selected != nullptr) {
        selected->setChecked(true);
    }
}

void ShapePalette::showNear(QWidget* anchor) {
    if (!positioned_ && anchor != nullptr) {
        adjustSize();
        const QPoint button_origin = anchor->mapToGlobal(QPoint(0, 0));
        QPoint position = anchor->mapToGlobal(QPoint(anchor->width() + 6, 0));
        QScreen* screen = QGuiApplication::screenAt(position);
        if (screen == nullptr) screen = QGuiApplication::primaryScreen();
        if (screen != nullptr) {
            const QRect available = screen->availableGeometry();
            if (position.x() + width() > available.right() + 1) {
                position.setX(button_origin.x() - width() - 6);
            }
            const int max_x = std::max(available.left(), available.right() - width() + 1);
            const int max_y = std::max(available.top(), available.bottom() - height() + 1);
            position.setX(std::clamp(position.x(), available.left(), max_x));
            position.setY(std::clamp(position.y(), available.top(), max_y));
        }
        move(position);
        positioned_ = true;
    }
    show();
    raise();
}

} // namespace image_editor
