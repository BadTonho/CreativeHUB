#pragma once

#include <QColor>
#include <QWidget>

class QToolButton;

namespace image_editor {

class ToolSidebar final : public QWidget {
    Q_OBJECT

public:
    explicit ToolSidebar(QWidget* parent = nullptr);

    void setDocumentAvailable(bool available);
    void setPaintingAllowed(bool allowed);
    void setPaintToolActive(bool active);

    [[nodiscard]] bool paintToolActive() const noexcept;
    [[nodiscard]] QColor brushColor() const;

signals:
    void paintToolToggled(bool active);
    void brushColorChanged(const QColor& color);

private:
    void updateControls();
    void updateColorButton();

    QToolButton* paint_button_ = nullptr;
    QToolButton* color_button_ = nullptr;
    QColor brush_color_ = Qt::black;
    bool document_available_ = false;
    bool painting_allowed_ = false;
};

} // namespace image_editor
