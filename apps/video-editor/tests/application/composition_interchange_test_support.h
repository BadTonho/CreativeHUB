#pragma once
#include "application/composition_interchange.h"
#include "application/editor_session.h"
#include "application/project_document_mapper.h"
#include "application/timeline_command_service.h"
#include "model/composition_interchange.h"
#include "persistence/motion_document_store.h"
#include "project/autosave_manager.h"
#include "project/project_file.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/graph_generators.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include <QApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <cmath>
#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition_exchange/raster.h>
#include <creative_suite/media/still_image_decoder.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_probe.h>
#include <cstdlib>
#include <iostream>
#include <set>

namespace x = creative_suite::composition_exchange;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
template <class F> void rejected(F f, const char *message) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, message);
}
creative_suite::media::RgbaFramePtr fusionFrame(const x::Document &d, int frame, int gpu_mode=0) {
    auto graph = application::importComposition(d);
    std::set<std::pair<double, double>> positions;
    for (const auto &n : graph.nodes)
        check(positions.emplace(n.x, n.y).second, "node positions overlap");
    fusion::nodes::InputFrames inputs;
    for (const auto &n : graph.nodes) {
        if (n.type != fusion::nodes::NodeType::Input)
            continue;
        if (n.source_is_still)
            inputs[n.id] = std::make_shared<const creative_suite::media::RgbaFrame>(
                creative_suite::media::StillImageDecoder{}.decode_first_frame(n.source_path));
        else {
            auto index =
                x::sourceFrame(*n.presentation, frame, n.source_frame_rate, n.source_frame_count,
                               double(d.fps_numerator) / d.fps_denominator);
            if (index) {
                auto decoder = creative_suite::media::VideoPlaybackSession::open(n.source_path);
                auto decoded = decoder->decode_frame_at(*index);
                if (decoded && *decoded)
                    inputs[n.id] = *decoded;
            }
        }
    }
    rendering::prepareGraphFrames(graph, inputs, d.width, d.height, true, {}, frame);
    fusion::nodes::EvaluationContext context;
    context.local_frame = frame;
    context.canvas_width = d.width;
    context.canvas_height = d.height;
    auto result = fusion::nodes::evaluate(graph, inputs, context);
    if (gpu_mode) {
        using namespace creative_suite::composition;
        QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
        QOffscreenSurface surface;surface.setFormat(format);surface.create();
        auto budget = gpu_mode==2 ? std::make_shared<OpenGlTexturePoolBudget>(1) : std::shared_ptr<OpenGlTexturePoolBudget>{};
        OpenGlFrameCompositor backend(&surface,OpenGlPrecisionPolicy::Automatic,nullptr,budget);
        bool used=false;context.gpu=&backend;context.gpu_used=&used;
        auto evaluated=fusion::nodes::evaluateFrame(graph,inputs,context);
        check(bool(evaluated),"GPU graph/fallback frame missing");
        if(evaluated->texture){auto rgba=backend.readback(evaluated->texture);check(bool(rgba.frame),"GPU readback");result=std::move(rgba.frame);}
        else result=std::move(evaluated->rgba);
        std::cout<<"GPU graph mode "<<gpu_mode<<" used "<<used<<'\n';
        if (gpu_mode==1 && surface.isValid() && QOpenGLContext::supportsThreadedOpenGL())
            check(used,"available GPU unexpectedly fell back for a compatible graph");
        if(gpu_mode==2)check(!used,"resource-limited graph must recover on CPU");
        evaluated.reset();
    }
    check(bool(result), "Fusion frame missing");
    auto output =
        creative_suite::composition::FrameCompositor::compose(d.width, d.height, {{&*result, {}}});
    check(bool(output), "Fusion composite missing");
    return std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*output));
}
creative_suite::media::RgbaFramePtr motionFrame(const motion::model::MotionProjectData &d,
                                                int frame, bool gpu_enabled=false, bool* used=nullptr) {
    motion::ui::PreviewRequest request;
    request.canvas_size = d.composition.canvas_size;
    request.frame_rate = d.composition.frame_rate;
    request.black_canvas_when_empty = true;
    request.masks=std::make_shared<const std::vector<motion::model::Mask>>(d.masks);request.composition_frame=frame;
    for (const auto &l : d.layers) {
        if (!l.visible || frame < l.timeline_start_frame ||
            frame - l.timeline_start_frame >= l.duration_frames)
            continue;
        motion::ui::PreviewLayerSnapshot s;
        s.id = l.id;
        s.kind = l.kind;
        s.local_frame = frame - l.timeline_start_frame;
        s.transform = l.transform;
        s.keyframes = l.keyframes;
        s.content = l.content;
        s.effects = l.effects;
        s.presentation = l.presentation;
        s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;
        s.source_path = l.source_path;
        s.source_frame_count = l.source_frame_count;
        s.source_frame_rate = l.source_frame_rate;
        s.source_start_frame = l.source_start_frame;
        if (l.kind == motion::model::LayerKind::Image)
            s.still_frame = std::make_shared<const creative_suite::media::RgbaFrame>(
                creative_suite::media::StillImageDecoder{}.decode_first_frame(l.source_path));
        request.layers.push_back(std::move(s));
    }
    std::unique_ptr<QOffscreenSurface> surface;
    if(gpu_enabled){surface=std::make_unique<QOffscreenSurface>();QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);surface->setFormat(format);surface->create();}
    motion::ui::CompositionGpuMetrics metrics;
    motion::ui::CompositionFrameRenderer renderer(false,gpu_enabled,surface.get(),&metrics);
    auto result=renderer.render(request, {}, true);
    if(used)*used=metrics.composition_frames>0;
    renderer.shutdown();return result;
}
void compare(const x::Document &d, const motion::model::MotionProjectData &project, int frame,
             const char *message) {
    auto a = fusionFrame(d, frame), b = motionFrame(project, frame);
    check(bool(b) && a->rgba_pixels.size() == b->rgba_pixels.size(), "render geometry");
    int maximum = 0;
    for (std::size_t i = 0; i < a->rgba_pixels.size(); ++i)
        maximum = std::max(maximum, std::abs(int(a->rgba_pixels[i]) - int(b->rgba_pixels[i])));
    std::cout << "frame " << frame << " maximum channel difference " << maximum << '\n';
    // Straight RGBA intermediates and final source-over composition round at
    // different boundaries in the two existing renderers.
    check(maximum <= 2, message);
}
} // namespace
