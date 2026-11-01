// Swap-chain hook installation and per-frame overlay rendering.

#include "overlay.h"

#include <memory>
#include <string>
#include <vector>

#include <MinHook.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

namespace ark::core {
namespace {

constexpr uint32_t kLogInfo = 1;
constexpr uint32_t kLogError = 3;

// IDXGISwapChain vtable slots.
constexpr size_t kPresentSlot = 8;
constexpr size_t kResizeBuffersSlot = 13;

constexpr wchar_t kProbeWindowClass[] = L"ArkFramework.DeviceProbe";
constexpr wchar_t kProbeWindowTitle[] = L"ArkFramework device probe";

HRESULT create_probe_device(const DXGI_SWAP_CHAIN_DESC& desc, D3D_DRIVER_TYPE driver,
                            IDXGISwapChain** chain, ID3D11Device** device,
                            ID3D11DeviceContext** context) {
    D3D_FEATURE_LEVEL level;
    return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                         &desc, chain, device, &level, context);
}

} // namespace

Overlay* Overlay::instance_ = nullptr;

bool Overlay::initialize() {
    instance_ = this;
    return probe_swap_chain_vtable() && install_hooks();
}

// Creates a hidden window and a throwaway device purely to read the swap-chain
// vtable; the application's own swap chain shares the same implementation.
bool Overlay::probe_swap_chain_vtable() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kProbeWindowClass;
    RegisterClassW(&window_class);
    HWND window =
        CreateWindowExW(0, window_class.lpszClassName, kProbeWindowTitle, WS_OVERLAPPED, 0, 0, 100,
                        100, nullptr, nullptr, window_class.hInstance, nullptr);
    if (!window) {
        plugins_.log(kLogError, "Could not create D3D11 probe window");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT result = create_probe_device(desc, D3D_DRIVER_TYPE_HARDWARE, &chain, &device, &context);
    if (FAILED(result)) {
        result = create_probe_device(desc, D3D_DRIVER_TYPE_WARP, &chain, &device, &context);
    }
    if (SUCCEEDED(result)) {
        void** vtable = *reinterpret_cast<void***>(chain);
        present_target_ = vtable[kPresentSlot];
        resize_buffers_target_ = vtable[kResizeBuffersSlot];
    }

    if (context) {
        context->Release();
    }
    if (device) {
        device->Release();
    }
    if (chain) {
        chain->Release();
    }
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);

    if (FAILED(result)) {
        plugins_.log(kLogError,
                     "D3D11 probe failed: " + std::to_string(static_cast<unsigned long>(result)));
        return false;
    }
    return true;
}

bool Overlay::install_hooks() {
    const MH_STATUS present_status =
        MH_CreateHook(present_target_, reinterpret_cast<void*>(present_detour),
                      reinterpret_cast<void**>(&original_present_));
    const MH_STATUS resize_status =
        MH_CreateHook(resize_buffers_target_, reinterpret_cast<void*>(resize_buffers_detour),
                      reinterpret_cast<void**>(&original_resize_buffers_));
    if (present_status != MH_OK || resize_status != MH_OK) {
        plugins_.log(kLogError, "Could not create Present/Resize hooks");
        return false;
    }
    if (MH_EnableHook(present_target_) != MH_OK || MH_EnableHook(resize_buffers_target_) != MH_OK) {
        plugins_.log(kLogError, "Could not enable Present/Resize hooks");
        return false;
    }
    plugins_.log(kLogInfo, "D3D11 Present and ResizeBuffers hooks installed");
    return true;
}

void Overlay::shutdown() {
    stopping_ = true;
    if (present_target_) {
        MH_DisableHook(present_target_);
    }
    if (resize_buffers_target_) {
        MH_DisableHook(resize_buffers_target_);
    }
    std::lock_guard lock(mutex_);
    release_renderer();
    // Disabled trampolines intentionally stay in the pinned core DLL until
    // process exit: a thread may still be returning through one.
}

HRESULT STDMETHODCALLTYPE Overlay::present_detour(IDXGISwapChain* chain, UINT sync_interval,
                                                  UINT flags) {
    Overlay* self = instance_;
    if (!self) {
        return E_FAIL;
    }
    try {
        if (!(flags & DXGI_PRESENT_TEST)) {
            self->draw_frame(chain);
        }
    } catch (const std::exception& error) {
        self->plugins_.log(kLogError, std::string("Overlay: ") + error.what());
    }
    return self->original_present_(chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE Overlay::resize_buffers_detour(IDXGISwapChain* chain, UINT buffer_count,
                                                         UINT width, UINT height,
                                                         DXGI_FORMAT format, UINT flags) {
    Overlay* self = instance_;
    std::lock_guard lock(self->mutex_);
    // ResizeBuffers fails while any back-buffer reference is outstanding.
    if (chain == self->swap_chain_ && self->render_target_) {
        self->render_target_->Release();
        self->render_target_ = nullptr;
    }
    if (self->ready_) {
        ImGui::SetCurrentContext(self->imgui_);
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }
    return self->original_resize_buffers_(chain, buffer_count, width, height, format, flags);
}

void Overlay::draw_frame(IDXGISwapChain* chain) {
    std::lock_guard lock(mutex_);
    if (stopping_) {
        return;
    }
    if (!ready_ || chain != swap_chain_) {
        if (!bind(chain)) {
            return;
        }
    }
    if (!ensure_render_target()) {
        return;
    }

    ImGui::SetCurrentContext(imgui_);
    apply_ui_scale();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    publish_input_capture();

    ArkFrameV1 frame;
    frame.imgui_version = IMGUI_VERSION_NUM;
    frame.imgui_context = imgui_;
    frame.delta_time = ImGui::GetIO().DeltaTime;

    // Leases keep every plugin that contributed draw data loaded until the
    // frame has been rendered.
    std::vector<std::unique_ptr<Invocation>> frame_leases;
    plugins_.dispatch(CallbackKind::Frame, frame, &frame_leases);
    if (overlay_visible_) {
        draw_manager();
        plugins_.dispatch(CallbackKind::Draw, frame, &frame_leases);
        if (!plugins_.test_host() && !plugins_.input_protected()) {
            draw_input_warning();
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = overlay_visible_ && (menu_visible_ || io.WantCaptureMouse);
    ImGui::Render();
    publish_input_capture();
    if (overlay_visible_) {
        render_draw_data();
    }
    rendered_ = true;
}

void Overlay::draw_input_warning() {
    ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.95f);
    ImGui::Begin("Native input adapter unavailable", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored({1.f, .25f, .15f, 1.f}, "NATIVE POLLING INPUT IS NOT PROTECTED");
    ImGui::TextUnformatted("Wait for the native adapter before interacting with the overlay.");
    ImGui::End();
}

// Renders on top of the application's back buffer and restores whatever
// render targets the application had bound.
void Overlay::render_draw_data() {
    ID3D11RenderTargetView* previous_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* previous_depth = nullptr;
    context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_targets,
                                 &previous_depth);
    context_->OMSetRenderTargets(1, &render_target_, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_targets,
                                 previous_depth);
    for (ID3D11RenderTargetView* target : previous_targets) {
        if (target) {
            target->Release();
        }
    }
    if (previous_depth) {
        previous_depth->Release();
    }
}

} // namespace ark::core
