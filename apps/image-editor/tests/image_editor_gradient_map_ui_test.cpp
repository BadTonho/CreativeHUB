#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QTabBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QTimer>
#include <QTest>
#include <iostream>
#include <stdexcept>
using namespace image_editor;
bool testGradientMapUi(const QString& root){
 const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 try{
    ImageDocumentSession fixture;require(fixture.createCanvas({96,72},QColor(40,80,150)),"Gradient fixture");require(fixture.applyPaintStroke({{10,30},{80,30}},Qt::red,25),"Gradient source");const auto path=root+"/gradient-ui.cimg";require(fixture.saveDocument(path),"Save fixture");
    ImageEditorWindow window;window.resize(1200,950);window.show();require(window.openDocumentPath(path),"Open fixture");processImageEditorEvents();
    auto* create=window.findChild<QAction*>("addGradientMapAction");auto* save=window.findChild<QAction*>("saveDocumentAction");auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");auto* canvas=window.findChild<ImageCanvas*>();auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");require(create && create->isEnabled() && canvas && save && undo && redo && tree,"Gradient controls");
    QPainterPath selection;selection.addEllipse(10,10,55,40);static_cast<void>(canvas->applyAreaSelectionPath(selection));create->trigger();save->trigger();ImageDocumentData doc;require(ImageDocumentStore::loadDocument(path,&doc),"Read adjustment");const auto id=doc.layers.back().id;require(doc.layers.back().adjustment && doc.layers.back().mask->initial_selection==selection,"Selection was not captured");
    auto* add=window.findChild<QPushButton*>("gradientMapAddPoint");auto* remove=window.findChild<QPushButton*>("gradientMapRemovePoint");auto* color=window.findChild<QPushButton*>("gradientMapColor");auto* position=window.findChild<QDoubleSpinBox*>("gradientMapPosition");require(add && remove && color && position && !position->isEnabled() && !remove->isEnabled(),"Endpoint restrictions");
    auto* bar=window.findChild<QWidget*>("gradientMapBar");require(bar,"Gradient bar missing");bar->setFocus();QTest::keyClick(bar,Qt::Key_Right);require(position->value()==100 && !remove->isEnabled(),"Keyboard endpoint selection");QTest::keyClick(bar,Qt::Key_Left);require(position->value()==0,"Keyboard point selection");
    require(!window.findChild<QAction*>("quickExportImageAction")->isEnabled(),"Adjustment Quick Export enabled");require(!window.findChild<QAction*>("rotateLeftAction")->isEnabled(),"Adjustment transform enabled");
    add->click();require(position->isEnabled() && position->value()==50 && remove->isEnabled(),"Add point");position->setValue(60);position->setValue(70);QMetaObject::invokeMethod(position,"editingFinished",Qt::DirectConnection);undo->trigger();require(position->value()==50,"Continuous edit history");redo->trigger();require(position->value()==70,"Redo point position");
    QTimer::singleShot(0,[]{if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())){dialog->setCurrentColor(Qt::magenta);dialog->reject();}});color->click();save->trigger();require(ImageDocumentStore::loadDocument(path,&doc) && doc.layers.back().adjustment->stops[1].color==std::array<unsigned char,3>{128,128,128},"Cancelled color remained");
    for(int i=0;i<29;++i)add->click();require(!add->isEnabled(),"Gradient point limit");remove->click();require(add->isEnabled(),"Remove point did not restore capacity");
    QMetaObject::invokeMethod(window.findChild<LayerPanel*>(),"layerMaskSelected",Qt::DirectConnection,Q_ARG(QString,id));processImageEditorEvents();require(window.findChild<QWidget*>("gradientMapEditor")->isVisible(),"Mask selection hid owner properties");require(window.findChild<QAction*>("paintToolAction")->isEnabled(),"Adjustment mask paint disabled");
    tree->topLevelItem(1)->setSelected(true);require(!create->isEnabled(),"Multiple selection creation enabled");tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));require(create->isEnabled(),"Background cannot create adjustment above");
    ImageDocumentSession second;const auto secondPath=root+"/gradient-tab.cimg";require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second tab fixture");
    QTimer::singleShot(0,[secondPath]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->selectFile(secondPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});
    window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");require(tabs && tabs->count()==2,"Second tab missing");create->trigger();save->trigger();require(ImageDocumentStore::loadDocument(secondPath,&doc) && doc.layers.back().adjustment->stops.size()==2,"Gradient state leaked to another tab");
    tabs->setCurrentIndex(0);tree->setCurrentItem(tree->topLevelItem(0));save->trigger();require(ImageDocumentStore::loadDocument(path,&doc) && doc.layers.back().adjustment->stops.size()==31,"Gradient state lost after tab switch");
    // Real linked publication must remain a PNG and preserve source bytes.
    const auto source=root+"/gradient-source.png",linked=root+"/gradient-linked.cimg",output=root+"/gradient-published.png";QImage original(96,72,QImage::Format_ARGB32);original.fill(QColor(40,120,180));require(original.save(source),"Source PNG");ImageDocumentSession producer;require(producer.openImage(source) && producer.saveDocument(linked),"Linked fixture");
    ImageEditorWindow linkedWindow;require(linkedWindow.openLinkedImage(source,linked,output),"Linked open");linkedWindow.findChild<QAction*>("addGradientMapAction")->trigger();linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();require(producer.openDocument(linked),"Reopen linked adjustment");require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Published adjustment mismatch");require(QImage(source)==original,"Source was modified");return true;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}
