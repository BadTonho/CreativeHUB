#include "model/anchor_reposition.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "export/motion_video_export.h"
#include "ui/main_window/main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/viewer/composition_viewer.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_playback.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using namespace creative_suite::animation;
using Store = motion::persistence::MotionDocumentStore;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(double a, double b, double epsilon = 1e-8) { return std::abs(a-b) < epsilon; }
template<class T> T* widget(QObject& owner, const char* name) {
    auto* result = dynamic_cast<T*>(owner.findChild<QObject*>(QString::fromLatin1(name)));
    if (!result) throw std::runtime_error(std::string("Missing widget: ") + name);
    return result;
}
std::filesystem::path path(const QString& value) { return std::filesystem::path(value.toStdU16String()); }
MotionProjectData project(const CompositionDocument& document) {
    MotionProjectData result; result.composition = {document.canvasSize(), document.frameRate()};
    result.layers = document.layers(); result.masks = document.masks();
    for (const auto& layer : document.layers()) if (!layer.source_path.empty())
        result.media.push_back({layer.source_path, layer.kind==LayerKind::Image
            ?creative_suite::media::MediaKind::Image:creative_suite::media::MediaKind::Video,"Offline"});
    return result;
}
CompositionLayer shape(LayerId id) {
    CompositionLayer layer{id, LayerKind::Shape, "Shape"}; layer.duration_frames = 90;
    auto content = defaultShapeLayerContent({400,200}); content.width = 40; content.height = 24;
    content.stroke_width_pixels = 0; layer.content = content; return layer;
}
auto resolve(const CompositionDocument& document, std::int64_t frame) {
    return LayerTransformHierarchy(document.canvasSize(), document.layers()).evaluate(frame);
}
void same(const AffineTransform& left, const AffineTransform& right) {
    for (const auto& p : {QPointF(0,0), QPointF(12,-9), QPointF(-73,101)}) {
        const auto a = left.map(p.x(),p.y()), b = right.map(p.x(),p.y());
        check(near(a[0],b[0]) && near(a[1],b[1]), "complete layer/descendant geometry preserved");
    }
}
void waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 8000) { QApplication::processEvents(); QThread::msleep(1); }
    check(condition(), "asynchronous UI completed");
}
void mouse(QWidget& target, QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, target.mapToGlobal(point), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&target, &event);
}
void press(QWidget& target, QPointF point) { mouse(target,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton); }
void move(QWidget& target, QPointF point) { mouse(target,QEvent::MouseMove,point,Qt::NoButton,Qt::LeftButton); }
void release(QWidget& target, QPointF point) { mouse(target,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton); }
QRectF canvasRect(const QWidget& viewer, CanvasSize size) {
    const auto available = QRectF(viewer.rect()).adjusted(24,42,-24,-24);
    const auto scale = std::min(available.width()/size.width, available.height()/size.height);
    const QSizeF fitted(size.width*scale,size.height*scale);
    return QRectF(available.center()-QPointF(fitted.width()/2,fitted.height()/2),fitted);
}
QPointF pivot(const CompositionViewer& viewer, CanvasSize size) {
    check(viewer.selectedLayerAnchor().has_value(), "visible pivot");
    const auto rect = canvasRect(viewer,size);
    const auto anchor = *viewer.selectedLayerAnchor();
    return {rect.left()+anchor.x()*rect.width(),rect.top()+anchor.y()*rect.height()};
}
QPointF pixels(const QWidget& viewer, CanvasSize size, QPointF displacement) {
    const auto rect = canvasRect(viewer,size);
    return {displacement.x()*size.width/rect.width(),displacement.y()*size.height/rect.height()};
}
void discardClose(MainWindow& window) {
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            dialog->button(QMessageBox::Discard)->click();
    });
    window.close(); QApplication::processEvents();
}
void newComposition(MainWindow& window) {
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget(); check(dialog,"new composition dialog");
        widget<QLineEdit>(*dialog,"motion-canvas-width")->setText("400");
        widget<QLineEdit>(*dialog,"motion-canvas-height")->setText("200");
        widget<QComboBox>(*dialog,"motion-frame-rate")->setCurrentIndex(4);
        widget<QDialogButtonBox>(*dialog,"motion-new-composition-buttons")->button(QDialogButtonBox::Ok)->click();
    });
    widget<QAction>(window,"motion-new-composition-action")->trigger();
}

