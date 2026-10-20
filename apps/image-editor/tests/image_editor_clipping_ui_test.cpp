#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "layer_panel.h"
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace image_editor;
bool testLayerClippingUi(const QString& root) {
    const auto require=[](bool ok,const char* cause) { if(!ok) throw std::runtime_error(cause); };
    try {
        const auto path=root+"/clipping-ui.cimg";
        ImageDocumentSession fixture; require(fixture.createCanvas({96,72},Qt::transparent),"Create clipping fixture");
        const auto base=fixture.selectedLayerId();
        require(fixture.applyPaintStroke({{25,30},{60,40}},Qt::red,12),"Paint base");
        const auto child=fixture.addLayer(); require(fixture.applyPaintStroke({{10,30},{80,40}},Qt::blue,30),"Paint child");
        require(fixture.addLayerMask(child),"Add child mask"); require(fixture.saveDocument(path),"Save fixture");
        ImageEditorWindow window; window.resize(1100,800); window.show(); require(window.openDocumentPath(path),"Open fixture");
        processImageEditorEvents();
        auto* action=window.findChild<QAction*>("toggleLayerClippingAction");
        auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* undo=window.findChild<QAction*>("undoAction");
        auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");
        auto* panel=window.findChild<LayerPanel*>();
        require(action && save && undo && tree && panel && action->isEnabled(),"Missing clipping command");
        QSignalSpy changes(panel,&LayerPanel::layerClippingChanged);
        panel->setSelectedMask(child); action->trigger(); require(changes.count()==1,"Clipping command targets mask owner");
        save->trigger(); ImageDocumentData document; require(ImageDocumentStore::loadDocument(path,&document),"Read clipped document");
        require(document.layers.back().clip_to_base,"Command did not persist clipping");
        require(tree->topLevelItem(0)->toolTip(0).contains(document.layers[1].name),"Missing clipping base tooltip");
        undo->trigger(); save->trigger(); require(ImageDocumentStore::loadDocument(path,&document) && !document.layers.back().clip_to_base,"Undo clipping");
        QTest::keyClick(&window,Qt::Key_G,Qt::ControlModifier|Qt::AltModifier); save->trigger();
        require(ImageDocumentStore::loadDocument(path,&document) && document.layers.back().clip_to_base,"Clipping keyboard shortcut");
        tree->clearSelection(); tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));
        require(!action->isEnabled(),"Background clipping must be disabled");
        tree->clearSelection(); tree->setCurrentItem(tree->topLevelItem(0)); tree->topLevelItem(1)->setSelected(true);
        require(!action->isEnabled(),"Multiple selection clipping must be disabled");
        // A second tab has its own clipping state and history.
        const auto secondPath=root+"/clipping-tab.cimg";
        ImageDocumentSession second; require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second tab fixture");
        QTimer::singleShot(0,[secondPath] {
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->selectFile(secondPath); QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
            }
        });
        window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();
        auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar"); require(tabs && tabs->count()==2,"Open second tab");
        require(!action->text().contains("Release"),"Clipping state leaked between tabs");
        tabs->setCurrentIndex(0); require(action->text().contains("Release"),"Clipping state lost on tab switch");
        const auto group=fixture.groupLayers({base,child}); require(!group.isEmpty(),"Group clipping fixture");
        LayerPanel grouped;
        grouped.setDocument(fixture.data(),{},group,{});
        require(!grouped.clippingAction()->isEnabled(),"Group clipping must be disabled");
        grouped.setDocument(fixture.data(),base,{},{});
        require(!grouped.clippingAction()->isEnabled(),"First group child has no base");
        grouped.setDocument(fixture.data(),child,{},{});
        require(grouped.clippingAction()->isEnabled(),"Sibling clipping must work inside groups");
        // Saved assignments must win over the newly introduced default.
        { QSettings settings; settings.beginGroup("ImageEditor/KeyboardShortcuts");
          settings.remove("toggleLayerClippingAction"); settings.setValue("openImageAction","Ctrl+Alt+G"); settings.sync(); }
        { ImageEditorWindow collision; require(collision.findChild<QAction*>("toggleLayerClippingAction")->shortcut().isEmpty(),"New default displaced saved binding");
          require(collision.findChild<QAction*>("openImageAction")->shortcut()==QKeySequence("Ctrl+Alt+G"),"Saved binding changed"); }
        { QSettings settings; settings.beginGroup("ImageEditor/KeyboardShortcuts"); settings.remove("openImageAction"); settings.remove("toggleLayerClippingAction"); settings.sync(); }
        { QSettings settings; settings.beginGroup("ImageEditor/KeyboardShortcuts"); settings.setValue("toggleLayerClippingAction","Ctrl+Shift+G"); settings.sync(); }
        { ImageEditorWindow customized; require(customized.findChild<QAction*>("toggleLayerClippingAction")->shortcut()==QKeySequence("Ctrl+Shift+G"),"Custom clipping shortcut was not restored"); }
        { QSettings settings; settings.beginGroup("ImageEditor/KeyboardShortcuts"); settings.remove("toggleLayerClippingAction"); settings.sync(); }
        const auto source=root+"/clipping-source.png", linked=root+"/clipping-linked.cimg", output=root+"/clipping-published.png";
        QImage original(96,72,QImage::Format_ARGB32); original.fill(Qt::transparent); require(original.save(source),"Write source");
        ImageDocumentSession producer; require(producer.openImage(source),"Open producer source");
        require(producer.applyPaintStroke({{25,30},{60,40}},Qt::red,12),"Paint linked base");
        require(!producer.addLayer().isEmpty() && producer.applyPaintStroke({{10,30},{80,40}},Qt::blue,30),"Paint linked child");
        require(producer.saveDocument(linked),"Save linked fixture");
        ImageEditorWindow linkedWindow; require(linkedWindow.openLinkedImage(source,linked,output),"Open linked producer");
        linkedWindow.findChild<QAction*>("toggleLayerClippingAction")->trigger(); linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();
        require(producer.openDocument(linked) && producer.data().layers.back().clip_to_base,"Publish clipped producer");
        require(QImage(output).convertToFormat(QImage::Format_ARGB32)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32),"Published clipping mismatch");
        require(QImage(source)==original,"Clipping modified original image");
        Q_UNUSED(base);
        return true;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return false; }
}
