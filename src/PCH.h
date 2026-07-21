#pragma once

// Precompiled header. CommonLibSSE-NG pulls in the RE/ and SKSE/ namespaces
// as well as spdlog. Keep this lean.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;

namespace logger = SKSE::log;
