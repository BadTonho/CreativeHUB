#include "ui/main_window.h"
#include "ui/composition_viewer.h"
#include "ui/autosave_recovery_dialog.h"
#include "ui/media_pool_widget.h"
#include "ui/new_composition_dialog.h"
#include "ui/timeline_navigator.h"
#include "ui/timeline_navigator_math.h"
#include "model/motion_project_data.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "settings/autosave_preferences.h"

#include <QAction>
#include <QCheckBox>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequenceEdit>
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
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
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

bool hasBrightPixel(const QImage& image, const QRect& area, int minimum_lightness)
{
    const auto bounded = area.intersected(image.rect());
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            if (image.pixelColor(x, y).lightness() >= minimum_lightness) return true;
        }
    }
    return false;
}

bool frameHasVisibleRgb(const creative_suite::media::RgbaFrame& frame)
{
    for (int y = 0; y < frame.height; ++y) {
        const auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            if (pixel[3] != 0 && (pixel[0] > 40 || pixel[1] > 40 || pixel[2] > 40)) {
                return true;
            }
        }
    }
    return false;
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

void interactWithShortcutDialog(
    MainWindow& window,
    const std::function<void(QDialog*)>& interaction)
{
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "keyboard shortcut settings dialog opens");
        interaction(dialog);
    });
    action(window, "motion-shortcut-settings-action")->trigger();
}

void testMotionShortcutSettings()
{
#if defined(Q_OS_MACOS)
    const QKeySequence expected_import_sequence(Qt::META | Qt::Key_I);
#else
    const QKeySequence expected_import_sequence(Qt::CTRL | Qt::Key_I);
#endif
    QTemporaryDir recovery_directory;
    require(recovery_directory.isValid(), "shortcut test recovery directory is available");
    MainWindow window(nullptr, pathFromQString(recovery_directory.path()), "shortcut-tests");
    auto* create_action = action(window, "motion-new-composition-action");
    auto* open_action = action(window, "motion-open-composition-action");
    auto* save_action = action(window, "motion-save-composition-action");
    auto* save_as_action = action(window, "motion-save-composition-as-action");
    auto* import_action = action(window, "motion-import-media-action");
    auto* undo_action = action(window, "motion-undo-action");
    auto* redo_action = action(window, "motion-redo-action");
    auto* play_action = action(window, "motion-play-pause-action");
    auto* previous_action = action(window, "motion-previous-frame-action");
    auto* next_action = action(window, "motion-next-frame-action");
    auto* loop_action = action(window, "motion-loop-action");
    auto* zoom_in_action = action(window, "motion-zoom-in-action");
    auto* zoom_out_action = action(window, "motion-zoom-out-action");
    require(create_action->shortcut() == QKeySequence::New &&
                open_action->shortcut() == QKeySequence::Open &&
                save_action->shortcut() == QKeySequence::Save &&
                undo_action->shortcut() == QKeySequence::Undo &&
                redo_action->shortcut() == QKeySequence::Redo &&
                import_action->shortcut() == expected_import_sequence &&
                play_action->shortcut() == QKeySequence(Qt::Key_Space) &&
                previous_action->shortcut() == QKeySequence(Qt::Key_Left) &&
                next_action->shortcut() == QKeySequence(Qt::Key_Right) &&
                loop_action->shortcut().isEmpty() && zoom_in_action->shortcut().isEmpty() &&
                zoom_out_action->shortcut().isEmpty(),
            "Motion Studio registers its own default shortcut catalog");
    require(create_action->isEnabled() && open_action->isEnabled() &&
                !save_action->isEnabled() && !save_as_action->isEnabled() &&
                !import_action->isEnabled() &&
                !undo_action->isEnabled() && !redo_action->isEnabled() &&
                !play_action->isEnabled() && !previous_action->isEnabled() &&
                !next_action->isEnabled() && !loop_action->isEnabled(),
            "commands reflect the empty composition state");

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        auto* editor = findWidget<QKeySequenceEdit>(dialog,
            "motion-shortcut-editor-file.new_composition");
        auto* undo_editor = findWidget<QKeySequenceEdit>(dialog,
            "motion-shortcut-editor-edit.undo");
        auto* buttons = findWidget<QDialogButtonBox>(dialog, "motion-shortcut-dialog-buttons");
        editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
        undo_editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    require(create_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N) &&
                undo_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U),
            "OK applies the shortcut settings as a batch");

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        auto* create_editor = findWidget<QKeySequenceEdit>(dialog,
            "motion-shortcut-editor-file.new_composition");
        auto* import_editor = findWidget<QKeySequenceEdit>(dialog,
            "motion-shortcut-editor-media.import");
        auto* buttons = findWidget<QDialogButtonBox>(dialog, "motion-shortcut-dialog-buttons");
        create_editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
        import_editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
        buttons->button(QDialogButtonBox::Ok)->click();
        require(findWidget<QLabel>(dialog, "motion-shortcut-validation-message")->isVisible(),
                "duplicate shortcut assignments show an in-dialog conflict");
        buttons->button(QDialogButtonBox::Cancel)->click();
    });
    require(create_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
            "a conflicted and cancelled edit leaves the active binding unchanged");

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        auto* editor = findWidget<QKeySequenceEdit>(dialog,
            "motion-shortcut-editor-file.new_composition");
        editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::Key_M));
        findWidget<QPushButton>(dialog, "motion-shortcut-dialog-cancel")->click();
    });
    require(create_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
            "Cancel discards shortcut edits");

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        findWidget<QPushButton>(dialog,
            "motion-shortcut-clear-media.import")->click();
        findWidget<QDialogButtonBox>(dialog, "motion-shortcut-dialog-buttons")
            ->button(QDialogButtonBox::Ok)->click();
    });
    require(import_action->shortcut().isEmpty(),
            "Clear removes one command shortcut and OK applies the change");

    QSettings settings;
    settings.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    require(settings.value(QStringLiteral("file.new_composition")).toString() ==
                QStringLiteral("Ctrl+Shift+N") &&
                settings.value(QStringLiteral("edit.undo")).toString() ==
                    QStringLiteral("Ctrl+Shift+U"),
            "accepted shortcut preferences persist in the Motion Studio group");
    settings.endGroup();

    {
        MainWindow reopened(nullptr, pathFromQString(recovery_directory.path()),
                            "shortcut-tests-reopened");
        require(action(reopened, "motion-new-composition-action")->shortcut() ==
                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N) &&
                    action(reopened, "motion-undo-action")->shortcut() ==
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U) &&
                    action(reopened, "motion-import-media-action")->shortcut().isEmpty(),
                "shortcut settings load when a new window is created");
    }

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        findWidget<QPushButton>(dialog, "motion-shortcut-reset-all")->click();
        require(findWidget<QKeySequenceEdit>(dialog,
                    "motion-shortcut-editor-file.new_composition")->keySequence() ==
                    QKeySequence::New,
                "Reset All previews the registered defaults");
        findWidget<QPushButton>(dialog, "motion-shortcut-dialog-cancel")->click();
    });
    require(create_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
            "cancelling Reset All keeps the saved assignments");

    interactWithShortcutDialog(window, [](QDialog* dialog) {
        findWidget<QPushButton>(dialog, "motion-shortcut-reset-all")->click();
        findWidget<QDialogButtonBox>(dialog, "motion-shortcut-dialog-buttons")
            ->button(QDialogButtonBox::Ok)->click();
    });
    require(create_action->shortcut() == QKeySequence::New &&
                import_action->shortcut() == expected_import_sequence &&
                undo_action->shortcut() == QKeySequence::Undo &&
                redo_action->shortcut() == QKeySequence::Redo,
            "Reset All applies and persists all registered defaults");

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "New Composition action opens its existing dialog");
        auto* width = findWidget<QLineEdit>(dialog, "motion-canvas-width");
        auto* height = findWidget<QLineEdit>(dialog, "motion-canvas-height");
        auto* rates = findWidget<QComboBox>(dialog, "motion-frame-rate");
        auto* buttons = findWidget<QDialogButtonBox>(dialog, "motion-new-composition-buttons");
        width->setText(QStringLiteral("640"));
        height->setText(QStringLiteral("360"));
        rates->setCurrentIndex(3);
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    create_action->trigger();
    require(window.compositionDocument() != nullptr && import_action->isEnabled() &&
                save_action->isEnabled() && save_as_action->isEnabled() &&
                !play_action->isEnabled() && !loop_action->isEnabled() &&
                zoom_in_action->isEnabled() && zoom_out_action->isEnabled(),
            "composition creation enables import and timeline navigation but not empty playback");

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "Import Media action opens the existing file picker");
        dialog->reject();
    });
    import_action->trigger();
    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "closing an unsaved composition requests a decision");
        prompt->button(QMessageBox::Discard)->click();
    });
    window.close();
}