void calculationAndPixels(const QString& root) {
    for (const auto scales : {std::array{1.,1.},std::array{2.,.5},std::array{.0125,30.}})
    for (const double angle : {0.,33.,90.,-151.}) {
        auto parent = shape(10); parent.kind = LayerKind::Null; parent.content = std::monostate{};
        parent.transform = {.1,-.05,1.5,21,1}; parent.scale_y = ScaleChannel{.75,{}};
        parent.visible = false; parent.timeline_start_frame = 30; parent.duration_frames = 1;
        auto layer = shape(1); layer.parent_id = 10; layer.transform = {.5,.5,scales[0],angle,1};
        layer.scale_y = ScaleChannel{scales[1],{}}; layer.anchor_point = {-73.125,100.5};
        layer.parent_affine = AffineTransform{1,.2,.3,1,5,-8};
        auto child = shape(2); child.parent_id = 1;
        CompositionDocument document(400,200,{30,1},{child,layer,parent});
        const auto before = resolve(document,10);
        const AnchorPoint next{13.75,-22.125};
        const auto values = compensatedAnchorValues(layer.transform,scales[1],document.canvasSize(),layer.anchor_point,next);
        // Independent Qt geometry, including the local pixel and non-square canvas spaces.
        QTransform rs; rs.rotate(angle); rs.scale(scales[0],scales[1]);
        const auto delta = rs.map(QPointF(next.x_pixels-layer.anchor_point.x_pixels,next.y_pixels-layer.anchor_point.y_pixels));
        check(near(values.position_x,.5+delta.x()/400) && near(values.position_y,.5+delta.y()/200), "independent compensated position");
        check(document.repositionLayerAnchorKeepingPose(1,next,10), "atomic document compensation");
        const auto after = resolve(document,10); same(before.at(1).world,after.at(1).world); same(before.at(2).world,after.at(2).world);
        check(document.layers()[1].parent_affine == layer.parent_affine, "binding intact");
    }
    bool rejected = false;
    try { (void)compensatedAnchorValues({},1,{400,200},{},{std::numeric_limits<double>::infinity(),0}); }
    catch (const std::exception&) { rejected = true; }
    check(rejected,"nonfinite compensation rejected");

    // Same visible pixels and source-cache identity after the exact drag commit.
    for (const auto kind : {LayerKind::Shape,LayerKind::Text,LayerKind::Image}) {
        auto layer = shape(1); layer.kind = kind; layer.duration_frames = 1; layer.transform.rotation_degrees = 90;
        layer.transform.scale = 2; layer.scale_y = ScaleChannel{.5,{}};
        if (kind == LayerKind::Text) { auto text=defaultTextLayerContent({400,200}); text.text="Pivot";layer.content=text; }
        if (kind == LayerKind::Image) { layer.content=std::monostate{}; layer.source_path=path(root+"/offline.png"); }
        layer.effects = {ColorAdjustmentEffect{true,4,100,100},GaussianBlurEffect{true,1}};
        CompositionDocument document(400,200,{30,1},{layer});
        check(document.createMask(1,-2,creative_suite::composition::ShapeMaskKind::Rectangle)!=0,"source mask");
        check(document.createMask(1,-1,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"transform mask");
        check(document.createMask(1,0,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"effect mask");
        CompositionFrameRenderer renderer;
        auto source=std::make_shared<creative_suite::media::RgbaFrame>();source->width=20;source->height=10;source->stride=80;source->rgba_pixels.resize(800,255);
        auto request = [&] {
            PreviewRequest r; r.canvas_size=document.canvasSize();r.frame_rate={30,1};r.composition_frame=0;
            r.hierarchy=std::make_shared<const LayerTransformHierarchy>(document.canvasSize(),document.layers());
            r.masks=std::make_shared<const std::vector<Mask>>(document.masks());
            const auto& l=document.layers()[0];PreviewLayerSnapshot s;s.id=1;s.kind=kind;s.transform=l.transform;s.scale_y=l.scale_y;
            s.anchor_point=l.anchor_point;s.content=l.content;s.effects=l.effects;s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;s.still_frame=kind==LayerKind::Image?source:nullptr;
            r.layers.push_back(s);return r;
        };
        auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
        const auto original=renderer.render(request());check(original!=nullptr,"reference pixels");
        std::array<creative_suite::media::RgbaFramePtr,3> mask_views;
        for(int stage=-2;stage<=0;++stage) { auto view=request();view.mask_view_layer=1;view.mask_view_target=stage;mask_views[stage+2]=renderer.render(view); }
        const MotionExportSnapshot export_before{document.canvasSize(),document.frameRate(),document.layers(),{{1,source}},document.masks()};
        (void)metrics.takeSnapshotAndReset();
        check(document.repositionLayerAnchorKeepingPose(1,{12,-8},0),"pixel compensation");
        const auto edited=renderer.render(request());check(edited && edited->rgba_pixels==original->rgba_pixels,"unchanged rendered pixels");
        const auto counters=metrics.takeSnapshotAndReset();
        check(counters&&counters->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"reuse text/shape raster cache");
        check(counters->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::Decode)].count==0,"no additional decode for compensated geometry");
        const auto playback=renderer.render(request(),{},false,PreviewRequestMode::Playback);
        check(playback&&playback->rgba_pixels==original->rgba_pixels,"playback shares compensated geometry");
        for(int stage=-2;stage<=0;++stage) { auto view=request();view.mask_view_layer=1;view.mask_view_target=stage;const auto result=renderer.render(view);
            check(result&&mask_views[stage+2]&&result->rgba_pixels==mask_views[stage+2]->rgba_pixels,"mask View retains stage geometry"); }
        const MotionExportSnapshot export_after{document.canvasSize(),document.frameRate(),document.layers(),{{1,source}},document.masks()};
        MotionExportSettings settings{path(root+"/before.mkv"),"matroska","ffv1",400,200,{30,1},1};
        std::atomic_bool cancel{false};MotionVideoExporter::exportVideo(export_before,settings,cancel);
        auto other=settings;other.output_path=path(root+"/after.mkv");MotionVideoExporter::exportVideo(export_after,other,cancel);
        auto pre=creative_suite::media::VideoPlaybackSession::open(settings.output_path),post=creative_suite::media::VideoPlaybackSession::open(other.output_path);
        const auto a=pre->decode_next_frame(),b=post->decode_next_frame();
        check(a&&b&&(*a)->rgba_pixels==(*b)->rgba_pixels,"export pixels preserved after identical codec conversion");
        Store::save(path(root+"/pixels.motion"),project(document));
        check(Store::load(path(root+"/pixels.motion")).layers==document.layers(),"committed values round trip in v12");
    }
}

