#include "native_capture.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "../common/il2cpp_exports.h"

namespace ark::damage_meter {
namespace {

// Torappu.FP is a signed 32.32 fixed-point number.
constexpr std::int64_t kFixedPointOneRaw = 4294967296LL;
constexpr double kFixedPointOne = 4294967296.0;

// BattleController.m_state values.
constexpr int kBattleStateRunning = 2;
constexpr int kBattleStateFinished = 3;

// Minimum game-time distance between ARK_CLOCK events.
constexpr double kClockInterval = .2;

constexpr int kMaxManagedStringLength = 4096;
constexpr std::uint32_t kMethodAttributeStatic = 0x10;
constexpr std::size_t kHookCount = 7;

using LogModifierFn = void (*)(void* self, void* modifier, void* method);
using InstanceMethodFn = void (*)(void* self, void* method);

// Detours are static, so they reach the capture through this pointer; CallbackScope pins it.
NativeCapture* active_capture = nullptr;

// Gates returned by the host; valid until the corresponding hook has been removed.
LogModifierFn original_log_modifier = nullptr;
LogModifierFn original_log_ep_modifier = nullptr;
InstanceMethodFn original_start_game = nullptr;
InstanceMethodFn original_clear_runtime_data = nullptr;
InstanceMethodFn original_on_destroy = nullptr;
InstanceMethodFn original_update = nullptr;
InstanceMethodFn original_global_update = nullptr;

std::string utf16_to_utf8(const wchar_t* text, int length) {
    if (!text || length <= 0) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
    return out;
}

void copy_id(char* target, std::size_t size, const std::string& value) {
    std::snprintf(target, size, "%s", value.c_str());
}

double from_fixed_point(std::int64_t raw) {
    return static_cast<double>(raw) / kFixedPointOne;
}

} // namespace

// IL2CPP exports and compiled managed getters, resolved once per successful initialize().
struct NativeCapture::Bindings {
    void* (*domain_get)() = nullptr;
    void* (*corlib)() = nullptr;
    void** (*attached_threads)(std::size_t*) = nullptr;
    void* (*thread_current)() = nullptr;
    void* (*thread_attach)(void*) = nullptr;
    void (*thread_detach)(void*) = nullptr;
    void** (*domain_assemblies)(void*, std::size_t*) = nullptr;
    void* (*assembly_image)(void*) = nullptr;
    const char* (*image_name)(void*) = nullptr;
    void* (*class_from_name)(void*, const char*, const char*) = nullptr;
    void* (*class_method)(void*, const char*, int) = nullptr;
    void* (*class_field)(void*, const char*) = nullptr;
    int (*field_offset)(void*) = nullptr;
    void (*field_value)(void*, void*, void*) = nullptr;
    void (*field_static)(void*, void*) = nullptr;
    int (*class_value_size)(void*, std::uint32_t*) = nullptr;
    bool (*class_value_type)(void*) = nullptr;
    void* (*object_class)(void*) = nullptr;
    bool (*assignable)(void*, void*) = nullptr;
    int (*string_length)(void*) = nullptr;
    const wchar_t* (*string_chars)(void*) = nullptr;
    int (*method_param_count)(void*) = nullptr;
    std::uint32_t (*method_flags)(void*, std::uint32_t*) = nullptr;

    // Compiled property getters take (this, MethodInfo*).
    using ReferenceGetter = void* (*)(void*, void*);
    using IntGetter = int (*)(void*, void*);
    using BoolGetter = bool (*)(void*, void*);
    using FixedPointGetter = std::int64_t (*)(void*, void*);
    using ClockGetter = std::int64_t (*)(void*);
    using FrameGetter = std::uint32_t (*)(void*);