void chooseDocumentFile(const std::filesystem::path& path,
                        QDialogButtonBox::StandardButton button)
{
    auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
    require(dialog != nullptr && !dialog->testOption(QFileDialog::DontUseNativeDialog),
            "document action leaves the platform-native picker enabled");
    dialog->selectFile(pathToQString(path));
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    require(buttons != nullptr && buttons->button(button) != nullptr,
            "document file picker exposes the expected action");
    buttons->button(button)->click();
}

void createComposition(MainWindow& window, int width, int height, int frame_rate_index)
{
    QTimer::singleShot(0, [width, height, frame_rate_index] {
        completeCompositionDialog(width, height, frame_rate_index);
    });
    action(window, "motion-new-composition-action")->trigger();
}

void chooseRecoveryActionOnNextDialog(const char* action_name,
                                      int attempts = 0,
                                      bool* handled = nullptr,
                                      std::optional<QMessageBox::StandardButton> followup =
                                          std::nullopt)
{
    const QString target_name = QString::fromLatin1(action_name);
    QTimer::singleShot(0, [target_name, attempts, handled, followup] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog != nullptr &&
            dialog->objectName() == QStringLiteral("motion-recovery-choice-dialog")) {
            if (handled != nullptr) *handled = true;
            if (followup.has_value()) {
                QTimer::singleShot(0, [button = *followup] {
                    auto* prompt = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                    require(prompt != nullptr,
                            "recovery replacement prompts for the current dirty document");
                    prompt->button(button)->click();
                });
            }
            findWidget<QPushButton>(dialog, target_name.toUtf8().constData())->click();
            return;
        }
        require(attempts < 1000, "the requested recovery choice dialog appears");
        chooseRecoveryActionOnNextDialog(target_name.toUtf8().constData(), attempts + 1,
                                         handled, followup);
    });
}

void testAutosavePreferencesAndSnapshots()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "autosave test directory is available");
    const auto root = pathFromQString(temporary.path());
    motion::settings::setAutosaveEnabled(true);
    motion::settings::setAutosaveIntervalSeconds(30);
    motion::settings::setRecoveryRetention(5);

    MainWindow window(nullptr, root / "recovery", "autosave-ui-session");
    window.show();
    createComposition(window, 320, 180, 2);
    auto* timer = findWidget<QTimer>(&window, "motion-autosave-timer");
    require(timer->isActive() && timer->interval() == 30000,
            "autosave uses the enabled 30-second default interval");

    const bool invoked = QMetaObject::invokeMethod(
        timer, "timeout", Qt::DirectConnection);
    require(invoked, "autosave timer can be triggered deterministically");
    motion::persistence::MotionRecoveryStore store(root / "recovery", "autosave-ui-session");
    auto snapshots = store.unsavedSnapshots();
    require(snapshots.size() == 1 &&
                motion::persistence::MotionDocumentStore::loadRecovery(snapshots.front().path)
                    .document.composition.canvas_size == motion::model::CanvasSize{320, 180},
            "a dirty untitled composition is captured in a recovery snapshot");
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    require(store.unsavedSnapshots().size() == 1,
            "an unchanged composition is not snapshotted twice");

    QImage image(12, 8, QImage::Format_RGBA8888);
    image.fill(QColor(60, 120, 200, 255));
    const auto media_path = root / "pool-entry.png";
    require(image.save(pathToQString(media_path)), "autosave media fixture is written");
    window.mediaPoolWidget()->importFiles({media_path});
    require(waitFor([&] { return window.mediaPoolWidget()->library().size() == 1; }),
            "the Media Pool import finishes before the next autosave check");
    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    require(store.unsavedSnapshots().size() == 2,
            "Media Pool changes are included in the next dirty snapshot");
    const auto newest = motion::persistence::MotionDocumentStore::loadRecovery(
        store.unsavedSnapshots().front().path);
    require(newest.document.media.size() == 1 &&
                newest.document.media.front().source_path ==
                    creative_suite::media::MediaLibrary::canonicalPath(media_path),
            "recovery snapshots include the full Media Pool catalog");

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr && dialog->objectName() ==
                    QStringLiteral("motion-autosave-recovery-dialog"),
                "Settings opens Autosave & Recovery");
        auto* snapshots_table = findWidget<QTableWidget>(dialog, "motion-recovery-snapshots");
        require(snapshots_table->rowCount() == 2,
                "recovery settings lists current-session snapshots");
        auto* interval = findWidget<QSpinBox>(dialog, "motion-autosave-interval");
        auto* retention = findWidget<QSpinBox>(dialog, "motion-autosave-retention");
        require(interval->minimum() == 10 && interval->maximum() == 300 &&
                    retention->minimum() == 5 && retention->maximum() == 20,
                "the autosave settings controls enforce the documented bounds");
        interval->setValue(17);
        retention->setValue(8);
        findWidget<QCheckBox>(dialog, "motion-autosave-enabled")->setChecked(false);
        findWidget<QPushButton>(dialog, "motion-recovery-refresh")->click();
        snapshots_table->selectRow(0);
        QTimer::singleShot(0, [] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(prompt != nullptr, "deleting a recovery snapshot asks for confirmation");
            prompt->button(QMessageBox::Yes)->click();
        });
        findWidget<QPushButton>(dialog, "motion-recovery-delete")->click();
        require(snapshots_table->rowCount() == 1,
                "Delete removes the selected snapshot and refreshes the list");
        findWidget<QPushButton>(dialog, "motion-recovery-refresh")->click();
        snapshots_table->selectRow(0);
        QTimer::singleShot(0, [] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(prompt != nullptr, "restoring from Settings confirms replacing the dirty document");
            prompt->button(QMessageBox::Discard)->click();
        });
        findWidget<QPushButton>(dialog, "motion-recovery-restore")->click();
    });
    action(window, "motion-autosave-recovery-action")->trigger();
    require(!motion::settings::autosaveEnabled() &&
                motion::settings::autosaveIntervalSeconds() == 17 &&
                motion::settings::recoveryRetention() == 8 && !timer->isActive() &&
                window.compositionDocument()->canvasSize() == motion::model::CanvasSize{320, 180} &&
                window.mediaPoolWidget()->library().empty() && window.isWindowModified(),
            "Settings persist preferences and restore the selected snapshot as dirty");
    {
        MainWindow reopened(nullptr, root / "reopened-recovery", "autosave-settings-reopened");
        auto* reopened_timer = findWidget<QTimer>(&reopened, "motion-autosave-timer");
        require(!reopened_timer->isActive() && reopened_timer->interval() == 17000,
                "autosave preferences are applied when a new window is created");
    }

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "closing an autosaved dirty document asks for a decision");
        prompt->button(QMessageBox::Discard)->click();
    });
    window.close();
    motion::settings::setAutosaveEnabled(true);
    motion::settings::setAutosaveIntervalSeconds(30);
    motion::settings::setRecoveryRetention(5);
}

void testRecoveryDialogFolderAction()
{
    motion::ui::AutosaveRecoveryDialog dialog(true, 30, 5);
    dialog.setSnapshots({{
        QStringLiteral("Untitled composition"), QStringLiteral("Untitled project"),
        QStringLiteral("Now"), QStringLiteral("snapshot.motion-recovery"),
        QStringLiteral("C:/recovery/snapshot.motion-recovery"), QString{},
        QStringLiteral("C:/recovery")}});
    QString opened_folder;
    QObject::connect(&dialog, &motion::ui::AutosaveRecoveryDialog::openFolderRequested,
                     &dialog, [&opened_folder](const QString& path) {
        opened_folder = path;
    });
    auto* open_folder = findWidget<QPushButton>(&dialog, "motion-recovery-open-folder");
    require(open_folder->isEnabled(), "Open Folder is enabled for a selected snapshot");
    open_folder->click();
    require(opened_folder == QStringLiteral("C:/recovery"),
            "Open Folder reports the selected snapshot directory to the application");
}

