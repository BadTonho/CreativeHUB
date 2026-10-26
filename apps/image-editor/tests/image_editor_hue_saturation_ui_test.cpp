#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QSpinBox>
#include <QSlider>
#include <QFileDialog>
#include <QTabBar>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QTreeWidget>
#include <QTimer>
#include <QTest>
#include <QWheelEvent>
#include <iostream>
#include <stdexcept>
using namespace image_editor;
bool testHueSaturationUi(const QString& root){
 const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 try{
    ImageDocumentSession fixture;require(fixture.createCanvas({96,72},QColor(40,80,150)),"Hue fixture");const auto path=root+"/hue-ui.cimg";require(fixture.saveDocument(path),"Save fixture");
    ImageEditorWindow window;window.resize(1200,950);window.show();require(window.openDocumentPath(path),"Open fixture");processImageEditorEvents();
    auto* create=window.findChild<QAction*>("addHueSaturationAction");auto* save=window.findChild<QAction*>("saveDocumentAction");auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");auto* canvas=window.findChild<ImageCanvas*>();auto* tree=window.findChild<QTreeWidget*>("imageLayerTree");require(create && create->isEnabled() && canvas && save && undo && redo && tree,"Hue controls");
    QPainterPath selection;selection.addEllipse(10,10,55,40);static_cast<void>(canvas->applyAreaSelectionPath(selection));create->trigger();save->trigger();ImageDocumentData doc;require(ImageDocumentStore::loadDocument(path,&doc),"Read adjustment");const auto id=doc.layers.back().id;require(doc.layers.back().adjustment && doc.layers.back().mask->initial_selection==selection,"Selection capture");
    auto* hue=window.findChild<QSpinBox*>("hueSpin");auto* saturation=window.findChild<QSpinBox*>("saturationSpin");auto* slider=window.findChild<QSlider*>("hueSlider");auto* reset=window.findChild<QPushButton*>("resetHueSaturationButton");require(hue && saturation && slider && reset,"Properties missing");require(hue->minimum()==-180 && hue->maximum()==180 && saturation->value()==0 && !reset->isEnabled(),"Defaults/range");
    auto* lightness=window.findChild<QSpinBox*>("lightnessSpin"); require(lightness && lightness->minimum()==-100 && lightness->maximum()==100 && lightness->value()==0 && saturation->minimum()==-100 && saturation->maximum()==100,"Integer ranges/defaults");
    require(hue->singleStep()==1 && saturation->singleStep()==1 && lightness->singleStep()==1 && hue->suffix()==QStringLiteral("\u00b0") && saturation->suffix()==QStringLiteral("%") && lightness->suffix()==QStringLiteral("%"),"Steps/units");
    lightness->setValue(10); QMetaObject::invokeMethod(lightness,"editingFinished",Qt::DirectConnection);
    hue->setValue(10);hue->setValue(30);QMetaObject::invokeMethod(hue,"editingFinished",Qt::DirectConnection);undo->trigger();require(hue->value()==0,"Continuous edit history");hue->setValue(0);redo->trigger();require(hue->value()==30,"No-op discarded Redo");
    saturation->setValue(-25);QMetaObject::invokeMethod(saturation,"editingFinished",Qt::DirectConnection);reset->click();require(hue->value()==0 && saturation->value()==0,"Reset");require(lightness->value()==0,"Lightness reset");undo->trigger();require(lightness->value()==10,"Atomic lightness undo");require(hue->value()==30 && saturation->value()==-25,"Atomic reset undo");
    QMetaObject::invokeMethod(slider,"sliderPressed",Qt::DirectConnection);slider->setValue(40);slider->setValue(50);QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection);undo->trigger();require(hue->value()==30,"Slider undo");redo->trigger();require(hue->value()==50,"Slider redo");
    slider->setFocus(); QTest::keyClick(slider,Qt::Key_Right); require(hue->value()==51,"Keyboard step mapping"); undo->trigger(); require(hue->value()==50,"Keyboard edit undo");
    QWheelEvent wheel(QPointF(5,5),slider->mapToGlobal(QPoint(5,5)),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(slider,&wheel);processImageEditorEvents();require(hue->value()>50,"Wheel edit");undo->trigger();require(hue->value()==50,"Wheel gesture undo");
    auto* saturationSlider=window.findChild<QSlider*>("saturationSlider"); auto* lightnessSlider=window.findChild<QSlider*>("lightnessSlider"); require(saturationSlider && lightnessSlider,"Additional sliders missing");
    QMetaObject::invokeMethod(saturationSlider,"sliderPressed",Qt::DirectConnection); saturationSlider->setValue(-13); QMetaObject::invokeMethod(saturationSlider,"sliderReleased",Qt::DirectConnection); require(saturation->value()==-13,"Saturation slider mapping"); undo->trigger(); require(saturation->value()==-25,"Saturation gesture undo");
    QMetaObject::invokeMethod(lightnessSlider,"sliderPressed",Qt::DirectConnection); lightnessSlider->setValue(25); QMetaObject::invokeMethod(lightnessSlider,"sliderReleased",Qt::DirectConnection); require(lightness->value()==25,"Lightness slider mapping"); undo->trigger(); require(lightness->value()==10,"Lightness gesture undo");
    auto* range=window.findChild<QComboBox*>("hueRangeSelector"); require(range && range->count()==7,"Range selector");
    range->setCurrentIndex(1); require(hue->value()==0 && saturation->value()==0,"Range defaults");
    hue->setValue(75); QMetaObject::invokeMethod(hue,"editingFinished",Qt::DirectConnection);
    range->setCurrentIndex(6); saturation->setValue(-30); QMetaObject::invokeMethod(saturation,"editingFinished",Qt::DirectConnection);
    range->setCurrentIndex(1); require(hue->value()==75 && saturation->value()==0,"Range values lost");
    reset->click(); require(hue->value()==0,"Range reset"); undo->trigger(); require(hue->value()==75,"Range reset undo");
    redo->trigger(); require(hue->value()==0,"Range reset redo"); range->setCurrentIndex(6); require(saturation->value()==-30,"Reset changed another range");
    range->setCurrentIndex(0); require(hue->value()==50 && saturation->value()==-25,"Global changed by ranges");
    auto* colorize=window.findChild<QCheckBox*>("colorizeCheckBox");require(colorize&&colorize->text()==QCoreApplication::translate("ImageEditorUI","Colorize"),"Colorize control missing");
    colorize->setChecked(true);require(!range->isEnabled() && hue->minimum()==0&&hue->maximum()==360&&saturation->minimum()==0&&saturation->maximum()==100,"Colorize ranges");
    require(hue->value()==0&&saturation->value()==25&&lightness->value()==0,"Colorize defaults");
    hue->setValue(210);QMetaObject::invokeMethod(hue,"editingFinished",Qt::DirectConnection);lightness->setValue(-12);QMetaObject::invokeMethod(lightness,"editingFinished",Qt::DirectConnection);
    colorize->setChecked(false);require(hue->value()==50&&saturation->value()==-25&&lightness->value()==10,"Normal controls were not preserved");
    colorize->setChecked(true);require(hue->value()==210&&saturation->value()==25&&lightness->value()==-12,"Colorize controls were not preserved");
    reset->click();require(colorize->isChecked()&&hue->value()==0&&saturation->value()==25&&lightness->value()==0,"Colorize reset did not restore only its mode");
    undo->trigger();require(colorize->isChecked()&&hue->value()==210&&saturation->value()==25&&lightness->value()==-12,"Colorize reset undo");
    redo->trigger();require(colorize->isChecked()&&hue->value()==0&&saturation->value()==25&&lightness->value()==0,"Colorize reset redo");
    colorize->setChecked(false);require(hue->value()==50&&saturation->value()==-25&&lightness->value()==10,"Colorize reset changed normal values");
    range->setCurrentIndex(6); require(saturation->value()==-30,"Range selection lost before Colorize");
    colorize->setChecked(true); require(!range->isEnabled(),"Colorize did not disable selected range");
    colorize->setChecked(false); require(range->isEnabled() && range->currentIndex()==6 && saturation->value()==-30,"Colorize did not preserve selected range");
    range->setCurrentIndex(0);
    require(!window.findChild<QAction*>("quickExportImageAction")->isEnabled() && !window.findChild<QAction*>("rotateLeftAction")->isEnabled(),"Adjustment restrictions");
    QMetaObject::invokeMethod(window.findChild<LayerPanel*>(),"layerMaskSelected",Qt::DirectConnection,Q_ARG(QString,id));processImageEditorEvents();require(window.findChild<QWidget*>("hueSaturationEditor")->isVisible() && window.findChild<QAction*>("paintToolAction")->isEnabled(),"Mask owner/tool");
    tree->topLevelItem(1)->setSelected(true);require(!create->isEnabled(),"Multiple selection enabled");tree->clearSelection();tree->setCurrentItem(tree->topLevelItem(tree->topLevelItemCount()-1));require(create->isEnabled(),"Background creation");tree->setCurrentItem(tree->topLevelItem(0));hue->setValue(65);save->trigger();
    ImageDocumentSession second;const auto secondPath=root+"/hue-tab.cimg";require(second.createCanvas({32,32},Qt::transparent) && second.saveDocument(secondPath),"Second fixture");
    QTimer::singleShot(0,[secondPath]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->selectFile(secondPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});window.findChild<QAction*>("newTabOpenEditableDocumentAction")->trigger();auto* tabs=window.findChild<QTabBar*>("imageDocumentTabBar");require(tabs && tabs->count()==2,"Second tab");create->trigger();require(hue->value()==0 && saturation->value()==0 && lightness->value()==0,"Tab state leaked");tabs->setCurrentIndex(0);tree->setCurrentItem(tree->topLevelItem(0));require(hue->value()==65 && saturation->value()==-25 && lightness->value()==10,"Tab state lost");
    const auto source=root+"/hue-source.png",linked=root+"/hue-linked.cimg",output=root+"/hue-published.png";QImage original(96,72,QImage::Format_ARGB32);original.fill(QColor(40,120,180));require(original.save(source),"Source PNG");ImageDocumentSession producer;require(producer.openImage(source) && producer.saveDocument(linked),"Linked fixture");ImageEditorWindow linkedWindow;require(linkedWindow.openLinkedImage(source,linked,output),"Linked open");linkedWindow.findChild<QAction*>("addHueSaturationAction")->trigger();
    auto* linkedRange=linkedWindow.findChild<QComboBox*>("hueRangeSelector"); auto* rangeHue=linkedWindow.findChild<QSpinBox*>("hueSpin");
    require(linkedRange && rangeHue,"Linked range controls"); linkedRange->setCurrentIndex(5); rangeHue->setValue(90);
    QMetaObject::invokeMethod(rangeHue,"editingFinished",Qt::DirectConnection); linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();
    require(producer.openDocument(linked),"Reopen ranged publication");
    require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Ranged publication pixels differ");
    require(QImage(source)==original,"Range publication modified source");
    auto* linkedColorize=linkedWindow.findChild<QCheckBox*>("colorizeCheckBox");linkedColorize->setChecked(true);auto* linkedHue=linkedWindow.findChild<QSpinBox*>("hueSpin");auto* linkedSaturation=linkedWindow.findChild<QSpinBox*>("saturationSpin");auto* linkedLightness=linkedWindow.findChild<QSpinBox*>("lightnessSpin");linkedHue->setValue(210);linkedSaturation->setValue(80);linkedLightness->setValue(-15);QMetaObject::invokeMethod(linkedHue,"editingFinished",Qt::DirectConnection);QMetaObject::invokeMethod(linkedSaturation,"editingFinished",Qt::DirectConnection);QMetaObject::invokeMethod(linkedLightness,"editingFinished",Qt::DirectConnection);linkedWindow.findChild<QAction*>("saveDocumentAction")->trigger();require(producer.openDocument(linked),"Reopen publication");require(QImage(output).convertToFormat(QImage::Format_ARGB32_Premultiplied)==producer.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),"Published pixels differ");require(QImage(source)==original,"Source modified");return true;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}
