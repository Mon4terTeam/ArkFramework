#pragma once

#include <filesystem>

#include "load_report.h"
#include "target.h"

namespace ark::loader {

// Loads ArkBootstrap.dll into the game's UI thread through a documented WH_GETMESSAGE
// hook. The bootstrap loads and starts ArkFramework.dll, writes
// `logs\ArkBootstrap-<pid>.txt` and signals `Local\ArkFramework.HookReady.<pid>`.
LoadReport load_with_window_hook(const Target& target, const std::filesystem::path& root,
                                 ULONGLONG deadline);

} // namespace ark::loader
