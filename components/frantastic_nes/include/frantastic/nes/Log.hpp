#pragma once
#include <functional>
#include <string>

namespace frantastic::nes {

enum class LogLevel { Info, Warning, Error };
// Host-provided diagnostic sink. The core never writes to stdio directly.
using LogSink = std::function<void(LogLevel, const std::string&)>;

} // namespace frantastic::nes
