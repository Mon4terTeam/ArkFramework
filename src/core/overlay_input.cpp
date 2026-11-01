// Window-procedure subclassing and input arbitration between the overlay and
// the application.

#include <imgui.h>
#include <imgui_internal.h>

#include "overlay.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ark::core {
namespace {

constexpr WPARAM kOverlayToggleKey = VK_INSERT;

bool is_mouse_message(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

bool is_keyboard_message(UINT message) {
    return message >= WM_KEYFIRST && message <= WM_KEYLAST;
}

bool is_mouse_button_release(UINT message) {
    return message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP ||
           message == WM_XBUTTONUP;
}

bool is_key_release(UINT message) {
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

LRESULT forward_message(WNDPROC previous, HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return previous ? CallWindowProcW(previous, window, message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

LRESULT CALLBACK Overlay::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return instance_->handle_window_message(window, message, wparam, lparam);
}

LRESULT Overlay::handle_window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    std::lock_guard lock(mutex_);
    const auto found = window_procs_.find(window);
    const WNDPROC previous = found == window_procs_.end() ? previous_window_proc_ : found->second;

    // Alt+F4 always reaches the application untouched.
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && wparam == VK_F4) {
        return forward_message(previous, window, message, wparam, lparam);
    }

    const bool toggle = message == WM_KEYUP && wparam == kOverlayToggleKey;
    if (toggle) {
        overlay_visible_ = !overlay_visible_;
        if (overlay_visible_) {
            menu_visible_ = true;
        }
    }

    if (window == window_ && ready_ && !stopping_) {
        ImGui::SetCurrentContext(imgui_);
        if (toggle && !overlay_visible_) {
            // A hidden widget must not retain keyboard focus or an active drag.
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus(nullptr);
        }
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);

        bool mouse_capture = false;
        if (message == WM_KILLFOCUS) {
            plugins_.set_input_capture(false, false);
        } else {
            mouse_capture = publish_input_capture(message, lparam);
        }
        const bool owned = plugins_.route_window_input(message, wparam, lparam);
        if (toggle) {
            return 0;
        }
        if (owned) {
            return 1;
        }

        // With polling protection installed, ownership decides button/key
        // releases, so a game-origin control cannot lose its Win32 Up message.
        const bool routed = plugins_.input_protected();
        if (is_mouse_message(message) && mouse_capture) {
            if (!routed || !is_mouse_button_release(message)) {
                return 1;
            }
        }
        if (is_keyboard_message(message) && overlay_visible_ && ImGui::GetIO().WantCaptureKeyboard) {
            if (!routed || !is_key_release(message)) {
                return 1;
            }
        }
    }

    if (toggle) {
        return 0;
    }
    return forward_message(previous, window, message, wparam, lparam);
}

bool Overlay::publish_input_capture(UINT message, LPARAM lparam) {
    const bool active =
        overlay_visible_ && ready_ && !stopping_ && GetForegroundWindow() == window_;
    ImGuiIO& io = ImGui::GetIO();
    bool mouse = io.WantCaptureMouse;

    if (active && is_mouse_message(message)) {
        // Win32 queues ImGui events until NewFrame. A move+press can arrive
        // before that frame: the previous WantCaptureMouse belongs to the old
        // pointer position and must not decide ownership of this new press.
        POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
            ScreenToClient(window_, &point);
        }
        const ImVec2 position{static_cast<float>(point.x) / io.DisplayFramebufferScale.x,
                              static_cast<float>(point.y) / io.DisplayFramebufferScale.y};
        ImGuiWindow* hovered = nullptr;
        ImGui::FindHoveredWindowEx(position, false, &hovered, nullptr);

        const ImGuiContext& context = *ImGui::GetCurrentContext();
        const bool interacting = hovered || context.OpenPopupStack.Size > 0 ||
                                 (context.ActiveId && context.ActiveIdMouseButton >= 0);
        mouse = !(io.ConfigFlags & ImGuiConfigFlags_NoMouse) && interacting;
    }

    plugins_.set_input_capture(active && mouse, active && io.WantCaptureKeyboard);
    return active && mouse;
}

} // namespace ark::core