    ReferenceGetter source = nullptr;
    ReferenceGetter target = nullptr;
    ReferenceGetter original_source = nullptr;
    ReferenceGetter character_id = nullptr;
    ReferenceGetter entity_id = nullptr;
    ReferenceGetter fetch_host = nullptr;
    ReferenceGetter level_id = nullptr;
    IntGetter damage_type = nullptr;
    BoolGetter is_token = nullptr;
    BoolGetter is_damage = nullptr;
    BoolGetter is_heal = nullptr;
    BoolGetter is_cancelled = nullptr;
    BoolGetter is_element_damage = nullptr;
    FixedPointGetter final_delta = nullptr;
    ClockGetter fixed_play_time = nullptr;
    FrameGetter fixed_frame_count = nullptr;

    void* source_info = nullptr;
    void* target_info = nullptr;
    void* original_source_info = nullptr;
    void* character_id_info = nullptr;
    void* entity_id_info = nullptr;
    void* fetch_host_info = nullptr;
    void* level_id_info = nullptr;
    void* damage_type_info = nullptr;
    void* is_token_info = nullptr;
    void* is_damage_info = nullptr;
    void* is_heal_info = nullptr;
    void* is_cancelled_info = nullptr;
    void* is_element_damage_info = nullptr;
    void* final_delta_info = nullptr;
    void* fixed_play_time_info = nullptr;
    void* fixed_frame_count_info = nullptr;

    void* logger_controller_field = nullptr;
    void* battle_state_field = nullptr;

    void bind_exports() {
        bind(domain_get, "il2cpp_domain_get");
        bind(thread_attach, "il2cpp_thread_attach");
        bind(thread_detach, "il2cpp_thread_detach");
        bind(corlib, "il2cpp_get_corlib");
        bind(attached_threads, "il2cpp_thread_get_all_attached_threads");
        bind(thread_current, "il2cpp_thread_current");
        bind(domain_assemblies, "il2cpp_domain_get_assemblies");
        bind(assembly_image, "il2cpp_assembly_get_image");
        bind(image_name, "il2cpp_image_get_name");
        bind(class_from_name, "il2cpp_class_from_name");
        bind(class_method, "il2cpp_class_get_method_from_name");
        bind(class_field, "il2cpp_class_get_field_from_name");
        bind(field_offset, "il2cpp_field_get_offset");
        bind(field_value, "il2cpp_field_get_value");
        bind(field_static, "il2cpp_field_static_get_value");
        bind(class_value_size, "il2cpp_class_value_size");
        bind(class_value_type, "il2cpp_class_is_valuetype");
        bind(object_class, "il2cpp_object_get_class");
        bind(assignable, "il2cpp_class_is_assignable_from");
        bind(string_length, "il2cpp_string_length");
        bind(string_chars, "il2cpp_string_chars");
        bind(method_param_count, "il2cpp_method_get_param_count");
        bind(method_flags, "il2cpp_method_get_flags");
    }

    void* find_class(void* image, const char* name_space, const char* name) const {
        void* klass = class_from_name(image, name_space, name);
        if (!klass) {
            throw std::runtime_error(std::string("Missing class ") + name_space + "." + name +
                                     " in " + image_name(image));
        }
        return klass;
    }

    // Returns a MethodInfo whose compiled entry point (its first word) is executable.
    void* find_method(void* klass, const char* name, int param_count) const {
        void* method = class_method(klass, name, param_count);
        if (!method || method_param_count(method) != param_count) {
            throw std::runtime_error(std::string("Missing or incompatible method ") + name);
        }
        if (!il2cpp::is_executable(entry_point(method))) {
            throw std::runtime_error(std::string("Method is not executable: ") + name);
        }
        return method;
    }

    void* find_field(void* klass, const char* name) const {
        void* field = class_field(klass, name);
        if (!field) {
            throw std::runtime_error(std::string("Missing field ") + name);
        }
        return field;
    }

    template <class Getter>
    void bind_getter(Getter& getter, void*& info, void* klass, const char* name) {
        info = find_method(klass, name, 0);
        getter = reinterpret_cast<Getter>(entry_point(info));
    }

