#include "unity_input.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "il2cpp_exports.h"

namespace ark {
namespace {

using il2cpp::is_executable;
using il2cpp::resolve_export;

// Capture bits shared by UnityInput::capture_ and Impl::capture_.
constexpr unsigned kMouseCapture = 1u;
constexpr unsigned kKeyboardCapture = 2u;

constexpr uint32_t kMethodAttributeStatic = 0x10;

// Unity KeyCode.Mouse0 .. KeyCode.Mouse6.
constexpr int kMouseButtonCount = 7;
constexpr int kMaxMouseKeyCode = 1024;

// Physical virtual key of each Unity mouse button; Mouse5/Mouse6 have none.
constexpr unsigned kMouseVirtualKeys[kMouseButtonCount] = {
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2, 0, 0};

// Pointer position reported while the pointer is owned by plugin UI: far outside the
// game surface, so a synthetic release cannot raycast into a game UI click.
constexpr float kOffscreenPointer = -1000000.f;

constexpr auto kDrainTimeout = std::chrono::seconds(2);

constexpr LPARAM kExtendedKeyFlag = 1LL << 24;

struct NamedKey {
    int virtual_key;
    const char* key_code;
};

constexpr NamedKey kNamedKeys[] = {
    {VK_BACK, "Backspace"},
    {VK_TAB, "Tab"},
    {VK_RETURN, "Return"},
    {VK_PAUSE, "Pause"},
    {VK_ESCAPE, "Escape"},
    {VK_SPACE, "Space"},
    {VK_UP, "UpArrow"},
    {VK_DOWN, "DownArrow"},
    {VK_LEFT, "LeftArrow"},
    {VK_RIGHT, "RightArrow"},
    {VK_INSERT, "Insert"},
    {VK_DELETE, "Delete"},
    {VK_HOME, "Home"},
    {VK_END, "End"},
    {VK_PRIOR, "PageUp"},
    {VK_NEXT, "PageDown"},
    {VK_LSHIFT, "LeftShift"},
    {VK_RSHIFT, "RightShift"},
    {VK_LCONTROL, "LeftControl"},
    {VK_RCONTROL, "RightControl"},
    {VK_LMENU, "LeftAlt"},
    {VK_RMENU, "RightAlt"},
    {VK_CAPITAL, "CapsLock"},
    {VK_NUMLOCK, "Numlock"},
    {VK_SCROLL, "ScrollLock"},
    {VK_MULTIPLY, "KeypadMultiply"},
    {VK_ADD, "KeypadPlus"},
    {VK_SUBTRACT, "KeypadMinus"},
    {VK_DECIMAL, "KeypadPeriod"},
    {VK_DIVIDE, "KeypadDivide"},
    {VK_OEM_1, "Semicolon"},
    {VK_OEM_PLUS, "Equals"},
    {VK_OEM_COMMA, "Comma"},
    {VK_OEM_MINUS, "Minus"},
    {VK_OEM_PERIOD, "Period"},
    {VK_OEM_2, "Slash"},
    {VK_OEM_3, "BackQuote"},
    {VK_OEM_4, "LeftBracket"},
    {VK_OEM_5, "Backslash"},
    {VK_OEM_6, "RightBracket"},
    {VK_OEM_7, "Quote"},
    {VK_LWIN, "LeftWindows"},
    {VK_RWIN, "RightWindows"},
};

// The three polling flavours of a button, in hook-table order.
enum class ButtonQuery : size_t {
    Held,
    Down,
    Up,
};

bool is_physically_pressed(unsigned virtual_key) {
    return (GetAsyncKeyState(static_cast<int>(virtual_key)) & 0x8000) != 0;
}

// Subset of the IL2CPP embedding API needed to locate the Unity input icalls.
struct Il2cppApi {
    void* (*domain_get)();
    void* (*corlib)();
    void** (*attached_threads)(size_t*);
    void* (*thread_current)();
    void* (*thread_attach)(void*);
    void (*thread_detach)(void*);
    void** (*domain_assemblies)(void*, size_t*);
    void* (*assembly_image)(void*);
    const char* (*image_name)(void*);
    void* (*class_from_name)(void*, const char*, const char*);
    void* (*class_method)(void*, const char*, int);
    void* (*class_field)(void*, const char*);
    void (*field_static)(void*, void*);
    int (*class_value_size)(void*, uint32_t*);
    int (*method_param_count)(void*);
    uint32_t (*method_flags)(void*, uint32_t*);
};

// Attaches the calling thread to the IL2CPP domain for the scope unless it already is.
class ScopedThreadAttachment {
public:
    ScopedThreadAttachment(const Il2cppApi& api, void* domain) : api_(api) {
        if (api.thread_current()) {
            return;
        }
        attached_ = api.thread_attach(domain);
        if (!attached_) {
            throw std::runtime_error("IL2CPP thread attachment failed");
        }
    }

