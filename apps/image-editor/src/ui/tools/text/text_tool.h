#pragma once

#include "image_document_store.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>

#include <optional>

class QEvent;
class QMouseEvent;
class QPainter;
class QPlainTextEdit;
class QWidget;

namespace image_editor {

struct TextToolContext {
    QSize image_size;
    QRectF image_target;
    qreal zoom = 1.0;
};

class TextTool final : public QObject {
    Q_OBJECT

public:
    explicit TextTool(QWidget* canvas_host);
    ~TextTool() override;

    [[nodiscard]] QPlainTextEdit* editor() const noexcept { return editor_; }
    [[nodiscard]] bool editing() const noexcept;
    [[nodiscard]] bool frameGestureActive() const noexcept { return creating_frame_; }

    void setStyle(const ImageTextData& style);
    void setViewContext(const TextToolContext& context);
    void beginFrame(const QPointF& image_position) noexcept;
    void updateFrame(const QPointF& image_position) noexcept;
    [[nodiscard]] std::optional<ImageTextData> finishFrame(
        const QPointF& image_position, const TextToolContext& context);
    [[nodiscard]] bool cancelFrame() noexcept;
    void paintFramePreview(QPainter& painter,
                           const TextToolContext& context) const;

    void beginEditing(const ImageTextData& text, bool existing,
                      const TextToolContext& context);
    void finishEditing(bool commit);

    static void paintText(QPainter& painter, const ImageTextData& text,
                          const TextToolContext& context, int opacity = 100);

signals:
    void textCommitted(const image_editor::ImageTextData& text, bool existing);
    void textEditingStarted(const image_editor::ImageTextData& text, bool existing);
    void textEditingCancelled();
    void repaintRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyEditorStyle();
    void updateEditorGeometry();
    void updateContentAndGeometry();

    QWidget* canvas_host_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    TextToolContext context_;
    ImageTextData style_;
    ImageTextData editing_text_;
    bool editing_existing_ = false;
    bool geometry_update_pending_ = false;
    qreal editing_initial_box_width_ = 1.0;
    bool creating_frame_ = false;
    QPointF frame_start_;
    QPointF frame_current_;
};

} // namespace image_editor
