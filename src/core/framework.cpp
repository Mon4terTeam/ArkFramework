// Core DLL entry points: explicit bootstrap, worker lifecycle and the exported
// control surface used by the loader and the test host.

#include <windows.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>

#include <MinHook.h>
#include <ark/plugin.h>

#include "overlay.h"
#include "plugin_manager.h"

namespace {

constexpr DWORD kMaxPath = 32768;
constexpr size_t kKernelObjectNameLength = 96;
constexpr uint32_t kLogInfo = 1;
constexpr uint32_t kLogError = 3;
constexpr DWORD kStopPollIntervalMs = 1000;
constexpr wchar_t kTestHostExecutable[] = L"ArkFrameworkHost.exe";

HMODULE g_self_module = nullptr;
HANDLE g_process_guard = nullptr;
HANDLE g_ready_event = nullptr;
HANDLE g_failed_event = nullptr;

std::atomic<bool> g_starting = false;
std::atomic<bool> g_stopping = false;
std::atomic<bool> g_running = false;
std::atomic<bool> g_failed = false;

// Copied by ark_framework_start before the worker thread is created.
struct BootConfig {
    wchar_t root[kMaxPath]{};
    bool test_host = false;
};

BootConfig g_boot;

struct CoreState {
    std::unique_ptr<ark::core::PluginManager> plugins;
    std::unique_ptr<ark::core::Overlay> overlay;
};

// Published only after all API-visible members have been initialized. The core
// stays pinned and the state is never freed, so readers never race destruction.
std::atomic<CoreState*> g_state = nullptr;

void initialize_minhook() {
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        throw std::runtime_error("MinHook initialization failed");
    }
}

void run_core(CoreState& state) {
    state.plugins = std::make_unique<ark::core::PluginManager>(g_boot.root, g_boot.test_host);
    state.plugins->log(kLogInfo, "Core starting (explicit bootstrap)");
    initialize_minhook();

    state.overlay = std::make_unique<ark::core::Overlay>(*state.plugins);
    if (!state.overlay->initialize()) {
        throw std::runtime_error("D3D11 overlay initialization failed");
    }
    state.plugins->start();

    g_state.store(&state);
    g_running = true;
    SetEvent(g_ready_event);
    state.plugins->log(
        kLogInfo, "Core running; application adapters and domain services are provided by plugins");

    while (!g_stopping) {
        Sleep(kStopPollIntervalMs);
    }

    ResetEvent(g_ready_event);
    state.overlay->shutdown();
    state.plugins->stop();
    g_running = false;
}

void handle_core_failure(CoreState* state, const std::exception& error) {
    ResetEvent(g_ready_event);
    g_failed = true;
    SetEvent(g_failed_event);
    g_running = false;
    if (!state) {
        return;
    }
    if (state->plugins) {
        state->plugins->log(kLogError, error.what());
    }
    if (state->overlay) {
        state->overlay->shutdown();
    }
    if (state->plugins) {
        state->plugins->stop();
    }
}

DWORD WINAPI core_worker(void*) {
    // Intentionally never deleted: exported functions may still read it.
    CoreState* state = nullptr;
    try {
        state = new CoreState;
        run_core(*state);
    } catch (const std::exception& error) {
        handle_core_failure(state, error);
    }
    return 0;
}

bool fail_startup(DWORD error) {
    g_failed = true;
    if (g_failed_event) {
        SetEvent(g_failed_event);
    }
    SetLastError(error);
    return false;
}

bool reject(DWORD error) {
    SetLastError(error);
    return false;
}

bool is_test_host_process(bool& test_host) {
    wchar_t executable[kMaxPath];
    const DWORD length = GetModuleFileNameW(nullptr, executable, kMaxPath);
    if (!length || length >= kMaxPath) {
        return false;
    }
    const wchar_t* leaf = wcsrchr(executable, L'\\');
    leaf = leaf ? leaf + 1 : executable;
    test_host = _wcsicmp(leaf, kTestHostExecutable) == 0;
    return true;
}

