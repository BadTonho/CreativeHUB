#pragma once
#include "motion_project_data.h"
namespace motion::model {
[[nodiscard]] creative_suite::composition_exchange::Document
exportComposition(const MotionProjectData &);
[[nodiscard]] MotionProjectData
importComposition(const creative_suite::composition_exchange::Document &);
} // namespace motion::model
