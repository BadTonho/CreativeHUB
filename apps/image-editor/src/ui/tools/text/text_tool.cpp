#include "text_tool.h"

#include "image_document_store.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QFrame>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace image_editor {

TextTool::TextTool(QWidget* canvas_host)
    : QObject(nullptr), canvas_host_(canvas_host), editor_(new QPlainTextEdit(canvas_host)) {
    editor_->setObjectName(QStringLiteral("imageCanvasTextEditor"));
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setContentsMargins(0, 0, 0, 0);
    editor_->document()->setDocumentMargin(1.0);
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->installEventFilter(this);
    editor_->hide();
    connect(editor_, &QPlainTextEdit::textChanged, this,
            &TextTool::updateContentAndGeometry);
}

TextTool::~TextTool() {
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }
    if (editor_ != nullptr) editor_->removeEventFilter(this);
}

bool TextTool::editing() const noexcept {
    return editor_ != nullptr && editor_->isVisible();
}

void TextTool::setStyle(const ImageTextData& style) {
    style_ = style;
    if (editing()) {
        const QString content = editing_text_.content;
        const QString id = editing_text_.id;
        const QPointF position = editing_text_.position;
        const qreal box_width = editing_text_.box_width;
        editing_text_ = style;
        editing_text_.id = id;
        editing_text_.content = content;
        editing_text_.position = position;
        editing_text_.box_width = box_width;
        applyEditorStyle();
        updateContentAndGeometry();
    }
    emit repaintRequested();
}

void TextTool::setViewContext(const TextToolContext& context) {
    context_ = context;
    updateEditorGeometry();
}

void TextTool::beginFrame(const QPointF& image_position) noexcept {
    frame_start_ = image_position;
    frame_current_ = image_position;
    creating_frame_ = true;
    emit repaintRequested();
}

void TextTool::updateFrame(const QPointF& image_position) noexcept {
    if (!creating_frame_) return;
    frame_current_ = image_position;
    emit repaintRequested();
}

std::optional<ImageTextData> TextTool::finishFrame(
    const QPointF& image_position, const TextToolContext& context) {
    if (!creating_frame_) return std::nullopt;
    frame_current_ = image_position;
    creating_frame_ = false;

    const qreal dragged_width = std::abs(frame_current_.x() - frame_start_.x());
    const bool dragged_to_set_width = dragged_width >= 4.0;
    const qreal left = dragged_to_set_width
        ? std::min(frame_start_.x(), frame_current_.x()) : frame_start_.x();
    const qreal available_width = std::max<qreal>(1.0, context.image_size.width() - left);
    const qreal initial_width = dragged_to_set_width ? dragged_width : style_.box_width;

    ImageTextData text = style_;
    text.id.clear();
    text.content.clear();
    text.position = QPointF(left, frame_start_.y());
    text.box_width = std::clamp(initial_width, 1.0, available_width);
    emit repaintRequested();
    return text;
}

bool TextTool::cancelFrame() noexcept {
    if (!creating_frame_) return false;
    creating_frame_ = false;
    emit repaintRequested();
    return true;
}

void TextTool::paintFramePreview(QPainter& painter,
                                const TextToolContext& context) const {
    if (!creating_frame_) return;
    const qreal zoom = context.zoom;
    const QRectF frame(context.image_target.left() +
                           std::min(frame_start_.x(), frame_current_.x()) * zoom,
                       context.image_target.top() + frame_start_.y() * zoom,
                       std::abs(frame_current_.x() - frame_start_.x()) * zoom,
                       1.0);
    painter.save();
    painter.setClipRect(context.image_target);
    painter.setPen(QPen(QColor(64, 181, 246), 1.0, Qt::DashLine));
    painter.setBrush(QColor(64, 181, 246, 24));
    painter.drawRect(frame);
    painter.restore();
}

