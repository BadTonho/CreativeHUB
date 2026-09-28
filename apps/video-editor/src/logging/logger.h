#pragma once

#include <creative_suite/diagnostics/logger.h>

namespace logging {

using Level = creative_suite::diagnostics::Level;
using Context = creative_suite::diagnostics::Context;
using Options = creative_suite::diagnostics::Options;
using Logger = creative_suite::diagnostics::Logger;
using creative_suite::diagnostics::current_thread_id;
using creative_suite::diagnostics::level_name;

} // namespace logging
