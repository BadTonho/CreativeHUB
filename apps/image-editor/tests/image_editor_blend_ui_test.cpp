#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
#include <stdexcept>

using namespace image_editor;
using creative_suite::composition::BlendMode;
bool testLayerBlendUi(const QString& root) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    try {
        const auto path=root+"/blend-ui.cimg";
        ImageDocumentSession fixture;require(fixture.createCanvas({96,72},QColor(80,120,180)),"Create blend fixture");
        const auto base=fixture.selectedLayerId();require(fixture.applyPaintStroke({{15,30},{70,30}},Qt::red,30),"Paint base");
        const auto child=fixture.addLayer();require(fixture.applyPaintStroke({{10,30},{80,30}},Qt::blue,40),"Paint child");
        require(fixture.addLayerMask(child) && fixture.saveDocument(path),"Save blend fixture");
        ImageEditorWindow window;window.resize(1100,800);window.show();require(window.openDocumentPath(path),"Open blend fixture");
        processImageEditorEvents();
        auto* combo=window.findChild<QComboBox*>("layerBlendMode");auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");
        auto* panel=window.findChild<LayerPanel*>();auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
        require(combo && tree && panel && save && undo && redo && combo->count()==6 && combo->currentData().toInt()==0 && combo->isEnabled(),"Blend selector defaults");
        QSignalSpy changes(panel,&LayerPanel::stackBlendModeChanged);panel->setSelectedMask(child);
        combo->setCurrentIndex(1);require(changes.count()==1,"Mask selection did not target its owner");save->trigger();
        ImageDocumentData doc;require(ImageDocumentStore::loadDocument(path,&doc) && doc.layers.back().blend_mode==BlendMode::Multiply,"Persist blend mode");
        combo->setCurrentIndex(1);require(changes.count()==1,"Equal mode created an edit signal");
        undo->trigger();require(combo->currentData().toInt()==0,"Undo blend");redo->trigger();require(combo->currentData().toInt()==1,"Redo blend");
        auto* canvas=window.findChild<ImageCanvas*>();require(canvas,"Missing blend canvas");
        canvas->setLinearGradientMode(true);
        QSignalSpy gradients(canvas,&ImageCanvas::linearGradientRequested),cleared(canvas,&ImageCanvas::linearGradientPreviewCleared);
        const QPoint center=canvas->rect().center();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center);
        QTest::mouseMove(canvas,center+QPoint(12,8));combo->setCurrentIndex(2);
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(12,8));
        require(cleared.count()>0 && gradients.count()==0,"Mode change committed a cancelled gradient");canvas->setLinearGradientMode(false);
        for(int mode=2;mode<6;++mode){combo->setCurrentIndex(mode);require(combo->currentData().toInt()==mode,"Switch blend mode");}
        save->trigger();require(ImageDocumentStore::loadDocument(path,&doc) && doc.layers.back().blend_mode==BlendMode::Difference,"Persist final mode");
        tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));require(!combo->isEnabled(),"Background selector enabled");
        tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(0));tree->topLevelItem(1)->setSelected(true);require(!combo->isEnabled(),"Multiple selection selector enabled");
        tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(0));
        const auto group=fixture.groupLayers({base,child});require(!group.isEmpty(),"Create group fixture");
        require(fixture.setStackBlendMode(group,true,BlendMode::Screen) && fixture.setGroupVisible(group,false),"Hidden group mode");
        LayerPanel groupPanel;groupPanel.setDocument(fixture.data(),{},group,{});
        auto* groupCombo=groupPanel.findChild<QComboBox*>("layerBlendMode");require(groupCombo->isEnabled() && groupCombo->currentData().toInt()==2,"Group selector state");
        QSignalSpy groupChanges(&groupPanel,&LayerPanel::stackBlendModeChanged);groupCombo->setCurrentIndex(3);
        require(groupChanges.count()==1 && groupChanges[0][1].toBool(),"Group selector destination");
        const auto secondPath=root+"/blend-tab.cimg";ImageDocumentSession second;require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second blend tab");
        QTimer::singleShot(0,[secondPath]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->selectFile(secondPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});
        window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");
        require(tabs && tabs->count()==2 && combo->currentData().toInt()==0,"Blend leaked across tabs");tabs->setCurrentIndex(0);require(combo->currentData().toInt()==5,"Blend lost after tab switch");
        const auto source=root+"/blend-source.png",linked=root+"/blend-linked.cimg",output=root+"/blend-published.png";
        QImage pixels(96,72,QImage::Format_ARGB32);pixels.fill(QColor(40,130,180));require(pixels.save(source),"Write linked source");
        ImageDocumentSession producer;require(producer.openImage(source) && producer.applyPaintStroke({{15,30},{70,30}},Qt::red,30) && producer.saveDocument(linked),"Create linked blend");
        ImageEditorWindow linkedWindow;require(linkedWindow.openLinkedImage(source,linked,output),"Open linked blend");
        linkedWindow.findChild<QComboBox*>("layerBlendMode")->setCurrentIndex(2);linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();
        require(producer.openDocument(linked) && producer.data().layers.back().blend_mode==BlendMode::Screen,"Linked mode persistence");
        require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Published blend mismatch");
        require(QImage(source)==pixels,"Blend modified source");return true;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}
