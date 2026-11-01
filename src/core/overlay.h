#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <map>
#include <mutex>

#include "plugin_manager.h"

struct ImGuiContext;

namespace ark::core {

// Hooks the application's D3D11 swap chain, renders the ImGui overlay (plugin
// callbacks plus the plugin manager window) and arbitrates window input between
// the overlay and the application.
//
// All Present/ResizeBuffers/window-procedure work runs under `mutex_`, which is
// recursive because window messages can be dispatched re-entrantly while a
// frame is being drawn.
class Overlay {
public:
    explicit Overlay(PluginManager& plugins) : plugins_(plugins) {}

    // Probes the IDXGISwapChain vtable with a throwaway device and installs the
    // Present/ResizeBuffers hooks.
    bool initialize();

    // Disables the hooks and releases the renderer. Trampolines stay alive in
    // the pinned core DLL until process exit.
    void shutdown();

    bool rendered() const { return rendered_; }

private:
    using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                        DXGI_FORMAT, UINT);

    static constexpr int kDefaultUiScaleIndex = 2;

    // Hook detours (overlay.cpp).
    static HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* chain, UINT sync_interval,
                                                    UINT flags);
    static HRESULT STDMETHODCALLTYPE resize_buffers_detour(IDXGISwapChain* chain, UINT buffer_count,
                                                           UINT width, UINT height,
                                                           DXGI_FORMAT format, UINT flags);
    bool probe_swap_chain_vtable();
    bool install_hooks();
    void draw_frame(IDXGISwapChain* chain);
    void draw_input_warning();
    void render_draw_data();

    // Renderer binding (overlay_renderer.cpp).
    bool bind(IDXGISwapChain* chain);
    bool create_imgui_context();
    bool subclass_window();
    bool ensure_render_target();
    void release_renderer();

    // Window input (overlay_input.cpp).
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    bool publish_input_capture(UINT message = 0, LPARAM lparam = 0);

    // Plugin manager window and UI scale (overlay_manager_ui.cpp).
    void draw_manager();
    void draw_plugin_entry(const PluginView& plugin);
    void apply_ui_scale();

    // The hooks and window procedure are free functions from the OS point of
    // view; they reach the single overlay through this pointer.
    static Overlay* instance_;

    PluginManager& plugins_;
    std::recursive_mutex mutex_;

    PresentFn original_present_ = nullptr;
    ResizeBuffersFn original_resize_buffers_ = nullptr;
    void* present_target_ = nullptr;
    void* resize_buffers_target_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11RenderTargetView* render_target_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    HWND window_ = nullptr;
    WNDPROC previous_window_proc_ = nullptr;
    // Every window ever subclassed, so late messages still reach their
    // original procedure after the overlay rebinds.
    std::map<HWND, WNDPROC> window_procs_;
    ImGuiContext* imgui_ = nullptr;

    bool ready_ = false;
    bool overlay_visible_ = true;
    bool menu_visible_ = true;
    bool platform_ready_ = false;
    bool renderer_ready_ = false;
    int ui_scale_index_ = kDefaultUiScaleIndex;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> rendered_{false};
};

} // namespace ark::core