void TextTool::beginEditing(const ImageTextData& text, bool existing,
                            const TextToolContext& context) {
    if (editor_ == nullptr) return;
    if (editing()) finishEditing(true);
    context_ = context;
    editing_text_ = text;
    editing_existing_ = existing;
    editing_initial_box_width_ = std::max<qreal>(1.0, text.box_width);
    const QSignalBlocker blocker(editor_);
    editor_->setPlainText(text.content);
    applyEditorStyle();
    editor_->show();
    editor_->raise();
    updateEditorGeometry();
    if (auto* application = QCoreApplication::instance()) {
        application->installEventFilter(this);
    }
    editor_->setFocus(Qt::OtherFocusReason);
    editor_->moveCursor(existing ? QTextCursor::Start : QTextCursor::End);
    emit textEditingStarted(editing_text_, existing);
    emit repaintRequested();
}

void TextTool::finishEditing(bool commit) {
    if (!editing()) return;
    editing_text_.content = editor_->toPlainText();
    const ImageTextData text = editing_text_;
    const bool existing = editing_existing_;
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }
    editor_->hide();
    editor_->clear();
    editing_text_ = {};
    editing_existing_ = false;
    editing_initial_box_width_ = 1.0;
    if (commit) emit textCommitted(text, existing);
    else emit textEditingCancelled();
    emit repaintRequested();
}