void testRecoveryRestoreAndIgnore()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "startup recovery directory is available");
    const auto root = pathFromQString(temporary.path());
    motion::persistence::MotionRecoveryStore store(root, "crashed-session");
    motion::model::MotionProjectData recovered_project;
    recovered_project.composition = {{720, 405}, {30000, 1001}};
    motion::model::CompositionDocument layers(720, 405, {30000, 1001});
    (void)layers.addLayer(motion::model::LayerKind::Shape, "Recovered shape");
    recovered_project.layers = layers.layers();
    const auto missing_image = root / "recovery-source-missing.png";
    recovered_project.bins = {"Unsorted", "Footage"};
    recovered_project.media.push_back({
        creative_suite::media::MediaLibrary::canonicalPath(missing_image),
        creative_suite::media::MediaKind::Image, "Recovered offline still", "Footage"});
    store.saveSnapshot(recovered_project, 5);
    const auto recovery_path = store.unsavedSnapshots().front().path;
    motion::settings::setAutosaveEnabled(false);

    {
        MainWindow ignored(nullptr, root, "ignore-session");
        bool ignore_handled = false;
        chooseRecoveryActionOnNextDialog("motion-recovery-choice-ignore", 0,
                                         &ignore_handled);
        ignored.show();
        require(waitFor([&] {
                    return ignore_handled && QApplication::activeModalWidget() == nullptr &&
                        ignored.compositionDocument() == nullptr;
                }), "Ignore continues startup without opening the snapshot");
        require(std::filesystem::is_regular_file(recovery_path),
                "ignoring startup recovery preserves the snapshot");
        ignored.close();
    }

    {
        MainWindow restored(nullptr, root, "restore-session");
        bool restore_handled = false;
        chooseRecoveryActionOnNextDialog("motion-recovery-choice-restore", 0,
                                         &restore_handled);
        restored.show();
        require(waitFor([&] {
                    return restore_handled && restored.compositionDocument() != nullptr &&
                        restored.isWindowModified();
                }), "Restore stages the untitled snapshot and keeps it dirty");
        require(restored.compositionDocument()->canvasSize() ==
                    motion::model::CanvasSize{720, 405} &&
                    restored.compositionDocument()->layers().front().name == "Recovered shape" &&
                    restored.mediaPoolWidget()->library().size() == 1 &&
                    restored.mediaPoolWidget()->library().items().front().offline &&
                    restored.mediaPoolWidget()->library().items().front().display_name ==
                        "Recovered offline still" && store.unsavedSnapshots().size() == 1,
                "startup recovery keeps its source snapshot until the recovered work is safe");
        QTimer::singleShot(0, [] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(prompt != nullptr, "closing a restored dirty composition asks for a decision");
            prompt->button(QMessageBox::Discard)->click();
        });
        restored.close();
        require(store.unsavedSnapshots().empty(),
                "discarding the restored untitled composition removes its recovery source");
    }
    motion::settings::setAutosaveEnabled(true);
}

void testSavedProjectRecoveryOnOpen()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "saved-project recovery directory is available");
    const auto root = pathFromQString(temporary.path());
    const auto project_path = root / "saved.motion";
    motion::model::MotionProjectData saved_project;
    saved_project.composition = {{640, 360}, {24, 1}};
    motion::model::CompositionDocument saved_layers(640, 360, {24, 1});
    (void)saved_layers.addLayer(motion::model::LayerKind::Shape, "Saved version");
    saved_project.layers = saved_layers.layers();
    motion::persistence::MotionDocumentStore::save(project_path, saved_project);

    auto recovery_project = saved_project;
    recovery_project.layers.front().name = "Recovered version";
    motion::persistence::MotionRecoveryStore store(root / "recovery", "recovery-session");
    store.saveSnapshot(recovery_project, project_path, 5);
    const auto snapshot_path = store.validSnapshotsForProject(project_path).front().path;
    std::filesystem::last_write_time(snapshot_path,
        std::filesystem::last_write_time(project_path) + std::chrono::seconds(2));

    MainWindow window(nullptr, root / "recovery", "saved-project-ui-session");
    createComposition(window, 200, 100, 2);
    bool cancel_restore_handled = false;
    QTimer::singleShot(0, [&] {
        chooseDocumentFile(project_path, QDialogButtonBox::Open);
        chooseRecoveryActionOnNextDialog("motion-recovery-choice-restore", 0,
            &cancel_restore_handled, QMessageBox::Cancel);
    });
    action(window, "motion-open-composition-action")->trigger();
    require(cancel_restore_handled &&
                window.compositionDocument()->canvasSize() == motion::model::CanvasSize{200, 100} &&
                window.isWindowModified() && std::filesystem::is_regular_file(snapshot_path),
            "cancelling recovery preserves the open document and its source snapshot");

    bool restore_handled = false;
    QTimer::singleShot(0, [&] {
        chooseDocumentFile(project_path, QDialogButtonBox::Open);
        chooseRecoveryActionOnNextDialog("motion-recovery-choice-restore", 0,
            &restore_handled, QMessageBox::Discard);
    });
    action(window, "motion-open-composition-action")->trigger();
    require(restore_handled && window.compositionDocument() != nullptr && window.isWindowModified() &&
                window.compositionDocument()->layers().front().name == "Recovered version" &&
                window.windowTitle().contains(QStringLiteral("saved.motion")),
            "Open offers and restores a newer saved-project snapshot as dirty");
    action(window, "motion-save-composition-action")->trigger();
    require(!window.isWindowModified() &&
                motion::persistence::MotionDocumentStore::load(project_path)
                    .layers.front().name == "Recovered version" &&
                std::filesystem::is_regular_file(snapshot_path),
            "restored saved projects keep their Save target and recovery history after Save");

    auto* rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    sendMouseClick(rows, QPoint(15, 15));
    require(window.isWindowModified(),
            "editing the restored saved project marks it dirty again");
    auto* timer = findWidget<QTimer>(&window, "motion-autosave-timer");
    require(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection),
            "the saved-project autosave timer can be triggered deterministically");
    const auto project_snapshots = store.validSnapshotsForProject(project_path);
    require(project_snapshots.size() >= 2 &&
                std::any_of(project_snapshots.begin(), project_snapshots.end(), [](const auto& snapshot) {
                    return !motion::persistence::MotionDocumentStore::loadRecovery(
                        snapshot.path).document.layers.front().visible;
                }),
            "dirty saved projects write recovery snapshots beside the document");
}

void answerMessageBoxWhenShown(QMessageBox::StandardButton button, int attempts = 0)
{
    QTimer::singleShot(5, [button, attempts] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt != nullptr) {
            prompt->button(button)->click();
            return;
        }
        require(attempts < 1000, "expected document decision or error dialog appears");
        answerMessageBoxWhenShown(button, attempts + 1);
    });
}

void acceptCompositionThenResolvePrompt(
    MainWindow& window,
    int width,
    int height,
    int frame_rate_index,
    QMessageBox::StandardButton decision,
    const std::optional<std::filesystem::path>& save_path = std::nullopt)
{
    QTimer::singleShot(0, [&window, width, height, frame_rate_index, decision, save_path] {
        completeCompositionDialog(width, height, frame_rate_index);
        QTimer::singleShot(0, [decision, save_path] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(prompt != nullptr, "replacing a dirty composition requests a decision");
            if (decision == QMessageBox::Save && save_path.has_value()) {
                QTimer::singleShot(0, [save_path] {
                    chooseDocumentFile(*save_path, QDialogButtonBox::Save);
                });
            }
            prompt->button(decision)->click();
        });
    });
    action(window, "motion-new-composition-action")->trigger();
}

