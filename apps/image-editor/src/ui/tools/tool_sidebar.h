#pragma once

#include <QColor>
#include <QWidget>

class QToolButton;

namespace image_editor {

class ToolSidebar final : public QWidget {
    Q_OBJECT

public:
    enum class Tool { None, Paint, Eraser, Shapes, Text, Select, AreaSelect };
    Q_ENUM(Tool)

    explicit ToolSidebar(QWidget* parent = nullptr);

    void setDocumentAvailable(bool available);
    void setPaintingAllowed(bool allowed);
    void setPaintToolActive(bool active);
    void setEraserToolActive(bool active);
    void setShapesToolActive(bool active);
    void setTextToolActive(bool active);
    void setSelectToolActive(bool active);
    void setAreaSelectionToolActive(bool active);
    void setActiveTool(Tool tool);

    [[nodiscard]] bool paintToolActive() const noexcept;
    [[nodiscard]] bool eraserToolActive() const noexcept;
    [[nodiscard]] bool shapesToolActive() const noexcept;
    [[nodiscard]] bool textToolActive() const noexcept;
    [[nodiscard]] bool selectToolActive() const noexcept;
    [[nodiscard]] bool areaSelectionToolActive() const noexcept;
    [[nodiscard]] Tool activeTool() const noexcept { return active_tool_; }
    [[nodiscard]] QColor brushColor() const;

signals:
    void activeToolChanged(image_editor::ToolSidebar::Tool tool);
    void brushColorChanged(const QColor& color);
    void shapesPaletteRequested();

private:
    void updateControls();
    void updateColorButton();

    QToolButton* paint_button_ = nullptr;
    QToolButton* eraser_button_ = nullptr;
    QToolButton* shapes_button_ = nullptr;
    QToolButton* text_button_ = nullptr;
    QToolButton* select_shapes_button_ = nullptr;
    QToolButton* area_selection_button_ = nullptr;
    QToolButton* color_button_ = nullptr;
    QColor brush_color_ = Qt::black;
    bool document_available_ = false;
    bool painting_allowed_ = false;
    Tool active_tool_ = Tool::None;
};

} // namespace image_editor
