// Dear ImGui user configuration: ImGui is exported by the shared ArkImGui.dll.
#pragma once

#ifdef ARK_IMGUI_BUILD
#define IMGUI_API __declspec(dllexport)
#else
#define IMGUI_API __declspec(dllimport)
#endif
#define IMGUI_IMPL_API IMGUI_API
