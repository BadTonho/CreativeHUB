#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include "levels_editor.h"
#include <QAction>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace image_editor;

bool testLevelsUi(const QString& root) {
    const auto require=[](bool condition,const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    try {
        const auto path=root+"/levels-ui.cimg";
        ImageDocumentSession fixture;
        require(fixture.createCanvas({48,32},QColor(40,80,150)) &&
            fixture.saveDocument(path),"Levels fixture");
        ImageEditorWindow window;
        window.resize(1150,950); window.show();
        require(window.openDocumentPath(path),"Open Levels fixture");
        processImageEditorEvents();
        auto* create=window.findChild<QAction*>("addLevelsAction");
        auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* undo=window.findChild<QAction*>("undoAction");
        auto* redo=window.findChild<QAction*>("redoAction");
        auto* canvas=window.findChild<ImageCanvas*>();
        require(create && create->isEnabled() && save && undo && redo && canvas,"Levels actions");
        QPainterPath selection;selection.addRect(0,0,24,32);
        static_cast<void>(canvas->applyAreaSelectionPath(selection));
        create->trigger(); save->trigger();
        ImageDocumentData document;
        require(ImageDocumentStore::loadDocument(path,&document),"Persist Levels creation");
        require(document.layers.back().mask->initial_selection==selection,"Selection mask");
        auto* histogram=static_cast<LevelsHistogram*>(window.findChild<QWidget*>("levelsHistogram"));
        auto* black=window.findChild<QSpinBox*>("inputBlackSpin");
        auto* white=window.findChild<QSpinBox*>("inputWhiteSpin");
        auto* gamma=window.findChild<QDoubleSpinBox*>("midtoneGammaSpin");
        auto* outBlack=window.findChild<QSpinBox*>("outputBlackSpin");
        auto* outWhite=window.findChild<QSpinBox*>("outputWhiteSpin");
        auto* slider=window.findChild<QSlider*>("inputBlackSlider");
        auto* reset=window.findChild<QPushButton*>("resetLevelsButton");
        require(histogram && black && white && gamma && outBlack && outWhite && slider && reset,
            "Levels properties");
        require(black->value()==0 && white->value()==255 && gamma->value()==1. &&
            outBlack->value()==0 && outWhite->value()==255 && !reset->isEnabled(),"Levels defaults");
        for (int i=0;i<100 && histogram->bins()[40]==0;++i) QTest::qWait(10);
        require(histogram->bins()[40]>0 && histogram->bins()[80]>0 &&
            histogram->bins()[150]>0,"Input histogram");
        const auto bins=histogram->bins();
        black->setValue(20); black->setValue(30);
        QMetaObject::invokeMethod(black,"editingFinished",Qt::DirectConnection);
        require(histogram->bins()==bins,"Parameter edit recomputed input histogram");
        undo->trigger(); require(black->value()==0,"Continuous edit undo");
        redo->trigger(); require(black->value()==30,"Continuous edit redo");
        white->setValue(230); QMetaObject::invokeMethod(white,"editingFinished",Qt::DirectConnection);
        gamma->setValue(1.4); QMetaObject::invokeMethod(gamma,"editingFinished",Qt::DirectConnection);
        outBlack->setValue(10); QMetaObject::invokeMethod(outBlack,"editingFinished",Qt::DirectConnection);
        outWhite->setValue(240); QMetaObject::invokeMethod(outWhite,"editingFinished",Qt::DirectConnection);
        require(black->maximum()==229 && white->minimum()==31 &&
            outBlack->maximum()==240 && outWhite->minimum()==10,"Coupled bounds");
        reset->click();
        require(black->value()==0 && white->value()==255 && gamma->value()==1. &&
            outBlack->value()==0 && outWhite->value()==255,"Atomic reset");
        undo->trigger(); require(black->value()==30 && gamma->value()==1.4 &&
            outWhite->value()==240,"Reset undo");
        QMetaObject::invokeMethod(slider,"sliderPressed",Qt::DirectConnection);
        slider->setValue(35);slider->setValue(40);
        QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection);
        undo->trigger();require(black->value()==30,"Slider gesture undo");
        require(!window.findChild<QAction*>("quickExportImageAction")->isEnabled(),
            "Adjustment Quick Export restriction");
        save->trigger();
        require(fixture.openDocument(path),"Reopen Levels document");
        require(std::holds_alternative<creative_suite::effects::LevelsParameters>(
            *fixture.data().layers.back().adjustment),"Persist Levels type");
        return true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return false;
    }
}
