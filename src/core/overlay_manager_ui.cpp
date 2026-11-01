// Plugin manager window and overlay UI scaling.

#include <iterator>

#include <imgui.h>
#include <imgui_internal.h>

#include "overlay.h"

namespace ark::core {
namespace {

// Indexed by the "UI 缩放" combo; keep both lists in sync.
constexpr float kUiScales[] = {.5f, .75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f};
constexpr char kUiScaleLabels[] = "50%\0"
                                  "75%\0"
                                  "100%\0"
                                  "125%\0"
                                  "150%\0"
                                  "175%\0"
                                  "200%\0";
static_assert(std::size(kUiScales) == 7);

} // namespace

void Overlay::apply_ui_scale() {
    ImGuiIO& io = ImGui::GetIO();
    const float scale = kUiScales[ui_scale_index_];
    if (io.DisplayFramebufferScale.x == scale) {
        return;
    }
    // Do not move an active mouse gesture between coordinate systems.
    for (bool down : io.MouseDown) {
        if (down) {
            return;
        }
    }

    // Queued and remembered positions are in the old logical space; rescale
    // them so the next frame sees a consistent pointer.
    const float ratio = io.DisplayFramebufferScale.x / scale;
    auto remap = [ratio](ImVec2& point) {
        if (ImGui::IsMousePosValid(&point)) {
            point.x *= ratio;
            point.y *= ratio;
        }
    };
    remap(io.MousePos);
    remap(io.MousePosPrev);
    for (ImVec2& point : io.MouseClickedPos) {
        remap(point);
    }
    ImGuiContext& context = *ImGui::GetCurrentContext();
    remap(context.MouseLastValidPos);
    for (ImGuiInputEvent& event : context.InputEventsQueue) {
        if (event.Type != ImGuiInputEventType_MousePos) {
            continue;
        }
        ImVec2 point{event.MousePos.PosX, event.MousePos.PosY};
        remap(point);
        event.MousePos.PosX = point.x;
        event.MousePos.PosY = point.y;
    }
    io.DisplayFramebufferScale = {scale, scale};
}

void Overlay::draw_manager() {
    if (!menu_visible_) {
        return;
    }
    ImGui::SetNextWindowPos({18, 18}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 340}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ArkFramework 插件管理器", &menu_visible_)) {
        ImGui::SetNextItemWidth(180);
        ImGui::Combo("UI 缩放", &ui_scale_index_, kUiScaleLabels);
        ImGui::SameLine();
        if (ImGui::Button("恢复默认")) {
            ui_scale_index_ = kDefaultUiScaleIndex;
        }
        if (plugins_.test_host()) {
            ImGui::TextColored({1.f, .7f, .2f, 1.f}, "测试宿主：仅用于图形与插件生命周期验证");
        }
        if (ImGui::Button("扫描插件目录")) {
            plugins_.request("scan", "");
        }
        ImGui::SameLine();
        bool auto_reload = plugins_.auto_reload.load();
        if (ImGui::Checkbox("文件变化时热重载", &auto_reload)) {
            plugins_.auto_reload = auto_reload;
        }
        for (const PluginView& plugin : plugins_.views()) {
            draw_plugin_entry(plugin);
        }
        if (ImGui::CollapsingHeader("日志")) {
            for (const std::string& line : plugins_.recent_logs()) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
    }
    ImGui::End();
}

void Overlay::draw_plugin_entry(const PluginView& plugin) {
    ImGui::PushID(plugin.file.c_str());
    ImGui::Separator();
    ImGui::Text("%s  %s  [%s]", plugin.name.c_str(), plugin.version.c_str(), plugin.status.c_str());
    ImGui::TextDisabled("%s", plugin.file.c_str());
    if (plugin.active && plugin.unloadable) {
        if (ImGui::Button("卸载")) {
            plugins_.request("unload", plugin.file);
        }
        ImGui::SameLine();
        if (ImGui::Button("重新加载")) {
            plugins_.request("reload", plugin.file);
        }
    } else if (plugin.active) {
        ImGui::TextDisabled("进程常驻插件：保留至进程退出");
    } else if (ImGui::Button("加载 / 重试")) {
        // A plugin stuck in Stopping is retried by finishing its unload.
        plugins_.request(plugin.status == "Stopping" ? "unload" : "load", plugin.file);
    }
    if (!plugin.error.empty()) {
        ImGui::TextWrapped("%s", plugin.error.c_str());
    }
    ImGui::PopID();
}

} // namespace ark::core