void TextTool::paintText(QPainter& painter, const ImageTextData& text,
                         const TextToolContext& context, int opacity) {
    painter.save();
    painter.setClipRect(context.image_target);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(std::clamp(opacity, 0, 100) / 100.0);
    painter.translate(context.image_target.topLeft());
    painter.scale(context.zoom, context.zoom);
    QFont font(text.font_family);
    font.setPixelSize(text.font_pixel_size);
    painter.setFont(font);
    painter.setPen(text.color);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(text.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (text.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    QTextLayout layout(text.content, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(text.box_width);
        line.setPosition(QPointF(0.0, height));
        height += line.height();
    }
    layout.endLayout();
    layout.draw(&painter, text.position);
    painter.restore();
}

bool TextTool::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride && editing()) {
        QWidget* focus_widget = QApplication::focusWidget();
        const bool editor_has_focus = focus_widget == editor_ ||
            (focus_widget != nullptr && editor_->isAncestorOf(focus_widget));
        auto* key_event = static_cast<QKeyEvent*>(event);
        const auto modifiers = key_event->modifiers();
        const bool altgr = modifiers.testFlag(Qt::GroupSwitchModifier);
        const bool command_modifier =
            modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
        const bool menu_modifier = modifiers.testFlag(Qt::AltModifier) && !altgr;
        // Unmodified key presses belong to the focused text editor even when a
        // platform sends ShortcutOverride without the corresponding text.
        if (editor_has_focus && (!command_modifier || altgr) && !menu_modifier) {
            key_event->accept();
            return false;
        }
    }
    if (watched == editor_ && event->type() == QEvent::KeyPress) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Escape) {
            finishEditing(false);
            return true;
        }
        if ((key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
            key_event->modifiers().testFlag(Qt::ControlModifier)) {
            finishEditing(true);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void TextTool::applyEditorStyle() {
    if (editor_ == nullptr) return;
    QFont font(editing_text_.font_family);
    font.setPixelSize(std::max(1, qRound(editing_text_.font_pixel_size * context_.zoom)));
    editor_->setFont(font);
    editor_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { color: %1; background: rgba(255,255,255,24); "
        "border: 1px solid #299bea; padding: 0px; "
        "selection-background-color: #359bdc; selection-color: #ffffff; }")
        .arg(editing_text_.color.name(QColor::HexArgb)));
    QTextOption option = editor_->document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(editing_text_.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (editing_text_.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    editor_->document()->setDefaultTextOption(option);
}

void TextTool::updateEditorGeometry() {
    if (editor_ == nullptr || !editing() || context_.image_size.isEmpty()) return;
    const QRectF text_bounds = imageTextBounds(editing_text_);
    const qreal minimum_height = editing_text_.font_pixel_size * 1.5;
    const qreal height = editing_text_.content.isEmpty()
        ? minimum_height : std::max(minimum_height, text_bounds.height());
    const int width = std::max(24, qRound(editing_text_.box_width * context_.zoom));
    const int left = qRound(context_.image_target.left() +
                            editing_text_.position.x() * context_.zoom);
    const int top = qRound(context_.image_target.top() +
                           editing_text_.position.y() * context_.zoom);
    QFont font(editing_text_.font_family);
    font.setPixelSize(std::max(1, qRound(editing_text_.font_pixel_size * context_.zoom)));
    const bool keep_focus = editor_->hasFocus();
    const QTextCursor cursor = editor_->textCursor();
    if (editor_->font() != font) editor_->setFont(font);
    // Lay out at the new width before measuring height. The native editor's
    // rounded font size and fixed screen-pixel margins can wrap differently
    // from the canvas renderer, especially below 100% zoom.
    QRect editor_geometry(left, top, width, editor_->height());
    if (editor_->geometry() != editor_geometry) editor_->setGeometry(editor_geometry);
    qreal native_height = 0.0;
    auto* document_layout = editor_->document()->documentLayout();
    for (QTextBlock block = editor_->document()->begin(); block.isValid();
         block = block.next()) {
        native_height += document_layout->blockBoundingRect(block).height();
    }
    const qreal vertical_inset = editor_->height() - editor_->viewport()->height()
        + 2.0 * editor_->document()->documentMargin();
    const int pixel_height = std::max({24, qRound(height * context_.zoom),
        static_cast<int>(std::ceil(native_height + vertical_inset))});
    editor_geometry.setHeight(pixel_height);
    if (editor_->geometry() != editor_geometry) editor_->setGeometry(editor_geometry);
    editor_->verticalScrollBar()->setValue(0);
    editor_->horizontalScrollBar()->setValue(0);
    if (keep_focus) {
        if (!editor_->hasFocus()) editor_->setFocus(Qt::OtherFocusReason);
        if (editor_->textCursor() != cursor) editor_->setTextCursor(cursor);
    }
}

void TextTool::updateContentAndGeometry() {
    if (editor_ == nullptr || !editing() || context_.image_size.isEmpty()) return;

    editing_text_.content = editor_->toPlainText();
    QFont font(editing_text_.font_family);
    font.setPixelSize(std::clamp(editing_text_.font_pixel_size, 1, 1024));
    const QFontMetricsF metrics(font);
    const QFontMetricsF native_metrics(editor_->font(), editor_->viewport());
    qreal content_width = 0.0;
    qreal native_content_width = 0.0;
    const QStringList lines = editing_text_.content.split(QLatin1Char('\n'),
                                                          Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        content_width = std::max(content_width, metrics.horizontalAdvance(line));
        native_content_width = std::max(native_content_width,
            native_metrics.horizontalAdvance(line));
    }

    const qreal available_width = std::max<qreal>(1.0,
        context_.image_size.width() - editing_text_.position.x());
    const qreal minimum_width = std::min(editing_initial_box_width_, available_width);
    constexpr qreal kTextEditorHorizontalInset = 4.0;
    const qreal native_inset = editor_->width() - editor_->viewport()->width()
        + 2.0 * editor_->document()->documentMargin() + editor_->cursorWidth() + 2.0;
    const qreal desired_width = editing_text_.content.isEmpty() ? minimum_width
        : std::max({minimum_width, content_width + kTextEditorHorizontalInset,
            (native_content_width + native_inset) / context_.zoom});
    editing_text_.box_width = std::clamp(desired_width, minimum_width, available_width);

    // Defer geometry changes until the input event finishes to avoid disrupting
    // QPlainTextEdit's active layout and key handling.
    if (!geometry_update_pending_) {
        geometry_update_pending_ = true;
        QMetaObject::invokeMethod(this, [this]() {
            geometry_update_pending_ = false;
            updateEditorGeometry();
            if (editor_ != nullptr && editor_->isVisible()) editor_->viewport()->repaint();
        }, Qt::QueuedConnection);
    }
    emit repaintRequested();
}

} // namespace image_editor
