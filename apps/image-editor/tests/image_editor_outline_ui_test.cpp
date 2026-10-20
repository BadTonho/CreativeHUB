#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "layer_panel.h"
#include "selection/object/object_selection_tool.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QTabBar>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTest>
#include <iostream>
#include <stdexcept>
#include <algorithm>

using namespace image_editor;
bool testLayerOutlineUi(const QString& root) {
    const auto require=[](bool ok,const char* cause) { if(!ok) throw std::runtime_error(cause); };
    try {
        ObjectSelectionTool tool;
        ImageObjectPlacement shape; shape.layer_id="outlined-layer"; shape.operation.kind=OperationKind::Shape;
        shape.operation.shape.id="outlined-object"; shape.operation.shape.start={10,10}; shape.operation.shape.end={50,50};
        tool.setObjects({shape},{"outlined-object"}); tool.setCompositionPreviewRequired(true);
        ObjectSelectionToolTransformRequest request;
        request.context.image_bounds={0,0,100,100}; request.context.image_target={0,0,100,100};
        request.context.pointer_on_image=true; request.context.zoom=1;
        request.context.image_position={25,25}; request.context.unbounded_image_position={25,25};
        require(tool.beginTransform(request).handled,"Outlined shape transform did not start");
        auto moved=request.context; moved.image_position={30,30}; moved.unbounded_image_position={30,30};
        const auto events=tool.move(moved).events;
        require(std::any_of(events.cbegin(),events.cend(),[](const ObjectSelectionToolEvent& event) {
            return event.type==ObjectSelectionToolEventType::PreviewRequested;
        }),"Outlined shape drag must request a composed preview");
        require(tool.transformRequiresComposition() && !tool.transformContainsRaster(),"Outline composition must not change raster geometry rules");
        require(tool.cancelGesture(),"Cancel outlined shape transform");
        const auto path=root+"/outline-ui.cimg";
        ImageDocumentSession fixture; require(fixture.createCanvas({96,72},Qt::transparent),"Create outline fixture");
        const auto id=fixture.selectedLayerId();
        require(fixture.applyPaintStroke({{30,30},{65,40}},Qt::red,12),"Paint outline fixture");
        require(fixture.addLayerMask(id),"Add mask to outline fixture");
        require(fixture.saveDocument(path),"Save outline fixture");
        ImageEditorWindow window; window.resize(1100,800); window.show();
        require(window.openDocumentPath(path),"Open outline fixture"); processImageEditorEvents();
        auto* enabled=window.findChild<QCheckBox*>("layerOutlineEnabled");
        auto* thickness=window.findChild<QSpinBox*>("layerOutlineThickness");
        auto* position=window.findChild<QComboBox*>("layerOutlinePosition");
        auto* color=window.findChild<QPushButton*>("layerOutlineColor");
        auto* remove=window.findChild<QPushButton*>("layerOutlineRemove");
        auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");
        auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* undo=window.findChild<QAction*>("undoAction");
        require(enabled && thickness && position && color && remove && tree && save && undo,"Missing outline UI controls");
        require(enabled->isEnabled() && !enabled->isChecked() && thickness->value()==2 && position->currentIndex()==1,"Outline defaults");
        enabled->click(); thickness->setValue(4); thickness->setValue(7);
        QMetaObject::invokeMethod(thickness,"editingFinished",Qt::DirectConnection);
        undo->trigger(); require(thickness->value()==2 && enabled->isChecked(),"Width edits must undo together");
        thickness->setValue(5); QMetaObject::invokeMethod(thickness,"editingFinished",Qt::DirectConnection);
        position->setCurrentIndex(0);
        QTimer::singleShot(0,[] {
            if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                dialog->setCurrentColor(QColor(20,200,70,120)); dialog->reject();
            }
        });
        color->click(); save->trigger();
        ImageDocumentData document;
        require(ImageDocumentStore::loadDocument(path,&document),"Read outlined UI document");
        auto outline=document.layers.back().outline;
        require(outline && outline->color==QColor(Qt::black) && outline->thickness==5 && outline->position==ImageOutlinePosition::Inside,"Cancel color must restore previous outline");
        QTimer::singleShot(0,[] {
            if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                dialog->setCurrentColor(QColor(20,200,70,120)); dialog->accept();
            }
        });
        color->click(); enabled->click(); save->trigger();
        require(ImageDocumentStore::loadDocument(path,&document),"Read disabled outline");
        outline=document.layers.back().outline;
        require(outline && !outline->enabled && outline->color==QColor(20,200,70,120) && outline->thickness==5,"Disabled outline retains parameters");
        auto* background=tree->topLevelItem(tree->topLevelItemCount()-1); auto* editable=tree->topLevelItem(0);
        background->setSelected(true);
        require(!enabled->isEnabled() && !thickness->isEnabled(),"Multiple selection must disable outline");
        tree->clearSelection(); tree->setCurrentItem(background); background->setSelected(true);
        require(!enabled->isEnabled(),"Background must disable outline");
        tree->clearSelection(); tree->setCurrentItem(editable); editable->setSelected(true);
        require(enabled->isEnabled(),"Editable layer must enable outline");
        remove->click(); save->trigger();
        require(ImageDocumentStore::loadDocument(path,&document) && !document.layers.back().outline && thickness->value()==2,"Remove outline resets defaults");
        const auto secondPath=root+"/outline-second.cimg";
        ImageDocumentSession second; require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second outline tab fixture");
        thickness->setValue(9); // Leave the coalesced edit open while changing tabs.
        auto* newTab=window.findChild<QAction*>("newTabOpenEditableDocumentAction");
        require(newTab,"Missing new-tab command");
        QTimer::singleShot(0,[secondPath] {
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->selectFile(secondPath); QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
            }
        });
        newTab->trigger();
        auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");
        require(tabs && tabs->count()==2 && thickness->value()==2 && !enabled->isChecked(),"Outline settings leaked into another tab");
        tabs->setCurrentIndex(0); require(thickness->value()==9,"Original tab lost outline settings");
        undo->trigger(); require(thickness->value()==2 && !enabled->isChecked(),"Original tab outline history did not close on switch");
        // The real producer's save command must publish the same outlined pixels.
        const auto source=root+"/outline-source.png", linked=root+"/outline-linked.cimg", output=root+"/outline-published.png";
        QImage base(96,72,QImage::Format_ARGB32); base.fill(Qt::transparent); require(base.save(source),"Write linked source");
        ImageDocumentSession producer; require(producer.openImage(source),"Open linked source");
        require(producer.applyPaintStroke({{30,30},{65,40}},Qt::red,12),"Paint linked source");
        require(producer.saveDocument(linked),"Save linked producer");
        ImageEditorWindow linkedWindow; require(linkedWindow.openLinkedImage(source,linked,output),"Open linked workflow");
        auto* linkedEnabled=linkedWindow.findChild<QCheckBox*>("layerOutlineEnabled"); require(linkedEnabled && linkedEnabled->isEnabled(),"Linked outline UI");
        linkedEnabled->click(); linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();
        require(producer.openDocument(linked),"Reopen linked outline");
        require(QImage(output).convertToFormat(QImage::Format_ARGB32)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32),"Published outline differs from document");
        require(QImage(source)==base,"Outline changed original source");
        return true;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return false; }
}