    ~ScopedThreadAttachment() {
        if (attached_) {
            api_.thread_detach(attached_);
        }
    }

    ScopedThreadAttachment(const ScopedThreadAttachment&) = delete;
    ScopedThreadAttachment& operator=(const ScopedThreadAttachment&) = delete;

private:
    const Il2cppApi& api_;
    void* attached_ = nullptr;
};

} // namespace

// Native Input icalls have no trailing MethodInfo argument. Their signatures and the
// injected vector layouts are verified against InputLegacyModule's metadata rather
// than GameAssembly/UnityPlayer offsets.
struct UnityInput::Impl {
    using ButtonFn = bool (*)(int);
    using VectorFn = void (*)(float*);
    using FrameCountFn = int (*)();

    static constexpr size_t kHookCount = 8;

    // Per-KeyCode routing state. Frames are Unity frame counts; -1 means unset.
    struct KeyState {
        // Frame on which a blocked key was first seen neutral; the block lifts on the
        // next distinct neutral frame.
        int neutral = -1;
        // Frame on which the game receives a synthetic release for a press it saw.
        int cancel = -1;
        // Frame on which a stale window marker was first seen neutral.
        int window_neutral = -1;
        // Win32 virtual key used to check the physical button state.
        unsigned physical_key = 0;
        // The UI owns this key until it is physically released for a neutral frame.
        bool blocked = false;
        // A window press was consumed by the UI.
        bool window_held = false;
        // A window press was passed through to the game.
        bool window_game_held = false;
        // The game currently believes the key is held.
        bool game_held = false;
        // Values last reported to the game.
        bool held = false;
        bool down = false;
        bool up = false;
    };

    explicit Impl(const ArkPluginApiV1& host) : host_(host) { virtual_keys_.fill(-1); }

    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    bool disabled() const { return disabled_.load(std::memory_order_acquire); }

    void set_capture(unsigned capture) { capture_.store(capture, std::memory_order_release); }

    void request_stop() { stop_requested_.store(true, std::memory_order_release); }

    bool stop_requested() const { return stop_requested_.load(std::memory_order_acquire); }

    static void clear_current() { current_.store(nullptr, std::memory_order_release); }

    // Binds the IL2CPP exports and returns the domain, or nullptr while the runtime
    // has not finished starting up. Throws when an export is missing.
    void* bind_runtime() {
#define ARK_BIND_EXPORT(member, symbol)                                                            \
    api_.member = resolve_export<decltype(api_.member)>("il2cpp_" symbol)
        ARK_BIND_EXPORT(domain_get, "domain_get");
        ARK_BIND_EXPORT(corlib, "get_corlib");
        ARK_BIND_EXPORT(attached_threads, "thread_get_all_attached_threads");
        ARK_BIND_EXPORT(thread_current, "thread_current");
        ARK_BIND_EXPORT(thread_attach, "thread_attach");
        ARK_BIND_EXPORT(thread_detach, "thread_detach");
        ARK_BIND_EXPORT(domain_assemblies, "domain_get_assemblies");
        ARK_BIND_EXPORT(assembly_image, "assembly_get_image");
        ARK_BIND_EXPORT(image_name, "image_get_name");
        ARK_BIND_EXPORT(class_from_name, "class_from_name");
        ARK_BIND_EXPORT(class_method, "class_get_method_from_name");
        ARK_BIND_EXPORT(class_field, "class_get_field_from_name");
        ARK_BIND_EXPORT(field_static, "field_static_get_value");
        ARK_BIND_EXPORT(class_value_size, "class_value_size");
        ARK_BIND_EXPORT(method_param_count, "method_get_param_count");
        ARK_BIND_EXPORT(method_flags, "method_get_flags");
#undef ARK_BIND_EXPORT
        size_t registered = 0;
        if (!api_.corlib() || !api_.attached_threads(&registered) || !registered) {
            return nullptr;
        }
        return api_.domain_get();
    }

