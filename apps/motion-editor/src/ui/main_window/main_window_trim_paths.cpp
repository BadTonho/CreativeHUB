#include "main_window.h"
#include "ui/inspector/trim_paths_inspector.h"
#include "ui/timeline/timeline_navigator.h"
#include <algorithm>
#include <stdexcept>
namespace motion::ui {
void MainWindow::editTrimPaths(std::function<bool(model::CompositionDocument &)> edit) {
    finishPendingTransformEdit();
    if (!document_ || export_worker_ || audio_keyframe_worker_)
        return;
    try {
        auto before = captureEditState();
        if (edit(*document_)) {
            (void)recordCompositionEdit(std::move(before));
            refreshTimeline();
            updateDocumentState();
            refreshCurveEditor();
            requestPreview();
        }
        syncTransformInspector();
    } catch (const std::exception &e) {
        reportDocumentError("edit_trim_paths", document_path_.value_or(std::filesystem::path{}),
                            std::runtime_error("Layer " + std::to_string(selected_layer_id_) + ", frame " +
                                               std::to_string(timeline_->currentFrame()) + ": " + e.what()));
        syncTransformInspector();
        requestPreview();
    }
}
void MainWindow::selectTrimCurveSegment(model::LayerId id, model::TrimPathProperty property,
                                        std::int64_t frame) {
    if (!document_ || !model::validTrimProperty(property))
        return;
    const auto *animation = document_->layerTrimPaths(id);
    if (!animation)
        return;
    const auto &keys = animation->get().keys[static_cast<std::size_t>(property)];
    std::int64_t segment = 0;
    if (keys.size() >= 2) {
        const auto at = std::upper_bound(keys.begin(), keys.end(), frame,
                                         [](auto f, const auto &k) { return f < k.frame; });
        const auto i = at == keys.begin() ? 0 : std::min(std::size_t(at - keys.begin() - 1), keys.size() - 2);
        segment = keys[i].frame;
    }
    CurveSelection selection;
    selection.layer_id = id;
    selection.segment_start_frame = segment;
    selection.trim_property = property;
    curve_selection_ = selection;
    if (trim_paths_inspector_)
        trim_paths_inspector_->selectProperty(property);
    refreshCurveEditor();
}
} // namespace motion::ui
