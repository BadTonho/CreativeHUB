#pragma once
#include "model/bezier_path.h"
#include <QJsonObject>
#include <QJsonValue>
namespace motion::persistence {
[[nodiscard]] QJsonObject writePathAnimation(const model::PathAnimation &);
[[nodiscard]] model::PathAnimation readPathAnimation(const QJsonValue &, bool mask = false);
} // namespace motion::persistence
