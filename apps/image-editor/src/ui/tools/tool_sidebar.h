#pragma once

#include <QColor>
#include <QWidget>

class QToolButton;

namespace image_editor {

class ToolSidebar final : public QWidget {
    Q_OBJECT

public:
    enum class Tool { None, Paint, Eraser };
    Q_ENUM(Tool)

    explicit ToolSidebar(QWidget* parent = nullptr);

    void setDocumentAvailable(bool available);
    void setPaintingAllowed(bool allowed);
    void setPaintToolActive(bool active);
    void setEraserToolActive(bool active);
    void setActiveTool(Tool tool);

    [[nodiscard]] bool paintToolActive() const noexcept;
    [[nodiscard]] bool eraserToolActive() const noexcept;
    [[nodiscard]] Tool activeTool() const noexcept { return active_tool_; }
    [[nodiscard]] QColor brushColor() const;

signals:
    void activeToolChanged(image_editor::ToolSidebar::Tool tool);
    void brushColorChanged(const QColor& color);

private:
    void updateControls();
    void updateColorButton();

    QToolButton* paint_button_ = nullptr;
    QToolButton* eraser_button_ = nullptr;
    QToolButton* color_button_ = nullptr;
    QColor brush_color_ = Qt::black;
    bool document_available_ = false;
    bool painting_allowed_ = false;
    Tool active_tool_ = Tool::None;
};

} // namespace image_editor
