#include "model/composition_document.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
void requireThrows(Function&& function, const char* message)
{
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    require(false, message);
}

} // namespace

int main()
{
    using namespace creative_suite::animation;
    using namespace motion::model;

    requireThrows([] { CompositionDocument document(0, 720); }, "zero canvas width is rejected");
    requireThrows([] { CompositionDocument document(1920, -1); }, "negative canvas height is rejected");

    CompositionDocument document(1920, 1080);
    require(document.canvasSize() == CanvasSize{1920, 1080}, "explicit canvas size is retained");
    require(document.layers().empty(), "new document starts without layers");

    const LayerId back = document.addLayer(LayerKind::Image, "Background image");
    const LayerId middle = document.addLayer(LayerKind::Shape, "Accent shape");
    const LayerId front = document.addLayer(LayerKind::Text, "Title");
    require(back != middle && middle != front && back != front, "layer IDs are unique within the document");
    require(document.layers().size() == 3, "layers are appended");
    require(document.layers()[0].id == back && document.layers()[2].id == front,
        "new layers append in back-to-front order");
    require(document.layers()[2].kind == LayerKind::Text && document.layers()[2].name == "Title",
        "layer kind and name are stored");
    require(document.layers()[2].visible, "layers start visible");

    require(document.moveLayer(front, 0), "moving an existing layer succeeds");
    require(document.layers()[0].id == front && document.layers()[1].id == back,
        "reordering places the layer at its final index");
    require(document.moveLayer(front, 0), "moving a layer to its current index succeeds");
    require(!document.moveLayer(front, 3), "out-of-range destination is rejected");
    require(!document.moveLayer(9999, 0), "unknown layer cannot be reordered");

    require(document.setLayerName(middle, "Shape 1"), "existing layer can be renamed");
    require(document.layers()[2].name == "Shape 1", "new layer name is stored");
    require(document.setLayerVisible(middle, false), "existing layer visibility can be changed");
    require(!document.layers()[2].visible, "new visibility is stored");

    Transform2D transform;
    transform.position_x = -0.25;
    transform.position_y = 1.25;
    transform.scale = 1.5;
    transform.rotation_degrees = 37.0;
    transform.opacity = 0.65;
    require(document.setLayerTransform(middle, transform), "valid shared transform is accepted");
    require(document.layers()[2].transform == transform, "shared transform values are retained");
    Transform2D invalid_transform = transform;
    invalid_transform.scale = 0.0;
    require(!document.setLayerTransform(middle, invalid_transform), "invalid transform is rejected");
    require(document.layers()[2].transform == transform, "invalid transform leaves prior values intact");

    require(document.setLayerKeyframe(middle, TransformProperty::PositionX, 12, -0.5),
        "valid shared keyframe is accepted");
    require(document.setLayerKeyframe(middle, TransformProperty::Opacity, 30, 0.25),
        "opacity keyframe is stored");
    require(keyframesFor(document.layers()[2].keyframes, TransformProperty::PositionX)
                == std::vector<Keyframe>{{12, -0.5}},
        "shared keyframe value and local frame are retained");
    require(!document.setLayerKeyframe(middle, TransformProperty::Scale, -1, 2.0),
        "negative local keyframe frame is rejected");
    require(!document.setLayerKeyframe(middle, TransformProperty::Opacity, 31, 1.5),
        "invalid property value is rejected");
    require(!document.setLayerKeyframe(9999, TransformProperty::Opacity, 0, 0.5),
        "keyframe cannot be set on an unknown layer");
    const auto stored_keyframes = document.layers()[2].keyframes;
    transform.position_x = 0.8;
    require(document.setLayerTransform(middle, transform),
        "base transform can change after keyframes are present");
    require(document.layers()[2].keyframes == stored_keyframes,
        "changing the base transform leaves keyframes untouched");

    require(document.removeLayer(back), "existing layer can be removed");
    require(!document.removeLayer(back), "removed layer cannot be removed again");
    const LayerId next = document.addLayer(LayerKind::Video, "Footage");
    require(next > front, "removed IDs are not reused");
    require(document.layers().size() == 3, "removal and insertion update layer storage");

    std::cout << "Motion Studio composition document tests passed.\n";
    return EXIT_SUCCESS;
}
