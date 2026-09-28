#include "ui/main_window.h"
#include "ui/composition_viewer.h"
#include "ui/media_pool_widget.h"
#include "ui/new_composition_dialog.h"
#include "ui/timeline_navigator.h"
#include "ui/timeline_navigator_math.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollBar>
#include <QSlider>
#include <QCoreApplication>
#include <QPointer>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>

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

QAction* action(MainWindow& window, const char* object_name)
{
    return findWidget<QAction>(&window, object_name);
}

void completeCompositionDialog(int width, int height, int frame_rate_index)
{
    auto* modal = QApplication::activeModalWidget();
    require(modal != nullptr, "composition dialog is active");
    auto* width_edit = findWidget<QLineEdit>(modal, "motion-canvas-width");
    auto* height_edit = findWidget<QLineEdit>(modal, "motion-canvas-height");
    auto* frame_rate = findWidget<QComboBox>(modal, "motion-frame-rate");
    auto* buttons = findWidget<QDialogButtonBox>(modal, "motion-new-composition-buttons");
    width_edit->setText(QString::number(width));
    height_edit->setText(QString::number(height));
    frame_rate->setCurrentIndex(frame_rate_index);
    require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),
            "valid canvas settings enable Create");
    buttons->button(QDialogButtonBox::Ok)->click();
}

bool waitFor(const std::function<bool()>& condition, int timeout_ms = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

void requestContextMenu(QListWidget* list, const QPoint& position)
{
    const bool invoked = QMetaObject::invokeMethod(
        list, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, position));
    require(invoked, "the media list exposes its context menu request");
}

void chooseActionOnNextMenu(const QString& text)
{
    QTimer::singleShot(0, [text] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) return;
        std::function<QAction*(QMenu*)> find_action = [&](QMenu* current) -> QAction* {
            for (auto* candidate : current->actions()) {
                if (candidate->text() == text) return candidate;
                if (candidate->menu() != nullptr) {
                    if (auto* found = find_action(candidate->menu()); found != nullptr)
                        return found;
                }
            }
            return nullptr;
        };
        if (auto* candidate = find_action(menu); candidate != nullptr) {
            QPointer<QMenu> menu_guard(menu);
            candidate->trigger();
            if (!menu_guard.isNull()) menu_guard->close();
        }
    });
}

QTreeWidgetItem* findBin(QTreeWidgetItem* parent, const QString& path)
{
    if (parent == nullptr) return nullptr;
    if (parent->data(0, Qt::UserRole + 1).toString() == path) return parent;
    for (int index = 0; index < parent->childCount(); ++index) {
        if (auto* match = findBin(parent->child(index), path); match != nullptr) return match;
    }
    return nullptr;
}

void setInputDialogText(const QString& text)
{
    QTimer::singleShot(0, [text] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "input dialog is active");
        dialog->setTextValue(text);
        dialog->accept();
    });
}

QString pathToQString(const std::filesystem::path& path);

