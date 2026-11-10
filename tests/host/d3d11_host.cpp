// Graphical test host (ArkFrameworkHost.exe): a plain D3D11 window that loads
// the core, feeds a synthetic battle replay to the damage meter and optionally
// runs the plugin unload/load/reload self-test.
//
// Flags:
//   --self-test     run the self-test and exit (implies --auto-close)
//   --await-loader  wait for an external loader to inject and start the core
//   --auto-close    exit after a fixed timeout
//
// Exit codes: 0 success, 2 no D3D11 device, 3 core DLL not loadable,
// 4 auto-close without a rendered overlay frame, 5 missing start export,
// 6 start failed, 7 self-test failed.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <chrono>
#include <cwchar>
#include <filesystem>

#include <ark/battle.h>
#include <ark/plugin.h>

#include "battle_replay.h"
#include "self_test.h"

namespace {

using ark::test_host::FrameworkControl;
using ark::test_host::ReplayState;
using ark::test_host::SelfTestState;

constexpr int kExitSuccess = 0;
constexpr int kExitNoDevice = 2;
constexpr int kExitLoadFailed = 3;
constexpr int kExitNotRendered = 4;
constexpr int kExitNoStartExport = 5;
constexpr int kExitStartFailed = 6;
constexpr int kExitSelfTestFailed = 7;

constexpr ULONGLONG kSelfTestTimeoutMs = 60000;
constexpr ULONGLONG kAutoCloseTimeoutMs = 12000;
constexpr ULONGLONG kShutdownTimeoutMs = 3000;
constexpr DWORD kMaxPath = 32768;

constexpr wchar_t kWindowClass[] = L"ArkFramework.TestHost";
constexpr wchar_t kWindowTitle[] = L"ArkFramework Test Host — D3D11 / 插件动态加载卸载测试";
constexpr wchar_t kFrameworkDll[] = L"ArkFramework.dll";

struct Graphics {
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
};

// Owned by the UI thread; the window procedure resizes it in place.
Graphics g_graphics;

void create_render_target() {
    ID3D11Texture2D* back_buffer = nullptr;
    if (SUCCEEDED(g_graphics.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                   reinterpret_cast<void**>(&back_buffer)))) {
        g_graphics.device->CreateRenderTargetView(back_buffer, nullptr, &g_graphics.render_target);
        back_buffer->Release();
    }
}

void resize_swap_chain(UINT width, UINT height) {
    g_graphics.context->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_graphics.render_target) {
        g_graphics.render_target->Release();
        g_graphics.render_target = nullptr;
    }
    g_graphics.swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    create_render_target();
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SIZE && g_graphics.swap_chain && wparam != SIZE_MINIMIZED) {
        resize_swap_chain(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND create_host_window(HINSTANCE instance) {
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&window_class);
    return CreateWindowExW(0, window_class.lpszClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, 80, 80,
                           1400, 850, nullptr, nullptr, instance, nullptr);
}

bool create_graphics(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    HRESULT result = E_FAIL;
    for (D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &desc, &g_graphics.swap_chain,
                                               &g_graphics.device, &level, &g_graphics.context);
        if (SUCCEEDED(result)) {
            break;
        }
    }
    return SUCCEEDED(result);
}

void present_clear_frame() {
    constexpr float kClearColor[] = {.035f, .065f, .095f, 1};
    g_graphics.context->OMSetRenderTargets(1, &g_graphics.render_target, nullptr);
    g_graphics.context->ClearRenderTargetView(g_graphics.render_target, kClearColor);
    g_graphics.swap_chain->Present(1, 0);
}

void release_graphics() {
    if (g_graphics.render_target) {
        g_graphics.render_target->Release();
    }
    g_graphics.swap_chain->Release();
    g_graphics.context->Release();
    g_graphics.device->Release();
}

struct FrameworkExports {
    bool (*is_running)() = nullptr;
    bool (*has_rendered)() = nullptr;
    void (*shutdown)() = nullptr;
    ArkFrameworkWithService with_service = nullptr;
    FrameworkControl control = nullptr;

    bool running() const { return is_running && is_running(); }
};