    std::string string(void* value) const {
        if (!value) {
            return {};
        }
        const int length = string_length(value);
        if (length < 0 || length > kMaxManagedStringLength) {
            return {};
        }
        return utf16_to_utf8(string_chars(value), length);
    }

    static void* entry_point(void* method) { return *static_cast<void**>(method); }

private:
    template <class Function>
    static void bind(Function& function, const char* symbol) {
        function = il2cpp::resolve_export<Function>(symbol);
    }
};

// Counts a detour as in flight while callbacks are enabled, so shutdown() can wait for it.
class NativeCapture::CallbackScope {
public:
    explicit CallbackScope(NativeCapture* candidate) {
        if (!candidate) {
            return;
        }
        std::lock_guard lock(candidate->callback_mutex_);
        if (candidate->callbacks_enabled_) {
            capture_ = candidate;
            ++capture_->callbacks_in_flight_;
        }
    }

    ~CallbackScope() {
        if (!capture_) {
            return;
        }
        std::lock_guard lock(capture_->callback_mutex_);
        if (--capture_->callbacks_in_flight_ == 0) {
            capture_->callbacks_drained_.notify_all();
        }
    }

    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

    NativeCapture* capture() const { return capture_; }

    // The capture is entered and the plugin has not started shutting down.
    bool recording() const { return capture_ && capture_->host_active_.load(); }

private:
    NativeCapture* capture_ = nullptr;
};

bool NativeCapture::initialize() {
    std::lock_guard lifecycle(lifecycle_mutex_);
    if (ready_) {
        return true;
    }
    if (!il2cpp::game_assembly()) {
        return false;
    }
    void* attached_thread = nullptr;
    try {
        if (!bindings_) {
            bindings_ = std::make_unique<Bindings>();
        }
        Bindings& il2cpp = *bindings_;
        il2cpp.bind_exports();

        // domain_get allocates lazily. Wait for GC/thread startup and an engine-owned managed
        // thread before calling it from this thread.
        std::size_t registered_threads = 0;
        if (!il2cpp.corlib() || !il2cpp.attached_threads(&registered_threads) ||
            !registered_threads) {
            return false;
        }
        void* domain = il2cpp.domain_get();
        if (!domain) {
            return false;
        }
        if (!il2cpp.thread_current()) {
            attached_thread = il2cpp.thread_attach(domain);
            if (!attached_thread) {
                throw std::runtime_error("IL2CPP thread attachment failed");
            }
        }

        std::size_t assembly_count = 0;
        void** assemblies = il2cpp.domain_assemblies(domain, &assembly_count);
        void* game_image = nullptr;
        void* common_image = nullptr;
        for (std::size_t i = 0; i < assembly_count; ++i) {
            void* image = il2cpp.assembly_image(assemblies[i]);
            const char* name = il2cpp.image_name(image);
            if (std::strcmp(name, "Assembly-CSharp.dll") == 0) {
                game_image = image;
            } else if (std::strcmp(name, "Torappu.Common.dll") == 0) {
                common_image = image;
            }
        }
        if (!game_image || !common_image) {
            if (attached_thread) {
                il2cpp.thread_detach(attached_thread);
            }
            return false;
        }

        character_class_ = il2cpp.find_class(game_image, "Torappu.Battle", "Character");
        void* entity_class = il2cpp.find_class(game_image, "Torappu.Battle", "Entity");
        battle_controller_class_ =
            il2cpp.find_class(game_image, "Torappu.Battle", "BattleController");
        void* logger_class = il2cpp.find_class(game_image, "Torappu.Battle", "BattleLogger");
        void* modifier_class = il2cpp.find_class(game_image, "Torappu.Battle", "Modifier");
        void* fixed_point_class = il2cpp.find_class(common_image, "Torappu", "FP");
        void* global_updater_class =
            il2cpp.find_class(game_image, "Torappu", "GlobalInitializerAndUpdater");

        void* global_update_method = il2cpp.find_method(global_updater_class, "Update", 0);
        std::uint32_t implementation_flags = 0;
        if (il2cpp.method_flags(global_update_method, &implementation_flags) &
            kMethodAttributeStatic) {
            throw std::runtime_error(
                "GlobalInitializerAndUpdater.Update must be an instance method");
        }

        std::int64_t fixed_point_one = 0;
        il2cpp.field_static(il2cpp.find_field(fixed_point_class, "ONE"), &fixed_point_one);
        if (fixed_point_one != kFixedPointOneRaw) {
            throw std::runtime_error("Unsupported fixed point scale");
        }

        // Modifier is passed unboxed, so field offsets (which include the object header) must
        // be rebased onto the raw value.
        if (!il2cpp.class_value_type(modifier_class)) {
            throw std::runtime_error("Modifier is not a value type");
        }
        const int real_delta_offset =
            il2cpp.field_offset(il2cpp.find_field(modifier_class, "realDelta")) -
            static_cast<int>(2 * sizeof(void*));
        std::uint32_t alignment = 0;
        const int modifier_size = il2cpp.class_value_size(modifier_class, &alignment);
        if (real_delta_offset < 0 ||
            real_delta_offset + static_cast<int>(sizeof(std::int64_t)) > modifier_size) {
            throw std::runtime_error("Unsupported Modifier field layout");
        }
        real_delta_offset_ = static_cast<std::size_t>(real_delta_offset);

        il2cpp.bind_getter(il2cpp.source, il2cpp.source_info, modifier_class, "get_source");
        il2cpp.bind_getter(il2cpp.target, il2cpp.target_info, modifier_class, "get_target");
        il2cpp.bind_getter(il2cpp.original_source, il2cpp.original_source_info,
                           modifier_class, "get_originalSource");
        il2cpp.bind_getter(il2cpp.final_delta, il2cpp.final_delta_info, modifier_class,
                           "get_finalDelta");
        il2cpp.bind_getter(il2cpp.is_damage, il2cpp.is_damage_info, modifier_class, "get_isDamage");
        try {
            il2cpp.bind_getter(il2cpp.is_heal, il2cpp.is_heal_info, modifier_class,
                               "get_isHeal");
        } catch (...) {
            il2cpp.is_heal = nullptr;
            il2cpp.is_heal_info = nullptr;
        }
        il2cpp.bind_getter(il2cpp.is_cancelled, il2cpp.is_cancelled_info, modifier_class,
                           "get_isCancelled");
        il2cpp.bind_getter(il2cpp.is_element_damage, il2cpp.is_element_damage_info, modifier_class,
                           "get_isElementDamage");
        il2cpp.bind_getter(il2cpp.damage_type, il2cpp.damage_type_info, modifier_class,
                           "get_damageType");
        il2cpp.bind_getter(il2cpp.character_id, il2cpp.character_id_info, character_class_,
                           "get_characterId");
        il2cpp.bind_getter(il2cpp.entity_id, il2cpp.entity_id_info, entity_class, "get_id");
        try {
            il2cpp.bind_getter(il2cpp.fetch_host, il2cpp.fetch_host_info, character_class_,
                               "FetchHost");
            il2cpp.bind_getter(il2cpp.is_token, il2cpp.is_token_info, character_class_,
                               "get_isToken");
        } catch (...) {
            il2cpp.fetch_host = nullptr;
            il2cpp.fetch_host_info = nullptr;
            il2cpp.is_token = nullptr;
            il2cpp.is_token_info = nullptr;
        }
        il2cpp.bind_getter(il2cpp.level_id, il2cpp.level_id_info, battle_controller_class_,
                           "get_levelId");
        il2cpp.bind_getter(il2cpp.fixed_play_time, il2cpp.fixed_play_time_info,
                           battle_controller_class_, "get_fixedPlayTime");
        il2cpp.bind_getter(il2cpp.fixed_frame_count, il2cpp.fixed_frame_count_info,
                           battle_controller_class_, "get_fixedFrameCnt");

        il2cpp.logger_controller_field = il2cpp.find_field(logger_class, "m_controller");
        il2cpp.battle_state_field = il2cpp.find_field(battle_controller_class_, "m_state");

        hooks_.reserve(kHookCount);
        const auto hook = [&](void* klass, const char* name, int param_count, auto detour,
                              auto& original) {
            void* method = il2cpp.find_method(klass, name, param_count);
            const std::uint64_t id = host_.create_native_hook(
                host_.owner, Bindings::entry_point(method), reinterpret_cast<void*>(detour),
                reinterpret_cast<void**>(&original));
            if (!id) {
                throw std::runtime_error(std::string("Hook ") + name + " could not be created");
            }
            hooks_.push_back(id);
        };
        active_capture = this;
        hook(logger_class, "LogModifier", 1, on_log_modifier, original_log_modifier);
        hook(logger_class, "LogEpModifier", 1, on_log_ep_modifier, original_log_ep_modifier);
        hook(battle_controller_class_, "StartGame", 0, on_start_game, original_start_game);
        hook(battle_controller_class_, "ClearRunTimeData", 0, on_clear_runtime_data,
             original_clear_runtime_data);
        hook(battle_controller_class_, "OnDestroy", 0, on_destroy, original_on_destroy);
        hook(battle_controller_class_, "Update", 0, on_update, original_update);
        hook(global_updater_class, "Update", 0, on_global_update, original_global_update);

        host_active_ = true;
        {
            std::lock_guard lock(callback_mutex_);
            callbacks_enabled_ = true;
        }
        for (const std::uint64_t id : hooks_) {
            if (!host_.enable_native_hook(host_.owner, id, true)) {
                throw std::runtime_error("Could not enable IL2CPP hook");
            }
        }
        if (attached_thread) {
            il2cpp.thread_detach(attached_thread);
        }
        attached_thread = nullptr;
        ready_ = true;
        return true;
    } catch (...) {
        if (attached_thread) {
            bindings_->thread_detach(attached_thread);
            attached_thread = nullptr;
        }
        shutdown();
        throw;
    }
}

bool NativeCapture::shutdown() {
    std::lock_guard lifecycle(lifecycle_mutex_);
    {
        std::lock_guard lock(host_dispatch_mutex_);
        host_active_ = false;
    }
    {
        std::lock_guard lock(callback_mutex_);
        callbacks_enabled_ = false;
        ready_ = false;
    }
    // Hooks that fail to drain keep their IDs so the next attempt retries only those.
    bool removed_all = true;
    for (std::uint64_t& id : hooks_) {
        if (!id) {
            continue;
        }
        if (host_.remove_native_hook(host_.owner, id)) {
            id = 0;
        } else {
            removed_all = false;
        }
    }
    if (!removed_all) {
        return false;
    }
    {
        std::unique_lock lock(callback_mutex_);
        callbacks_drained_.wait(lock, [&] { return callbacks_in_flight_ == 0; });
    }
    hooks_.clear();
    active_capture = nullptr;
    original_log_modifier = nullptr;
    original_log_ep_modifier = nullptr;
    original_start_game = nullptr;
    original_clear_runtime_data = nullptr;
    original_on_destroy = nullptr;
    original_update = nullptr;
    original_global_update = nullptr;
    controller_ = nullptr;
    battle_active_ = false;
    battle_ended_ = false;
    last_clock_ = -1;
    return true;
}

NativeCapture::NativeCapture(EventHistory& history, const ArkPluginApiV1& host)
    : history_(history),
      host_(host) {}

NativeCapture::~NativeCapture() = default;

double NativeCapture::game_clock() {
    return from_fixed_point(bindings_->fixed_play_time(bindings_->fixed_play_time_info));
}

std::uint32_t NativeCapture::current_frame() {
    return bindings_->fixed_frame_count(bindings_->fixed_frame_count_info);
}

void NativeCapture::publish_frame() {
    history_.set_frame(current_frame());
}

void NativeCapture::begin_battle(void* controller, bool partial) {
    controller_ = controller;
    battle_active_ = true;
    battle_ended_ = false;
    last_clock_ = -1;

    ArkEventV2 event;
    event.kind = ARK_BATTLE_START;
    event.partial = partial;
    event.frame = current_frame();
    event.game_time = game_clock();
    const Bindings& il2cpp = *bindings_;
    copy_id(event.stage_id, sizeof(event.stage_id),
            il2cpp.string(il2cpp.level_id(controller, il2cpp.level_id_info)));
    history_.publish(event);
    // A battle start invalidates the cached frame, so publish it afterwards.
    publish_frame();
}

void NativeCapture::end_battle() {
    if (!battle_active_ || battle_ended_) {
        return;
    }
    publish_frame();
    ArkEventV2 event;
    event.kind = ARK_BATTLE_END;
    event.frame = current_frame();
    event.game_time = game_clock();
    history_.publish(event);
    battle_ended_ = true;
    battle_active_ = false;
}

void NativeCapture::update_battle(void* controller) {
    int state = 0;
    bindings_->field_value(controller, bindings_->battle_state_field, &state);
    // Joining a running battle (e.g. after a plugin reload) records it as partial.
    if (state == kBattleStateRunning && !battle_active_ && !battle_ended_) {
        begin_battle(controller, true);
    }
    if (state == kBattleStateFinished) {
        end_battle();
        return;
    }
    if (!battle_active_) {
        return;
    }
    publish_frame();
    const double now = game_clock();
    if (now - last_clock_ < kClockInterval && last_clock_ >= 0) {
        return;
    }
    last_clock_ = now;
    ArkEventV2 event;
    event.kind = ARK_CLOCK;
    event.frame = current_frame();
    event.game_time = now;
    history_.publish(event);
}

void NativeCapture::record_damage(void* logger, void* modifier, bool elemental) {
    const Bindings& il2cpp = *bindings_;
    if (il2cpp.is_cancelled(modifier, il2cpp.is_cancelled_info)) {
        return;
    }
    // Both logger entry points receive modifiers they may ignore internally. Classify the
    // modifier itself; reaching LogEpModifier does not imply elemental damage.
    const bool healing =
        il2cpp.is_heal &&
        il2cpp.is_heal(modifier, il2cpp.is_heal_info);
    const bool counted =
        elemental
            ? il2cpp.is_element_damage(modifier, il2cpp.is_element_damage_info)
            : (il2cpp.is_damage(modifier, il2cpp.is_damage_info) || healing);
    if (!counted) {
        return;
    }
    void* source = il2cpp.source(modifier, il2cpp.source_info);
    void* original_source =
        il2cpp.original_source(modifier, il2cpp.original_source_info);
    if (!source || !il2cpp.assignable(character_class_, il2cpp.object_class(source))) {
        if (original_source &&
            il2cpp.assignable(character_class_,
                              il2cpp.object_class(original_source))) {
            source = original_source;
        } else {
            source = nullptr;
        }
    }
    void* controller = nullptr;
    il2cpp.field_value(logger, il2cpp.logger_controller_field, &controller);
    if (!controller) {
        return;
    }
    if (!battle_active_) {
        if (battle_ended_) {
            return;
        }
        begin_battle(controller, true);
    }

    ArkEventV2 event;
    event.kind = elemental && !healing ? ARK_ELEMENTAL : ARK_DAMAGE;
    event.frame = current_frame();
    event.game_time = game_clock();
    event.calculated_damage =
        std::abs(from_fixed_point(il2cpp.final_delta(modifier, il2cpp.final_delta_info)));
    std::int64_t real_delta = 0;
    std::memcpy(&real_delta, static_cast<char*>(modifier) + real_delta_offset_, sizeof(real_delta));
    event.actual_damage = std::abs(from_fixed_point(real_delta));
    event.damage_type = healing
                            ? ARK_DAMAGE_HEAL
                            : il2cpp.damage_type(modifier, il2cpp.damage_type_info);
    if (source) {
        copy_id(event.actor_id, sizeof(event.actor_id),
                il2cpp.string(il2cpp.character_id(source, il2cpp.character_id_info)));
    }
    void* target = il2cpp.target(modifier, il2cpp.target_info);
    if (target) {
        copy_id(event.target_id, sizeof(event.target_id),
                il2cpp.string(il2cpp.entity_id(target, il2cpp.entity_id_info)));
    }
    if (original_source) {
        copy_id(event.original_source_id, sizeof(event.original_source_id),
                il2cpp.string(il2cpp.entity_id(original_source, il2cpp.entity_id_info)));
    }
    if (event.actor_id[0] && il2cpp.is_token && il2cpp.is_token_info &&
        il2cpp.fetch_host && il2cpp.fetch_host_info &&
        il2cpp.is_token(source, il2cpp.is_token_info)) {
        try {
            void* host = source;
            for (int depth = 0; depth < 4; ++depth) {
                void* next = il2cpp.fetch_host(host, il2cpp.fetch_host_info);
                if (!next || next == host) {
                    break;
                }
                host = next;
            }
            if (host && host != source &&
                il2cpp.assignable(character_class_, il2cpp.object_class(host))) {
                const std::string owner_id =
                    il2cpp.string(il2cpp.character_id(host, il2cpp.character_id_info));
                if (!owner_id.empty() && owner_id != event.actor_id) {
                    copy_id(event.owner_id, sizeof(event.owner_id), owner_id);
                }
            }
        } catch (...) {
        }
    }
    if ((event.actor_id[0] || event.original_source_id[0] ||
         event.target_id[0]) &&
        std::isfinite(event.calculated_damage) &&
        std::isfinite(event.actual_damage)) {
        history_.publish(event);
    }
}

void NativeCapture::on_log_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(active_capture);
    original_log_modifier(self, modifier, method);
    if (scope.recording()) {
        scope.capture()->record_damage(self, modifier, false);
    }
}