void viewerGestures() {
    CompositionViewer viewer;viewer.resize(848,466);viewer.setComposition({400,200},QPointF(.5,.5));
    int begins=0,commits=0,cancels=0;QPointF last;
    viewer.setPanBehindHandlers([&]{++begins;return true;},[&](const QPointF& delta){last=delta;return true;},[&]{++commits;},[&]{++cancels;});
    viewer.setPanBehindEnabled(true);viewer.show();QApplication::processEvents();
    auto start=pivot(viewer,{400,200});
    press(viewer,start+QPointF(11,0));release(viewer,start+QPointF(40,0));check(begins==0,"outside hit radius");
    mouse(viewer,QEvent::MouseButtonPress,start,Qt::RightButton,Qt::RightButton);check(begins==0,"right button preserved");
    press(viewer,start+QPointF(9,0));release(viewer,start+QPointF(9,0));check(begins==1&&commits==0&&cancels==1,"edge hit and click no-op");
    const auto grab=start+QPointF(7,1);press(viewer,grab);
    move(viewer,grab+QPointF(QApplication::startDragDistance()-1,0));check(last.isNull(),"platform threshold");
    move(viewer,grab+QPointF(30,20));
    const auto expected=pixels(viewer,{400,200},{30,20});check(near(last.x(),expected.x())&&near(last.y(),expected.y()),"press offset retained in logical pixels");
    release(viewer,grab+QPointF(60,40));check(commits==1,"release commits once");
    const auto final=pixels(viewer,{400,200},{60,40});check(near(last.x(),final.x())&&near(last.y(),final.y()),"release uses final coordinates without last move");
    press(viewer,start);move(viewer,QPointF(-40,-50));release(viewer,QPointF(-80,-90));check(commits==2,"capture continues outside viewer");
    for (const auto type : {QEvent::UngrabMouse,QEvent::WindowDeactivate,QEvent::Hide}) {
        press(viewer,start);move(viewer,start+QPointF(30,0));QEvent event(type);QApplication::sendEvent(&viewer,&event);
        check(!viewer.panBehindActive(),"lifecycle cancels gesture");release(viewer,start+QPointF(40,0));
    }
    press(viewer,start);viewer.resize(700,400);check(!viewer.panBehindActive(),"resize cancels frozen mapping");
    start=pivot(viewer,{400,200});press(viewer,start);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&viewer,&escape);
    check(!viewer.panBehindActive(),"Escape cancels locally");
    // Off-canvas markers inside the gutter can be grabbed; off-widget markers cannot.
    viewer.setSelectedLayerAnchor(QPointF(-.01,.5));start=pivot(viewer,{400,200});press(viewer,start);check(viewer.panBehindActive(),"gutter marker editable");viewer.cancelPanBehind();
    viewer.setSelectedLayerAnchor(QPointF(-100,.5));press(viewer,{2,200});check(!viewer.panBehindActive(),"invisible outside marker cannot start");
    viewer.setSelectedLayerAnchor(QPointF(.5,.5));start=pivot(viewer,{400,200});press(viewer,start);
    viewer.setComposition({200,400},QPointF(.5,.5));check(!viewer.panBehindActive(),"document replacement cancels capture");
    viewer.enableTexturePresentation({});
    auto* surface=viewer.findChild<QOpenGLWidget*>("motion-gpu-composition-surface");
    check(surface&&surface->testAttribute(Qt::WA_TransparentForMouseEvents),"GPU child leaves pointer input to viewer");
    viewer.hide();
}

