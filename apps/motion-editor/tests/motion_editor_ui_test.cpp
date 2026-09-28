#include "ui/main_window.h"
#include "ui/media_pool_widget.h"
#include "ui/new_composition_dialog.h"
#include "ui/timeline_navigator.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QCoreApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>

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
                window.findChild<QWidget*>(QStringLiteral("motion-layer-list")) == nullptr &&
                window.findChild<QWidget*>(QStringLiteral("motion-add-layer-button")) == nullptr &&
                window.findChild<QWidget*>(QStringLiteral("motion-transform-inspector")) == nullptr,
            "the workspace shows media panels and defers layer controls");
    require(action(window, "motion-import-media-action")->isEnabled(),
            "File import is enabled for an open composition");
    QTimer::singleShot(0, [] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr && file_dialog->testOption(QFileDialog::DontUseNativeDialog)
                    && file_dialog->fileMode() == QFileDialog::ExistingFiles,
                "media import opens the non-native multiple-file picker");
        require(!file_dialog->nameFilters().join(QLatin1Char(' ')).contains(QStringLiteral("*.gif"),
                                                                               Qt::CaseInsensitive),
                "animated GIF is excluded from the supported import filters");
        file_dialog->reject();
    });
    action(window, "motion-import-media-action")->trigger();
    require(window.mediaPoolWidget()->library().empty(),
            "cancelling the media picker leaves the pool unchanged");
    QTimer::singleShot(0, [] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr &&
                    file_dialog->objectName() == QStringLiteral("motion-import-media-dialog"),
                "the Media Pool import button opens the shared file picker");
        file_dialog->reject();
    });
    findWidget<QPushButton>(&window, "motion-media-import-button")->click();
    require(window.mediaPoolWidget()->library().empty(),
            "cancelling either import entry point keeps the pool unchanged");

    auto* pool = window.mediaPoolWidget();
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
    auto* timeline = findWidget<motion::ui::TimelineNavigator>(&window, "motion-timeline");
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
    const auto image_catalog_path = pathFromQString(image_row_path);
    require(waitFor([&] {
        const auto index = pool->library().indexForPath(image_catalog_path);
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
    require(pool->library().empty() && media_list->count() == 0 && timeline->currentFrame() == 0,
            "replacing a composition clears its in-memory Media Pool and resets navigation");

    std::cout << "Motion Studio Media Pool UI tests passed.\n";
    return EXIT_SUCCESS;
}
