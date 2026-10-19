#include "path_serialization.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace motion::persistence {
namespace {
QJsonObject writeGeometry(const model::PathGeometry &p) {
    QJsonArray nodes;
    for (const auto &n : p.nodes)
        nodes.append(
            QJsonArray{n.point.x, n.point.y, n.incoming.x, n.incoming.y, n.outgoing.x, n.outgoing.y});
    return {{"closed", p.closed}, {"nodes", nodes}};
}
model::PathGeometry readGeometry(const QJsonValue &input, bool mask) {
    if (!input.isObject())
        throw std::invalid_argument("Path geometry must be an object");
    const auto o = input.toObject();
    if (!o.value("closed").isBool() || !o.value("nodes").isArray())
        throw std::invalid_argument("Invalid path geometry fields");
    const auto nodes = o.value("nodes").toArray();
    if (nodes.size() > model::maximum_path_nodes)
        throw std::invalid_argument("Too many path points");
    model::PathGeometry p;
    p.closed = o.value("closed").toBool();
    p.nodes.reserve(nodes.size());
    for (const auto &value : nodes) {
        if (!value.isArray())
            throw std::invalid_argument("Invalid path point");
        const auto n = value.toArray();
        if (n.size() != 6)
            throw std::invalid_argument("A path point needs six coordinates");
        for (const auto &c : n)
            if (!c.isDouble() || !std::isfinite(c.toDouble()))
                throw std::invalid_argument("Nonfinite path coordinate");
        p.nodes.push_back({{n[0].toDouble(), n[1].toDouble()},
                           {n[2].toDouble(), n[3].toDouble()},
                           {n[4].toDouble(), n[5].toDouble()}});
    }
    if (!model::validPathGeometry(p, mask))
        throw std::invalid_argument("Invalid path geometry");
    return p;
}
} // namespace
QJsonObject writePathAnimation(const model::PathAnimation &p) {
    QJsonObject result{{"base", writeGeometry(p.base)}};
    QJsonArray keys;
    for (const auto &k : p.keys)
        keys.append(QJsonObject{
            {"frame", QString::number(k.frame)},
            {"geometry", writeGeometry(k.geometry)},
            {"interpolation", k.interpolation == creative_suite::animation::InterpolationMode::Linear
                                  ? "linear"
                                  : "cubic_bezier"},
            {"easing", QJsonArray{k.easing.x1, k.easing.y1, k.easing.x2, k.easing.y2}}});
    if (!keys.empty())
        result.insert("keyframes", keys);
    return result;
}
model::PathAnimation readPathAnimation(const QJsonValue &input, bool mask) {
    if (!input.isObject())
        throw std::invalid_argument("Path animation must be an object");
    const auto o = input.toObject();
    model::PathAnimation p{readGeometry(o.value("base"), mask), {}};
    if (o.contains("keyframes") && !o.value("keyframes").isArray())
        throw std::invalid_argument("Invalid path keyframe array");
    const auto keys = o.value("keyframes").toArray();
    if (std::size_t(keys.size()) > 2'000'000 / (6 * p.base.nodes.size()))
        throw std::invalid_argument("Too many path keyframe coordinates");
    p.keys.reserve(keys.size());
    for (const auto &value : keys) {
        if (!value.isObject())
            throw std::invalid_argument("Invalid path keyframe");
        const auto k = value.toObject();
        bool ok = false;
        const auto frame = k.value("frame").toString().toLongLong(&ok);
        if (!k.value("frame").isString() || !ok || frame < 0)
            throw std::invalid_argument("Invalid path keyframe time");
        model::PathKeyframe key{frame, readGeometry(k.value("geometry"), mask)};
        const auto mode = k.value("interpolation").toString();
        if (mode == "linear")
            key.interpolation = creative_suite::animation::InterpolationMode::Linear;
        else if (mode == "cubic_bezier")
            key.interpolation = creative_suite::animation::InterpolationMode::CubicBezier;
        else
            throw std::invalid_argument("Invalid path interpolation");
        if (!k.value("easing").isArray() || k.value("easing").toArray().size() != 4)
            throw std::invalid_argument("Invalid path easing");
        const auto easing = k.value("easing").toArray();
        for (const auto &c : easing)
            if (!c.isDouble())
                throw std::invalid_argument("Invalid path easing control");
        key.easing = {easing[0].toDouble(), easing[1].toDouble(), easing[2].toDouble(), easing[3].toDouble()};
        p.keys.push_back(std::move(key));
    }
    if (!model::validPathAnimation(p, mask))
        throw std::invalid_argument("Invalid path animation or incompatible topology");
    return p;
}
} // namespace motion::persistence