void inspectorHistoryAndRecovery(const QString& root) {
    MainWindow window(nullptr,path(root+"/recovery"),"pan-behind");window.show();newComposition(window);
    widget<QAction>(window,"motion-new-rectangle-layer-action")->trigger();QApplication::processEvents();
    auto* viewer=widget<CompositionViewer>(window,"motion-composition-viewer");
    auto* timeline=window.findChild<TimelineNavigator*>();check(timeline,"timeline");
    auto* anchor_x=widget<QDoubleSpinBox>(window,"motion-anchor-x");
    auto* anchor_y=widget<QDoubleSpinBox>(window,"motion-anchor-y");
    auto* position_x=widget<QDoubleSpinBox>(window,"motion-transform-position-x");
    auto* position_y=widget<QDoubleSpinBox>(window,"motion-transform-position-y");
    auto* undo=widget<QAction>(window,"motion-undo-action");auto* redo=widget<QAction>(window,"motion-redo-action");
    const auto initial=window.compositionDocument()->layers();const bool dirty=window.isWindowModified();
    auto start=pivot(*viewer,{400,200})+QPointF(7,0);const QPointF delta(53,29);
    widget<QAction>(window,"motion-play-pause-action")->trigger();check(timeline->isPlaying(),"playback starts");
    press(*viewer,start);check(!timeline->isPlaying(),"press pauses playback");
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
    const auto frame=viewer->renderedFrame();move(*viewer,start+delta);
    const auto dp=pixels(*viewer,{400,200},delta);
    check(near(anchor_x->value(),dp.x(),1e-6)&&near(anchor_y->value(),dp.y(),1e-6),"Inspector live Anchor");
    check(near(position_x->value(),.5+dp.x()/400,1e-6)&&near(position_y->value(),.5+dp.y()/200,1e-6),"Inspector live Position");
    check(anchor_x->isReadOnly()&&anchor_y->isReadOnly()&&position_x->isReadOnly()&&position_y->isReadOnly(),"draft fields readonly");
    check(window.compositionDocument()->layers()==initial&&window.isWindowModified()==dirty,"draft leaves document and dirty state unchanged");
    check(viewer->renderedFrame()==frame,"draft reuses displayed frame");
    if(const auto snapshot=metrics.takeSnapshotAndReset())check(snapshot->requests==0,"no per-move render request");
    auto* timer=widget<QTimer>(window,"motion-autosave-timer");QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/recovery"),"reader");
    const auto snapshots=recovery.unsavedSnapshots();check(!snapshots.empty(),"autosave during draft");
    check(Store::loadRecovery(snapshots.front().path).document.layers==initial,"autosave only committed document");
    move(*viewer,start+QPointF(3,1));move(*viewer,start+delta);release(*viewer,start+delta+QPointF(11,3));
    const auto committed=window.compositionDocument()->layers();check(committed!=initial,"release publishes edit");
    undo->trigger();check(window.compositionDocument()->layers()==initial&&redo->isEnabled(),"single Undo restores all values");
    // Cancellation never clears the existing redo branch.
    start=pivot(*viewer,{400,200});press(*viewer,start);move(*viewer,start+delta);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(viewer,&escape);
    check(window.compositionDocument()->layers()==initial&&redo->isEnabled(),"Escape preserves redo");
    press(*viewer,start);move(*viewer,start+delta);undo->trigger();check(window.compositionDocument()->layers()==initial&&redo->isEnabled(),"Undo cancels gesture only");
    press(*viewer,start);move(*viewer,start+delta);release(*viewer,start);check(window.compositionDocument()->layers()==initial&&redo->isEnabled(),"return to origin has no history");
    redo->trigger();check(window.compositionDocument()->layers()==committed,"one Redo restores entire gesture");
    for (int reason=0;reason<3;++reason) {
        start=pivot(*viewer,{400,200});press(*viewer,start);move(*viewer,start+delta);
        if(reason==0)timeline->setCurrentFrame(5);
        if(reason==1)widget<QAction>(window,"motion-new-null-layer-action")->trigger();
        if(reason==2)widget<QAction>(window,"motion-previous-frame-action")->trigger();
        check(!viewer->panBehindActive(),"frame/selection/other action cancels before proceeding");
        release(*viewer,start+delta);
    }
    // Nulls support the same gesture without producing any pixels.
    timeline->setCurrentFrame(5);
    const auto null_before=window.compositionDocument()->layers();start=pivot(*viewer,{400,200});press(*viewer,start);move(*viewer,start+delta);release(*viewer,start+delta);
    check(window.compositionDocument()->layers().back().kind==LayerKind::Null&&window.compositionDocument()->layers().back().anchor_point!=null_before.back().anchor_point,"Null pivot drag");
    undo->trigger();check(window.compositionDocument()->layers()==null_before,"Null atomic Undo");
    auto* rows=widget<QWidget>(window,"motion-timeline-layer-rows");
    start=pivot(*viewer,{400,200});press(*viewer,start);move(*viewer,start+delta);
    press(*rows,{10,17});release(*rows,{10,17});check(!viewer->panBehindActive()&&!viewer->selectedLayerAnchor(),"hiding the selected layer cancels and removes marker");
    press(*viewer,start);check(!viewer->panBehindActive(),"hidden selected layer cannot start");
    press(*rows,{10,17});release(*rows,{10,17});
    timeline->setCurrentFrame(200);start=pivot(*viewer,{400,200});press(*viewer,start);check(!viewer->panBehindActive(),"outside interval unavailable");
    timeline->setCurrentFrame(5);
    // Limit refusal is atomic, does not log an expected user boundary as an error.
    start=pivot(*viewer,{400,200});const auto kept=window.compositionDocument()->layers();press(*viewer,start);move(*viewer,start+QPointF(1e12,0));
    check(!viewer->panBehindActive()&&window.compositionDocument()->layers()==kept,"out-of-range gesture rejected entirely");
    // Dirty state and Undo branch after a save-command cancellation.
    start=pivot(*viewer,{400,200});press(*viewer,start);move(*viewer,start+delta);
    QTimer::singleShot(0,[]{auto* d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(d,"save dialog");d->reject();});
    widget<QAction>(window,"motion-save-composition-as-action")->trigger();check(!viewer->panBehindActive()&&window.compositionDocument()->layers()==kept,"save cancels draft before dialog");
    discardClose(window);
}

