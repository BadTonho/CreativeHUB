#include "image_editor_ui_wait.h"
#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"
#include "curves_editor.h"
#include <QAction>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace image_editor;

bool testCurvesUi(const QString& root) {
    const auto require=[](bool condition,const char* message) {
        if(!condition)throw std::runtime_error(message);
    };
    try {
        const auto path=root+"/curves-ui.cimg";
        ImageDocumentSession fixture;
        require(fixture.createCanvas({48,32},QColor(40,80,150)) &&
            fixture.saveDocument(path),"Curves fixture");
        ImageEditorWindow window;window.resize(1150,950);window.show();
        require(window.openDocumentPath(path),"Open Curves fixture");
        processImageEditorEvents();
        auto* create=window.findChild<QAction*>("addCurvesAction");
        auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* undo=window.findChild<QAction*>("undoAction");
        auto* redo=window.findChild<QAction*>("redoAction");
        auto* canvas=window.findChild<ImageCanvas*>();
        require(create && create->isEnabled() && save && undo && redo && canvas,"Curves actions");
        QPainterPath selection;selection.addRect(0,0,24,32);
        static_cast<void>(canvas->applyAreaSelectionPath(selection));
        create->trigger();save->trigger();
        ImageDocumentData document;
        require(ImageDocumentStore::loadDocument(path,&document),"Persist Curves creation");
        require(document.layers.back().mask->initial_selection==selection,"Selection mask");
        auto* graph=static_cast<CurvesGraph*>(window.findChild<QWidget*>("curvesGraph"));
        auto* input=window.findChild<QSpinBox*>("curvesInputSpin");
        auto* output=window.findChild<QSpinBox*>("curvesOutputSpin");
        auto* add=window.findChild<QPushButton*>("curvesAddPoint");
        auto* remove=window.findChild<QPushButton*>("curvesRemovePoint");
        auto* reset=window.findChild<QPushButton*>("curvesReset");
        require(graph && input && output && add && remove && reset,"Curves properties");
        require(input->value()==0 && !input->isEnabled() && output->value()==0 &&
            !remove->isEnabled() && !reset->isEnabled(),"Curves defaults");
        for(int i=0;i<100 && graph->bins()[40]==0;++i)QTest::qWait(10);
        require(graph->bins()[40]>0 && graph->bins()[80]>0 && graph->bins()[150]>0,
            "Curves input histogram");
        const auto bins=graph->bins();
        add->click();
        require(graph->value.points.size()==3 && input->value()==127,"Add midpoint");
        const auto graphPoint=[&](int x,int y){
            return QPoint(12+int((graph->width()-25)*x/255.0),
                graph->height()-13-int((graph->height()-25)*y/255.0));
        };
        QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,graphPoint(127,127));
        QTest::mouseMove(graph,graphPoint(150,80));
        QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,graphPoint(150,80));
        require(graph->value.points[1].input==150 && graph->value.points[1].output==80,
            "Drag control point");
        undo->trigger();require(graph->value.points[1].input==127 &&
            graph->value.points[1].output==127,"Drag is one undo step");
        output->setValue(180);QMetaObject::invokeMethod(output,"editingFinished",Qt::DirectConnection);
        require(graph->value.points[1].output==180,"Curves numeric output");
        require(graph->bins()==bins,"Curves edit and stable input histogram");
        undo->trigger();require(graph->value.points[1].output==127,"Curves edit undo");
        redo->trigger();require(graph->value.points[1].output==180,"Curves edit redo");
        input->setValue(140);QMetaObject::invokeMethod(input,"editingFinished",Qt::DirectConnection);
        require(graph->value.points[1].input==140,"Curves numeric input");
        remove->click();require(graph->value.points.size()==2,"Remove point");
        undo->trigger();require(graph->value.points.size()==3,"Remove undo");
        reset->click();require(graph->value.points.size()==2 && !reset->isEnabled(),"Atomic Reset");
        undo->trigger();require(graph->value.points.size()==3 &&
            graph->value.points[1].input==140,"Reset undo");
        QTest::mouseClick(graph,Qt::LeftButton,Qt::NoModifier,graphPoint(220,200));
        require(graph->value.points.size()==4 && graph->value.points[2].input==220,
            "Click graph to add point");
        undo->trigger();require(graph->value.points.size()==3,"Graph add undo");
        require(!window.findChild<QAction*>("quickExportImageAction")->isEnabled(),
            "Adjustment Quick Export restriction");
        save->trigger();require(fixture.openDocument(path),"Reopen Curves document");
        require(std::holds_alternative<creative_suite::effects::CurvesParameters>(
            *fixture.data().layers.back().adjustment),"Persist Curves type");
        return true;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return false;}
}