void testMotionDocumentSaveOpen()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary document directory is available");
    const auto root = pathFromQString(temporary.path());
    const auto saved_as_path = root / "first.motion";
    const auto replacement_save_path = root / "replacement.motion";
    const auto load_path = root / "loaded.motion";
    const auto missing_media_path = root / "missing-image.png";
    const auto missing_media_document_path = root / "offline.motion";
    const auto corrupt_path = root / "corrupt.motion";

    motion::model::MotionProjectData second_document;
    second_document.composition = {{320, 200}, {24, 1}};
    motion::model::CompositionDocument opened_document(320, 200, {24, 1});
    (void)opened_document.addLayer(motion::model::LayerKind::Shape, "Open history fixture");
    second_document.layers = opened_document.layers();
    motion::persistence::MotionDocumentStore::save(load_path, second_document);
    motion::model::MotionProjectData offline_document;
    offline_document.composition = {{800, 450}, {30000, 1001}};
    offline_document.media.push_back({
        missing_media_path, creative_suite::media::MediaKind::Image,
        "Missing source", "Unsorted"});
    motion::persistence::MotionDocumentStore::save(
        missing_media_document_path, offline_document);
    {
        QFile corrupt(pathToQString(corrupt_path));
        require(corrupt.open(QIODevice::WriteOnly), "corrupt fixture opens");
        require(corrupt.write("not-json") == 8, "corrupt fixture is written");
    }

    MainWindow window(nullptr, root / "recovery", "document-tests");
    window.show();
    QTimer::singleShot(0, [] { completeCompositionDialog(640, 360, 2); });
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument() != nullptr && window.isWindowModified(),
            "an unsaved new composition is marked dirty");

    QTimer::singleShot(0, [&] {
        chooseDocumentFile(saved_as_path, QDialogButtonBox::Save);
    });
    action(window, "motion-save-composition-as-action")->trigger();
    require(std::filesystem::is_regular_file(saved_as_path) && !window.isWindowModified(),
            "Save As writes a native document and clears its dirty state");
    require(motion::persistence::MotionDocumentStore::load(saved_as_path).composition.canvas_size ==
                motion::model::CanvasSize{640, 360},
            "the saved document contains the current composition settings");

    QTimer::singleShot(0, [&] { chooseDocumentFile(load_path, QDialogButtonBox::Open); });
    action(window, "motion-open-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{320, 200} &&
                window.compositionDocument()->frameRate() == motion::model::FrameRate{24, 1} &&
                !window.isWindowModified(),
            "Open applies a staged document and starts with a clean state");
    auto* undo_action = action(window, "motion-undo-action");
    auto* redo_action = action(window, "motion-redo-action");
    require(!undo_action->isEnabled() && !redo_action->isEnabled(),
            "successfully opening a document clears prior history");
    auto* timeline = findWidget<motion::ui::TimelineNavigator>(&window, "motion-timeline");
    auto* display_mode = findWidget<QComboBox>(&window, "motion-timeline-display-mode");
    require(timeline->currentFrame() == 0 && timeline->zoomFactor() == 1.0 &&
                display_mode->currentIndex() ==
                    static_cast<int>(motion::ui::TimelineDisplayMode::Time),
            "Open resets navigation-only UI state");
    auto* rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    sendMouseClick(rows, QPoint(15, 15));
    require(undo_action->isEnabled() && window.isWindowModified(),
            "a composition visibility edit becomes undoable and dirty");

    QTimer::singleShot(0, [] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr, "Open Composition picker opens before cancellation");
        file_dialog->reject();
    });
    action(window, "motion-open-composition-action")->trigger();
    require(window.compositionDocument()->layers().front().visible == false &&
                window.isWindowModified() && undo_action->isEnabled(),
            "cancelling Open preserves both the edited composition and its history");

    QTimer::singleShot(0, [&] { chooseDocumentFile(corrupt_path, QDialogButtonBox::Open); });
    answerMessageBoxWhenShown(QMessageBox::Ok);
    action(window, "motion-open-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{320, 200} &&
                window.isWindowModified() && undo_action->isEnabled(),
            "failed Open preserves the current document and its undo history");
    undo_action->trigger();
    require(window.compositionDocument()->layers().front().visible &&
                !window.isWindowModified() && redo_action->isEnabled(),
            "Undo restores the saved state and clears the dirty marker");

    createComposition(window, 400, 300, 2);
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{400, 300} &&
                window.isWindowModified(),
            "a clean document can be replaced and the new document becomes unsaved");
    QTimer::singleShot(0, [] {
        auto* file_dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        require(file_dialog != nullptr, "Open Composition picker is available to cancel");
        file_dialog->reject();
    });
    action(window, "motion-open-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{400, 300} &&
                window.isWindowModified(),
            "cancelling Open leaves an unsaved current composition intact");
    acceptCompositionThenResolvePrompt(window, 800, 600, 4, QMessageBox::Cancel);
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{400, 300} &&
                window.isWindowModified(),
            "Cancel on a replacement prompt preserves the current document");

    acceptCompositionThenResolvePrompt(
        window, 800, 600, 4, QMessageBox::Save, replacement_save_path);
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{800, 600} &&
                window.isWindowModified() &&
                motion::persistence::MotionDocumentStore::load(replacement_save_path)
                    .composition.canvas_size == motion::model::CanvasSize{400, 300},
            "Save in the replacement prompt saves the old document before replacing it");
    acceptCompositionThenResolvePrompt(window, 1000, 500, 4, QMessageBox::Discard);
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{1000, 500},
            "Discard replaces the dirty document without changing the saved file");

    QTimer::singleShot(0, [&] { chooseDocumentFile(missing_media_document_path,
                                                   QDialogButtonBox::Open); });
    answerMessageBoxWhenShown(QMessageBox::Discard);
    action(window, "motion-open-composition-action")->trigger();
    auto* pool = window.mediaPoolWidget();
    require(waitFor([&] {
        return pool->library().size() == 1 && pool->library().items().front().offline;
    }), "missing external media is restored as an offline Media Pool entry");
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{800, 450} &&
                !window.isWindowModified(),
            "offline references do not prevent a valid composition from opening");
    auto* offline_item = pool->mediaListWidget()->item(0);
    require(offline_item != nullptr, "the offline source is visible in the Media Pool");
    offline_item->setText(QStringLiteral("Missing source renamed"));
    require(window.isWindowModified(),
            "renaming an offline Media Pool entry marks the document dirty");
    action(window, "motion-save-composition-action")->trigger();
    require(!window.isWindowModified() &&
                motion::persistence::MotionDocumentStore::load(missing_media_document_path)
                    .media.front().display_name == "Missing source renamed",
            "Save updates the current file with Media Pool changes");

    createComposition(window, 640, 480, 2);

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "closing a dirty document offers Save, Discard, and Cancel");
        prompt->button(QMessageBox::Cancel)->click();
    });
    require(!window.close(), "Cancel keeps the window open");
    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "close can be retried after cancellation");
        prompt->button(QMessageBox::Discard)->click();
    });
    require(window.close(), "Discard closes the window without saving");
}

