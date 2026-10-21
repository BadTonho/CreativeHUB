#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
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
    QTimer::singleShot(0,[]{if(auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget())){dialog->setCurrentColor(Qt::magenta);dialog->reject();}});color->click();save->trigger();require(ImageDocumentStore::loadDocument(path,&doc) && std::get<creative_suite::effects::GradientMapParameters>(*doc.layers.back().adjustment).stops[1].color==std::array<unsigned char,3>{128,128,128},"Cancelled color remained");
    for(int i=0;i<29;++i)add->click();require(!add->isEnabled(),"Gradient point limit");remove->click();require(add->isEnabled(),"Remove point did not restore capacity");
    QMetaObject::invokeMethod(window.findChild<LayerPanel*>(),"layerMaskSelected",Qt::DirectConnection,Q_ARG(QString,id));processImageEditorEvents();require(window.findChild<QWidget*>("gradientMapEditor")->isVisible(),"Mask selection hid owner properties");require(window.findChild<QAction*>("paintToolAction")->isEnabled(),"Adjustment mask paint disabled");
    tree->topLevelItem(1)->setSelected(true);require(!create->isEnabled(),"Multiple selection creation enabled");tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));require(create->isEnabled(),"Background cannot create adjustment above");
    ImageDocumentSession second;const auto secondPath=root+"/gradient-tab.cimg";require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second tab fixture");
    QTimer::singleShot(0,[secondPath]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->selectFile(secondPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});
    window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");require(tabs && tabs->count()==2,"Second tab missing");create->trigger();save->trigger();require(ImageDocumentStore::loadDocument(secondPath,&doc) && std::get<creative_suite::effects::GradientMapParameters>(*doc.layers.back().adjustment).stops.size()==2,"Gradient state leaked to another tab");
    tabs->setCurrentIndex(0);tree->setCurrentItem(tree->topLevelItem(0));save->trigger();require(ImageDocumentStore::loadDocument(path,&doc) && std::get<creative_suite::effects::GradientMapParameters>(*doc.layers.back().adjustment).stops.size()==31,"Gradient state lost after tab switch");
    // Real linked publication must remain a PNG and preserve source bytes.
    const auto source=root+"/gradient-source.png",linked=root+"/gradient-linked.cimg",output=root+"/gradient-published.png";QImage original(96,72,QImage::Format_ARGB32);original.fill(QColor(40,120,180));require(original.save(source),"Source PNG");ImageDocumentSession producer;require(producer.openImage(source) && producer.saveDocument(linked),"Linked fixture");
    ImageEditorWindow linkedWindow;require(linkedWindow.openLinkedImage(source,linked,output),"Linked open");linkedWindow.findChild<QAction*>("addGradientMapAction")->trigger();linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();require(producer.openDocument(linked),"Reopen linked adjustment");require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Published adjustment mismatch");require(QImage(source)==original,"Source was modified");return true;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}

bool testBrightnessContrastUi(const QString& root){
 const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 try{
    ImageDocumentSession fixture;require(fixture.createCanvas({96,72},QColor(40,80,150)),"Brightness fixture");const auto path=root+"/brightness-ui.cimg";require(fixture.saveDocument(path),"Save fixture");
    ImageEditorWindow window;window.resize(1200,950);window.show();require(window.openDocumentPath(path),"Open fixture");processImageEditorEvents();
    auto* create=window.findChild<QAction*>("addBrightnessContrastAction");auto* save=window.findChild<QAction*>("saveDocumentAction");auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");auto* canvas=window.findChild<ImageCanvas*>();auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");require(create && create->isEnabled() && canvas && save && undo && redo && tree,"Brightness controls");
    QPainterPath selection;selection.addEllipse(10,10,55,40);static_cast<void>(canvas->applyAreaSelectionPath(selection));create->trigger();save->trigger();ImageDocumentData doc;require(ImageDocumentStore::loadDocument(path,&doc),"Read adjustment");const auto id=doc.layers.back().id;require(doc.layers.back().adjustment && doc.layers.back().mask->initial_selection==selection,"Selection capture");
    auto* brightness=window.findChild<QSpinBox*>("brightnessSpin");auto* contrast=window.findChild<QSpinBox*>("contrastSpin");auto* slider=window.findChild<QSlider*>("brightnessSlider");auto* reset=window.findChild<QPushButton*>("resetBrightnessContrastButton");require(brightness && contrast && slider && reset,"Properties missing");require(brightness->minimum()==-100 && brightness->maximum()==100 && contrast->value()==0 && !reset->isEnabled(),"Defaults/range");
    brightness->setValue(10);brightness->setValue(30);QMetaObject::invokeMethod(brightness,"editingFinished",Qt::DirectConnection);undo->trigger();require(brightness->value()==0,"Continuous edit history");brightness->setValue(0);redo->trigger();require(brightness->value()==30,"No-op discarded Redo");
    contrast->setValue(-40);QMetaObject::invokeMethod(contrast,"editingFinished",Qt::DirectConnection);reset->click();require(brightness->value()==0 && contrast->value()==0,"Reset");undo->trigger();require(brightness->value()==30 && contrast->value()==-40,"Atomic reset undo");
    QMetaObject::invokeMethod(slider,"sliderPressed",Qt::DirectConnection);slider->setValue(40);slider->setValue(50);QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection);undo->trigger();require(brightness->value()==30,"Slider undo");redo->trigger();require(brightness->value()==50,"Slider redo");
    require(!window.findChild<QAction*>("quickExportImageAction")->isEnabled() && !window.findChild<QAction*>("rotateLeftAction")->isEnabled(),"Adjustment restrictions");
    QMetaObject::invokeMethod(window.findChild<LayerPanel*>(),"layerMaskSelected",Qt::DirectConnection,Q_ARG(QString,id));processImageEditorEvents();require(window.findChild<QWidget*>("brightnessContrastEditor")->isVisible() && window.findChild<QAction*>("paintToolAction")->isEnabled(),"Mask owner/tool");
    tree->topLevelItem(1)->setSelected(true);require(!create->isEnabled(),"Multiple selection enabled");tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));require(create->isEnabled(),"Background creation");tree->setCurrentItem(tree->topLevelItem(0));brightness->setValue(65);save->trigger();
    ImageDocumentSession second;const auto secondPath=root+"/brightness-tab.cimg";require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second fixture");
    QTimer::singleShot(0,[secondPath]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->selectFile(secondPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");require(tabs && tabs->count()==2,"Second tab");create->trigger();require(brightness->value()==0 && contrast->value()==0,"Tab state leaked");tabs->setCurrentIndex(0);tree->setCurrentItem(tree->topLevelItem(0));require(brightness->value()==65 && contrast->value()==-40,"Tab state lost");
    const auto source=root+"/brightness-source.png",linked=root+"/brightness-linked.cimg",output=root+"/brightness-published.png";QImage original(96,72,QImage::Format_ARGB32);original.fill(QColor(40,120,180));require(original.save(source),"Source PNG");ImageDocumentSession producer;require(producer.openImage(source) && producer.saveDocument(linked),"Linked fixture");ImageEditorWindow linkedWindow;require(linkedWindow.openLinkedImage(source,linked,output),"Linked open");linkedWindow.findChild<QAction*>("addBrightnessContrastAction")->trigger();auto* linkedBrightness=linkedWindow.findChild<QSpinBox*>("brightnessSpin");linkedBrightness->setValue(25);QMetaObject::invokeMethod(linkedBrightness,"editingFinished",Qt::DirectConnection);linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();require(producer.openDocument(linked),"Reopen publication");require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Published pixels differ");require(QImage(source)==original,"Source modified");return true;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}
