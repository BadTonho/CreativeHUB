#include "ui/main_window.h"
#include "ui/composition_viewer.h"
#include "ui/new_composition_dialog.h"
#include "ui/timeline_navigator.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

using motion::ui::MainWindow;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Widget>
Widget* findWidget(QObject* parent, const char* object_name)
{
    auto* widget = parent->findChild<Widget*>(QString::fromLatin1(object_name));
    require(widget != nullptr, object_name);
    return widget;
}

void completeCompositionDialog(
    int width,
    int height,
    int frame_rate_index)
{
    QWidget* modal = QApplication::activeModalWidget();
    require(modal != nullptr, "composition dialog is active");
    auto* width_edit = findWidget<QLineEdit>(modal, "motion-canvas-width");
    auto* height_edit = findWidget<QLineEdit>(modal, "motion-canvas-height");
    auto* frame_rate = findWidget<QComboBox>(modal, "motion-frame-rate");
    auto* buttons = findWidget<QDialogButtonBox>(modal, "motion-new-composition-buttons");
    width_edit->setText(QString::number(width));
    height_edit->setText(QString::number(height));
    frame_rate->setCurrentIndex(frame_rate_index);
    require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "valid canvas dimensions and frame rate enable Create");
    buttons->button(QDialogButtonBox::Ok)->click();
}

QAction* action(MainWindow& window, const char* object_name)
{
    return findWidget<QAction>(&window, object_name);
}

