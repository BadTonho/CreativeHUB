#include "ui/main_window.h"
#include "ui/composition_viewer.h"
#include "ui/new_composition_dialog.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
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

void completeCompositionDialog(int width, int height)
{
    QWidget* modal = QApplication::activeModalWidget();
    require(modal != nullptr, "composition dialog is active");
    auto* width_edit = findWidget<QLineEdit>(modal, "motion-canvas-width");
    auto* height_edit = findWidget<QLineEdit>(modal, "motion-canvas-height");
    auto* buttons = findWidget<QDialogButtonBox>(modal, "motion-new-composition-buttons");
    width_edit->setText(QString::number(width));
    height_edit->setText(QString::number(height));
    require(buttons->button(QDialogButtonBox::Ok)->isEnabled(), "valid canvas dimensions enable Create");
    buttons->button(QDialogButtonBox::Ok)->click();
}

QAction* action(MainWindow& window, const char* object_name)
{
    return findWidget<QAction>(&window, object_name);
}

void createComposition(MainWindow& window, int width, int height)
{
    QTimer::singleShot(0, [width, height] { completeCompositionDialog(width, height); });
    action(window, "motion-new-composition-action")->trigger();
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
    auto* dialog_buttons = findWidget<QDialogButtonBox>(&dialog, "motion-new-composition-buttons");
    require(dialog_width->text().isEmpty() && dialog_height->text().isEmpty(),
        "new composition has no prefilled canvas dimensions");
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "Create is disabled until dimensions are entered");
    dialog_width->setText(QStringLiteral("0"));
    dialog_height->setText(QStringLiteral("720"));
    require(!dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "non-positive canvas dimensions do not enable Create");
    dialog_width->setText(QStringLiteral("1280"));
    require(dialog_buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "positive explicit canvas dimensions are accepted");
    dialog_buttons->button(QDialogButtonBox::Ok)->click();
    require(dialog.canvasSize() == motion::model::CanvasSize{1280, 720},
        "dialog returns the explicitly entered canvas size");

    motion::ui::MainWindow window;
    require(window.compositionDocument() == nullptr,
        "Motion Studio starts without creating a composition");
    createComposition(window, 640, 360);
    require(window.compositionDocument() != nullptr
            && window.compositionDocument()->canvasSize() == motion::model::CanvasSize{640, 360},
        "new composition is stored in the window");

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
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{640, 360}
            && window.compositionDocument()->layers().size() == 1,
        "declining replacement preserves the current in-memory composition");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(prompt != nullptr, "replacement confirmation is shown before a new composition");
        prompt->button(QMessageBox::Yes)->click();
        QTimer::singleShot(0, [] { completeCompositionDialog(1920, 1080); });
    });
    action(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument()->canvasSize() == motion::model::CanvasSize{1920, 1080}
            && window.compositionDocument()->layers().empty()
            && !window.selectedLayerId().has_value(),
        "confirmed replacement creates a clean composition with explicit dimensions");

    action(window, "motion-add-video-layer-action")->trigger();
    require(window.compositionDocument()->layers().size() == 1
            && window.compositionDocument()->layers().front().kind == motion::model::LayerKind::Video,
        "the video layer action creates the requested kind");

    std::cout << "Motion Studio editing UI tests passed.\n";
    return EXIT_SUCCESS;
}
