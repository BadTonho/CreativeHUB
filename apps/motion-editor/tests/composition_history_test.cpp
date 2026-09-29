#include "ui/composition_history.h"

#include <creative_suite/animation/animation.h>

#include <cstdlib>
#include <iostream>

namespace {

using motion::model::CompositionDocument;
using motion::model::LayerKind;
using motion::ui::CompositionEditState;
using motion::ui::CompositionHistory;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

CompositionEditState state(const CompositionDocument& document,
                           motion::model::LayerId selected = 0)
{
    return {document, selected};
}

void testUndoRedoAndSelection()
{
    CompositionHistory history;
    motion::model::CompositionLayer back_layer{};
    back_layer.id = 1;
    back_layer.kind = LayerKind::Image;
    back_layer.name = "Back";
    back_layer.source_path = "back.png";
    back_layer.duration_frames = 20;
    motion::model::CompositionLayer front_layer{};
    front_layer.id = 2;
    front_layer.kind = LayerKind::Image;
    front_layer.name = "Front";
    front_layer.source_path = "front.png";
    front_layer.timeline_start_frame = 2;
    front_layer.duration_frames = 10;
    const auto back = back_layer.id;
    const auto front = front_layer.id;
    CompositionDocument document(640, 360, {24, 1}, {back_layer, front_layer});
    const auto before = state(document, front);
    require(history.canUndo() == false && history.canRedo() == false,
            "new history starts empty");

    require(document.setLayerVisible(front, false), "visibility mutation succeeds");
    require(document.moveLayer(front, 0), "layer reorder succeeds");
    require(document.moveLayerInTimeline(front, 5), "timeline movement succeeds");
    require(document.resizeLayerDuration(front, 8), "duration resize succeeds");
    auto transform = document.layers().front().transform;
    transform.position_x = 0.75;
    require(document.setLayerTransform(front, transform), "transform mutation succeeds");
    require(document.setLayerKeyframe(
                front, creative_suite::animation::TransformProperty::PositionX, 4, 0.75),
            "keyframe mutation succeeds");
    require(document.moveLayerKeyframe(
                front, creative_suite::animation::TransformProperty::PositionX, 4, 6),
            "keyframe movement succeeds");
    history.recordBeforeEdit(before);
    auto restored = history.undo(state(document));
    require(restored.has_value() && restored->selected_layer_id == front &&
                restored->document.layers().size() == 2 &&
                restored->document.layers()[0].id == back &&
                restored->document.layers()[1].id == front &&
                restored->document.layers()[1].visible &&
                restored->document.layers()[1].timeline_start_frame == 2 &&
                restored->document.layers()[1].duration_frames == 10 &&
                restored->document.layers()[1].transform.position_x == 0.5 &&
                restored->document.layers()[1].keyframes.position_x.empty() &&
                !history.canUndo() && history.canRedo(),
            "Undo restores layer order, timing, visibility, transforms, keyframes, and selection");

    auto redone = history.redo(*restored);
    require(redone.has_value() && redone->document.layers()[0].id == front &&
                !redone->document.layers()[0].visible &&
                redone->document.layers()[0].timeline_start_frame == 5 &&
                redone->document.layers()[0].duration_frames == 8 &&
                redone->document.layers()[0].transform.position_x == 0.75 &&
                redone->document.layers()[0].keyframes.position_x ==
                    std::vector<creative_suite::animation::Keyframe>{{6, 0.75}} &&
                !history.canRedo() && history.canUndo(),
            "Redo reapplies the complete composition edit state");
}

void testBranchAndBoundedHistory()
{
    CompositionHistory history;
    CompositionDocument document(320, 200, {30, 1});
    auto before = state(document);
    (void)document.addLayer(LayerKind::Shape, "First");
    history.recordBeforeEdit(std::move(before));
    auto prior = history.undo(state(document));
    require(prior.has_value() && prior->document.layers().empty(),
            "the first edit can be undone");

    before = state(prior->document);
    auto branched_document = prior->document;
    (void)branched_document.addLayer(LayerKind::Text, "Branch");
    history.recordBeforeEdit(std::move(before));
    require(!history.canRedo() && branched_document.layers().front().name == "Branch",
            "a new edit after Undo clears Redo and creates a new branch");

    history.clear();
    CompositionDocument bounded(320, 200, {30, 1});
    for (std::size_t index = 0; index < CompositionHistory::max_states + 1; ++index) {
        auto snapshot = state(bounded);
        (void)bounded.addLayer(LayerKind::Shape, "Layer");
        history.recordBeforeEdit(std::move(snapshot));
    }
    require(history.undoCount() == CompositionHistory::max_states,
            "history is bounded to 100 states");
    for (std::size_t index = 0; index < CompositionHistory::max_states; ++index) {
        auto previous = history.undo(state(bounded));
        require(previous.has_value(), "every retained state can be undone");
        bounded = std::move(previous->document);
    }
    require(bounded.layers().size() == 1 && !history.canUndo(),
            "oldest history entries are trimmed without corrupting retained states");
}

void testCoalescedInspectorEdits()
{
    CompositionHistory history;
    CompositionDocument document(640, 360, {24, 1});
    const auto layer = document.addLayer(LayerKind::Shape, "Transform");
    const auto before = state(document, layer);
    auto transform = document.layers().front().transform;
    transform.position_x = 0.6;
    require(document.setLayerTransform(layer, transform), "first field edit succeeds");
    history.beginCoalescedEdit(before);
    transform.position_x = 0.8;
    require(document.setLayerTransform(layer, transform), "second field edit succeeds");
    history.finishCoalescedEdit(state(document, layer));
    require(history.undoCount() == 1 && !history.canRedo(),
            "a continuous inspector edit creates one undo step");

    auto original = history.undo(state(document, layer));
    require(original.has_value() &&
                original->document.layers().front().transform.position_x == 0.5,
            "Undo restores the value before the coalesced inspector interaction");
    auto redo_target = document;
    require(history.canRedo(), "Undo exposes the edit for Redo");

    const auto no_op_before = state(original->document, layer);
    history.beginCoalescedEdit(no_op_before);
    history.finishCoalescedEdit(state(original->document, layer));
    require(history.canRedo() && history.undoCount() == 0,
            "a coalesced no-op does not erase a valid Redo state");
    auto reapplied = history.redo(*original);
    require(reapplied.has_value() &&
                reapplied->document.layers().front().transform.position_x == 0.8 &&
                redo_target.layers().front().transform.position_x == 0.8,
            "Redo restores the coalesced final value");
}

void testRestoredLayerIdAllocator()
{
    CompositionHistory history;
    CompositionDocument document(320, 200, {24, 1});
    const auto first = document.addLayer(LayerKind::Shape, "First");
    auto before_second = state(document, first);
    const auto second = document.addLayer(LayerKind::Shape, "Second");
    history.recordBeforeEdit(std::move(before_second));
    auto undone = history.undo(state(document, second));
    require(undone.has_value() && undone->document.layers().size() == 1,
            "Undo restores the document before its second generated layer");
    auto redone = history.redo(*undone);
    require(redone.has_value() && redone->document.layers().back().id == second,
            "Redo restores the original stable layer ID");
    require(redone->document.addLayer(LayerKind::Shape, "Third") == second + 1,
            "restoring a history state also restores its layer ID allocator");
}

} // namespace

int main()
{
    testUndoRedoAndSelection();
    testBranchAndBoundedHistory();
    testCoalescedInspectorEdits();
    testRestoredLayerIdAllocator();
    std::cout << "Motion Studio composition history tests passed.\n";
    return EXIT_SUCCESS;
}