template <typename Function>
Function resolve(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

FrameworkExports resolve_exports(HMODULE framework) {
    FrameworkExports exports;
    exports.with_service =
        resolve<ArkFrameworkWithService>(framework, "ark_framework_with_service");
    exports.control = resolve<FrameworkControl>(framework, "ark_framework_control");
    exports.is_running =
        resolve<decltype(exports.is_running)>(framework, "ark_framework_is_running");
    exports.has_rendered =
        resolve<decltype(exports.has_rendered)>(framework, "ark_framework_has_rendered");
    exports.shutdown = resolve<decltype(exports.shutdown)>(framework, "ark_framework_shutdown");
    return exports;
}

std::filesystem::path host_directory() {
    wchar_t executable[kMaxPath];
    GetModuleFileNameW(nullptr, executable, kMaxPath);
    return std::filesystem::path(executable).parent_path();
}

void pump_messages(MSG& message) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void shutdown_framework(const FrameworkExports& exports) {
    if (!exports.shutdown) {
        return;
    }
    exports.shutdown();
    const ULONGLONG deadline = GetTickCount64() + kShutdownTimeoutMs;
    while (exports.running() && GetTickCount64() < deadline) {
        Sleep(10);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
    const bool self_test = wcsstr(command_line, L"--self-test") != nullptr;
    const bool await_loader = wcsstr(command_line, L"--await-loader") != nullptr;
    const bool auto_close = wcsstr(command_line, L"--auto-close") != nullptr || self_test;
    const ULONGLONG host_started = GetTickCount64();
    const ULONGLONG auto_close_timeout = self_test ? kSelfTestTimeoutMs : kAutoCloseTimeoutMs;

    HWND window = create_host_window(instance);
    if (!create_graphics(window)) {
        return kExitNoDevice;
    }
    create_render_target();
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    HMODULE framework = nullptr;
    if (!await_loader) {
        framework =
            LoadLibraryExW((host_directory() / kFrameworkDll).c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!framework) {
            MessageBoxW(window, L"ArkFramework.dll could not be loaded", L"Test host",
                        MB_ICONERROR);
            return kExitLoadFailed;
        }
        // A local LoadLibrary is not boot. The external-loader path deliberately
        // never calls start, so a missing loader invocation cannot pass.
        auto start = resolve<ArkFrameworkStart>(framework, "ark_framework_start");
        if (!start) {
            return kExitNoStartExport;
        }
        if (!start(nullptr)) {
            return kExitStartFailed;
        }
    }

    FrameworkExports exports;
    SelfTestState self_test_state = SelfTestState::Initial;
    bool self_test_passed = false;
    ReplayState replay;
    auto previous_tick = std::chrono::steady_clock::now();

    MSG message{};
    while (message.message != WM_QUIT) {
        pump_messages(message);
        if (message.message == WM_QUIT) {
            break;
        }
        if (!framework && await_loader) {
            framework = GetModuleHandleW(kFrameworkDll);
        }
        if (framework && !exports.is_running) {
            exports = resolve_exports(framework);
        }
        if (auto_close && GetTickCount64() - host_started > auto_close_timeout) {
            DestroyWindow(window);
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        replay.elapsed += std::chrono::duration<double>(now - previous_tick).count();
        previous_tick = now;

        if (exports.with_service && exports.running()) {
            if (!self_test || ark::test_host::publishes_replay(self_test_state)) {
                if (!exports.with_service(ARK_BATTLE_TEST_SERVICE_NAME_V2,
                                          ARK_BATTLE_ABI_VERSION_V2,
                                          sizeof(ArkBattleTestApiV2),
                                          ark::test_host::publish_replay, &replay)) {
                    replay = {};
                }
            }
            if (self_test && exports.control) {
                ark::test_host::advance_self_test(self_test_state, exports.control,
                                                  exports.with_service, replay);
                if (self_test_state == SelfTestState::Complete) {
                    self_test_passed = !replay.failed;
                    DestroyWindow(window);
                    continue;
                }
            }
        }

        present_clear_frame();
        Sleep(1);
    }

    const bool was_rendered = exports.has_rendered && exports.has_rendered();
    shutdown_framework(exports);
    release_graphics();
    if (self_test && !self_test_passed) {
        return kExitSelfTestFailed;
    }
    return auto_close && !was_rendered ? kExitNotRendered : kExitSuccess;
}
