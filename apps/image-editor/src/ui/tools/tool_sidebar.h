#pragma once

#include <QColor>
#include <QWidget>

class QPushButton;
class QSpinBox;
class QToolButton;

namespace image_editor {

class ToolSidebar final : public QWidget {
    Q_OBJECT

public:
    explicit ToolSidebar(QWidget* parent = nullptr);

    void setDocumentAvailable(bool available);
    void setPaintToolActive(bool active);

    [[nodiscard]] bool paintToolActive() const noexcept;
    [[nodiscard]] QColor brushColor() const;
    [[nodiscard]] int brushDiameter() const;

signals:
    void paintToolToggled(bool active);
    void brushColorChanged(const QColor& color);
    void brushDiameterChanged(int diameter);

private:
    void updateControls();
    void updateColorButton();

    QToolButton* paint_button_ = nullptr;
    QWidget* brush_options_ = nullptr;
    QPushButton* color_button_ = nullptr;
    QSpinBox* diameter_spin_ = nullptr;
    QColor brush_color_ = Qt::black;
    bool document_available_ = false;
};

} // namespace image_editor