void deliverMediaDrop(QWidget* target, const std::filesystem::path& path,
                      const QPoint& position, bool check_preview)
{
    const auto before = target->grab().toImage();
    QMimeData payload;
    payload.setData(QStringLiteral("application/x-creative-suite-motion-media"),
                    pathToQString(path).toUtf8());
    QDragEnterEvent enter(position, Qt::CopyAction, &payload,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    require(enter.isAccepted(), "timeline accepts dragged Media Pool entries");
    QDragMoveEvent move(position, Qt::CopyAction, &payload,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    require(move.isAccepted(), "timeline tracks a media drag preview");
    if (check_preview) {
        QCoreApplication::processEvents();
        require(target->grab().toImage() != before,
                "timeline draws a drag preview before inserting media");
    }
    QDropEvent drop(QPointF(position), Qt::CopyAction, &payload,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    require(drop.isAccepted(), "timeline accepts the media drop");
}

void sendMouseClick(QWidget* target, const QPoint& position)
{
    const QPointF point(position);
    QMouseEvent press(QEvent::MouseButtonPress, point, point, point, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, point, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

void sendMouseDrag(QWidget* target, const QPoint& from, const QPoint& to)
{
    const QPointF from_point(from);
    const QPointF to_point(to);
    QMouseEvent press(QEvent::MouseButtonPress, from_point, from_point, from_point,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent move(QEvent::MouseMove, to_point, to_point, to_point, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to_point, to_point, to_point,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

void sendControlWheel(QWidget* target, int delta)
{
    const QPointF point(target->rect().center());
    QWheelEvent wheel(point, point, QPoint(0, 0), QPoint(0, delta), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &wheel);
}

void dragPastTimelineEnd(motion::ui::TimelineRuler* ruler, int from_x)
{
    const QPointF start(from_x, 48);
    const QPointF first_outside(ruler->width() + 2, 48);
    const QPointF farther_outside(ruler->width() + 24, 48);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ruler, &press);
    QMouseEvent move_once(QEvent::MouseMove, first_outside, first_outside, first_outside,
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ruler, &move_once);
    QMouseEvent move_again(QEvent::MouseMove, farther_outside, farther_outside,
                           farther_outside, Qt::NoButton, Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(ruler, &move_again);
    QMouseEvent release(QEvent::MouseButtonRelease, farther_outside, farther_outside,
                        farther_outside, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(ruler, &release);
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(first, first + bytes.size()));
}

QString pathToQString(const std::filesystem::path& path)
{
    const auto utf8_path = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8_path.data()),
                             static_cast<qsizetype>(utf8_path.size()));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    motion::ui::TimelineNavigator frame_rate_range_check;
    frame_rate_range_check.resize(1200, 760);
    frame_rate_range_check.show();
    application.processEvents();
    frame_rate_range_check.setCompositionTiming({24, 1});
    require(frame_rate_range_check.visibleEndFrame() == 24 * 60 * 60 - 1,
            "24 fps starts with a one-hour navigation range");
    frame_rate_range_check.setCompositionTiming({60, 1});
    require(frame_rate_range_check.visibleEndFrame() == 60 * 60 * 60 - 1,
            "60 fps starts with a one-hour navigation range");
    frame_rate_range_check.setCompositionTiming({30000, 1001});
    const std::int64_t fractional_hour_frames = (30000 * 60 * 60 + 1001 - 1) / 1001;
    require(frame_rate_range_check.visibleEndFrame() == fractional_hour_frames - 1,
            "fractional rates use exact rational math for the initial range");

    frame_rate_range_check.setCompositionTiming({60, 1});
    const auto one_hour_end = frame_rate_range_check.visibleEndFrame();
    auto* display_mode = findWidget<QComboBox>(
        &frame_rate_range_check, "motion-timeline-display-mode");
    auto* position_readout = findWidget<QLabel>(
        &frame_rate_range_check, "motion-timeline-position-readout");
    auto* zoom_slider = findWidget<QSlider>(
        &frame_rate_range_check, "motion-timeline-zoom-slider");
    auto* zoom_out = findWidget<QPushButton>(
        &frame_rate_range_check, "motion-timeline-zoom-out");
    auto* zoom_in = findWidget<QPushButton>(
        &frame_rate_range_check, "motion-timeline-zoom-in");
    auto* zoom_label = findWidget<QLabel>(
        &frame_rate_range_check, "motion-timeline-zoom-level");
    auto* horizontal_scroll = findWidget<QScrollBar>(
        &frame_rate_range_check, "motion-timeline-horizontal-scroll");
    auto* ruler_for_zoom = findWidget<motion::ui::TimelineRuler>(
        &frame_rate_range_check, "motion-timeline-ruler");
    require(display_mode->count() == 2 && display_mode->itemText(0) == QStringLiteral("Time") &&
                display_mode->itemText(1) == QStringLiteral("Frames") &&
                display_mode->currentIndex() == 0 &&
                position_readout->text() == QStringLiteral("00:00:00.000"),
            "timeline time display is the initial mode and frames remains an available option");
    frame_rate_range_check.setCompositionTiming({24, 1});
    frame_rate_range_check.setCurrentFrame(1);
    require(motion::ui::detail::formatElapsedTime(1, 24, 1) == "00:00:00.042" &&
                motion::ui::detail::formatElapsedTime(24, 24, 1) == "00:00:01.000" &&
                motion::ui::detail::formatElapsedTime(
                    std::numeric_limits<std::int64_t>::max(), 24, 1) ==
                    "106751991167300:38:45.292" &&
                position_readout->text() == QStringLiteral("00:00:00.042"),
            "integer frame rates format elapsed time exactly through the signed frame limit");
    const auto frame_rate_check_frame_before_display_switch = frame_rate_range_check.currentFrame();
    display_mode->setCurrentIndex(1);
    require(position_readout->text() == QStringLiteral("Frame 1") &&
                frame_rate_range_check.currentFrame() ==
                    frame_rate_check_frame_before_display_switch,
            "frame display changes the readout without seeking");
    frame_rate_range_check.setCompositionTiming({30000, 1001});
    frame_rate_range_check.setCurrentFrame(30);
    require(display_mode->currentIndex() == 0 &&
                motion::ui::detail::formatElapsedTime(30, 30000, 1001) == "00:00:01.001" &&
                position_readout->text() == QStringLiteral("00:00:01.001"),
            "fractional frame rates use exact rational timing and new compositions reset to Time");
    display_mode->setCurrentIndex(1);
    require(position_readout->text() == QStringLiteral("Frame 30"),
            "the frame option retains the existing frame-number readout");
    const auto narrow_label_ticks = motion::ui::detail::timelineRulerTickStep(
        60 * 60 * 60, 1000, 80);
    const auto wide_label_ticks = motion::ui::detail::timelineRulerTickStep(
        60 * 60 * 60, 1000, 280);
    require(wide_label_ticks > narrow_label_ticks,
            "ruler ticks spread farther apart when the selected label format is wider");
    frame_rate_range_check.setCompositionTiming({60, 1});

    require(frame_rate_range_check.zoomFactor() == 1.0 &&
                frame_rate_range_check.framesPerView() == 60 * 60 * 60 &&
                zoom_slider->value() == motion::ui::detail::kTimelineZoomDefaultIndex &&
                zoom_label->text() == QStringLiteral("100%"),
            "timeline zoom starts at 100 percent with one hour in view");
    zoom_out->click();
    require(frame_rate_range_check.zoomFactor() == 0.75 &&
                zoom_label->text() == QStringLiteral("75%"),
            "visible zoom buttons update the discrete level and percentage readout");
    zoom_in->click();
    require(frame_rate_range_check.zoomFactor() == 1.0,
            "the visible zoom-in button restores the one-hour view");
    zoom_slider->setValue(0);
    require(frame_rate_range_check.zoomFactor() == 0.25 &&
                frame_rate_range_check.framesPerView() == 4 * 60 * 60 * 60 &&
                frame_rate_range_check.visibleEndFrame() == one_hour_end &&
                !zoom_out->isEnabled() && !horizontal_scroll->isEnabled(),
            "minimum zoom shows a wider viewport without changing the navigation range");
    zoom_slider->setValue(21);
    require(frame_rate_range_check.zoomFactor() == 512.0 &&
                frame_rate_range_check.framesPerView() == 422 &&
                !zoom_in->isEnabled() && horizontal_scroll->isEnabled(),
            "maximum zoom and horizontal navigation use the bounded discrete zoom levels");
    sendControlWheel(ruler_for_zoom, 120);
    require(frame_rate_range_check.zoomLevelIndex() == 21,
            "Ctrl+wheel cannot zoom beyond the maximum level");
    sendControlWheel(ruler_for_zoom, -120);
    require(frame_rate_range_check.zoomLevelIndex() == 20,
            "Ctrl+wheel moves through the discrete zoom levels");

    zoom_slider->setValue(motion::ui::detail::kTimelineZoomDefaultIndex);
    frame_rate_range_check.setCurrentFrame(30 * 60 * 60);
    const auto anchor_x_before_zoom = frame_rate_range_check.frameToViewportX(
        frame_rate_range_check.currentFrame());
    const auto frame_before_zoom = frame_rate_range_check.currentFrame();
    zoom_slider->setValue(motion::ui::detail::kTimelineZoomDefaultIndex + 1);
    const auto anchor_x_after_zoom = frame_rate_range_check.frameToViewportX(
        frame_rate_range_check.currentFrame());
    require(std::abs(anchor_x_after_zoom - anchor_x_before_zoom) <= 1 &&
                frame_rate_range_check.currentFrame() == frame_before_zoom &&
                frame_rate_range_check.visibleEndFrame() == one_hour_end,
            "zoom keeps the visible playhead anchored without changing navigation state");

    zoom_slider->setValue(6);
    horizontal_scroll->setValue(motion::ui::detail::kTimelineScrollResolution / 2);
    const auto round_trip_frame = frame_rate_range_check.frameAtViewportX(
        frame_rate_range_check.frameToViewportX(frame_before_zoom));
    const auto mapping_tolerance = std::max<std::int64_t>(1,
        frame_rate_range_check.framesPerView() /
            std::max(1, ruler_for_zoom->mappingWidth() - 220) + 1);
    require(frame_rate_range_check.viewStartFrame() > 0 &&
                std::llabs(round_trip_frame - frame_before_zoom) <= mapping_tolerance &&
                frame_rate_range_check.currentFrame() == frame_before_zoom,
            "horizontal scrolling and frame mapping preserve exact playhead state");
    frame_rate_range_check.setCurrentFrame(10 * 60 * 60);
    horizontal_scroll->setValue(motion::ui::detail::kTimelineScrollResolution);
    zoom_slider->setValue(7);
    const auto centered_playhead_x = frame_rate_range_check.frameToViewportX(
        frame_rate_range_check.currentFrame());
    const auto viewport_center_x = 200 + (ruler_for_zoom->mappingWidth() - 220) / 2;
    require(std::abs(centered_playhead_x - viewport_center_x) <= 1,
            "zoom centers the playhead when horizontal scrolling had taken it offscreen");
    frame_rate_range_check.setCompositionTiming({60, 1});
    require(frame_rate_range_check.zoomFactor() == 1.0 &&
                frame_rate_range_check.viewStartFrame() == 0 &&
                frame_rate_range_check.currentFrame() == 0,
            "a new composition resets timeline zoom and scroll to the one-hour overview");
    const auto maximum_frame = std::numeric_limits<std::int64_t>::max();
    require(motion::ui::detail::saturatingFrameAdd(maximum_frame - 2, 10) == maximum_frame,
            "range extension arithmetic saturates at the signed 64-bit frame limit");
    frame_rate_range_check.hide();

    motion::ui::NewCompositionDialog dialog;
    auto* dialog_width = findWidget<QLineEdit>(&dialog, "motion-canvas-width");
    auto* dialog_height = findWidget<QLineEdit>(&dialog, "motion-canvas-height");
    auto* dialog_frame_rate = findWidget<QComboBox>(&dialog, "motion-frame-rate");
    auto* dialog_buttons = findWidget<QDialogButtonBox>(&dialog, "motion-new-composition-buttons");
    require(dialog_width->text().isEmpty() && dialog_height->text().isEmpty()
                && dialog_frame_rate->currentIndex() == 0
                && dialog.findChild<QObject*>(QStringLiteral("motion-duration-frames")) == nullptr,
            "new composition starts with blank canvas and frame-rate fields and no duration");
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
            "Create is disabled until canvas and frame rate are provided");

    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary media directory is available");
    const auto image_path = pathFromQString(temporary.path()) / "poster.png";
    QImage image(48, 32, QImage::Format_RGBA8888);
    image.fill(QColor(20, 140, 210, 255));
    require(image.save(pathToQString(image_path)),
            "temporary image fixture can be written");
    const auto video_path = std::filesystem::path(MOTION_EDITOR_TEST_MEDIA_DIR) / "reference.mkv";
    require(std::filesystem::is_regular_file(video_path), "video fixture exists");

    MainWindow window;
    require(window.compositionDocument() == nullptr,
            "Motion Studio starts without a composition");
    window.show();
    application.processEvents();
    auto* empty_button = findWidget<QPushButton>(&window, "motion-empty-new-composition-button");
    require(empty_button->isVisible(), "the empty state has a New Composition button");
    require(!action(window, "motion-import-media-action")->isEnabled(),
            "media cannot be imported before a composition exists");

    QTimer::singleShot(0, [] { completeCompositionDialog(640, 360, 2); });
    empty_button->click();
    require(window.compositionDocument() != nullptr &&
                window.compositionDocument()->canvasSize() == motion::model::CanvasSize{640, 360},
            "the empty-state action creates an explicitly sized composition");
    require(window.centralWidget()->objectName() == QStringLiteral("motion-composition-splitter") &&
                window.isMaximized(),
            "the composition workspace replaces the empty state and preserves maximization");
    require(window.mediaPoolWidget() != nullptr && window.mediaPoolWidget()->library().empty(),
            "a new composition starts with an empty Media Pool");
    require(findWidget<QWidget>(&window, "motion-media-pool") != nullptr &&
                findWidget<QWidget>(&window, "motion-media-details") != nullptr &&
                findWidget<QWidget>(&window, "motion-timeline-layer-rows") != nullptr &&
                findWidget<QWidget>(&window, "motion-transform-inspector") != nullptr &&
                window.findChild<QWidget*>(QStringLiteral("motion-layer-list")) == nullptr &&
                window.findChild<QWidget*>(QStringLiteral("motion-add-layer-button")) == nullptr,
            "the workspace connects media, timeline layers, and transform inspection");
    require(action(window, "motion-import-media-action")->isEnabled(),
            "File import is enabled for an open composition");

    auto* timeline = findWidget<motion::ui::TimelineNavigator>(&window, "motion-timeline");
    auto* timeline_ruler = findWidget<motion::ui::TimelineRuler>(
        &window, "motion-timeline-ruler");
    auto* timeline_display_mode = findWidget<QComboBox>(
        &window, "motion-timeline-display-mode");
    auto* timeline_position_readout = findWidget<QLabel>(
        &window, "motion-timeline-position-readout");
    auto* timeline_previous_button = findWidget<QPushButton>(
        &window, "motion-timeline-previous-frame");
    auto* timeline_next_button = findWidget<QPushButton>(
        &window, "motion-timeline-next-frame");
    auto* timeline_zoom_slider = findWidget<QSlider>(
        &window, "motion-timeline-zoom-slider");
    auto* timeline_horizontal_scroll = findWidget<QScrollBar>(
        &window, "motion-timeline-horizontal-scroll");
    const auto composition_rate = window.compositionDocument()->frameRate();
    const auto composition_hour_frames =
        (composition_rate.numerator * 60 * 60 + composition_rate.denominator - 1) /
        composition_rate.denominator;
    const auto initial_navigation_end = timeline->visibleEndFrame();
    require(timeline->zoomFactor() == 1.0 &&
                initial_navigation_end == composition_hour_frames - 1 &&
                timeline_display_mode->currentIndex() == 0 &&
                timeline_position_readout->text() == QStringLiteral("00:00:00.000") &&
                !timeline_horizontal_scroll->isEnabled(),
            "a new composition opens at 100 percent with one hour in view and Time display");
    dragPastTimelineEnd(timeline_ruler, timeline->frameToViewportX(initial_navigation_end));
    require(timeline->visibleEndFrame() == initial_navigation_end + composition_hour_frames &&
                timeline->currentFrame() == timeline->visibleEndFrame() &&
                timeline->zoomFactor() == 1.0 && timeline_horizontal_scroll->isEnabled(),
            "dragging past the timeline end adds one hour and keeps the current scale");
    timeline->setCurrentFrame(0);
    timeline_zoom_slider->setValue(6);
    const auto range_before_zoomed_end_drag = timeline->visibleEndFrame();
    dragPastTimelineEnd(timeline_ruler,
                        timeline->frameToViewportX(range_before_zoomed_end_drag));
    require(timeline->visibleEndFrame() == range_before_zoomed_end_drag &&
                timeline->zoomFactor() == 2.0,
            "dragging the viewport edge does not extend a range whose end is offscreen");
    timeline_horizontal_scroll->setValue(motion::ui::detail::kTimelineScrollResolution);
    const auto once_extended_end = timeline->visibleEndFrame();
    dragPastTimelineEnd(timeline_ruler, timeline->frameToViewportX(once_extended_end));
    require(timeline->visibleEndFrame() == once_extended_end + composition_hour_frames &&
                timeline->currentFrame() == timeline->visibleEndFrame(),
            "scrolling to the actual end and dragging extends the range once");
    const auto twice_extended_end = timeline->visibleEndFrame();
    dragPastTimelineEnd(timeline_ruler, timeline->frameToViewportX(twice_extended_end));
    require(timeline->visibleEndFrame() == twice_extended_end + composition_hour_frames &&
                timeline->currentFrame() == timeline->visibleEndFrame(),
            "a separate drag at the new end extends the range again");
    timeline->setCurrentFrame(0);
    timeline_zoom_slider->setValue(motion::ui::detail::kTimelineZoomDefaultIndex);

    QTimer::singleShot(0, [&window] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr && file_dialog->testOption(QFileDialog::DontUseNativeDialog)
                    && file_dialog->fileMode() == QFileDialog::ExistingFiles,
                "media import opens the non-native multiple-file picker");
        require(window.findChild<QProgressDialog*>(
                    QStringLiteral("motion-media-import-progress")) == nullptr,
                "import progress is not created while choosing media");
        require(!file_dialog->nameFilters().join(QLatin1Char(' ')).contains(QStringLiteral("*.gif"),
                                                                               Qt::CaseInsensitive),
                "animated GIF is excluded from the supported import filters");
        file_dialog->reject();
    });
    action(window, "motion-import-media-action")->trigger();
    require(window.mediaPoolWidget()->library().empty(),
            "cancelling the media picker leaves the pool unchanged");
    QTimer::singleShot(0, [&window] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr &&
                    file_dialog->objectName() == QStringLiteral("motion-import-media-dialog"),
                "the Media Pool import button opens the shared file picker");
        require(window.findChild<QProgressDialog*>(
                    QStringLiteral("motion-media-import-progress")) == nullptr,
                "the Media Pool picker opens before any import progress dialog");
        file_dialog->reject();
    });
    findWidget<QPushButton>(&window, "motion-media-import-button")->click();
    require(window.mediaPoolWidget()->library().empty(),
            "cancelling either import entry point keeps the pool unchanged");

    auto* pool = window.mediaPoolWidget();
    pool->importFiles({});
    require(pool->findChild<QProgressDialog*>(
                QStringLiteral("motion-media-import-progress")) == nullptr,
            "an empty import request does not create an import progress dialog");

    QTimer::singleShot(0, [&window, image_path] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr,
                "the media picker remains active until a source is selected");
        require(window.findChild<QProgressDialog*>(
                    QStringLiteral("motion-media-import-progress")) == nullptr,
                "choosing media does not start background import before accepting the picker");
        file_dialog->selectFile(pathToQString(image_path));
        auto* picker_buttons = file_dialog->findChild<QDialogButtonBox*>();
        require(picker_buttons != nullptr &&
                    picker_buttons->button(QDialogButtonBox::Open) != nullptr,
                "the media picker exposes its Open button");
        picker_buttons->button(QDialogButtonBox::Open)->click();
    });
    findWidget<QPushButton>(&window, "motion-media-import-button")->click();
    auto* import_progress = findWidget<QProgressDialog>(
        &window, "motion-media-import-progress");
    require(import_progress->isVisible(),
            "accepting selected media starts its progress dialog after the picker closes");
    require(waitFor([&] { return pool->library().size() == 1; }),
            "a file selected in the Media Pool picker is imported");
    require(!import_progress->isVisible(),
            "the import progress dialog closes when the selected media is processed");

    pool->importFiles({image_path, video_path});
    require(waitFor([&] { return pool->library().size() == 2; }),
            "Motion Studio imports a still image and a video through the shared importer");
    auto* media_list = findWidget<QListWidget>(&window, "motion-media-items");
    require(media_list->count() == 2,
            "imported media appears in the pool");
    const auto& entries = pool->library().items();
    const auto image_entry = std::find_if(entries.begin(), entries.end(), [](const auto& item) {
        return item.metadata.kind == creative_suite::media::MediaKind::Image;
    });
    const auto video_entry = std::find_if(entries.begin(), entries.end(), [](const auto& item) {
        return item.metadata.kind == creative_suite::media::MediaKind::Video;
    });
    require(image_entry != entries.end() && video_entry != entries.end() &&
                !image_entry->offline && !video_entry->offline &&
                image_entry->first_frame.width == 48 && image_entry->first_frame.height == 32 &&
                video_entry->first_frame.width > 0 && video_entry->first_frame.height > 0,
            "image and video entries have decoded, cached first-frame thumbnails");

    auto* layer_rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    require(layer_rows->acceptDrops(), "timeline layer rows accept Media Pool drops");
    const auto image_catalog_path = image_entry->metadata.source_path;
    const auto video_catalog_path = video_entry->metadata.source_path;
    timeline->setCurrentFrame(0);
    deliverMediaDrop(layer_rows, image_catalog_path, QPoint(205, 15), true);
    require(window.compositionDocument()->layers().size() == 1 &&
                window.compositionDocument()->layers().front().kind == motion::model::LayerKind::Image &&
                window.compositionDocument()->layers().front().timeline_start_frame == 0 &&
                window.compositionDocument()->layers().front().duration_frames == 120,
            "dropping a still onto empty timeline space creates a five-second layer at frame zero");
    auto* viewer = static_cast<motion::ui::CompositionViewer*>(
        findWidget<QWidget>(&window, "motion-composition-viewer"));
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        return frame != nullptr && frame->width == 640 && frame->height == 360;
    }), "the shared compositor presents a still-image layer on the canvas");
    const auto still_preview = viewer->renderedFrame();
    const auto still_center = static_cast<std::size_t>(180 * still_preview->stride + 320 * 4);
    require(still_preview->rgba_pixels[still_center] == 20 &&
                still_preview->rgba_pixels[still_center + 1] == 140 &&
                still_preview->rgba_pixels[still_center + 2] == 210,
            "the image preview preserves imported RGBA pixels through composition");
    sendMouseDrag(layer_rows, QPoint(500, 15), QPoint(600, 15));
    require(window.compositionDocument()->layers().front().timeline_start_frame == 0 &&
                window.compositionDocument()->layers().front().duration_frames == 120,
            "dragging empty row space does not move or resize its clip");
    const auto frame_before_selecting = timeline->currentFrame();
    sendMouseClick(layer_rows, QPoint(60, 15));
    require(timeline->currentFrame() == frame_before_selecting,
            "selecting a timeline layer leaves the playhead unchanged");
    auto* position_x = findWidget<QDoubleSpinBox>(&window, "motion-transform-position-x");
    position_x->setValue(0.25);
    require(window.compositionDocument()->layers().front().transform.position_x == 0.25,
            "the transform inspector edits the selected layer base transform");

    deliverMediaDrop(layer_rows, video_catalog_path, QPoint(205, 15), false);
    const auto after_video_drop = window.compositionDocument()->layers();
    require(after_video_drop.size() == 2 &&
                after_video_drop.back().kind == motion::model::LayerKind::Video &&
                after_video_drop.back().timeline_start_frame == 120,
            "dropping media on an existing row creates a distinct layer above it");
    const auto image_preview_frame = viewer->renderedFrame();
    require(timeline->currentFrame() == frame_before_selecting,
            "dropping media does not move the playhead");
    timeline->setCurrentFrame(120);
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        return frame != nullptr && frame != image_preview_frame &&
               frame->width == 640 && frame->height == 360;
    }),
            "video frame decoding runs asynchronously for the composition preview");
    const auto video_preview_frame = viewer->renderedFrame();
    timeline->setCurrentFrame(0);
    timeline->setCurrentFrame(120);
    timeline->setCurrentFrame(0);
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        return frame != nullptr && frame != video_preview_frame &&
               frame->width == 640 && frame->height == 360;
    }), "rapid timeline seeks settle on the newest composition preview");
    const auto settled_preview = viewer->renderedFrame();
    const auto settled_center = static_cast<std::size_t>(
        180 * settled_preview->stride + 320 * 4);
    require(settled_preview->rgba_pixels[settled_center] == 20 &&
                settled_preview->rgba_pixels[settled_center + 1] == 140 &&
                settled_preview->rgba_pixels[settled_center + 2] == 210,
            "a stale video seek cannot replace the final still-image seek result");
    const auto frame_before_layer_operations = timeline->currentFrame();
    const auto video_layer_id = window.compositionDocument()->layers().back().id;
    sendMouseDrag(layer_rows, QPoint(60, 15), QPoint(60, 49));
    require(window.compositionDocument()->layers().front().id == video_layer_id,
            "dragging a layer header reorders front-to-back timeline rows");
    sendMouseClick(layer_rows, QPoint(15, 15));
    require(!window.compositionDocument()->layers().back().visible,
            "the timeline visibility control hides its layer");
    require(timeline->currentFrame() == frame_before_layer_operations,
            "adding, selecting, and reordering layers preserve the playhead frame");

    const auto image_layer_id = window.compositionDocument()->layers().back().id;
    sendMouseDrag(layer_rows, QPoint(200, 15), QPoint(225, 15));
    const auto moved_image = std::find_if(window.compositionDocument()->layers().begin(),
        window.compositionDocument()->layers().end(), [image_layer_id](const auto& layer) {
            return layer.id == image_layer_id;
        });
    require(moved_image != window.compositionDocument()->layers().end() &&
                moved_image->timeline_start_frame > 0,
            "dragging a clip body moves its composition start frame");
    sendMouseDrag(layer_rows, QPoint(202, 49), QPoint(196, 49));
    const auto shortened_video = std::find_if(window.compositionDocument()->layers().begin(),
        window.compositionDocument()->layers().end(), [video_layer_id](const auto& layer) {
            return layer.id == video_layer_id;
        });
    require(shortened_video != window.compositionDocument()->layers().end() &&
                shortened_video->duration_frames == 1,
            "dragging a video clip's right edge shortens it within its source duration");
    sendMouseClick(layer_rows, QPoint(60, 49));
    QKeyEvent remove_key(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(layer_rows, &remove_key);
    require(window.compositionDocument()->layers().size() == 1 &&
                window.compositionDocument()->layers().front().id == image_layer_id,
            "Delete removes the selected timeline layer");

    auto* document = window.compositionDocument();
    const auto layer_before_zoom = document->layers().front();
    require(layer_before_zoom.keyframes == creative_suite::animation::TransformKeyframes{},
            "the layer starts with no evaluated keyframes in the current workflow");

    timeline->setCurrentFrame(123);
    const auto frame_before_display_switch = timeline->currentFrame();
    const auto view_start_before_display_switch = timeline->viewStartFrame();
    const auto range_end_before_display_switch = timeline->visibleEndFrame();
    const auto zoom_before_display_switch = timeline->zoomFactor();
    timeline_display_mode->setCurrentIndex(1);
    require(timeline_position_readout->text() == QStringLiteral("Frame 123") &&
                timeline->currentFrame() == frame_before_display_switch &&
                timeline->viewStartFrame() == view_start_before_display_switch &&
                timeline->visibleEndFrame() == range_end_before_display_switch &&
                timeline->zoomFactor() == zoom_before_display_switch &&
                document->layers().front().timeline_start_frame ==
                    layer_before_zoom.timeline_start_frame &&
                document->layers().front().duration_frames == layer_before_zoom.duration_frames &&
                document->layers().front().transform == layer_before_zoom.transform &&
                document->layers().front().keyframes == layer_before_zoom.keyframes,
            "switching to Frames changes presentation only");
    timeline_next_button->click();
    require(timeline->currentFrame() == 124 &&
                timeline_position_readout->text() == QStringLiteral("Frame 124"),
            "Next frame continues stepping by one frame in Frames mode");
    timeline_previous_button->click();
    timeline_display_mode->setCurrentIndex(0);
    require(timeline->currentFrame() == 123 &&
                timeline_position_readout->text() == QStringLiteral("00:00:05.125"),
            "Time mode displays elapsed time at the composition rate");
    require(document->layers().front().transform == layer_before_zoom.transform &&
                document->layers().front().keyframes == layer_before_zoom.keyframes,
            "display mode changes leave layer transforms and keyframes unchanged");

    const auto frame_before_zoom_ui = timeline->currentFrame();
    timeline_zoom_slider->setValue(21);
    timeline_horizontal_scroll->setValue(motion::ui::detail::kTimelineScrollResolution);
    require(timeline->zoomFactor() == 512.0 &&
                timeline->currentFrame() == frame_before_zoom_ui &&
                document->layers().front().timeline_start_frame ==
                    layer_before_zoom.timeline_start_frame &&
                document->layers().front().duration_frames == layer_before_zoom.duration_frames &&
                document->layers().front().transform == layer_before_zoom.transform &&
                document->layers().front().keyframes == layer_before_zoom.keyframes,
            "zoom and horizontal scrolling leave playhead, transforms, keyframes, and layer timing unchanged");
    const auto mapped_frame = std::min(timeline->visibleEndFrame(),
        timeline->viewStartFrame() + timeline->framesPerView() / 2);
    const int mapped_x = timeline->frameToViewportX(mapped_frame);
    const auto frame_from_ruler = timeline->frameAtViewportX(mapped_x);
    sendMouseClick(timeline_ruler, QPoint(mapped_x, 48));
    require(std::llabs(frame_from_ruler - mapped_frame) <= 1 &&
                timeline->currentFrame() == frame_from_ruler &&
                document->layers().front().transform == layer_before_zoom.transform &&
                document->layers().front().keyframes == layer_before_zoom.keyframes,
            "ruler and layer viewport share frame mapping and seeking does not evaluate keyframes");

    timeline->setCurrentFrame(layer_before_zoom.timeline_start_frame);
    timeline_zoom_slider->setValue(21);
    const auto playhead_before_zoomed_drop = timeline->currentFrame();
    const auto drop_frame = timeline->viewStartFrame() + timeline->framesPerView() / 2;
    const int drop_x = timeline->frameToViewportX(drop_frame);
    const auto expected_drop_frame = timeline->frameAtViewportX(drop_x);
    deliverMediaDrop(layer_rows, video_catalog_path, QPoint(drop_x, 15), false);
    const auto zoomed_drop_layers = document->layers();
    const auto zoomed_video = std::find_if(
        zoomed_drop_layers.begin(), zoomed_drop_layers.end(), [](const auto& layer) {
            return layer.kind == motion::model::LayerKind::Video;
        });
    const auto row_snap_tolerance = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::ceil(
            8.0L * (timeline->framesPerView() - 1) /
            std::max(1, timeline_ruler->mappingWidth() - 220))));
    require(zoomed_video != zoomed_drop_layers.end() &&
                std::llabs(zoomed_video->timeline_start_frame - expected_drop_frame) <=
                    row_snap_tolerance &&
                timeline->currentFrame() == playhead_before_zoomed_drop,
            "media drop uses the zoomed frame mapping without moving the playhead");
    sendMouseClick(layer_rows, QPoint(60, 15));
    QKeyEvent remove_zoomed_video(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(layer_rows, &remove_zoomed_video);
    require(document->layers().size() == 1 &&
                document->layers().front().kind == motion::model::LayerKind::Image,
            "the zoomed media-drop regression removes its temporary video layer");

    const auto image_start_before_zoomed_move =
        document->layers().front().timeline_start_frame;
    const auto image_body_x = timeline->frameToViewportX(
        image_start_before_zoomed_move + document->layers().front().duration_frames / 2);
    sendMouseDrag(layer_rows, QPoint(image_body_x, 15), QPoint(image_body_x + 24, 15));
    require(document->layers().front().timeline_start_frame > image_start_before_zoomed_move,
            "layer movement remains frame-accurate when zoomed in");
    const auto duration_before_zoomed_resize = document->layers().front().duration_frames;
    const auto image_end_x = timeline->frameToViewportX(
        document->layers().front().timeline_start_frame + duration_before_zoomed_resize);
    sendMouseDrag(layer_rows, QPoint(image_end_x - 2, 15), QPoint(image_end_x - 26, 15));
    require(document->layers().front().duration_frames < duration_before_zoomed_resize &&
                document->layers().front().duration_frames > 1,
            "layer edge resizing remains usable at high zoom");
    timeline_zoom_slider->setValue(motion::ui::detail::kTimelineZoomDefaultIndex);

    auto* all_media = findWidget<QTreeWidget>(&window, "motion-media-bins")->topLevelItem(0);
    auto* bins_tree = findWidget<QTreeWidget>(&window, "motion-media-bins");
    bins_tree->setCurrentItem(all_media);
    media_list->setCurrentRow(0);
    auto* details_name = findWidget<QLabel>(&window, "motion-media-detail-name");
    auto* details_type = findWidget<QLabel>(&window, "motion-media-detail-type");
    auto* details_resolution = findWidget<QLabel>(&window, "motion-media-detail-resolution");
    require(details_name->text() != QStringLiteral("—") &&
                details_type->text() == QStringLiteral("Image") &&
                details_resolution->text() == QStringLiteral("48 × 32") &&
                !media_list->currentItem()->icon().isNull(),
            "selecting an image updates its metadata details and cached thumbnail");
    timeline->setCurrentFrame(123);
    const auto canvas_before_selecting = window.compositionDocument()->canvasSize();
    media_list->setCurrentRow(1);
    require(timeline->currentFrame() == 123 &&
                window.compositionDocument()->canvasSize() == canvas_before_selecting &&
                details_type->text() == QStringLiteral("Video"),
            "media selection leaves the timeline and composition canvas unchanged");

    auto* media_status = findWidget<QLabel>(&window, "motion-media-status");
    pool->importFiles({image_path});
    require(waitFor([&] { return media_status->text().contains(QStringLiteral("1 duplicates")); }) &&
                pool->library().size() == 2,
            "reimporting a canonical path keeps one catalog item");

    auto* list_mode = findWidget<QToolButton>(&window, "motion-media-list-mode");
    auto* thumbnail_mode = findWidget<QToolButton>(&window, "motion-media-thumbnail-mode");
    thumbnail_mode->click();
    require(media_list->viewMode() == QListView::IconMode && thumbnail_mode->isChecked() &&
                media_list->count() == 2 &&
                !media_list->item(0)->icon().isNull() &&
                !media_list->item(1)->icon().isNull(),
            "thumbnail mode displays a grid of cached previews");
    list_mode->click();
    require(media_list->viewMode() == QListView::ListMode && list_mode->isChecked(),
            "list mode can be restored");

    auto* new_bin_button = findWidget<QPushButton>(&window, "motion-media-new-bin-button");
    setInputDialogText(QStringLiteral("Footage/Day 1"));
    new_bin_button->click();
    require(std::find(pool->library().bins().begin(), pool->library().bins().end(),
                      "Footage/Day 1") != pool->library().bins().end(),
            "the pool can create nested bins");
    all_media = bins_tree->topLevelItem(0);
    bins_tree->setCurrentItem(all_media);
    require(media_list->count() == 2,
            "All Media continues to show items across nested bins");

    auto* image_row = media_list->item(0);
    const auto image_row_path = image_row->data(Qt::UserRole + 1).toString();
    chooseActionOnNextMenu(QStringLiteral("Footage/Day 1"));
    requestContextMenu(media_list, media_list->visualItemRect(image_row).center());
    const auto renamed_image_path = pathFromQString(image_row_path);
    require(waitFor([&] {
        const auto index = pool->library().indexForPath(renamed_image_path);
        return index < pool->library().size() &&
            pool->library().items()[index].bin_path == "Footage/Day 1";
    }), "the media context menu can move an item into a nested bin");
    auto* nested_bin = findBin(all_media, QStringLiteral("Footage/Day 1"));
    require(nested_bin != nullptr, "the nested bin appears in the hierarchy");
    bins_tree->setCurrentItem(nested_bin);
    require(media_list->count() == 1,
            "selecting a bin filters the pool to that bin and its descendants");
    nested_bin->setText(0, QStringLiteral("Day One"));
    require(std::find(pool->library().bins().begin(), pool->library().bins().end(),
                      "Footage/Day One") != pool->library().bins().end(),
            "renaming a bin updates the shared catalog path");
    QCoreApplication::processEvents();
    all_media = bins_tree->topLevelItem(0);
    bins_tree->setCurrentItem(all_media);

    media_list->setCurrentRow(0);
    auto* media_item_to_rename = media_list->item(0);
    const QString old_media_path = media_item_to_rename->data(Qt::UserRole + 1).toString();
    media_item_to_rename->setText(QStringLiteral("Poster renamed"));
    const auto renamed_index = pool->library().indexForPath(
        pathFromQString(old_media_path));
    require(renamed_index < pool->library().size() &&
                pool->library().items()[renamed_index].display_name == "Poster renamed",
            "editing a media label renames it in the shared catalog");

    const auto* selected_before_mark = pool->selectedMedia();
    require(selected_before_mark != nullptr, "a selected media item is available");
    const auto selected_source_path = selected_before_mark->metadata.source_path;
    const auto item_position = media_list->visualItemRect(media_list->currentItem()).center();
    chooseActionOnNextMenu(QStringLiteral("Mark Offline"));
    requestContextMenu(media_list, item_position);
    require(waitFor([&] {
        const auto index = pool->library().indexForPath(selected_source_path);
        return index < pool->library().size() && pool->library().items()[index].offline;
    }), "the media context menu can mark an item offline");
    chooseActionOnNextMenu(QStringLiteral("Restore Media"));
    requestContextMenu(media_list,
        media_list->visualItemRect(media_list->currentItem()).center());
    require(waitFor([&] {
        const auto index = pool->library().indexForPath(selected_source_path);
        return index < pool->library().size() && !pool->library().items()[index].offline;
    }), "an offline item can be restored through the shared importer");
    const auto restored_index = pool->library().indexForPath(selected_source_path);
    require(pool->library().items()[restored_index].display_name == "Poster renamed",
            "restoring an offline source preserves its Media Pool label");

    timeline_display_mode->setCurrentIndex(1);
    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "replacement prompt is shown");
        prompt->button(QMessageBox::Yes)->click();
        QTimer::singleShot(0, [] { completeCompositionDialog(1920, 1080, 4); });
    });
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{1920, 1080} &&
                window.compositionDocument()->frameRate() == motion::model::FrameRate{30000, 1001},
            "replacement creates a new composition with the selected exact frame rate");
    require(pool->library().empty() && media_list->count() == 0 && timeline->currentFrame() == 0 &&
                timeline_display_mode->currentIndex() == 0 &&
                timeline_position_readout->text() == QStringLiteral("00:00:00.000"),
            "replacing a composition clears its Media Pool, resets navigation, and restores Time display");

    std::cout << "Motion Studio Media Pool UI tests passed.\n";
    return EXIT_SUCCESS;
}