void NativeCapture::on_log_ep_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(active_capture);
    original_log_ep_modifier(self, modifier, method);
    if (scope.recording()) {
        scope.capture()->record_damage(self, modifier, true);
    }
}

void NativeCapture::on_start_game(void* self, void* method) {
    CallbackScope scope(active_capture);
    original_start_game(self, method);
    if (scope.recording()) {
        scope.capture()->begin_battle(self, false);
    }
}

void NativeCapture::on_clear_runtime_data(void* self, void* method) {
    CallbackScope scope(active_capture);
    // Read the final frame and clock before the controller clears them.
    if (scope.recording()) {
        scope.capture()->end_battle();
    }
    original_clear_runtime_data(self, method);
}

void NativeCapture::on_destroy(void* self, void* method) {
    CallbackScope scope(active_capture);
    if (scope.recording() && scope.capture()->controller_ == self) {
        // Leaving a battle directly destroys the scene without ClearRunTimeData.
        scope.capture()->end_battle();
        scope.capture()->controller_ = nullptr;
    }
    original_on_destroy(self, method);
}

void NativeCapture::on_update(void* self, void* method) {
    CallbackScope scope(active_capture);
    original_update(self, method);
    if (scope.recording()) {
        scope.capture()->update_battle(self);
    }
}

void NativeCapture::on_global_update(void* self, void* method) {
    CallbackScope scope(active_capture);
    original_global_update(self, method);
    if (NativeCapture* capture = scope.capture()) {
        std::lock_guard lock(capture->host_dispatch_mutex_);
        if (capture->host_active_.load()) {
            capture->host_.pump_game_tasks(capture->host_.owner);
        }
    }
}

} // namespace ark::damage_meter