void animatedAffineUi(const QString& root) {
    auto layer=shape(1);layer.transform={.45,.55,.75,-35,1};layer.scale_y=ScaleChannel{2.5,{}};layer.anchor_point={-2.125,3.5};
    layer.keyframes.position_x={{0,.4,InterpolationMode::CubicBezier,{.25,.1,.25,1}},{20,.6}};
    layer.anchor_keyframes[0]={{0,-2.125},{20,5.25}};
    auto parent=shape(2);parent.kind=LayerKind::Image;parent.content=std::monostate{};parent.source_path=path(root+"/missing.png");
    parent.visible=false;parent.timeline_start_frame=30;parent.duration_frames=1;parent.transform={-.25,-1,2,30,1};parent.scale_y=ScaleChannel{1,{}};
    layer.parent_id=2;layer.parent_affine=AffineTransform{1,.1,.2,1,0,0};
    auto child=shape(3);child.parent_id=1;
    CompositionDocument original(400,200,{30,1},{layer,parent,child});
    const auto filename=root+"/affine.motion";Store::save(path(filename),project(original));
    MainWindow window(nullptr,path(root+"/affine-recovery"),"affine-pan");window.show();
    QTimer::singleShot(0,[filename]{auto* d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(d,"open dialog");d->selectFile(filename);QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});
    widget<QAction>(window,"motion-open-composition-action")->trigger();waitFor([&]{return window.compositionDocument()!=nullptr;});
    auto* timeline=window.findChild<TimelineNavigator*>();check(timeline,"affine timeline");timeline->setCurrentFrame(10);
    // Select the animated layer via the public timeline selection handler's UI.
    auto* rows=widget<QWidget>(window,"motion-timeline-layer-rows");press(*rows,{60,85});release(*rows,{60,85});
    auto* viewer=widget<CompositionViewer>(window,"motion-composition-viewer");
    const auto before=window.compositionDocument()->layers();const auto previous=resolve(*window.compositionDocument(),10);
    const auto old_anchor=evaluateLayerAnchor(layer,10);
    QTransform local;local.translate(evaluateTransform(layer.transform,layer.keyframes,10).position_x*400,layer.transform.position_y*200);
    local.rotate(-35);local.scale(.75,2.5);local.translate(-old_anchor.x_pixels,-old_anchor.y_pixels);
    QTransform binding(1,.1,.2,1,0,0),ancestor;ancestor.translate(-100,-200);ancestor.rotate(30);ancestor.scale(2,1);
    const auto reference=local*binding*ancestor;bool invertible=false;const auto inverse=reference.inverted(&invertible);check(invertible,"independent reference inverse");
    const auto start=pivot(*viewer,{400,200})+QPointF(4,3);const QPointF delta(25,17);const auto dp=pixels(*viewer,{400,200},delta);
    const auto da=inverse.map(dp)-inverse.map(QPointF(0,0));
    press(*viewer,start);move(*viewer,start+delta);
    auto* x=widget<QDoubleSpinBox>(window,"motion-anchor-x");auto* y=widget<QDoubleSpinBox>(window,"motion-anchor-y");
    check(near(x->value(),old_anchor.x_pixels+da.x(),1e-6)&&near(y->value(),old_anchor.y_pixels+da.y(),1e-6),"full inverse includes rotated unequal parent and binding shear");
    check(window.compositionDocument()->layers()==before,"animated draft creates no keys");
    release(*viewer,start+delta);const auto after=window.compositionDocument()->layers();
    check(after[0].keyframes.position_x.size()==3&&after[0].anchor_keyframes[0].size()==3,"insert animated keys between keys on release");
    check(after[0].keyframes.position_x.front()==before[0].keyframes.position_x.front()&&after[0].keyframes.position_x.back()==before[0].keyframes.position_x.back(),"other easing/keys preserved");
    const auto current=resolve(*window.compositionDocument(),10);same(previous.at(1).world,current.at(1).world);same(previous.at(3).world,current.at(3).world);
    auto dialog_reference=original;check(dialog_reference.repositionLayerAnchorKeepingPose(1,evaluateLayerAnchor(after[0],10),10),"dialog model reference");
    check(dialog_reference.layers()==after,"dialog and gesture commit parity");
    Store::save(path(root+"/committed.motion"),project(*window.compositionDocument()));
    check(Store::load(path(root+"/committed.motion")).layers==after,"saved animated draft is v12 data");
    Store::saveRecovery(path(root+"/committed.recovery"),path(filename),"pan",project(*window.compositionDocument()));
    check(Store::loadRecovery(path(root+"/committed.recovery")).document.layers==after,"recovery keeps committed animated result");
    // Saving via the normal action cancels the in-progress draft and retains the last commit.
    const auto next=pivot(*viewer,{400,200});press(*viewer,next);move(*viewer,next+delta);
    widget<QAction>(window,"motion-save-composition-action")->trigger();check(!viewer->panBehindActive(),"Save cancels gesture");
    check(Store::load(path(filename)).layers==after,"Save excludes provisional values");
    press(*viewer,pivot(*viewer,{400,200}));move(*viewer,pivot(*viewer,{400,200})+QPointF(30,0));
    discardClose(window);check(window.compositionDocument()->layers()==after,"close cancels a draft without dirty prompt");
}

