#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <algorithm>
#include <creative_suite/composition_exchange/document.h>
#include <limits>
#include <set>

namespace creative_suite::composition_exchange {
namespace {
QString pathString(const std::filesystem::path &p) {
    return QString::fromStdU16String(p.u16string());
}
QString str(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
std::int64_t integer(const QJsonObject &o, const char *key) {
    bool ok = false;
    auto n = o.value(key).toString().toLongLong(&ok);
    if (!ok)
        throw Error(std::string("Invalid integer: ") + key);
    return n;
}
double number(const QJsonObject &o, const char *key) {
    auto v = o.value(key);
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        throw Error(std::string("Invalid number: ") + key);
    return v.toDouble();
}
int smallInteger(const QJsonObject &o, const char *key) {
    const auto n = number(o, key);
    if (n != std::floor(n) || n < std::numeric_limits<int>::min() ||
        n > std::numeric_limits<int>::max())
        throw Error(std::string("Invalid integer: ") + key);
    return static_cast<int>(n);
}
QJsonArray rgba(const std::array<std::uint8_t, 4> &c) { return {c[0], c[1], c[2], c[3]}; }
std::array<std::uint8_t, 4> color(const QJsonValue &v) {
    if (!v.isArray() || v.toArray().size() != 4)
        throw Error("Invalid RGBA color.");
    std::array<std::uint8_t, 4> c{};
    int i = 0;
    for (auto x : v.toArray()) {
        double d = x.toDouble(-1);
        if (!x.isDouble() || d < 0 || d > 255 || d != std::floor(d))
            throw Error("Invalid RGBA channel.");
        c[i++] = std::uint8_t(d);
    }
    return c;
}
constexpr animation::TransformProperty props[]{
    animation::TransformProperty::PositionX, animation::TransformProperty::PositionY,
    animation::TransformProperty::Scale, animation::TransformProperty::Rotation,
    animation::TransformProperty::Opacity};
} // namespace
bool validMask(const Mask& m) noexcept {
    if(!m.id || m.id > std::uint64_t(INT64_MAX) ||
       (m.kind!=composition::ShapeMaskKind::Rectangle && m.kind!=composition::ShapeMaskKind::Ellipse) ||
       !composition::validShapeMask(m.parameters,m.keyframes))return false;
    std::size_t count=0;
    for(const auto& curve:m.keyframes)for(const auto& key:curve) {
        if(key.frame >= (std::int64_t(1)<<50))return false;
        ++count;
    }
    return count<=100000;
}
QJsonArray masksJson(const std::vector<Mask>& masks) {
    QJsonArray result;
    if(masks.size()>4096)throw Error("Too many masks.");
    std::set<MaskId> ids;
    for(const auto& m:masks) {
        if(!validMask(m)||!ids.insert(m.id).second)throw Error("Invalid mask definition.");
        QJsonArray geometry, curves;
        for(std::size_t i=0;i<5;++i) {
            geometry.append(composition::shapeMaskValue(m.parameters,i));
            QJsonArray curve;
            for(const auto& k:m.keyframes[i])curve.append(QJsonObject{
                {"frame",QString::number(k.frame)}, {"value",k.value},
                {"bezier",k.interpolation==animation::InterpolationMode::CubicBezier},
                {"easing",QJsonArray{k.easing.x1,k.easing.y1,k.easing.x2,k.easing.y2}}});
            curves.append(curve);
        }
        result.append(QJsonObject{{"id",QString::number(m.id)},
            {"kind",m.kind==composition::ShapeMaskKind::Rectangle?"rectangle":"ellipse"},
            {"geometry",geometry},{"soft_edge",m.parameters.soft_edge},
            {"invert",m.parameters.invert},{"curves",curves}});
    }
    return result;
}
std::vector<Mask> parseMasksJson(const QJsonArray& array) {
    if(array.size()>4096)throw Error("Too many masks.");
    std::vector<Mask> result;
    std::set<MaskId> ids;
    for(const auto& value:array) {
        const auto o=value.toObject();Mask m;
        const auto id=integer(o,"id");if(id<=0)throw Error("Invalid mask ID.");m.id=id;
        if(o.value("kind")!="rectangle" && o.value("kind")!="ellipse")throw Error("Unknown mask kind.");
        m.kind=o.value("kind")=="rectangle"?composition::ShapeMaskKind::Rectangle:composition::ShapeMaskKind::Ellipse;
        auto geometry=o.value("geometry").toArray();auto curves=o.value("curves").toArray();
        if(geometry.size()!=5||curves.size()!=5||!o.value("invert").isBool())throw Error("Invalid mask properties.");
        m.parameters.soft_edge=number(o,"soft_edge");m.parameters.invert=o.value("invert").toBool();
        for(std::size_t i=0;i<5;++i) {
            if(!geometry[qsizetype(i)].isDouble() || !curves[qsizetype(i)].isArray())throw Error("Invalid mask geometry/curve.");
            composition::setShapeMaskValue(m.parameters,i,geometry[qsizetype(i)].toDouble());
            const auto curve=curves[qsizetype(i)].toArray();if(curve.size()>100000)throw Error("Too many mask keys.");
            for(const auto& key:curve) {
                const auto k=key.toObject();auto easing=k.value("easing").toArray();
                if(!k.value("bezier").isBool()||easing.size()!=4)throw Error("Invalid mask interpolation.");
                for(const auto& e:easing)if(!e.isDouble())throw Error("Invalid mask easing.");
                m.keyframes[i].push_back({integer(k,"frame"),number(k,"value"),
                    k.value("bezier").toBool()?animation::InterpolationMode::CubicBezier:animation::InterpolationMode::Linear,
                    {easing[0].toDouble(),easing[1].toDouble(),easing[2].toDouble(),easing[3].toDouble()}});
            }
        }
        if(!validMask(m)||!ids.insert(m.id).second)throw Error("Invalid mask definition or duplicate ID.");
        result.push_back(std::move(m));
    }
    return result;
}
bool validPresentation(const Presentation &p) noexcept {
    constexpr auto limit = std::int64_t(1) << 50;
    return (p.profile == Profile::FusionCanvas || p.profile == Profile::MotionLayer) &&
           p.start > -limit && p.start < limit && p.duration >= 0 && p.duration < limit &&
           p.offset > -limit && p.offset < limit && p.source_start >= 0 && p.source_start < limit &&
           p.text_width >= 0 && p.text_width <= 32768 && p.text_height >= 0 &&
           p.text_height <= 32768 && p.name.size() <= 1048576;
}
QJsonObject presentationJson(const Presentation &p) {
    return {{"profile", p.profile == Profile::FusionCanvas ? "fusion_canvas" : "motion_layer"},
            {"name", str(p.name)},
            {"visible", p.visible},
            {"start", QString::number(p.start)},
            {"duration", QString::number(p.duration)},
            {"offset", QString::number(p.offset)},
            {"source_start", QString::number(p.source_start)},
            {"text_width", p.text_width},
            {"text_height", p.text_height}};
}
Presentation parsePresentation(const QJsonObject &o) {
    Presentation p;
    auto profile = o.value("profile").toString();
    if (profile != "fusion_canvas" && profile != "motion_layer")
        throw Error("Unknown presentation profile.");
    p.profile = profile == "fusion_canvas" ? Profile::FusionCanvas : Profile::MotionLayer;
    if (!o.value("name").isString() || !o.value("visible").isBool())
        throw Error("Invalid presentation metadata.");
    p.name = o.value("name").toString().toUtf8().toStdString();
    p.visible = o.value("visible").toBool();
    p.start = integer(o, "start");
    p.duration = integer(o, "duration");
    p.offset = integer(o, "offset");
    p.source_start = integer(o, "source_start");
    p.text_width = smallInteger(o, "text_width");
    p.text_height = smallInteger(o, "text_height");
    if (!validPresentation(p))
        throw Error("Presentation metadata exceeds supported bounds.");
    return p;
}
std::vector<Issue> validate(const Document &d, bool media) {
    std::vector<Issue> issues;
    std::set<std::uint64_t> ids;
    std::size_t stages = 1 + d.masks.size();
    std::set<MaskId> mask_ids, referenced_masks;
    if(d.masks.size()>4096)issues.push_back({0,"Too many masks."});
    for(const auto& m:d.masks)if(!validMask(m)||!mask_ids.insert(m.id).second)
        issues.push_back({m.id,"Invalid mask definition or duplicate ID."});
    if (d.width < 1 || d.height < 1 || std::uint64_t(d.width) * d.height > 16777216 ||
        d.fps_numerator <= 0 || d.fps_denominator <= 0 || d.fps_numerator > 1000000 ||
        d.fps_denominator > 1000000 || double(d.fps_numerator) / d.fps_denominator > 1000 ||
        d.duration < 1 || d.duration >= (std::int64_t(1) << 50) || d.layers.size() > 4096 ||
        d.name.size() > 1048576)
        issues.push_back(
            {0, "Invalid composition dimensions, frame rate, duration or resource limits."});
    for (const auto &l : d.layers) {
        stages += 3 + l.colors.size();
        const auto reference = [&](MaskId id) {
            if(!id)return;
            referenced_masks.insert(id);
            if(!mask_ids.contains(id))issues.push_back({l.id,"Unknown mask reference."});
        };
        reference(l.source_mask);reference(l.transform_mask);
        if(!l.color_masks.empty() && l.color_masks.size()!=l.colors.size())issues.push_back({l.id,"Invalid effect mask bindings."});
        for(auto id:l.color_masks)reference(id);
        if (!l.id || !ids.insert(l.id).second || !validPresentation(l.presentation) ||
            !animation::validTransform(l.transform) ||
            !animation::validTransformKeyframes(l.curves) || l.colors.size() > 1024 ||
            (!l.colors_enabled.empty() && l.colors_enabled.size() != l.colors.size()))
            issues.push_back({l.id, "Invalid layer identity, timing, transform or curves."});
        if (l.kind != Kind::Video && l.kind != Kind::Image && l.kind != Kind::Background &&
            l.kind != Kind::Text)
            issues.push_back({l.id, "Unknown layer kind."});
        if (l.presentation.start < 0 || l.presentation.start >= d.duration ||
            (l.presentation.duration > 0 &&
             l.presentation.duration > d.duration - l.presentation.start))
            issues.push_back({l.id, "Layer activity exceeds the composition duration."});
        if (!std::isfinite(l.source_rate) || l.source_count < 0)
            issues.push_back({l.id, "Invalid source timing metadata."});
        if (l.source_count >= (std::int64_t(1) << 50))
            issues.push_back({l.id, "Source frame count exceeds the interchange time range."});
        if (l.kind != Kind::Text &&
            (l.presentation.text_width != 0 || l.presentation.text_height != 0))
            issues.push_back({l.id, "Text layout metadata is only valid on Text layers."});
        if (l.kind == Kind::Video || l.kind == Kind::Image) {
            if (l.source.empty() || !l.source.is_absolute() ||
                (l.kind == Kind::Video && (!std::isfinite(l.source_rate) || l.source_rate <= 0 ||
                                           l.source_rate > 1000 || l.source_count <= 0)))
                issues.push_back({l.id, "Invalid media reference or timing."});
            std::error_code ec;
            if (media && !std::filesystem::is_regular_file(l.source, ec))
                issues.push_back(
                    {l.id, "Media is missing: " + pathString(l.source).toUtf8().toStdString()});
        } else if (!l.source.empty()) {
            issues.push_back({l.id, "Generated layers cannot reference a primary media source."});
        }
        if (l.kind == Kind::Text &&
            (l.text.content.size() > 1048576 || l.text.font.empty() || l.text.font.size() > 4096 ||
             !std::isfinite(l.text.size) || l.text.size < 1 || l.text.size > 512 ||
             l.text.alignment < 0 || l.text.alignment > 2 ||
             (l.presentation.profile == Profile::MotionLayer &&
              (l.presentation.text_width <= 0 || l.presentation.text_height <= 0))))
            issues.push_back({l.id, "Unsupported text properties."});
        for (const auto &c : l.colors)
            if (!std::isfinite(c.brightness) || c.brightness < -100 || c.brightness > 100 ||
                !std::isfinite(c.contrast_percent) || c.contrast_percent < 0 ||
                c.contrast_percent > 200 || !std::isfinite(c.saturation_percent) ||
                c.saturation_percent < 0 || c.saturation_percent > 200)
                issues.push_back({l.id, "Invalid color adjustment."});
        std::size_t keys = 0;
        for (auto p : props)
            for (const auto &key : animation::keyframesFor(l.curves, p)) {
                ++keys;
                if (key.frame >= (std::int64_t(1) << 50))
                    issues.push_back({l.id, "Animation time exceeds the interchange range."});
            }
        if (keys > 100000)
            issues.push_back({l.id, "Too many animation keys."});
    }
    for(auto id:mask_ids)if(!referenced_masks.contains(id))issues.push_back({id,"Unreferenced mask."});
    if (stages > 16384)
        issues.push_back({0, "The composition exceeds the 16384-stage interchange limit."});
    return issues;
}
QJsonObject toJson(const Document &d, const std::filesystem::path &file) {
    auto issues = validate(d);
    if (!issues.empty())
        throw Error(issues.front().reason);
    QJsonArray layers;
    for (const auto &l : d.layers) {
        QJsonObject o{{"id", QString::number(l.id)},
                      {"kind", l.kind == Kind::Video   ? "video"
                               : l.kind == Kind::Image ? "image"
                               : l.kind == Kind::Text  ? "text"
                                                       : "background"},
                      {"presentation", presentationJson(l.presentation)},
                      {"source_rate", l.source_rate},
                      {"source_count", QString::number(l.source_count)},
                      {"color", rgba(l.color)},
                      {"text", QJsonObject{{"content", str(l.text.content)},
                                           {"font", str(l.text.font)},
                                           {"size", l.text.size},
                                           {"alignment", l.text.alignment},
                                           {"color", rgba(l.text.color)}}}};
        if (!l.source.empty()) {
            auto rel = l.source.lexically_relative(std::filesystem::absolute(file).parent_path());
            o.insert("source",
                     pathString(!rel.empty() && *rel.begin() != std::filesystem::path("..")
                                    ? rel
                                    : l.source));
        }
        o.insert("transform",
                 QJsonArray{l.transform.position_x, l.transform.position_y, l.transform.scale,
                            l.transform.rotation_degrees, l.transform.opacity});
        QJsonArray curves;
        for (auto p : props) {
            QJsonArray curve;
            for (const auto &k : animation::keyframesFor(l.curves, p))
                curve.append(QJsonObject{
                    {"frame", QString::number(k.frame)},
                    {"value", k.value},
                    {"bezier", k.interpolation == animation::InterpolationMode::CubicBezier},
                    {"easing", QJsonArray{k.easing.x1, k.easing.y1, k.easing.x2, k.easing.y2}}});
            curves.append(curve);
        }
        o.insert("curves", curves);
        QJsonArray colors;
        for (std::size_t i = 0; i < l.colors.size(); ++i) {
            const auto &c = l.colors[i];
            colors.append(QJsonArray{c.brightness, c.contrast_percent, c.saturation_percent,
                                     l.colors_enabled.empty() ? true : bool(l.colors_enabled[i])});
        }
        o.insert("colors", colors);
        if(l.source_mask)o.insert("source_mask",QString::number(l.source_mask));
        if(l.transform_mask)o.insert("transform_mask",QString::number(l.transform_mask));
        if(std::any_of(l.color_masks.begin(),l.color_masks.end(),[](auto id){return id!=0;})) {QJsonArray refs;for(auto id:l.color_masks)refs.append(QString::number(id));o.insert("color_masks",refs);}
        layers.append(o);
    }
    QJsonObject result{{"format", "creative-suite.composition-interchange"},
            {"version", d.masks.empty()?1:2},
            {"name", str(d.name)},
            {"width", d.width},
            {"height", d.height},
            {"fps_numerator", QString::number(d.fps_numerator)},
            {"fps_denominator", QString::number(d.fps_denominator)},
            {"duration", QString::number(d.duration)},
            {"layers", layers}};
    if(!d.masks.empty())result.insert("masks",masksJson(d.masks));
    return result;
}
Document fromJson(const QJsonObject &o, const std::filesystem::path &file) {
    if (o.value("format") != "creative-suite.composition-interchange" || (o.value("version") != 1 && o.value("version") != 2))
        throw Error("Unsupported composition interchange format or version.");
    Document d;
    const bool masks_allowed=o.value("version")==2;
    if(o.contains("masks")) {
        if(!masks_allowed||!o.value("masks").isArray())throw Error("Masks require interchange v2.");
        d.masks=parseMasksJson(o.value("masks").toArray());
    }
    d.name = o.value("name").toString().toUtf8().toStdString();
    d.width = smallInteger(o, "width");
    d.height = smallInteger(o, "height");
    d.fps_numerator = integer(o, "fps_numerator");
    d.fps_denominator = integer(o, "fps_denominator");
    d.duration = integer(o, "duration");
    if (!o.value("layers").isArray() || o.value("layers").toArray().size() > 4096)
        throw Error("Invalid layer list.");
    for (auto v : o.value("layers").toArray()) {
        auto a = v.toObject();
        Layer l;
        bool ok = false;
        l.id = a.value("id").toString().toULongLong(&ok);
        if (!ok)
            throw Error("Invalid layer ID.");
        const auto ref=[&](const char* field) -> MaskId {
            if(!a.contains(field))return 0;
            if(!masks_allowed)throw Error("Mask bindings require interchange v2.");
            const auto id=integer(a,field);if(id<0)throw Error("Invalid mask reference.");return id;
        };
        l.source_mask=ref("source_mask");l.transform_mask=ref("transform_mask");
        if(a.contains("color_masks")) {
            if(!masks_allowed||!a.value("color_masks").isArray())throw Error("Invalid effect mask bindings.");
            for(const auto& v:a.value("color_masks").toArray()) {
                QJsonObject temp{{"ref",v}};const auto id=integer(temp,"ref");if(id<0)throw Error("Invalid effect mask reference.");l.color_masks.push_back(id);
            }
        }
        auto k = a.value("kind").toString();
        if (k != "video" && k != "image" && k != "text" && k != "background")
            throw Error("Unknown layer kind.");
        l.kind = k == "video"   ? Kind::Video
                 : k == "image" ? Kind::Image
                 : k == "text"  ? Kind::Text
                                : Kind::Background;
        l.presentation = parsePresentation(a.value("presentation").toObject());
        l.source_rate = number(a, "source_rate");
        l.source_count = integer(a, "source_count");
        l.color = color(a.value("color"));
        if (a.contains("source")) {
            auto p = std::filesystem::path(a.value("source").toString().toStdU16String());
            l.source = (p.is_absolute() ? p : std::filesystem::absolute(file).parent_path() / p)
                           .lexically_normal();
        }
        auto t = a.value("text").toObject();
        l.text.content = t.value("content").toString().toUtf8().toStdString();
        l.text.font = t.value("font").toString().toUtf8().toStdString();
        l.text.size = number(t, "size");
        l.text.alignment = smallInteger(t, "alignment");
        l.text.color = color(t.value("color"));
        auto tr = a.value("transform").toArray();
        if (tr.size() != 5)
            throw Error("Invalid transform.");
        for (auto x : tr)
            if (!x.isDouble() || !std::isfinite(x.toDouble()))
                throw Error("Invalid transform number.");
        l.transform = {tr[0].toDouble(), tr[1].toDouble(), tr[2].toDouble(), tr[3].toDouble(),
                       tr[4].toDouble()};
        auto curves = a.value("curves").toArray();
        if (curves.size() != 5)
            throw Error("Invalid curves.");
        std::vector<animation::Keyframe> *lists[]{&l.curves.position_x, &l.curves.position_y,
                                                  &l.curves.scale, &l.curves.rotation,
                                                  &l.curves.opacity};
        for (int i = 0; i < 5; ++i) {
            if (!curves[i].isArray() || curves[i].toArray().size() > 100000)
                throw Error("Invalid curve.");
            for (auto key : curves[i].toArray()) {
                auto b = key.toObject();
                animation::Keyframe keyframe;
                keyframe.frame = integer(b, "frame");
                keyframe.value = number(b, "value");
                if (!b.value("bezier").isBool())
                    throw Error("Invalid interpolation.");
                keyframe.interpolation = b.value("bezier").toBool()
                                             ? animation::InterpolationMode::CubicBezier
                                             : animation::InterpolationMode::Linear;
                auto e = b.value("easing").toArray();
                if (e.size() != 4)
                    throw Error("Invalid easing.");
                keyframe.easing = {e[0].toDouble(-1), e[1].toDouble(-1), e[2].toDouble(-1),
                                   e[3].toDouble(-1)};
                lists[i]->push_back(keyframe);
            }
        }
        if (!a.value("colors").isArray() || a.value("colors").toArray().size() > 1024)
            throw Error("Invalid effects.");
        for (auto cv : a.value("colors").toArray()) {
            auto c = cv.toArray();
            if (c.size() != 4 || !c[3].isBool())
                throw Error("Invalid color adjustment.");
            l.colors.push_back({c[0].toDouble(NAN), c[1].toDouble(NAN), c[2].toDouble(NAN)});
            l.colors_enabled.push_back(c[3].toBool());
        }
        d.layers.push_back(std::move(l));
    }
    auto issues = validate(d);
    if (!issues.empty())
        throw Error(issues.front().reason);
    return d;
}
std::optional<Document> load(const std::filesystem::path &path, Cancel cancel) {
    if (cancel && cancel())
        return {};
    QFile file(pathString(path));
    if (!file.open(QIODevice::ReadOnly))
        throw Error(file.errorString().toStdString());
    if (file.size() > 64 * 1024 * 1024)
        throw Error("Interchange file exceeds 64 MiB.");
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        throw Error("Invalid interchange JSON.");
    if (cancel && cancel())
        return {};
    auto d = fromJson(json.object(), path);
    auto issues = validate(d, true);
    if (!issues.empty())
        throw Error(issues.front().reason);
    return d;
}
bool save(const std::filesystem::path &path, const Document &d, Cancel cancel) {
    if (cancel && cancel())
        return false;
    auto bytes = QJsonDocument(toJson(d, path)).toJson();
    if (bytes.size() > 64 * 1024 * 1024)
        throw Error("Interchange file exceeds 64 MiB.");
    QSaveFile file(pathString(path));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw Error(file.errorString().toStdString());
    if (cancel && cancel()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        throw Error(file.errorString().toStdString());
    return true;
}
std::optional<std::int64_t> sourceFrame(const Presentation &p, std::int64_t frame, double rate,
                                        std::int64_t count, double fps) {
    const long double elapsed = static_cast<long double>(frame) - p.start;
    if (!p.visible || elapsed < 0 || (p.duration > 0 && elapsed >= p.duration) || rate <= 0 ||
        fps <= 0 || count <= 0)
        return {};
    long double v =
        (static_cast<long double>(frame) - p.start + p.offset) * rate / fps + p.source_start;
    if (!std::isfinite(v) || v < 0)
        return {};
    if (p.profile == Profile::MotionLayer) {
        if (v >= static_cast<long double>(count) - 1)
            return count - 1;
        return std::int64_t(std::floor(v + 0.5L));
    }
    if (v >= count || v >= std::ldexp(1.L, 63))
        return {};
    return std::int64_t(v);
}
} // namespace creative_suite::composition_exchange
