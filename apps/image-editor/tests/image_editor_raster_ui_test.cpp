#include "image_editor_window.h"
#include "image_canvas.h"
#include "image_import_controller.h"
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QLineF>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
#include <cmath>
using namespace image_editor;
bool testRasterImagesUi(const QString& root) {
    const auto check=[](bool ok,const char* cause) { if (!ok) std::cerr<<cause<<'\n'; return ok; };
    const QString background=root+"/raster-ui-background.png";
    const QString source=root+"/raster-ui-source.png";
    const QString doc=root+"/raster-ui.cimg", output=root+"/raster-ui.png";
    QImage bg(64,64,QImage::Format_ARGB32); bg.fill(Qt::transparent);
    QImage photo(32,32,QImage::Format_ARGB32); photo.fill(Qt::red);
    if (!bg.save(background) || !photo.save(source)) return false;
    const auto decoded = ImageImportController::run(nullptr, {source});
    if (!check(decoded.status==RasterImportStatus::Ready && decoded.images.size()==1 &&
        decoded.images.front().image.size()==photo.size() &&
        decoded.images.front().image.pixelColor(0,0)==Qt::red &&
        decoded.failed_path.isEmpty(),
        "Import controller did not return the decoded raster.")) return false;
    const auto decode_failure = ImageImportController::run(
        nullptr, {root+"/missing-import-controller-image.png"});
    if (!check(decode_failure.status==RasterImportStatus::Failed &&
        decode_failure.images.isEmpty() && !decode_failure.cause.isEmpty() &&
        !decode_failure.failed_path.isEmpty(),
        "Import controller did not return the decoder error.")) return false;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName()=="imageImportProgressDialog")
            return check(false,"Import controller left its progress dialog open.");
    }
    ImageDocumentSession fixture;
    if (!fixture.openImage(background) || !fixture.saveDocument(doc)) return false;
    ImageEditorWindow window; window.resize(1100,800); window.show();
    auto* import=window.findChild<QAction*>("importImageAsLayerAction");
    auto* canvas=window.findChild<ImageCanvas*>();
    if (!check(import && !import->isEnabled() && canvas==nullptr,"Empty workspace exposed document controls.")) return false;
    if (!window.openLinkedImage(background,doc,output)) return false;
    canvas=window.findChild<ImageCanvas*>();
    if (!canvas) return false;
    QCoreApplication::processEvents();
    if (!check(import->isEnabled(),"Import disabled with document.")) return false;
    // Exercise the actual file dialog command.
    QTimer chooser; QObject::connect(&chooser,&QTimer::timeout,&window,[&]() {
        for (auto* widget : QApplication::topLevelWidgets()) if (auto* dialog=qobject_cast<QFileDialog*>(widget)) {
            dialog->selectFile(source); QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection); chooser.stop(); return;
        }
    });
    chooser.start(5); import->trigger(); chooser.stop();
    auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");
    auto* select=window.findChild<QAction*>("selectShapesToolAction");
    auto* save=window.findChild<QAction*>("saveDocumentAction");
    auto* undo=window.findChild<QAction*>("undoAction");
    auto* redo=window.findChild<QAction*>("redoAction");
    auto* relink=window.findChild<QAction*>("relinkRasterImageAction");
    if (!check(tree && tree->topLevelItemCount()==3 && select && select->isChecked() &&
        save && undo && redo && relink && relink->isEnabled(),"Import selection/UI state incorrect.")) return false;
    const auto point=[&](QPointF p) { return (QPointF(canvas->rect().center())+(p-QPointF(32,32))*canvas->zoomFactor()).toPoint(); };
    QSignalSpy geometry(canvas,&ImageCanvas::objectsGeometryChanged);
    QSignalSpy preview(canvas,&ImageCanvas::objectsPreviewRequested);
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({32,32}));
    QTest::mouseMove(canvas,point({36,32}));
    if (!check(preview.count()>0,"Raster move has no core preview.")) return false;
    QTest::keyClick(canvas,Qt::Key_Escape);
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({36,32}));
    if (!check(geometry.count()==0,"Esc committed a gesture.")) return false;
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({32,32}));
    QTest::mouseMove(canvas,point({36,32}));
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({36,32}));
    if (!check(geometry.count()==1,"Move did not commit one gesture.")) return false;
    auto moved=qvariant_cast<QVector<ImageObjectPlacement>>(geometry.last()[0]).front();
    if (!check(std::abs(moved.operation.raster.transform.dx()-20)<1,"Move geometry incorrect.")) return false;
    undo->trigger(); redo->trigger(); save->trigger();
    ImageDocumentSession saved;
    if (!saved.openDocument(doc) || !check(QImage(output)==saved.renderedImage(),"Published PNG differs from core composition.")) return false;
    // Default corner scaling keeps the source proportions.
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({52,48}));
    QTest::mouseMove(canvas,point({60,52}));
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({60,52}));
    if (!check(geometry.count()==2,"Corner resize did not commit.")) return false;
    auto scaled=qvariant_cast<QVector<ImageObjectPlacement>>(geometry.last()[0]).front();
    if (!check(std::abs(scaled.operation.raster.transform.m11()-scaled.operation.raster.transform.m22())<0.001,"Corner resize changed proportions.")) return false;
    undo->trigger();
    QTest::mousePress(canvas,Qt::LeftButton,Qt::AltModifier,point({52,48}));
    QTest::mouseMove(canvas,point({60,52}));
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::AltModifier,point({60,52}));
    // mouseMove carries the platform's key state, so send an explicit modifier event.
    undo->trigger();
    QMouseEvent press(QEvent::MouseButtonPress,point({52,48}),point({52,48}),Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);
    QApplication::sendEvent(canvas,&press);
    QMouseEvent move(QEvent::MouseMove,point({60,52}),point({60,52}),Qt::NoButton,Qt::LeftButton,Qt::AltModifier);
    QApplication::sendEvent(canvas,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,point({60,52}),point({60,52}),Qt::LeftButton,Qt::NoButton,Qt::AltModifier);
    QApplication::sendEvent(canvas,&release);
    const auto independent=qvariant_cast<QVector<ImageObjectPlacement>>(geometry.last()[0]).front().operation.raster.transform;
    if (!check(std::abs(independent.m11()-independent.m22())>0.05,"Alt resize did not use independent dimensions.")) return false;
    undo->trigger();
    const QPoint handle=point({36,16})-QPoint(0,28);
    const QPoint pivot=point({36,32});
    const qreal radius=QLineF(pivot,handle).length();
    const qreal angle=-60.0*std::acos(-1.0)/180.0;
    const QPoint turn=pivot+QPoint(qRound(std::cos(angle)*radius),qRound(std::sin(angle)*radius));
    QMouseEvent rotation_press(QEvent::MouseButtonPress,handle,handle,Qt::LeftButton,Qt::LeftButton,Qt::ShiftModifier);
    QApplication::sendEvent(canvas,&rotation_press);
    QMouseEvent rotation_move(QEvent::MouseMove,turn,turn,Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);
    QApplication::sendEvent(canvas,&rotation_move);
    QMouseEvent rotation_release(QEvent::MouseButtonRelease,turn,turn,Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
    QApplication::sendEvent(canvas,&rotation_release);
    const auto rotated=qvariant_cast<QVector<ImageObjectPlacement>>(geometry.last()[0]).front().operation.raster.transform;
    const qreal degrees=std::atan2(rotated.m12(),rotated.m11())*180.0/std::acos(-1.0);
    if (!check(std::abs(degrees-30)<0.01,"Rotation handle/Shift snap failed.")) return false;
    undo->trigger();
    // A dropped batch becomes one history action and uses the drop center.
    QMimeData mime; mime.setUrls({QUrl::fromLocalFile(source),QUrl::fromLocalFile(source)});
    QDragEnterEvent enter(point({12,12}),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&enter);
    QDropEvent drop(point({12,12}),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&drop);
    if (!check(drop.isAccepted() && tree->topLevelItemCount()==5,"Canvas drop batch failed.")) return false;
    undo->trigger(); if (!check(tree->topLevelItemCount()==3,"Dropped batch undo not atomic.")) return false;
    redo->trigger(); save->trigger();
    if (!saved.openDocument(doc)) return false;
    const auto drop_raster=saved.data().layers.back().operations.front().raster;
    if (!check(std::abs(drop_raster.transform.dx()+4)<1,"Drop placement incorrect.")) return false;
    // Cancellation arrives while the modal decoder is active; nothing is inserted.
    QTimer cancel; QObject::connect(&cancel,&QTimer::timeout,&window,[&]() {
        for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName()=="imageImportProgressDialog") {
            auto* button=widget->findChild<QPushButton*>("imageExportCancelButton");
            if (button) { button->click(); cancel.stop(); return; }
        }
    });
    cancel.start(0);
    const bool cancelled=window.importImagePaths({source});
    cancel.stop();
    if (!check(!cancelled && tree->topLevelItemCount()==5,"Cancelled import committed layers.")) return false;
    const QByteArray previous=[&](){QFile f(output); f.open(QIODevice::ReadOnly); return f.readAll();}();
    if (!QFile::remove(source)) return false;
    ImageDocumentSession unavailable;
    if (!unavailable.openDocument(doc)) return false;
    if (!window.openLinkedImage(background,doc,output)) return false;
    QCoreApplication::processEvents();
    if (!check(tree->topLevelItem(0)->toolTip(0).contains("Relink Image"),"Source problem missing from panel.")) return false;
    QTimer errors; QObject::connect(&errors,&QTimer::timeout,&window,[](){
        for(auto* w:QApplication::topLevelWidgets()) if(auto* m=qobject_cast<QMessageBox*>(w)) m->accept();
    });
    errors.start(5); save->trigger(); errors.stop();
    const QByteArray after=[&](){QFile f(output); f.open(QIODevice::ReadOnly); return f.readAll();}();
    if (!check(previous==after,"Unavailable linked source replaced published PNG.")) return false;
    const QString replacement=root+"/raster-ui-relinked.png";
    photo.fill(Qt::green); if (!photo.save(replacement)) return false;
    const QRect row=tree->visualItemRect(tree->currentItem());
    QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(row.left()+16,row.center().y()));
    if (!check(relink->isEnabled(),"Same-layer thumbnail did not select unavailable image.")) return false;
    QTimer relink_chooser; QObject::connect(&relink_chooser,&QTimer::timeout,&window,[&]() {
        for (auto* widget:QApplication::topLevelWidgets()) if (auto* dialog=qobject_cast<QFileDialog*>(widget)) {
            dialog->selectFile(replacement); QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection); relink_chooser.stop(); return;
        }
    });
    relink_chooser.start(5); relink->trigger(); relink_chooser.stop();
    if (!check(!tree->currentItem()->toolTip(0).contains("Relink Image"),"Relink action did not resolve selected image.")) return false;
    errors.start(5); save->trigger(); errors.stop();
    if (!saved.openDocument(doc)) return false;
    int replaced=0;
    for (const auto& layer:saved.data().layers) for (const auto& op:layer.operations)
        if (op.kind==OperationKind::RasterImage && op.raster.source_path==replacement) ++replaced;
    if (!check(replaced==1,"UI relink changed other duplicate references.")) return false;
    return true;
}