void sendMouseEvent(
    QWidget* widget,
    QEvent::Type type,
    int x,
    Qt::MouseButton button,
    Qt::MouseButtons buttons)
{
    const QPointF local_position(x, widget->height() / 2.0);
    QMouseEvent event(
        type,
        local_position,
        QPointF(widget->mapToGlobal(local_position.toPoint())),
        button,
        buttons,
        Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

void clickEditingFinished(QLineEdit* field, const char* value)
{
    field->setText(QString::fromLatin1(value));
    require(QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection),
        "transform edit completion signal is available");
}

const motion::model::CompositionLayer* findLayer(
    const motion::model::CompositionDocument* document,
    motion::model::LayerId id)
{
    if (document == nullptr) {
        return nullptr;
    }
    for (const auto& layer : document->layers()) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    motion::ui::TimelineNavigator frame_rate_range_check;
    frame_rate_range_check.setCompositionTiming({24, 1});
    require(frame_rate_range_check.visibleEndFrame() == 24 * 60 * 60 - 1,
        "24 fps starts with a one-hour range");
    frame_rate_range_check.setCompositionTiming({60, 1});
    require(frame_rate_range_check.visibleEndFrame() == 60 * 60 * 60 - 1,
        "60 fps starts with a one-hour range");
    frame_rate_range_check.setCompositionTiming({30000, 1001});
    const std::int64_t fractional_hour_frames = (30000 * 60 * 60 + 1001 - 1) / 1001;
    require(frame_rate_range_check.visibleEndFrame() == fractional_hour_frames - 1,
        "fractional frame rates use their exact rational value for one-hour ranges");

    motion::ui::CompositionViewer viewer;
    viewer.resize(400, 300);
    viewer.setComposition({100, 50}, QPointF(0.5, 0.5));
    viewer.show();
    application.processEvents();
    const QImage viewer_image = viewer.grab().toImage();
    require(viewer_image.pixelColor(200, 159) == QColor(255, 183, 54),
        "viewer paints the selected layer anchor on the fitted canvas");
    viewer.setComposition({100, 50}, std::nullopt);
    application.processEvents();
    require(viewer.grab().toImage().pixelColor(200, 159) == QColor(224, 226, 230),
        "viewer removes the selection guide when no layer is selected");

    motion::ui::NewCompositionDialog dialog;
    auto* dialog_width = findWidget<QLineEdit>(&dialog, "motion-canvas-width");
    auto* dialog_height = findWidget<QLineEdit>(&dialog, "motion-canvas-height");
    auto* dialog_frame_rate = findWidget<QComboBox>(&dialog, "motion-frame-rate");
    auto* dialog_buttons = findWidget<QDialogButtonBox>(&dialog, "motion-new-composition-buttons");
    require(dialog_width->text().isEmpty() && dialog_height->text().isEmpty()
            && dialog_frame_rate->currentIndex() == 0
            && dialog.findChild<QObject*>(QStringLiteral("motion-duration-frames")) == nullptr,
        "new composition has blank dimensions and frame rate, with no duration field");
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "Create is disabled until all composition settings are entered");
    dialog_width->setText(QStringLiteral("0"));
    dialog_height->setText(QStringLiteral("720"));
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "non-positive canvas dimensions do not enable Create");
    dialog_width->setText(QStringLiteral("1280"));
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "canvas dimensions alone do not require a fixed composition duration");
    dialog_frame_rate->setCurrentIndex(2);
    require(dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "explicit canvas dimensions and frame rate are accepted without a duration");
    dialog_buttons->button(QDialogButtonBox::Ok)->click();
    require(dialog.canvasSize() == motion::model::CanvasSize{1280, 720},
        "dialog returns the explicitly entered canvas size");
    require(dialog.compositionSettings() == motion::model::CompositionSettings{
                {1280, 720}, {24, 1}},
        "dialog returns the explicit canvas and exact frame rate");

    motion::ui::MainWindow window;
    require(window.compositionDocument() == nullptr,
        "Motion Studio starts without creating a composition");
    window.show();
    application.processEvents();
    QPointer<QPushButton> empty_state_button = findWidget<QPushButton>(
        &window, "motion-empty-new-composition-button");
    require(empty_state_button->text() == QStringLiteral("New Composition...")
            && empty_state_button->isVisible(),
        "empty state displays the centered New Composition button");
    QTimer::singleShot(0, [] { completeCompositionDialog(640, 360, 2); });
    empty_state_button->click();
    require(window.compositionDocument() != nullptr
            && window.compositionDocument()->canvasSize() == motion::model::CanvasSize{640, 360},
        "the empty-state button opens the existing dialog and creates a composition");
    require(window.compositionDocument()->frameRate() == motion::model::FrameRate{24, 1},
        "new composition stores its explicit frame rate without requiring an end frame");
    application.processEvents();
    require(window.centralWidget()->objectName() == QStringLiteral("motion-composition-splitter")
            && window.isMaximized()
            && (empty_state_button.isNull() || !empty_state_button->isVisible()),
        "creating a composition replaces the empty state and preserves maximization");

    action(window, "motion-add-shape-layer-action")->trigger();
    action(window, "motion-add-text-layer-action")->trigger();
    const auto* document = window.compositionDocument();
    require(document != nullptr && document->layers().size() == 2,
        "layer actions add model layers");
    const auto shape_id = document->layers()[0].id;
    const auto text_id = document->layers()[1].id;
    require(document->layers()[0].kind == motion::model::LayerKind::Shape
            && document->layers()[1].kind == motion::model::LayerKind::Text,
        "selected layer kinds are recorded in the model");
    require(window.selectedLayerId() == text_id,
        "the added layer becomes selected");

    auto* layer_list = findWidget<QListWidget>(&window, "motion-layer-list");
    require(layer_list->count() == 2
            && layer_list->item(0)->data(Qt::UserRole).toULongLong() == text_id
            && layer_list->item(1)->data(Qt::UserRole).toULongLong() == shape_id,
        "the visible stack lists frontmost layers first");

    layer_list->item(0)->setCheckState(Qt::Unchecked);
    require(!findLayer(document, text_id)->visible,
        "layer visibility control updates the document");
    layer_list->item(0)->setCheckState(Qt::Checked);
    require(findLayer(document, text_id)->visible,
        "layer visibility can be restored");

    auto* position_x = findWidget<QLineEdit>(&window, "motion-transform-position-x");
    auto* scale = findWidget<QLineEdit>(&window, "motion-transform-scale");
    auto* rotation = findWidget<QLineEdit>(&window, "motion-transform-rotation");
    auto* opacity = findWidget<QLineEdit>(&window, "motion-transform-opacity");
    clickEditingFinished(position_x, "1.25");
    clickEditingFinished(scale, "2.5");
    clickEditingFinished(rotation, "-30");
    clickEditingFinished(opacity, "0.5");
    auto* text_layer = findLayer(window.compositionDocument(), text_id);
    require(text_layer != nullptr
            && text_layer->transform.position_x == 1.25
            && text_layer->transform.scale == 2.5
            && text_layer->transform.rotation_degrees == -30.0
            && text_layer->transform.opacity == 0.5,
        "transform inspector writes the selected layer base transform");

    clickEditingFinished(scale, "0");
    clickEditingFinished(opacity, "1.5");
    text_layer = findLayer(window.compositionDocument(), text_id);
    require(text_layer->transform.scale == 2.5 && text_layer->transform.opacity == 0.5
            && scale->text() == QStringLiteral("2.5")
            && opacity->text() == QStringLiteral("0.5"),
        "invalid transforms are rejected and the inspector restores the stored values");

    auto* mutable_document = const_cast<motion::model::CompositionDocument*>(
        window.compositionDocument());
    require(mutable_document->setLayerKeyframe(
                text_id,
                creative_suite::animation::TransformProperty::PositionX,
                12,
                1.75),
        "test layer has an existing keyframe before timeline seeking");
    const auto transform_before_seeking = text_layer->transform;
    const auto keyframes_before_seeking = text_layer->keyframes;

    auto* timeline = findWidget<motion::ui::TimelineNavigator>(&window, "motion-timeline");
    auto* previous_frame = findWidget<QPushButton>(timeline, "motion-timeline-previous-frame");
    auto* next_frame = findWidget<QPushButton>(timeline, "motion-timeline-next-frame");
    auto* ruler = findWidget<QWidget>(timeline, "motion-timeline-ruler");
    constexpr std::int64_t frames_per_hour_at_24_fps = 24 * 60 * 60;
    constexpr std::int64_t first_hour_end_frame = frames_per_hour_at_24_fps - 1;
    require(timeline->currentFrame() == 0 && timeline->visibleEndFrame() == first_hour_end_frame
            && !previous_frame->isEnabled() && next_frame->isEnabled(),
        "timeline starts at frame zero with a one-hour navigation range");

    next_frame->click();
    next_frame->click();
    previous_frame->click();
    require(timeline->currentFrame() == 1,
        "frame-step controls navigate one frame at a time");

    sendMouseEvent(
        ruler, QEvent::MouseButtonPress, ruler->width() / 2,
        Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseButtonRelease, ruler->width() / 2,
        Qt::LeftButton, Qt::NoButton);
    require(timeline->currentFrame() == frames_per_hour_at_24_fps / 2,
        "clicking the ruler seeks within the current one-hour range");

    sendMouseEvent(
        ruler, QEvent::MouseButtonPress, ruler->width() - 1,
        Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseButtonRelease, ruler->width() - 1,
        Qt::LeftButton, Qt::NoButton);
    require(timeline->currentFrame() == first_hour_end_frame
            && timeline->visibleEndFrame() == first_hour_end_frame,
        "seeking to the last pixel clamps at one hour without extending the range");

    sendMouseEvent(
        ruler, QEvent::MouseButtonPress, ruler->width() - 1,
        Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseMove, ruler->width() + 10, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseMove, ruler->width() + 100, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseMove, ruler->width() + 500, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseButtonRelease, ruler->width() + 500,
        Qt::LeftButton, Qt::NoButton);
    constexpr std::int64_t second_hour_end_frame = frames_per_hour_at_24_fps * 2 - 1;
    require(timeline->currentFrame() == second_hour_end_frame
            && timeline->visibleEndFrame() == second_hour_end_frame
            && !next_frame->isEnabled() && previous_frame->isEnabled(),
        "one drag beyond the ruler adds exactly one hour and lands at the new end");

    next_frame->click();
    require(timeline->currentFrame() == second_hour_end_frame,
        "frame stepping cannot pass the current range end");
    previous_frame->click();
    next_frame->click();
    require(timeline->currentFrame() == second_hour_end_frame,
        "frame stepping works up to the extended range boundary");

    sendMouseEvent(
        ruler, QEvent::MouseButtonPress, ruler->width() - 1,
        Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseMove, ruler->width() + 10, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(
        ruler, QEvent::MouseButtonRelease, ruler->width() + 10,
        Qt::LeftButton, Qt::NoButton);
    constexpr std::int64_t third_hour_end_frame = frames_per_hour_at_24_fps * 3 - 1;
    require(timeline->currentFrame() == third_hour_end_frame
            && timeline->visibleEndFrame() == third_hour_end_frame,
        "a separate drag extends the navigation range by one more hour");

    timeline->setCurrentFrame(third_hour_end_frame + 10);
    require(timeline->currentFrame() == third_hour_end_frame,
        "programmatic seeking clamps to the current navigation range");

    sendMouseEvent(ruler, QEvent::MouseButtonPress, 0, Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(ruler, QEvent::MouseButtonRelease, 0, Qt::LeftButton, Qt::NoButton);
    previous_frame->click();
    require(timeline->currentFrame() == 0,
        "ruler seeking and frame stepping cannot move before frame zero");

    text_layer = findLayer(window.compositionDocument(), text_id);
    require(text_layer != nullptr
            && text_layer->transform == transform_before_seeking
            && text_layer->keyframes == keyframes_before_seeking,
        "timeline navigation leaves base transforms and keyframes untouched");

    auto* move_back = findWidget<QPushButton>(&window, "motion-layer-move-back-button");
    move_back->click();
    require(window.compositionDocument()->layers()[0].id == text_id
            && window.compositionDocument()->layers()[1].id == shape_id
            && layer_list->item(0)->data(Qt::UserRole).toULongLong() == shape_id
            && window.selectedLayerId() == text_id,
        "reordering maps the front-to-back UI stack to model order and preserves selection");
    findWidget<QPushButton>(&window, "motion-layer-move-front-button")->click();
    require(window.compositionDocument()->layers()[0].id == shape_id
            && window.compositionDocument()->layers()[1].id == text_id
            && layer_list->item(0)->data(Qt::UserRole).toULongLong() == text_id,
        "a layer can be moved back to the front of the stack");
    findWidget<QPushButton>(&window, "motion-remove-layer-button")->click();
    require(window.compositionDocument()->layers().size() == 1
            && window.selectedLayerId() == shape_id,
        "removing a layer selects the frontmost remaining layer");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "replacement confirmation is shown");
        prompt->button(QMessageBox::No)->click();
    });
    timeline->setCurrentFrame(7);
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{640, 360}
            && window.compositionDocument()->layers().size() == 1
            && timeline->currentFrame() == 7,
        "declining replacement preserves the current composition and playhead");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "replacement confirmation is shown before a new composition");
        prompt->button(QMessageBox::Yes)->click();
        QTimer::singleShot(0, [] { completeCompositionDialog(1920, 1080, 4); });
    });
    timeline->setCurrentFrame(50);
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{1920, 1080}
            && window.compositionDocument()->frameRate() == motion::model::FrameRate{30000, 1001}
            && window.compositionDocument()->layers().empty()
            && !window.selectedLayerId().has_value(),
        "confirmed replacement creates a clean composition without requiring an end frame");
    require(timeline->currentFrame() == 0 && timeline->visibleEndFrame() == 107892
            && !previous_frame->isEnabled() && next_frame->isEnabled(),
        "a new composition resets navigation to one hour at its selected fractional rate");

    action(window, "motion-add-video-layer-action")->trigger();
    require(window.compositionDocument()->layers().size() == 1
            && window.compositionDocument()->layers().front().kind == motion::model::LayerKind::Video,
        "the video layer action creates the requested kind");

    motion::ui::MainWindow restored_window;
    restored_window.show();
    application.processEvents();
    restored_window.showNormal();
    application.processEvents();
    require(!restored_window.isMaximized(),
        "the test can restore Motion Studio before creating a composition");
    const auto restored_window_geometry = restored_window.geometry();
    QTimer::singleShot(0, [] { completeCompositionDialog(800, 600, 2); });
    action(restored_window, "motion-new-composition-action")->trigger();
    application.processEvents();
    require(restored_window.compositionDocument() != nullptr,
        "composition is created in the manually restored window");
    require(!restored_window.isMaximized(),
        "creating a composition preserves the manually restored window state");
    require(restored_window.geometry() == restored_window_geometry,
        "creating a composition preserves the manually restored window geometry");
    restored_window.close();

    std::cout << "Motion Studio editing UI tests passed.\n";
    return EXIT_SUCCESS;
}