    // Installs and enables the hooks. Returns false while the Unity modules are not
    // loaded. On exception the caller must run UnityInput::shutdown().
    bool start(void* domain) {
        ScopedThreadAttachment attachment(api_, domain);
        void* input_image = nullptr;
        void* core_image = nullptr;
        size_t count = 0;
        void** assemblies = api_.domain_assemblies(domain, &count);
        for (size_t i = 0; i < count; ++i) {
            void* image = api_.assembly_image(assemblies[i]);
            const char* name = api_.image_name(image);
            if (!std::strcmp(name, "UnityEngine.InputLegacyModule.dll")) {
                input_image = image;
            } else if (!std::strcmp(name, "UnityEngine.CoreModule.dll")) {
                core_image = image;
            }
        }
        if (!input_image || !core_image) {
            return false;
        }
        connect(unity_class(input_image, "Input"), unity_class(core_image, "KeyCode"),
                unity_class(core_image, "Vector2"), unity_class(core_image, "Vector3"),
                unity_class(core_image, "Time"));
        for (uint64_t hook : hooks_) {
            if (!host_.enable_native_hook(host_.owner, hook, true)) {
                throw std::runtime_error("Could not enable Unity input hook");
            }
        }
        enabled_.store(true, std::memory_order_release);
        return true;
    }

    bool can_unload() {
        std::lock_guard lock(mutex_);
        const unsigned captured = capture_.load(std::memory_order_acquire);
        for (size_t i = 0; i < states_.size(); ++i) {
            const KeyState& state = states_[i];
            if (state.window_held) {
                return false;
            }
            const bool captured_game_press =
                (captured & capture_mask(static_cast<int>(i))) && state.game_held;
            if (state.blocked || captured_game_press) {
                // The reload button's released click can still be quarantined. Let
                // shutdown finish that tail; do not mistake it for a hold.
                if (!state.physical_key || is_physically_pressed(state.physical_key)) {
                    return false;
                }
            }
        }
        return true;
    }

    // Waits for owned presses to drain, then removes the hooks. On false the hooks
    // that could not be removed stay installed and the call may be retried.
    bool shutdown() {
        {
            std::unique_lock lock(mutex_);
            draining_ = true;
            const unsigned captured = capture_.exchange(0, std::memory_order_acq_rel);
            bool pending = false;
            for (size_t i = 0; i < states_.size(); ++i) {
                KeyState& state = states_[i];
                if ((captured & capture_mask(static_cast<int>(i))) && state.game_held) {
                    state.blocked = true;
                }
                pending |= state.blocked || state.window_held;
            }
            // Keep suppressing the captured press through its physical release and a
            // neutral Unity frame. A timeout retains the entire provider.
            if (!enabled() || !pending) {
                disable();
            }
            if (!drained_.wait_for(lock, kDrainTimeout, [&] { return disabled(); })) {
                return false;
            }
        }
        bool complete = true;
        for (uint64_t& hook : hooks_) {
            if (!hook) {
                continue;
            }
            if (host_.remove_native_hook(host_.owner, hook)) {
                hook = 0;
            } else {
                complete = false;
            }
        }
        return complete;
    }

    bool route_window_message(UINT message, WPARAM wparam, LPARAM lparam) {
        std::lock_guard lock(mutex_);
        if (!enabled()) {
            return false;
        }
        if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED) {
            reset_window_state(message);
            return false;
        }