bool is_absolute_root(const wchar_t* root, size_t length) {
    if (length < 3) {
        return false;
    }
    const bool drive_letter =
        (root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z');
    const bool drive = drive_letter && root[1] == L':' && (root[2] == L'\\' || root[2] == L'/');
    const bool unc = root[0] == L'\\' && root[1] == L'\\';
    return drive || unc;
}

// Copies the caller's root, or derives it from the core DLL directory.
bool resolve_root(const ArkBootOptionsV1* options, wchar_t (&root)[kMaxPath]) {
    if (options && options->root_directory) {
        const size_t length = wcsnlen(options->root_directory, kMaxPath);
        if (!length || length >= kMaxPath) {
            return false;
        }
        memcpy(root, options->root_directory, (length + 1) * sizeof(wchar_t));
        return is_absolute_root(root, length);
    }

    const DWORD length = GetModuleFileNameW(g_self_module, root, kMaxPath);
    if (!length || length >= kMaxPath) {
        return false;
    }
    wchar_t* slash = wcsrchr(root, L'\\');
    if (!slash) {
        return false;
    }
    *slash = 0;
    return true;
}

HANDLE create_named_event(const wchar_t* kind) {
    wchar_t name[kKernelObjectNameLength];
    swprintf(name, kKernelObjectNameLength, L"Local\\ArkFramework.%ls.%lu", kind,
             GetCurrentProcessId());
    return CreateEventW(nullptr, TRUE, FALSE, name);
}

HANDLE create_process_guard() {
    wchar_t name[kKernelObjectNameLength];
    swprintf(name, kKernelObjectNameLength, L"Local\\ArkFramework.Core.%lu", GetCurrentProcessId());
    return CreateMutexW(nullptr, FALSE, name);
}

CoreState* running_state() {
    CoreState* state = g_state.load();
    return g_running && state ? state : nullptr;
}

} // namespace

ARK_EXPORT bool ark_framework_start(const ArkBootOptionsV1* options) {
    if (g_starting.load()) {
        return !g_failed.load() && !g_stopping.load();
    }
    if (options && (options->size < sizeof(ArkBootOptionsV1) || options->test_host > 1)) {
        return reject(ERROR_INVALID_PARAMETER);
    }
    bool test_host = false;
    if (!is_test_host_process(test_host)) {
        return reject(ERROR_BAD_PATHNAME);
    }
    // Never allow a caller to enable synthetic test events inside the game.
    if (options && options->test_host && !test_host) {
        return reject(ERROR_ACCESS_DENIED);
    }
    wchar_t root[kMaxPath];
    if (!resolve_root(options, root)) {
        return reject(ERROR_BAD_PATHNAME);
    }

    if (g_starting.exchange(true)) {
        return !g_failed.load() && !g_stopping.load();
    }
    wcscpy(g_boot.root, root);
    g_boot.test_host = test_host;

    g_failed_event = create_named_event(L"Failed");
    if (!g_failed_event) {
        return fail_startup(GetLastError());
    }
    g_ready_event = create_named_event(L"Ready");
    if (!g_ready_event) {
        return fail_startup(GetLastError());
    }
    ResetEvent(g_ready_event);
    ResetEvent(g_failed_event);

    g_process_guard = create_process_guard();
    if (!g_process_guard) {
        return fail_startup(GetLastError());
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return fail_startup(ERROR_ALREADY_EXISTS);
    }

    // Hooks, trampolines and the published state live in this module, so it
    // must never be unloaded once the worker exists.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(core_worker), &pinned)) {
        return fail_startup(GetLastError());
    }
    HANDLE thread = CreateThread(nullptr, 0, core_worker, nullptr, 0, nullptr);
    if (!thread) {
        return fail_startup(GetLastError());
    }
    CloseHandle(thread);
    return true;
}

ARK_EXPORT bool ark_framework_is_running() {
    return g_running;
}

ARK_EXPORT bool ark_framework_has_rendered() {
    CoreState* state = g_state.load();
    return state && state->overlay->rendered();
}

ARK_EXPORT void ark_framework_shutdown() {
    g_stopping = true;
    if (g_ready_event) {
        ResetEvent(g_ready_event);
    }
}

ARK_EXPORT void ark_framework_control(const char* operation, const char* filename) {
    CoreState* state = running_state();
    if (state && operation && filename) {
        state->plugins->request(operation, filename);
    }
}

ARK_EXPORT bool ark_framework_with_service(const char* name, uint32_t abi_version,
                                           size_t minimum_size, ArkServiceCallback callback,
                                           void* user) {
    CoreState* state = running_state();
    return state &&
           state->plugins->with_external_service(name, abi_version, minimum_size, callback, user);
}

// All callers start the core explicitly after LoadLibrary returns, outside the
// loader lock, so DllMain only records the module handle.
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self_module = module;
    }
    return TRUE;
}
