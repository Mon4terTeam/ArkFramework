// Binding the overlay to the application's swap chain, device and window.

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include "overlay.h"

namespace ark::core {
namespace {

constexpr uint32_t kLogInfo = 1;

// Swap chains for windows smaller than this are splash screens or helper
// surfaces, not the application's main view.
constexpr LONG kMinimumClientWidth = 320;
constexpr LONG kMinimumClientHeight = 200;

constexpr char kFontPath[] = "C:\\Windows\\Fonts\\msyh.ttc";
constexpr float kFontSize = 18.f;
constexpr int kFontAtlasWidth = 4096;

bool is_application_window(const DXGI_SWAP_CHAIN_DESC& desc) {
    if (!desc.OutputWindow || !IsWindowVisible(desc.OutputWindow)) {
        return false;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(desc.OutputWindow, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return false;
    }
    RECT client{};
    GetClientRect(desc.OutputWindow, &client);
    return client.right >= kMinimumClientWidth && client.bottom >= kMinimumClientHeight;
}

// Full CJK coverage plus Greek and number-form symbols used by plugin UIs.
const ImWchar* overlay_glyph_ranges(ImGuiIO& io) {
    // Atlas building is deferred, so the ranges must outlive the caller.
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        static const ImWchar kSymbolRanges[] = {0x0370, 0x03ff, 0x1f00, 0x1fff, 0x2150, 0x218f, 0};
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        builder.AddRanges(kSymbolRanges);
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

void load_overlay_font(ImGuiIO& io) {
    if (GetFileAttributesA(kFontPath) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    const ImWchar* ranges = overlay_glyph_ranges(io);
    io.Fonts->TexDesiredWidth = kFontAtlasWidth;
    io.Fonts->AddFontFromFileTTF(kFontPath, kFontSize, nullptr, ranges);
}

} // namespace

bool Overlay::bind(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !is_application_window(desc)) {
        return false;
    }
    ID3D11Device* device = nullptr;
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
        return false;
    }

    release_renderer();
    device_ = device;
    device_->GetImmediateContext(&context_);
    swap_chain_ = chain;
    window_ = desc.OutputWindow;

    if (!create_imgui_context() || !subclass_window()) {
        release_renderer();
        return false;
    }
    ready_ = true;
    plugins_.log(kLogInfo, "ImGui bound to the application's swapchain and window");
    return ensure_render_target();
}

bool Overlay::create_imgui_context() {
    IMGUI_CHECKVERSION();
    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    load_overlay_font(io);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 7;
    style.FrameRounding = 4;

    platform_ready_ = ImGui_ImplWin32_Init(window_);
    renderer_ready_ = platform_ready_ && ImGui_ImplDX11_Init(device_, context_);
    return platform_ready_ && renderer_ready_;
}

bool Overlay::subclass_window() {
    // A null previous procedure is only an error if GetLastError says so.
    SetLastError(0);
    previous_window_proc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(window_proc)));
    if (!previous_window_proc_ && GetLastError() != 0) {
        return false;
    }
    window_procs_[window_] = previous_window_proc_;
    return true;
}

bool Overlay::ensure_render_target() {
    if (render_target_) {
        return true;
    }
    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&back_buffer)))) {
        return false;
    }
    const HRESULT result = device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    back_buffer->Release();
    return SUCCEEDED(result);
}

void Overlay::release_renderer() {
    plugins_.set_input_capture(false, false);
    // Preserve native ownership quarantine until the physical release drains.
    if (window_) {
        plugins_.route_window_input(WM_CANCELMODE, 0, 0);
    }
    // Only unhook if nobody subclassed the window after us; otherwise the
    // chain stays intact and window_procs_ keeps forwarding late messages.
    if (window_ && previous_window_proc_ && IsWindow(window_) &&
        GetWindowLongPtrW(window_, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(window_proc)) {
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_window_proc_));
    }
    previous_window_proc_ = nullptr;

    if (imgui_) {
        ImGui::SetCurrentContext(imgui_);
        if (renderer_ready_) {
            ImGui_ImplDX11_Shutdown();
        }
        if (platform_ready_) {
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
    }
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    swap_chain_ = nullptr;
    window_ = nullptr;
    ready_ = false;
    platform_ready_ = false;
    renderer_ready_ = false;
}

} // namespace ark::core