        const WindowButton button = decode_window_message(message, wparam, lparam);
        if (button.code < 0 || button.code >= static_cast<int>(states_.size())) {
            return false;
        }
        KeyState& state = states_[button.code];
        const bool captured =
            (capture_.load(std::memory_order_acquire) & capture_mask(button.code)) != 0;
        state.physical_key = button.code >= mouse_first_
                                 ? kMouseVirtualKeys[button.code - mouse_first_]
                                 : button.physical_key;
        state.window_neutral = -1;

        if (button.down) {
            if (captured || state.blocked) {
                state.blocked = true;
                state.neutral = -1;
                if (!state.window_game_held) {
                    state.window_held = true;
                }
                return true;
            }
            state.window_game_held = true;
        }
        if (button.up) {
            const bool owned = state.window_held;
            state.window_held = false;
            if (state.window_game_held) {
                state.window_game_held = false;
                return false;
            }
            return owned || state.blocked || captured;
        }
        return false;
    }

private:
    struct WindowButton {
        int code = -1;
        unsigned physical_key = 0;
        bool down = false;
        bool up = false;
    };

    unsigned capture_mask(int code) const {
        return code >= mouse_first_ ? kMouseCapture : kKeyboardCapture;
    }

    void* unity_class(void* image, const char* name) {
        void* type = api_.class_from_name(image, "UnityEngine", name);
        if (!type) {
            throw std::runtime_error(std::string("Missing Unity class: ") + name);
        }
        return type;
    }

    void* resolve_icall(const char* name) {
        void* address = resolve_icall_(name);
        if (!is_executable(address)) {
            throw std::runtime_error(std::string("Unity input polling unavailable: ") + name);
        }
        return address;
    }

    bool is_static_method(void* info, int param_count) {
        uint32_t implementation_flags = 0;
        return info && api_.method_param_count(info) == param_count &&
               (api_.method_flags(info, &implementation_flags) & kMethodAttributeStatic);
    }

    int key_code(void* key_code_class, const char* name) {
        void* field = api_.class_field(key_code_class, name);
        if (!field) {
            throw std::runtime_error(std::string("Missing Unity KeyCode: ") + name);
        }
        int value = 0;
        api_.field_static(field, &value);
        return value;
    }

    void connect(void* input, void* key_code_class, void* vector2, void* vector3, void* time) {
        resolve_icall_ = resolve_export<void* (*)(const char*)>("il2cpp_resolve_icall");
        map_key_codes(key_code_class);

        // Validate only the buffers whose native ABI we actually use.
        uint32_t alignment = 0;
        if (!vector2 || !vector3 ||
            static_cast<size_t>(api_.class_value_size(vector2, &alignment)) != 2 * sizeof(float) ||
            static_cast<size_t>(api_.class_value_size(vector3, &alignment)) != 3 * sizeof(float)) {
            throw std::runtime_error("Incompatible Unity input vector layout");
        }

        void* frame_info = time ? api_.class_method(time, "get_frameCount", 0) : nullptr;
        if (!is_static_method(frame_info, 0)) {
            throw std::runtime_error("Missing Unity frame counter");
        }
        frame_count_ =
            reinterpret_cast<FrameCountFn>(resolve_icall("UnityEngine.Time::get_frameCount()"));

        current_.store(this, std::memory_order_release);
        create_hooks(input);
    }