void numericFailureLog(const QString& root) {
    auto layer=shape(9);layer.transform={0,0,1e-308,0,1};
    const CompositionDocument original(400,200,{30,1},{layer});const auto filename=root+"/tiny.motion";
    Store::save(path(filename),project(original));
    MainWindow window(nullptr,path(root+"/tiny-recovery"),"tiny-pan");window.show();
    QTimer::singleShot(0,[filename]{auto* d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(d,"numeric fixture picker");d->selectFile(filename);QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});
    widget<QAction>(window,"motion-open-composition-action")->trigger();waitFor([&]{return window.compositionDocument()!=nullptr;});
    auto* rows=widget<QWidget>(window,"motion-timeline-layer-rows");press(*rows,{60,17});release(*rows,{60,17});
    auto* viewer=widget<CompositionViewer>(window,"motion-composition-viewer");const auto start=pivot(*viewer,{400,200});
    const auto log=creative_suite::diagnostics::Logger::instance().log_path();
    QFile before(QString::fromStdU16String(log.u16string()));check(before.open(QIODevice::ReadOnly),"read diagnostic log");const auto previous=before.size();before.close();
    press(*viewer,start);check(viewer->panBehindActive(),"finite tiny geometry can begin");move(*viewer,start+QPointF(100,0));
    check(!viewer->panBehindActive()&&window.compositionDocument()->layers()==original.layers(),"overflow cancels without mutation");
    QFile after(QString::fromStdU16String(log.u16string()));check(after.open(QIODevice::ReadOnly)&&after.seek(previous),"read numeric failure entry");
    const auto entries=after.readAll();check(entries.contains("pan_behind_preview")&&entries.contains("layer_id=\"9\"")&&entries.contains("frame=\"0\""),"technical failure logs operation/layer/frame");
    discardClose(window);
}
} // namespace

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionPanBehind");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try {
        check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");
        calculationAndPixels(root.path());viewerGestures();inspectorHistoryAndRecovery(root.path());animatedAffineUi(root.path());numericFailureLog(root.path());
        std::cout<<"Pan Behind tests passed (DPR "<<app.devicePixelRatio()<<").\n";return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