void testNativeTextAndShapeLayers()
{
    QTemporaryDir recovery_directory;
    require(recovery_directory.isValid(), "native-content recovery directory is available");
    MainWindow window(nullptr, pathFromQString(recovery_directory.path()), "content-layers");
    QTimer::singleShot(0, [] { completeCompositionDialog(640, 360, 4); });
    action(window, "motion-new-composition-action")->trigger();
    auto* timeline = findWidget<motion::ui::TimelineNavigator>(
        &window, "motion-timeline");
    timeline->setCurrentFrame(47);

    auto* new_text = action(window, "motion-new-text-layer-action");
    auto* new_rectangle = action(window, "motion-new-rectangle-layer-action");
    auto* new_ellipse = action(window, "motion-new-ellipse-layer-action");
    require(new_text->isEnabled() && new_rectangle->isEnabled() && new_ellipse->isEnabled(),
            "native content layer actions enable when a composition exists");
    new_text->trigger();
    require(window.compositionDocument()->layers().size() == 1,
            "New Text inserts one native layer");
    const auto text_layer = window.compositionDocument()->layers().front();
    const auto text_content = std::get<motion::model::TextLayerContent>(text_layer.content);
    require(text_layer.kind == motion::model::LayerKind::Text &&
                text_layer.timeline_start_frame == 47 && text_layer.duration_frames == 150 &&
                text_content == motion::model::defaultTextLayerContent({640, 360}) &&
                text_content.text == "Text" && text_content.font_size_pixels == 48 &&
                text_content.color == motion::model::ColorRgba{255, 255, 255, 255},
            "new text uses the selected frame, centered default box, and documented appearance");
    auto* inspector_tabs = findWidget<QTabWidget>(&window, "motion-inspector-tabs");
    auto* layer_inspector = findWidget<QWidget>(&window, "motion-layer-content-inspector");
    require(inspector_tabs->currentWidget() == layer_inspector,
            "creating text opens its content inspector");

    auto* text_field = findWidget<QTextEdit>(&window, "motion-text-content");
    auto* text_size = findWidget<QSpinBox>(&window, "motion-text-font-size");
    auto* text_alignment = findWidget<QComboBox>(&window, "motion-text-alignment");
    auto* text_box_width = findWidget<QSpinBox>(&window, "motion-text-box-width");
    auto* text_box_height = findWidget<QSpinBox>(&window, "motion-text-box-height");
    text_field->setPlainText(QStringLiteral("Hello\nMotion Studio"));
    text_size->setValue(72);
    text_alignment->setCurrentIndex(text_alignment->findData(
        static_cast<int>(motion::model::TextAlignment::Right)));
    text_box_width->setValue(500);
    text_box_height->setValue(200);
    QFocusEvent text_focus_out(QEvent::FocusOut);
    QApplication::sendEvent(text_field, &text_focus_out);
    const auto edited_text = std::get<motion::model::TextLayerContent>(
        window.compositionDocument()->layers().front().content);
    require(edited_text.text == "Hello\nMotion Studio" && edited_text.font_size_pixels == 72 &&
                edited_text.alignment == motion::model::TextAlignment::Right &&
                edited_text.box_width == 500 && edited_text.box_height == 200 &&
                action(window, "motion-undo-action")->isEnabled() && window.isWindowModified(),
            "text, size, alignment, and box edits update dirty state and grouped undo history");
    action(window, "motion-undo-action")->trigger();
    require(std::get<motion::model::TextLayerContent>(
                window.compositionDocument()->layers().front().content) == text_content,
            "one Undo restores the text and size fields edited in the same interaction");
    action(window, "motion-redo-action")->trigger();
    require(std::get<motion::model::TextLayerContent>(
                window.compositionDocument()->layers().front().content) == edited_text,
            "Redo restores text layer content edits");

    auto* viewer_widget = findWidget<QWidget>(&window, "motion-composition-viewer");
    auto* viewer = dynamic_cast<motion::ui::CompositionViewer*>(viewer_widget);
    require(viewer != nullptr, "composition viewer exists for native-layer preview checks");
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        return frame != nullptr && frameHasVisibleRgb(*frame);
    }), "native text content renders in the asynchronous composition preview");
    const auto text_preview = viewer->renderedFrame();

    new_rectangle->trigger();
    require(window.compositionDocument()->layers().size() == 2 &&
                std::get<motion::model::ShapeLayerContent>(
                    window.compositionDocument()->layers().back().content).shape ==
                    motion::model::ShapeKind::Rectangle,
            "New Rectangle creates a native rectangle layer");

    new_ellipse->trigger();
    require(window.compositionDocument()->layers().size() == 3 &&
                window.compositionDocument()->layers().back().kind ==
                    motion::model::LayerKind::Shape,
            "New Ellipse inserts an independent shape layer");
    const auto ellipse_layer_id = window.compositionDocument()->layers().back().id;
    auto ellipse = std::get<motion::model::ShapeLayerContent>(
        window.compositionDocument()->layers().back().content);
    require(ellipse.shape == motion::model::ShapeKind::Ellipse &&
                ellipse.width == 160 && ellipse.height == 90 &&
                ellipse.fill_color == motion::model::ColorRgba{255, 183, 54, 255} &&
                ellipse.stroke_width_pixels == 0 &&
                window.compositionDocument()->layers().back().timeline_start_frame == 47 &&
                window.compositionDocument()->layers().back().duration_frames == 150,
            "new ellipses use quarter-canvas dimensions, accent fill, and five-second timing");
    require(inspector_tabs->currentWidget() == layer_inspector,
            "creating a shape opens its content inspector");
    require(waitFor([&] {
        return viewer->renderedFrame() != nullptr && viewer->renderedFrame() != text_preview;
    }), "native shape insertion refreshes the asynchronous preview");
    const auto ellipse_preview = viewer->renderedFrame();

    auto* shape_fill = findWidget<QPushButton>(&window, "motion-shape-fill-color");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "shape color action opens the color picker");
        dialog->setCurrentColor(QColor(30, 80, 190, 128));
        dialog->accept();
    });
    shape_fill->click();
    ellipse = std::get<motion::model::ShapeLayerContent>(
        window.compositionDocument()->layers().back().content);
    require(ellipse.fill_color == motion::model::ColorRgba{30, 80, 190, 128},
            "shape fill inspector stores RGBA color including alpha");
    require(waitFor([&] {
        return viewer->renderedFrame() != nullptr && viewer->renderedFrame() != ellipse_preview;
    }), "shape color edits refresh the composed preview");
    const auto colored_ellipse_preview = viewer->renderedFrame();
    action(window, "motion-undo-action")->trigger();
    require(std::get<motion::model::ShapeLayerContent>(
                window.compositionDocument()->layers().back().content).fill_color ==
                motion::model::ColorRgba{255, 183, 54, 255},
            "Undo restores the previous shape fill color");
    action(window, "motion-redo-action")->trigger();

    auto* shape_width = findWidget<QSpinBox>(&window, "motion-shape-width");
    shape_width->setValue(220);
    (void)QMetaObject::invokeMethod(shape_width, "editingFinished", Qt::DirectConnection);
    require(std::get<motion::model::ShapeLayerContent>(
                window.compositionDocument()->layers().back().content).width == 220,
            "shape inspector edits intrinsic geometry");
    action(window, "motion-undo-action")->trigger();
    require(std::get<motion::model::ShapeLayerContent>(
                window.compositionDocument()->layers().back().content).width == 160,
            "Undo restores a grouped shape dimension edit");
    action(window, "motion-redo-action")->trigger();
    auto* shape_stroke_width = findWidget<QSpinBox>(&window, "motion-shape-stroke-width");
    shape_stroke_width->setValue(8);
    (void)QMetaObject::invokeMethod(shape_stroke_width, "editingFinished", Qt::DirectConnection);
    shape_width->setValue(4);
    const auto shape_after_rejected_edit = std::get<motion::model::ShapeLayerContent>(
        window.compositionDocument()->layers().back().content);
    require(shape_after_rejected_edit.width == 220 &&
                shape_after_rejected_edit.stroke_width_pixels == 8 &&
                shape_width->value() == 220,
            "an invalid shape width leaves the model intact and restores the inspector value");
    action(window, "motion-undo-action")->trigger();
    require(std::get<motion::model::ShapeLayerContent>(
                window.compositionDocument()->layers().back().content).stroke_width_pixels == 0,
            "a rejected inspector edit does not create an Undo step");
    action(window, "motion-redo-action")->trigger();
    const auto before_shape_resize_preview = viewer->renderedFrame();

    require(waitFor([&] {
        return viewer->renderedFrame() != nullptr &&
            viewer->renderedFrame() != before_shape_resize_preview &&
            viewer->renderedFrame() != colored_ellipse_preview;
    }), "native shape content refreshes the composed preview");
    require(window.compositionDocument()->layers().size() == 3 &&
                window.compositionDocument()->layers().back().id == ellipse_layer_id &&
                timeline->currentFrame() == 47,
            "native layer edits retain stable timeline rows and do not move the playhead");

    const auto* timeline_rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    require(timeline_rows != nullptr,
            "text and shape layers are represented in the existing timeline widget");
    auto* position_x_key = findWidget<QToolButton>(
        &window, "motion-transform-keyframe-position-x");
    position_x_key->click();
    require(window.compositionDocument()->layers().back().keyframes.position_x ==
                std::vector<creative_suite::animation::Keyframe>{{0, 0.5}},
            "shape layers participate in the existing transform keyframe system");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite Motion Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Motion Studio UI Tests"));
    QTemporaryDir settings_directory;
    require(settings_directory.isValid(), "temporary settings directory is available");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings_directory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       settings_directory.path());

    testMotionShortcutSettings();
    testAutosavePreferencesAndSnapshots();
    testRecoveryDialogFolderAction();
    testRecoveryRestoreAndIgnore();
    testSavedProjectRecoveryOnOpen();
    testMotionDocumentSaveOpen();
    testNativeTextAndShapeLayers();

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

    motion::model::CompositionLayer hierarchy_layer;
    hierarchy_layer.id = 7001;
    hierarchy_layer.name = "Hierarchy fixture";
    hierarchy_layer.duration_frames = 120;
    frame_rate_range_check.setLayers({hierarchy_layer});
    frame_rate_range_check.setLayerExpanded(hierarchy_layer.id, true);
    frame_rate_range_check.setTransformGroupExpanded(hierarchy_layer.id, true);
    auto* hierarchy_rows = findWidget<QWidget>(
        &frame_rate_range_check, "motion-timeline-layer-rows");
    const auto hierarchy_axis_left = frame_rate_range_check.frameToViewportX(
        frame_rate_range_check.viewStartFrame());
    require(hasBrightPixel(hierarchy_rows->grab().toImage(),
                           QRect(68, 68, hierarchy_axis_left - 68, 34), 100),
            "an expanded Transform group displays its property rows");
    frame_rate_range_check.setCompositionTiming({60, 1});
    const auto rows_after_timing_reset = hierarchy_rows->grab().toImage();
    require(!hasBrightPixel(rows_after_timing_reset,
                            QRect(66, 34, hierarchy_axis_left - 66, 34), 100) &&
                !hasBrightPixel(rows_after_timing_reset,
                                QRect(68, 68, hierarchy_axis_left - 68, 34), 100),
            "resetting composition timing collapses layers and Transform groups");
    frame_rate_range_check.setLayers({});

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
    require(motion::ui::detail::framesElapsedForNanoseconds(1'000'000'000, 24, 1) == 24 &&
                motion::ui::detail::framesElapsedForNanoseconds(
                    1'001'000'000, 30000, 1001) == 30 &&
                motion::ui::detail::framesElapsedForNanoseconds(
                    std::numeric_limits<std::int64_t>::max(), 120, 1) > 0,
            "the playback clock maps monotonic nanoseconds to exact integer and fractional frames");
    require(motion::ui::detail::loopFrameForElapsed(5, 5, 10) == 0 &&
                motion::ui::detail::loopFrameForElapsed(7, 5, 10) == 2 &&
                motion::ui::detail::extendRangeEndToInclude(9, 20, 10) == 29 &&
                motion::ui::detail::extendRangeEndToInclude(59, maximum_frame, 60) ==
                    maximum_frame,
            "loop position and playback range extension stay bounded without overflow");
    frame_rate_range_check.hide();

    motion::ui::TimelineNavigator playback_check;
    playback_check.resize(1000, 280);
    playback_check.show();
    playback_check.setCompositionTiming({60, 1});
    auto* playback_button = findWidget<QPushButton>(
        &playback_check, "motion-timeline-play-pause");
    auto* loop_button = findWidget<QPushButton>(
        &playback_check, "motion-timeline-loop");
    require(!playback_button->isEnabled() && !loop_button->isEnabled() &&
                !loop_button->isChecked(),
            "playback is unavailable without timeline layers and Loop defaults off");
    motion::model::CompositionLayer playback_layer{};
    playback_layer.id = 1;
    playback_layer.kind = motion::model::LayerKind::Video;
    playback_layer.name = "Playback fixture";
    playback_layer.duration_frames = 8;
    playback_check.setLayers({playback_layer});
    int loop_wrap_count = 0;
    QObject::connect(
        &playback_check,
        &motion::ui::TimelineNavigator::currentFrameChanged,
        &playback_check,
        [&loop_wrap_count](qint64 frame) {
            if (frame == 0) ++loop_wrap_count;
        });
    require(playback_button->isEnabled() && loop_button->isEnabled(),
            "adding a timed visual layer enables Play and Loop");

    playback_check.setCurrentFrame(1);
    playback_button->click();
    require(playback_check.isPlaying() &&
                playback_button->text() == QStringLiteral("Pause") &&
                playback_check.currentFrame() == 1,
            "Play starts from the current playhead and changes to Pause");
    require(waitFor([&] { return playback_check.currentFrame() >= 3; }, 250),
            "the exact-rate playback clock advances the playhead");
    playback_button->click();
    require(!playback_check.isPlaying() &&
                playback_button->text() == QStringLiteral("Play"),
            "Pause freezes playback and restores the Play label");
    const auto paused_frame = playback_check.currentFrame();
    QThread::msleep(30);
    application.processEvents();
    require(playback_check.currentFrame() == paused_frame,
            "the playhead remains fixed while paused");

    playback_button->click();
    require(waitFor([&] { return playback_check.currentFrame() > paused_frame; }, 200),
            "resuming advances from the paused frame");
    if (playback_check.isPlaying()) playback_button->click();

    playback_button->click();
    playback_check.setCurrentFrame(2);
    require(!playback_check.isPlaying() && playback_check.currentFrame() == 2,
            "manual seeking pauses playback at the requested frame");
    playback_check.setCurrentFrame(0);
    playback_button->click();
    require(waitFor([&] {
        return !playback_check.isPlaying() && playback_check.currentFrame() == 7;
    }, 250), "non-looping playback stops on the last frame of the furthest layer");
    playback_button->click();
    require(playback_check.isPlaying() && playback_check.currentFrame() == 0,
            "Play at the end restarts from frame zero");
    loop_button->setChecked(true);
    require(playback_check.isLoopEnabled(), "the Loop control enables wraparound");
    require(waitFor([&] {
        return playback_check.isPlaying() && playback_check.currentFrame() >= 5;
    }, 250), "looping playback reaches the end of its short test range");
    const auto wraps_before_restart = loop_wrap_count;
    require(waitFor([&] {
        return playback_check.isPlaying() && loop_wrap_count > wraps_before_restart;
    }, 250), "looping playback wraps to frame zero and continues");
    loop_button->setChecked(false);
    require(waitFor([&] {
        return !playback_check.isPlaying() && playback_check.currentFrame() == 7;
    }, 250), "turning Loop off makes playback stop on the final layer frame");
    playback_check.setCompositionTiming({60, 1});
    playback_check.setLayers({});
    require(!playback_check.isPlaying() && !loop_button->isChecked() &&
                !playback_button->isEnabled(),
            "new composition timing stops playback, clears Loop, and disables Play without layers");
    playback_check.hide();

    motion::ui::TimelineNavigator pause_preview_check;
    pause_preview_check.resize(1000, 280);
    pause_preview_check.show();
    pause_preview_check.setCompositionTiming({60, 1});
    motion::model::CompositionLayer pause_preview_layer{};
    pause_preview_layer.id = 3;
    pause_preview_layer.kind = motion::model::LayerKind::Image;
    pause_preview_layer.name = "Pause preview fixture";
    pause_preview_layer.duration_frames = 10;
    pause_preview_check.setLayers({pause_preview_layer});
    int pause_preview_requests = 0;
    QObject::connect(
        &pause_preview_check,
        &motion::ui::TimelineNavigator::currentFrameChanged,
        &pause_preview_check,
        [&pause_preview_requests](qint64) { ++pause_preview_requests; });
    auto* pause_preview_button = findWidget<QPushButton>(
        &pause_preview_check, "motion-timeline-play-pause");
    pause_preview_button->click();
    pause_preview_button->click();
    require(!pause_preview_check.isPlaying() && pause_preview_check.currentFrame() == 0 &&
                pause_preview_requests == 1,
            "pausing requests a fresh preview even when the playhead did not advance");
    pause_preview_check.hide();

    motion::ui::TimelineNavigator extended_playback_check;
    extended_playback_check.resize(1000, 280);
    extended_playback_check.show();
    extended_playback_check.setCompositionTiming({240, 1});
    const auto playback_hour_frames = 240 * 60 * 60;
    const auto playback_initial_end = extended_playback_check.visibleEndFrame();
    motion::model::CompositionLayer long_playback_layer{};
    long_playback_layer.id = 2;
    long_playback_layer.kind = motion::model::LayerKind::Video;
    long_playback_layer.name = "Long playback fixture";
    long_playback_layer.duration_frames = playback_hour_frames + 100;
    extended_playback_check.setLayers({long_playback_layer});
    extended_playback_check.setCurrentFrame(playback_initial_end);
    findWidget<QPushButton>(&extended_playback_check, "motion-timeline-play-pause")->click();
    require(waitFor([&] {
        return extended_playback_check.currentFrame() > playback_initial_end;
    }, 250), "playback advances beyond the initial navigation range");
    require(extended_playback_check.visibleEndFrame() ==
                playback_initial_end + playback_hour_frames &&
                extended_playback_check.viewStartFrame() > 0,
            "playback extends the navigation range by an hour and follows the playhead");
    findWidget<QPushButton>(&extended_playback_check, "motion-timeline-play-pause")->click();
    extended_playback_check.hide();

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

    MainWindow window(nullptr, pathFromQString(temporary.path()) / "recovery",
                      "timeline-ui-tests");
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
    auto* undo_action = action(window, "motion-undo-action");
    auto* redo_action = action(window, "motion-redo-action");
    require(!undo_action->isEnabled() && !redo_action->isEnabled(),
            "a new composition begins with empty Undo and Redo history");
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
        require(file_dialog != nullptr && !file_dialog->testOption(QFileDialog::DontUseNativeDialog)
                    && file_dialog->fileMode() == QFileDialog::ExistingFiles,
                "media import uses the platform picker and allows multiple files");
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

    const auto undo_baseline_path = pathFromQString(temporary.path()) / "undo-baseline.motion";
    QTimer::singleShot(0, [&undo_baseline_path] {
        chooseDocumentFile(undo_baseline_path, QDialogButtonBox::Save);
    });
    action(window, "motion-save-composition-as-action")->trigger();
    require(!window.isWindowModified(),
            "the composition and Media Pool baseline can be saved before history checks");

    auto* layer_rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    require(layer_rows->acceptDrops(), "timeline layer rows accept Media Pool drops");
    const auto image_catalog_path = image_entry->metadata.source_path;
    const auto video_catalog_path = video_entry->metadata.source_path;
    auto* previous_frame_action = action(window, "motion-previous-frame-action");
    auto* next_frame_action = action(window, "motion-next-frame-action");
    auto* play_pause_action = action(window, "motion-play-pause-action");
    auto* loop_action = action(window, "motion-loop-action");
    timeline->setCurrentFrame(0);
    deliverMediaDrop(layer_rows, image_catalog_path, QPoint(205, 15), true);
    require(window.compositionDocument()->layers().size() == 1 &&
                window.compositionDocument()->layers().front().kind == motion::model::LayerKind::Image &&
                window.compositionDocument()->layers().front().timeline_start_frame == 0 &&
                window.compositionDocument()->layers().front().duration_frames == 120,
            "dropping a still onto empty timeline space creates a five-second layer at frame zero");
    const auto inserted_image_id = window.compositionDocument()->layers().front().id;
    auto* transform_inspector = findWidget<QWidget>(&window, "motion-transform-inspector");
    require(undo_action->isEnabled() && !redo_action->isEnabled() && window.isWindowModified(),
            "inserting a layer creates an undo step and marks the composition dirty");
    undo_action->trigger();
    require(window.compositionDocument()->layers().empty() && !undo_action->isEnabled() &&
                redo_action->isEnabled() && !window.isWindowModified() &&
                pool->library().size() == 2 && !transform_inspector->isEnabled(),
            "Undo removes only the timeline layer, preserves the Media Pool, and returns to the saved state");
    redo_action->trigger();
    require(window.compositionDocument()->layers().size() == 1 &&
                window.compositionDocument()->layers().front().id == inserted_image_id &&
                undo_action->isEnabled() && !redo_action->isEnabled() &&
                window.isWindowModified() && transform_inspector->isEnabled(),
            "Redo restores the stable layer ID and marks the document dirty again");
    action(window, "motion-save-composition-action")->trigger();
    require(!window.isWindowModified() && undo_action->isEnabled(),
            "saving a document does not clear its undo history");
    undo_action->trigger();
    require(window.compositionDocument()->layers().empty() && window.isWindowModified() &&
                redo_action->isEnabled(),
            "undoing past a saved state marks the saved document dirty");
    redo_action->trigger();
    require(window.compositionDocument()->layers().front().id == inserted_image_id &&
                !window.isWindowModified(),
            "redo returns exactly to the saved composition state");
    require(play_pause_action->isEnabled() && loop_action->isEnabled() &&
                !previous_frame_action->isEnabled() && next_frame_action->isEnabled(),
            "timeline commands update availability when a layer is added");
    next_frame_action->trigger();
    require(timeline->currentFrame() == 1 && previous_frame_action->isEnabled(),
            "the registered Next frame shortcut action advances the playhead");
    previous_frame_action->trigger();
    require(timeline->currentFrame() == 0 && !previous_frame_action->isEnabled(),
            "the registered Previous frame shortcut action returns to frame zero");
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
    position_x->setValue(0.75);
    (void)QMetaObject::invokeMethod(position_x, "editingFinished", Qt::DirectConnection);
    require(window.compositionDocument()->layers().front().transform.position_x == 0.75,
            "the transform inspector edits the selected layer base transform");
    undo_action->trigger();
    require(window.compositionDocument()->layers().front().transform.position_x == 0.5 &&
                redo_action->isEnabled(),
            "Undo treats successive values in one inspector interaction as one step");
    redo_action->trigger();
    require(window.compositionDocument()->layers().front().transform.position_x == 0.75,
            "Redo restores the final grouped transform value");

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
    auto* main_play_pause = findWidget<QPushButton>(&window, "motion-timeline-play-pause");
    auto* main_loop_button = findWidget<QPushButton>(&window, "motion-timeline-loop");
    require(!main_loop_button->isChecked() &&
                window.compositionDocument()->layers().back().duration_frames > 2,
            "the imported video provides multiple frames and Loop begins disabled");
    const auto playback_layers_before = window.compositionDocument()->layers();
    main_play_pause->click();
    require(waitFor([&] {
        return timeline->currentFrame() > 120 &&
               viewer->renderedFrame() != video_preview_frame;
    }, 1500), "continuous playback decodes and presents advancing video frames");
    main_play_pause->click();
    const auto playback_layers_after = window.compositionDocument()->layers();
    require(!timeline->isPlaying() &&
                playback_layers_before.size() == playback_layers_after.size() &&
                std::equal(playback_layers_before.begin(), playback_layers_before.end(),
                    playback_layers_after.begin(), [](const auto& before, const auto& after) {
                        return before.id == after.id &&
                               before.timeline_start_frame == after.timeline_start_frame &&
                               before.duration_frames == after.duration_frames &&
                               before.transform == after.transform &&
                               before.keyframes == after.keyframes;
                    }),
            "playing and pausing leaves the document's layer timing and transforms unchanged");
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
    if (!document->layers().back().visible) sendMouseClick(layer_rows, QPoint(15, 15));
    sendMouseClick(layer_rows, QPoint(60, 15));
    require(document->layers().front().keyframes ==
                creative_suite::animation::TransformKeyframes{},
            "a new media layer starts without transform keyframes");

    const auto animated_layer_id = document->layers().front().id;
    const auto animation_start = document->layers().front().timeline_start_frame;
    auto* animated_position_x = findWidget<QDoubleSpinBox>(
        &window, "motion-transform-position-x");
    auto* position_x_key_button = findWidget<QToolButton>(
        &window, "motion-transform-keyframe-position-x");
    auto* animated_opacity = findWidget<QDoubleSpinBox>(
        &window, "motion-transform-opacity");
    auto* opacity_key_button = findWidget<QToolButton>(
        &window, "motion-transform-keyframe-opacity");
    auto* animated_rows = findWidget<QWidget>(&window, "motion-timeline-layer-rows");
    timeline->setCurrentFrame(animation_start);
    const auto axis_left = timeline->frameToViewportX(timeline->viewStartFrame());
    const auto collapsed_rows = animated_rows->grab().toImage();
    require(!hasBrightPixel(collapsed_rows, QRect(50, 0, axis_left - 50, 34), 100),
            "the collapsed layer header keeps its name out of the left column");
    require(!hasBrightPixel(collapsed_rows,
                            QRect(66, 34, axis_left - 66, 34), 100),
            "a new layer starts with its Transform group collapsed");
    sendMouseClick(animated_rows, QPoint(39, 15));
    const auto layer_expanded_rows = animated_rows->grab().toImage();
    require(hasBrightPixel(layer_expanded_rows,
                           QRect(66, 34, axis_left - 66, 34), 100) &&
                !hasBrightPixel(layer_expanded_rows,
                                QRect(68, 68, axis_left - 68, 34), 100),
            "expanding a layer reveals only its Transform group");
    sendMouseClick(animated_rows, QPoint(55, 51));
    const auto transform_expanded_rows = animated_rows->grab().toImage();
    bool five_transform_properties_visible = true;
    for (int property_index = 0; property_index < 5; ++property_index) {
        five_transform_properties_visible = five_transform_properties_visible &&
            hasBrightPixel(transform_expanded_rows,
                QRect(68, 68 + property_index * 34, axis_left - 68, 34), 100);
    }
    require(five_transform_properties_visible,
            "expanding Transform reveals all five transform property tracks");
    sendMouseClick(animated_rows, QPoint(55, 51));
    require(hasBrightPixel(animated_rows->grab().toImage(),
                            QRect(66, 34, axis_left - 66, 34), 100),
            "collapsing Transform keeps the group row while hiding its property tracks");
    sendMouseClick(animated_rows, QPoint(39, 15));
    require(!hasBrightPixel(animated_rows->grab().toImage(),
                            QRect(50, 0, axis_left - 50, 34), 100),
            "collapsing a layer hides its Transform group and property rows");

    const auto base_position_x = document->layers().front().transform.position_x;
    position_x_key_button->click();
    auto position_keys = creative_suite::animation::keyframesFor(
        document->layers().front().keyframes,
        creative_suite::animation::TransformProperty::PositionX);
    const auto key_added_rows = animated_rows->grab().toImage();
    bool key_add_expanded_transform =
        hasBrightPixel(key_added_rows, QRect(66, 34, axis_left - 66, 34), 100) &&
        hasBrightPixel(key_added_rows, QRect(68, 68, axis_left - 68, 34), 100);
    require(position_keys == std::vector<creative_suite::animation::Keyframe>{{0, base_position_x}} &&
                key_add_expanded_transform,
            "adding a transform key stores its value and reveals its property track");
    undo_action->trigger();
    require(document->layers().front().keyframes.position_x.empty() &&
                redo_action->isEnabled(),
            "Undo removes a newly inserted transform key");
    redo_action->trigger();
    require(document->layers().front().keyframes.position_x ==
                std::vector<creative_suite::animation::Keyframe>{{0, base_position_x}},
            "Redo restores the inserted transform key");
    timeline->setCurrentFrame(animation_start + 10);
    require(animated_position_x->isReadOnly() &&
                std::abs(animated_position_x->value() - base_position_x) < 0.000001,
            "animated properties show the interpolated value read-only between keys");
    animated_position_x->setValue(0.9);
    require(std::abs(animated_position_x->value() - base_position_x) < 0.000001 &&
                document->layers().front().keyframes.position_x.size() == 1,
            "editing between keys does not create an implicit keyframe");
    position_x_key_button->click();
    require(!animated_position_x->isReadOnly(),
            "inserting a key at the playhead makes the property editable there");
    animated_position_x->setValue(0.9);
    require(document->layers().front().keyframes.position_x ==
                std::vector<creative_suite::animation::Keyframe>{{0, base_position_x}, {10, 0.9}},
            "editing at a key updates that key without changing the base transform");
    timeline->setCurrentFrame(animation_start + 5);
    const double expected_position_x = (base_position_x + 0.9) / 2.0;
    require(std::abs(animated_position_x->value() - expected_position_x) < 0.000001,
            "the inspector shows linear interpolation at an intermediate frame");
    const auto guide_at_intermediate = viewer->grab().toImage();
    timeline->setCurrentFrame(animation_start);
    require(viewer->grab().toImage() != guide_at_intermediate,
            "the selected-layer canvas guide follows its evaluated animated position");
    timeline->setCurrentFrame(animation_start + 5);
    position_x_key_button->click();
    position_x_key_button->click();
    require(document->layers().front().keyframes.position_x.size() == 2,
            "the diamond control adds and removes a key at the current frame");

    timeline->setCurrentFrame(animation_start);
    opacity_key_button->click();
    animated_opacity->setValue(0.0);
    auto preview_before_opacity = viewer->renderedFrame();
    require(waitFor([&] {
        return viewer->renderedFrame() != preview_before_opacity &&
               viewer->renderedFrame() != nullptr;
    }), "changing an opacity key requests an updated preview");
    const auto transparent_frame = viewer->renderedFrame();
    timeline->setCurrentFrame(animation_start + 10);
    opacity_key_button->click();
    animated_opacity->setValue(0.8);
    preview_before_opacity = viewer->renderedFrame();
    require(waitFor([&] {
        return viewer->renderedFrame() != preview_before_opacity &&
               viewer->renderedFrame() != nullptr;
    }), "a second opacity key is evaluated during preview");
    timeline->setCurrentFrame(animation_start + 5);
    const auto half_opacity_generation = viewer->renderedFrame();
    require(waitFor([&] { return viewer->renderedFrame() != half_opacity_generation; }),
            "seeking between opacity keys updates the composed frame");
    const auto half_opacity_frame = viewer->renderedFrame();
    const auto animation_center = static_cast<std::size_t>(
        180 * half_opacity_frame->stride + 320 * 4);
    require(transparent_frame != nullptr &&
                half_opacity_frame->rgba_pixels[animation_center] >
                    transparent_frame->rgba_pixels[animation_center] &&
                half_opacity_frame->rgba_pixels[animation_center] < 40,
            "preview rendering interpolates opacity values at the playhead");

    timeline_zoom_slider->setValue(21);
    timeline->setCurrentFrame(animation_start + 10);
    const auto clip_label_image = animated_rows->grab().toImage();
    const int clip_left = timeline->frameToViewportX(animation_start);
    const int clip_right = timeline->frameToViewportX(
        animation_start + document->layers().front().duration_frames);
    const int playhead_x = timeline->frameToViewportX(timeline->currentFrame());
    bool clip_label_visible = false;
    for (int y = 8; y < 27 && !clip_label_visible; ++y) {
        for (int x = clip_left + 8; x < clip_right - 8; ++x) {
            if (x != playhead_x && clip_label_image.pixelColor(x, y).lightness() > 210) {
                clip_label_visible = true;
                break;
            }
        }
    }
    require(!document->layers().front().name.empty() && clip_label_visible,
            "the layer name remains visible inside its timeline clip");
    const int key_lane_y = 85;
    const auto x_for_local_frame = [timeline, animation_start](std::int64_t local_frame) {
        return timeline->frameToViewportX(animation_start + local_frame);
    };
    sendMouseClick(layer_rows, QPoint(x_for_local_frame(10), key_lane_y));
    require(timeline->currentFrame() == animation_start + 10,
            "clicking a keyframe marker seeks the composition playhead");
    sendMouseDrag(layer_rows, QPoint(x_for_local_frame(0), key_lane_y),
                  QPoint(x_for_local_frame(15), key_lane_y));
    require(document->layers().front().keyframes.position_x ==
                std::vector<creative_suite::animation::Keyframe>{{10, 0.9},
                                                                  {15, base_position_x}},
            "dragging a marker moves its layer-local frame while preserving its value");
    const auto position_keys_before_collision = document->layers().front().keyframes.position_x;
    sendMouseDrag(layer_rows, QPoint(x_for_local_frame(10), key_lane_y),
                  QPoint(x_for_local_frame(15), key_lane_y));
    require(document->layers().front().keyframes.position_x == position_keys_before_collision,
            "dragging a key onto another key of the same property is rejected safely");

    timeline->setCurrentFrame(animation_start);
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        return frame != nullptr && frame->width == 640 && frame->height == 360;
    }), "the first animated frame is available before playback");
    const auto playback_start_frame = viewer->renderedFrame();
    const auto layers_before_animated_playback = document->layers();
    main_play_pause->click();
    require(waitFor([&] {
        const auto frame = viewer->renderedFrame();
        if (timeline->currentFrame() < animation_start + 10 || frame == nullptr ||
            frame == playback_start_frame) return false;
        for (std::size_t pixel = 0; pixel + 3 < frame->rgba_pixels.size(); pixel += 4) {
            if (frame->rgba_pixels[pixel] != 0 || frame->rgba_pixels[pixel + 1] != 0 ||
                frame->rgba_pixels[pixel + 2] != 0) return true;
        }
        return false;
    }, 1500), "playback presents a frame evaluated beyond the opacity key");
    main_play_pause->click();
    require(!timeline->isPlaying() &&
                layers_before_animated_playback.size() == document->layers().size() &&
                std::equal(layers_before_animated_playback.begin(),
                    layers_before_animated_playback.end(), document->layers().begin(),
                    [](const auto& before, const auto& after) {
                        return before.id == after.id &&
                               before.timeline_start_frame == after.timeline_start_frame &&
                               before.duration_frames == after.duration_frames &&
                               before.transform == after.transform &&
                               before.keyframes == after.keyframes;
                    }), "animated playback evaluates keys without changing the document");
    timeline->setLayerExpanded(animated_layer_id, false);
    timeline_zoom_slider->setValue(motion::ui::detail::kTimelineZoomDefaultIndex);
    const auto layer_before_zoom = document->layers().front();

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
            "ruler and layer viewport share frame mapping and seeking leaves stored keyframes unchanged");

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
        completeCompositionDialog(1920, 1080, 4);
        QTimer::singleShot(0, [] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            require(prompt != nullptr, "replacement prompt is shown after composition setup");
            prompt->button(QMessageBox::Discard)->click();
        });
    });
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{1920, 1080} &&
                window.compositionDocument()->frameRate() == motion::model::FrameRate{30000, 1001},
            "replacement creates a new composition with the selected exact frame rate");
    require(!undo_action->isEnabled() && !redo_action->isEnabled(),
            "successfully creating a replacement composition clears history");
    require(pool->library().empty() && media_list->count() == 0 && timeline->currentFrame() == 0 &&
                timeline_display_mode->currentIndex() == 0 &&
                timeline_position_readout->text() == QStringLiteral("00:00:00.000"),
            "replacing a composition clears its Media Pool, resets navigation, and restores Time display");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "closing the final unsaved composition requests a decision");
        prompt->button(QMessageBox::Discard)->click();
    });
    window.close();

    std::cout << "Motion Studio Media Pool UI tests passed.\n";
    return EXIT_SUCCESS;
}