    void map_key_codes(void* key_code_class) {
        mouse_first_ = key_code(key_code_class, "Mouse0");
        mouse_count_ = key_code(key_code_class, "Mouse6") - mouse_first_ + 1;
        keypad_enter_ = key_code(key_code_class, "KeypadEnter");
        if (mouse_first_ <= 0 || mouse_first_ > kMaxMouseKeyCode ||
            mouse_count_ != kMouseButtonCount) {
            throw std::runtime_error("Unsupported Unity KeyCode range");
        }
        states_.resize(static_cast<size_t>(mouse_first_ + mouse_count_));

        for (int i = 0; i < 26; ++i) {
            const char name[] = {static_cast<char>('A' + i), 0};
            virtual_keys_['A' + i] = key_code(key_code_class, name);
        }
        for (int i = 0; i < 10; ++i) {
            char alpha[] = "Alpha0";
            char keypad[] = "Keypad0";
            alpha[5] = static_cast<char>('0' + i);
            keypad[6] = static_cast<char>('0' + i);
            virtual_keys_['0' + i] = key_code(key_code_class, alpha);
            virtual_keys_[VK_NUMPAD0 + i] = key_code(key_code_class, keypad);
        }
        for (int i = 0; i < 15; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "F%d", i + 1);
            virtual_keys_[VK_F1 + i] = key_code(key_code_class, name);
        }
        for (const NamedKey& key : kNamedKeys) {
            virtual_keys_[key.virtual_key] = key_code(key_code_class, key.key_code);
        }
    }

    void create_hooks(void* input) {
        struct HookSpec {
            const char* method;
            const char* icall;
            void* detour;
            void** original;
        };

        const HookSpec specs[kHookCount] = {
            {"GetMouseButton", "UnityEngine.Input::GetMouseButton(System.Int32)",
             reinterpret_cast<void*>(button_detour<true, ButtonQuery::Held>),
             reinterpret_cast<void**>(&mouse_originals_[0])},
            {"GetMouseButtonDown", "UnityEngine.Input::GetMouseButtonDown(System.Int32)",
             reinterpret_cast<void*>(button_detour<true, ButtonQuery::Down>),
             reinterpret_cast<void**>(&mouse_originals_[1])},
            {"GetMouseButtonUp", "UnityEngine.Input::GetMouseButtonUp(System.Int32)",
             reinterpret_cast<void*>(button_detour<true, ButtonQuery::Up>),
             reinterpret_cast<void**>(&mouse_originals_[2])},
            {"GetKeyInt", "UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)",
             reinterpret_cast<void*>(button_detour<false, ButtonQuery::Held>),
             reinterpret_cast<void**>(&key_originals_[0])},
            {"GetKeyDownInt", "UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)",
             reinterpret_cast<void*>(button_detour<false, ButtonQuery::Down>),
             reinterpret_cast<void**>(&key_originals_[1])},
            {"GetKeyUpInt", "UnityEngine.Input::GetKeyUpInt(UnityEngine.KeyCode)",
             reinterpret_cast<void*>(button_detour<false, ButtonQuery::Up>),
             reinterpret_cast<void**>(&key_originals_[2])},
            {"get_mousePosition_Injected",
             "UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)",
             reinterpret_cast<void*>(vector_detour<false>),
             reinterpret_cast<void**>(&position_original_)},
            {"get_mouseScrollDelta_Injected",
             "UnityEngine.Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)",
             reinterpret_cast<void*>(vector_detour<true>),
             reinterpret_cast<void**>(&scroll_original_)},
        };
        for (size_t i = 0; i < kHookCount; ++i) {
            const HookSpec& spec = specs[i];
            if (!is_static_method(api_.class_method(input, spec.method, 1), 1)) {
                throw std::runtime_error(std::string("Incompatible Unity input method: ") +
                                         spec.method);
            }
            void* target = resolve_icall(spec.icall);
            const uint64_t id =
                host_.create_native_hook(host_.owner, target, spec.detour, spec.original);
            if (!id) {
                throw std::runtime_error(std::string("Unity input hook ") + spec.method +
                                         " could not be created");
            }
            hooks_[i] = id;
        }
    }

    // Windows can omit a modifier's KeyUp (notably when releasing both Shift keys), so
    // a stale window marker must not override released native input forever. A native
    // Up, or a full neutral Unity frame, plus physical release retires the marker;
    // `blocked` still owns the release and its normal neutral-frame quarantine.
    void retire_stale_window_marker(KeyState& state, int frame, bool held, bool down, bool up) {
        if (!(state.window_held || state.window_game_held) || !state.physical_key) {
            return;
        }
        const bool physical = is_physically_pressed(state.physical_key);
        if (held || down || physical) {
            state.window_neutral = -1;
            return;
        }
        if (up || (state.window_neutral >= 0 && state.window_neutral != frame)) {
            if (!draining_) {
                state.window_held = false;
            }
            state.window_game_held = false;
            state.window_neutral = -1;
        } else if (state.window_neutral < 0) {
            state.window_neutral = frame;
        }
    }

    // Polls the native button state once and computes what the game may observe.
    KeyState& sample(int code, int frame, unsigned capture) {
        KeyState& state = states_[code];
        const bool is_mouse = code >= mouse_first_;
        const auto& originals = is_mouse ? mouse_originals_ : key_originals_;
        const int argument = is_mouse ? code - mouse_first_ : code;
        const bool held = originals[0](argument);
        const bool down = originals[1](argument);
        const bool up = originals[2](argument);

        retire_stale_window_marker(state, frame, held, down, up);

        const bool captured = (capture & capture_mask(code)) != 0;
        if ((captured || state.window_held) &&
            (held || down || up || state.game_held || state.window_held)) {
            state.blocked = true;
        }
        if ((captured || state.blocked) && state.game_held) {
            state.cancel = frame;
        }
        if (state.blocked) {
            if (held || down || up || state.window_held) {
                state.neutral = -1;
            } else if (state.neutral < 0) {
                state.neutral = frame;
            } else if (state.neutral != frame) {
                state.blocked = false;
                state.neutral = -1;
            }
        }
        const bool suppress = captured || state.blocked;
        state.held = !suppress && held;
        state.down = !suppress && down;
        state.up = state.cancel == frame || (!suppress && up);
        state.game_held = state.held || state.down;
        return state;
    }

    bool pointer_blocked(int frame, unsigned capture) {
        const bool mouse_captured = (capture & kMouseCapture) != 0;
        bool blocked = mouse_captured;
        for (int i = 0; i < mouse_count_; ++i) {
            const KeyState& state = states_[mouse_first_ + i];
            // A position/scroll read must not pre-sample ordinary game buttons or
            // manufacture a game-held press before its consumer.
            if (!state.blocked && !state.window_held && state.cancel != frame &&
                !(mouse_captured && state.game_held)) {
                continue;
            }
            const KeyState& routed = sample(mouse_first_ + i, frame, capture);
            blocked |= routed.blocked || routed.cancel == frame;
        }
        return blocked;
    }

    // While draining, advances every owned key and reports whether all have settled.
    bool drain(int frame) {
        if (!draining_) {
            return false;
        }
        bool pending = false;
        for (int i = 0; i < static_cast<int>(states_.size()); ++i) {
            if (!states_[i].blocked && !states_[i].window_held) {
                continue;
            }
            const KeyState& state = sample(i, frame, 0);
            pending |= state.blocked || state.window_held;
        }
        return !pending;
    }

    void disable() {
        std::lock_guard lock(mutex_);
        if (disabled()) {
            return;
        }
        enabled_.store(false, std::memory_order_release);
        for (uint64_t hook : hooks_) {
            if (hook) {
                host_.enable_native_hook(host_.owner, hook, false);
            }
        }
        disabled_.store(true, std::memory_order_release);
        drained_.notify_all();
    }

    void reset_window_state(UINT message) {
        for (KeyState& state : states_) {
            if (message == WM_CANCELMODE && (state.window_held || state.window_game_held)) {
                state.blocked = true;
            }
            state.window_held = false;
            if (message == WM_KILLFOCUS || message == WM_CANCELMODE) {
                state.window_game_held = false;
            }
            state.window_neutral = -1;
        }
    }

    WindowButton decode_window_message(UINT message, WPARAM wparam, LPARAM lparam) const {
        WindowButton button;
        const int x_button = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            button.code = mouse_first_;
            button.down = true;
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            button.code = mouse_first_ + 1;
            button.down = true;
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            button.code = mouse_first_ + 2;
            button.down = true;
            break;
        case WM_LBUTTONUP:
            button.code = mouse_first_;
            button.up = true;
            break;
        case WM_RBUTTONUP:
            button.code = mouse_first_ + 1;
            button.up = true;
            break;
        case WM_MBUTTONUP:
            button.code = mouse_first_ + 2;
            button.up = true;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            button.code = mouse_first_ + x_button;
            button.down = true;
            break;
        case WM_XBUTTONUP:
            button.code = mouse_first_ + x_button;
            button.up = true;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            unsigned virtual_key = static_cast<unsigned>(wparam);
            button.down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            button.up = !button.down;
            // Generic modifiers are resolved to their left/right variant.
            if (virtual_key == VK_SHIFT || virtual_key == VK_CONTROL || virtual_key == VK_MENU) {
                unsigned scan_code = (static_cast<unsigned long long>(lparam) >> 16) & 0xff;
                if (lparam & kExtendedKeyFlag) {
                    scan_code |= 0xe000;
                }
                virtual_key = MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX);
            }
            button.physical_key = virtual_key;
            if (virtual_key < virtual_keys_.size()) {
                button.code = virtual_keys_[virtual_key];
            }
            if (virtual_key == VK_RETURN && (lparam & kExtendedKeyFlag)) {
                button.code = keypad_enter_;
            }
            break;
        }
        default:
            break;
        }
        return button;
    }

    // Shared body of the six button detours.
    static bool button(bool is_mouse, int argument, ButtonQuery query) {
        Impl* self = current_.load(std::memory_order_acquire);
        const size_t query_index = static_cast<size_t>(query);
        const ButtonFn original =
            (is_mouse ? self->mouse_originals_ : self->key_originals_)[query_index];
        bool result = false;
        bool finished = false;
        {
            std::lock_guard lock(self->mutex_);
            if (!self->enabled()) {
                return original(argument);
            }
            const int code = is_mouse ? self->mouse_first_ + argument : argument;
            // Joysticks and invalid codes keep Unity's own behavior, including argument
            // errors. Alt-F4 remains an OS/window-system action.
            if (code < 0 || code >= static_cast<int>(self->states_.size()) ||
                (is_mouse && (argument < 0 || argument >= self->mouse_count_))) {
                return original(argument);
            }
            const int frame = self->frame_count_();
            const unsigned capture = self->capture_.load(std::memory_order_acquire);
            KeyState& state = self->states_[code];
            const unsigned mask = self->capture_mask(code);
            const auto owned_by_game = [&] {
                return !(capture & mask) && !state.blocked && !state.window_held &&
                       state.cancel != frame;
            };
            const auto passthrough = [&] {
                // Native input can advance between polling calls even when Unity's
                // frame counter has not. Never return a cached value or a different
                // icall's value to an unowned game consumer.
                const bool native = original(argument);
                switch (query) {
                case ButtonQuery::Held:
                    state.held = native;
                    state.game_held = native;
                    break;
                case ButtonQuery::Down:
                    state.down = native;
                    if (native) {
                        state.game_held = true;
                    }
                    break;
                case ButtonQuery::Up:
                    state.up = native;
                    if (native) {
                        state.game_held = false;
                    }
                    break;
                }
                return native;
            };

            if (owned_by_game()) {
                result = passthrough();
            } else {
                self->sample(code, frame, capture);
                if (owned_by_game()) {
                    result = passthrough();
                } else {
                    switch (query) {
                    case ButtonQuery::Held:
                        result = state.held;
                        break;
                    case ButtonQuery::Down:
                        result = state.down;
                        break;
                    case ButtonQuery::Up:
                        result = state.up;
                        break;
                    }
                }
            }
            finished = self->drain(frame);
        }
        if (finished) {
            self->disable();
        }
        return result;
    }

    // Shared body of the pointer position and scroll detours.
    static void vector(float* value, bool is_scroll) {
        Impl* self = current_.load(std::memory_order_acquire);
        bool finished = false;
        {
            std::lock_guard lock(self->mutex_);
            (is_scroll ? self->scroll_original_ : self->position_original_)(value);
            if (!self->enabled()) {
                return;
            }
            const int frame = self->frame_count_();
            const unsigned capture = self->capture_.load(std::memory_order_acquire);
            if (self->pointer_blocked(frame, capture)) {
                // A game-origin press gets one Up, but its raycast is outside the game
                // surface so cancellation cannot become a UI click.
                value[0] = value[1] = is_scroll ? 0.f : kOffscreenPointer;
                if (!is_scroll) {
                    value[2] = 0.f;
                }
            }
            finished = self->drain(frame);
        }
        if (finished) {
            self->disable();
        }
    }

    template <bool IsMouse, ButtonQuery Query>
    static bool button_detour(int argument) {
        return button(IsMouse, argument, Query);
    }

    template <bool IsScroll>
    static void vector_detour(float* value) {
        vector(value, IsScroll);
    }

    // Detours have no user pointer, so they reach the active instance through this.
    inline static std::atomic<Impl*> current_{nullptr};

    const ArkPluginApiV1& host_;
    Il2cppApi api_{};
    void* (*resolve_icall_)(const char*) = nullptr;

    std::atomic<unsigned> capture_{0};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> disabled_{false};

    // Guards all state below. Recursive because disable() is reached both from the
    // detours and from shutdown() while the lock is held.
    std::recursive_mutex mutex_;
    std::condition_variable_any drained_;
    // Set once shutdown starts; detours then drain owned keys and disable the hooks.
    bool draining_ = false;

    std::array<ButtonFn, 3> mouse_originals_{};
    std::array<ButtonFn, 3> key_originals_{};
    VectorFn position_original_ = nullptr;
    VectorFn scroll_original_ = nullptr;
    FrameCountFn frame_count_ = nullptr;
    std::array<uint64_t, kHookCount> hooks_{};

    // Win32 virtual key -> Unity KeyCode, -1 when unmapped.
    std::array<int, 256> virtual_keys_{};
    // Indexed by Unity KeyCode; mouse buttons occupy the tail.
    std::vector<KeyState> states_;
    int mouse_first_ = 0;
    int mouse_count_ = 0;
    int keypad_enter_ = -1;
};

UnityInput::UnityInput(const ArkPluginApiV1& host) : host_(host) {}

UnityInput::~UnityInput() {
    // On failure the hooks may still call into the implementation, so it is leaked.
    if (shutdown()) {
        delete impl_;
    }
}

bool UnityInput::initialize() {
    std::lock_guard operation(operation_);
    std::unique_lock lock(lifecycle_);
    if (impl_ && impl_->enabled()) {
        return true;
    }
    if (!il2cpp::game_assembly()) {
        return false;
    }
    if (impl_ && impl_->disabled()) {
        lock.unlock();
        if (!shutdown()) {
            throw std::runtime_error("Previous Unity input hooks have not drained");
        }
        lock.lock();
        delete impl_;
        impl_ = nullptr;
    }
    if (!impl_) {
        impl_ = new Impl(host_);
    }
    impl_->set_capture(capture_);
    void* domain = impl_->bind_runtime();
    if (!domain) {
        return false;
    }
    try {
        return impl_->start(domain);
    } catch (...) {
        // Keep the implementation alive if native removal cannot drain yet.
        lock.unlock();
        shutdown();
        throw;
    }
}

void UnityInput::set_capture(bool mouse, bool keyboard) {
    std::lock_guard lock(lifecycle_);
    if (impl_ && impl_->stop_requested()) {
        return;
    }
    capture_ = (mouse ? kMouseCapture : 0u) | (keyboard ? kKeyboardCapture : 0u);
    if (impl_) {
        impl_->set_capture(capture_);
    }
}

bool UnityInput::route_message(uint32_t message, uintptr_t wparam, intptr_t lparam) {
    std::lock_guard lock(lifecycle_);
    return impl_ && impl_->route_window_message(message, wparam, lparam);
}

bool UnityInput::protected_input() const {
    std::lock_guard lock(lifecycle_);
    return impl_ && impl_->enabled();
}

bool UnityInput::can_unload() const {
    std::lock_guard lock(lifecycle_);
    return !impl_ || impl_->can_unload();
}

bool UnityInput::shutdown() {
    // Pin the implementation without preventing its release messages from entering
    // route_message while the native neutral-frame rundown is waiting.
    std::lock_guard operation(operation_);
    Impl* retiring = nullptr;
    {
        std::lock_guard lock(lifecycle_);
        if (!impl_) {
            return true;
        }
        capture_ = 0;
        retiring = impl_;
        retiring->request_stop();
    }
    if (!retiring->shutdown()) {
        return false;
    }
    std::lock_guard lock(lifecycle_);
    Impl::clear_current();
    return true;
}

} // namespace ark
