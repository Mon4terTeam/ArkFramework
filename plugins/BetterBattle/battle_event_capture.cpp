#include "battle_event_capture.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "../common/il2cpp_exports.h"
#include "../common/il2cpp_runtime.h"

namespace ark::better_battle {
namespace {

constexpr std::int64_t kFixedPointOneRaw = 4294967296LL;
constexpr double kFixedPointOne = 4294967296.0;
constexpr double kClockInterval = 0.2;
constexpr std::size_t kEventCapacity = 65536;
constexpr int kBattleStateRunning = 2;
constexpr int kBattleStateFinished = 3;

using LogModifierFn = void (*)(void* self, void* modifier, void* method);
using InstanceMethodFn = void (*)(void* self, void* method);

BattleEventCapture* active_capture = nullptr;

LogModifierFn original_log_modifier = nullptr;
LogModifierFn original_log_ep_modifier = nullptr;
void (*original_update_hp_recovery)(void*, std::int64_t, void*) = nullptr;
InstanceMethodFn original_start_game = nullptr;
InstanceMethodFn original_clear_runtime_data = nullptr;
InstanceMethodFn original_on_destroy = nullptr;
InstanceMethodFn original_update = nullptr;

double from_fixed_point(std::int64_t raw) {
    return static_cast<double>(raw) / kFixedPointOne;
}

} // namespace

struct BattleEventCapture::Bindings {
    explicit Bindings(il2cpp::Runtime& value) : runtime(value) {}

    il2cpp::Runtime& runtime;
    void* character_class = nullptr;
    void* entity_class = nullptr;
    void* logger_class = nullptr;
    void* modifier_class = nullptr;
    void* battle_controller_class = nullptr;

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
    BoolGetter is_mine = nullptr;
    FixedPointGetter final_delta = nullptr;
    FixedPointGetter max_minus_hp = nullptr;
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
    void* is_mine_info = nullptr;
    void* final_delta_info = nullptr;
    void* max_minus_hp_info = nullptr;
    void* fixed_play_time_info = nullptr;
    void* fixed_frame_count_info = nullptr;

    void* logger_controller_field = nullptr;
    void* battle_state_field = nullptr;
    std::size_t target_hp_offset = 0;
    std::size_t minus_hp_offset = 0;
    std::size_t real_delta_offset = 0;

    static void* entry_point(void* method) {
        return *static_cast<void**>(method);
    }

    void* find_method(void* klass, const char* name, int count) const {
        void* method = runtime.method(klass, name, count);
        if (!method || !il2cpp::is_executable(entry_point(method))) {
            throw std::runtime_error(std::string("Missing executable method ") + name);
        }
        return method;
    }

    template <class Getter>
    void bind_getter(Getter& getter, void*& info, void* klass, const char* name) {
        info = find_method(klass, name, 0);
        getter = reinterpret_cast<Getter>(entry_point(info));
    }

    void bind() {
        character_class = runtime.find("Torappu.Battle", "Character");
        entity_class = runtime.find("Torappu.Battle", "Entity");
        logger_class = runtime.find("Torappu.Battle", "BattleLogger");
        modifier_class = runtime.find("Torappu.Battle", "Modifier");
        battle_controller_class = runtime.find("Torappu.Battle", "BattleController");

        if (!runtime.class_is_value(modifier_class)) {
            throw std::runtime_error("Modifier is not a value type");
        }
        const int field_offset =
            static_cast<int>(runtime.field_offset(runtime.field(modifier_class, "realDelta")));
        const int rebased = field_offset - static_cast<int>(2 * sizeof(void*));
        std::uint32_t alignment = 0;
        const int modifier_size = runtime.class_value_size(modifier_class, &alignment);
        if (rebased < 0 ||
            rebased + static_cast<int>(sizeof(std::int64_t)) > modifier_size) {
            throw std::runtime_error("Unsupported Modifier.realDelta layout");
        }
        real_delta_offset = static_cast<std::size_t>(rebased);

        bind_getter(source, source_info, modifier_class, "get_source");
        bind_getter(target, target_info, modifier_class, "get_target");
        bind_getter(original_source, original_source_info, modifier_class,
                    "get_originalSource");
        bind_getter(final_delta, final_delta_info, modifier_class, "get_finalDelta");
        bind_getter(is_damage, is_damage_info, modifier_class, "get_isDamage");
        try {
            bind_getter(is_heal, is_heal_info, modifier_class, "get_isHeal");
        } catch (...) {
            is_heal = nullptr;
            is_heal_info = nullptr;
        }
        bind_getter(is_cancelled, is_cancelled_info, modifier_class, "get_isCancelled");
        bind_getter(is_element_damage, is_element_damage_info, modifier_class,
                    "get_isElementDamage");
        bind_getter(is_mine, is_mine_info, entity_class, "get_isMine");
        bind_getter(damage_type, damage_type_info, modifier_class, "get_damageType");
        bind_getter(character_id, character_id_info, character_class, "get_characterId");
        bind_getter(entity_id, entity_id_info, entity_class, "get_id");
        const int hp_offset =
            static_cast<int>(runtime.field_offset(runtime.field(entity_class, "m_hp")));
        if (hp_offset < 0) {
            throw std::runtime_error("Unsupported Entity.m_hp layout");
        }
        target_hp_offset = static_cast<std::size_t>(hp_offset);
        const int minus_offset = static_cast<int>(
            runtime.field_offset(runtime.field(entity_class, "<minusHp>k__BackingField")));
        if (minus_offset < 0) {
            throw std::runtime_error("Unsupported Entity.minusHp layout");
        }
        minus_hp_offset = static_cast<std::size_t>(minus_offset);
        bind_getter(max_minus_hp, max_minus_hp_info, entity_class,
                    "get_maxMinusHp");
        try {
            bind_getter(fetch_host, fetch_host_info, character_class, "FetchHost");
            bind_getter(is_token, is_token_info, character_class, "get_isToken");
        } catch (...) {
            fetch_host = nullptr;
            fetch_host_info = nullptr;
            is_token = nullptr;
            is_token_info = nullptr;
        }
        bind_getter(level_id, level_id_info, battle_controller_class, "get_levelId");
        fixed_play_time_info =
            find_method(battle_controller_class, "get_fixedPlayTime", 0);
        fixed_play_time = reinterpret_cast<ClockGetter>(entry_point(fixed_play_time_info));
        fixed_frame_count_info = find_method(battle_controller_class, "get_fixedFrameCnt",
                                             0);
        fixed_frame_count =
            reinterpret_cast<FrameGetter>(entry_point(fixed_frame_count_info));

        logger_controller_field = runtime.field(logger_class, "m_controller");
        battle_state_field = runtime.field(battle_controller_class, "m_state");

        void* fixed_point_class = runtime.find("Torappu", "FP");
        const std::int64_t one =
            runtime.static_value<std::int64_t>(fixed_point_class, "ONE");
        if (one != kFixedPointOneRaw) {
            throw std::runtime_error("Unsupported fixed point scale");
        }
    }
};

class BattleEventCapture::CallbackScope {
public:
    explicit CallbackScope(BattleEventCapture* candidate) {
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

    BattleEventCapture* capture() const { return capture_; }
    bool recording() const {
        return capture_ && capture_->host_active_.load(std::memory_order_acquire);
    }

private:
    BattleEventCapture* capture_ = nullptr;
};

BattleEventCapture::BattleEventCapture(il2cpp::Runtime& runtime, const ArkPluginApiV1& host)
    : runtime_(runtime), host_(host) {}

BattleEventCapture::~BattleEventCapture() = default;

bool BattleEventCapture::initialize() {
    std::lock_guard lifecycle(lifecycle_mutex_);
    if (ready_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!runtime_.domain_get) {
        return false;
    }
    try {
        bindings_ = std::make_unique<Bindings>(runtime_);
        bindings_->bind();

        const auto hook = [&](void* klass, const char* name, int count, auto detour,
                              auto& original) {
            void* method = bindings_->find_method(klass, name, count);
            const std::uint64_t id = host_.create_native_hook(
                host_.owner, Bindings::entry_point(method), reinterpret_cast<void*>(detour),
                reinterpret_cast<void**>(&original));
            if (!id) {
                throw std::runtime_error(std::string("Hook ") + name + " could not be created");
            }
            hooks_.push_back(id);
        };

        active_capture = this;
        hook(bindings_->logger_class, "LogModifier", 1, on_log_modifier,
             original_log_modifier);
        hook(bindings_->logger_class, "LogEpModifier", 1, on_log_ep_modifier,
             original_log_ep_modifier);
        hook(bindings_->entity_class, "_UpdateHpRecovery", 1,
             on_update_hp_recovery, original_update_hp_recovery);
        hook(bindings_->battle_controller_class, "StartGame", 0, on_start_game,
             original_start_game);
        hook(bindings_->battle_controller_class, "ClearRunTimeData",
             0, on_clear_runtime_data, original_clear_runtime_data);
        hook(bindings_->battle_controller_class, "OnDestroy", 0, on_destroy,
             original_on_destroy);
        hook(bindings_->battle_controller_class, "Update", 0, on_update,
             original_update);

        host_active_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(callback_mutex_);
            callbacks_enabled_ = true;
        }
        for (const std::uint64_t id : hooks_) {
            if (!host_.enable_native_hook(host_.owner, id, true)) {
                throw std::runtime_error("Could not enable a battle event hook");
            }
        }
        ready_.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        shutdown();
        throw;
    }
}

bool BattleEventCapture::shutdown() {
    std::lock_guard lifecycle(lifecycle_mutex_);
    host_active_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(callback_mutex_);
        callbacks_enabled_ = false;
        ready_.store(false, std::memory_order_release);
    }

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
    original_update_hp_recovery = nullptr;
    original_start_game = nullptr;
    original_clear_runtime_data = nullptr;
    original_on_destroy = nullptr;
    original_update = nullptr;

    controller_ = nullptr;
    battle_active_ = false;
    battle_ended_ = false;
    last_clock_ = -1;
    last_hp_by_id_.clear();
    last_effective_hp_by_id_.clear();
    last_minus_hp_by_id_.clear();
    {
        std::lock_guard lock(regen_mutex_);
        regen_pending_.clear();
    }
    last_effective_hp_by_id_.clear();
    last_minus_hp_by_id_.clear();
    {
        std::lock_guard lock(regen_mutex_);
        regen_pending_.clear();
    }

    std::lock_guard lock(events_mutex_);
    events_.clear();
    partial_ = false;
    ended_ = false;
    frame_valid_ = false;
    return true;
}

bool BattleEventCapture::available() const {
    return ready_.load(std::memory_order_acquire);
}

void BattleEventCapture::seed_unit_health(const std::string& id, double hp,
                                          double effective_hp,
                                          double minus_hp) {
    if (id.empty() || !std::isfinite(hp)) {
        return;
    }
    last_hp_by_id_.try_emplace(id, hp);
    if (std::isfinite(effective_hp)) {
        last_effective_hp_by_id_.try_emplace(id, effective_hp);
    }
    if (std::isfinite(minus_hp)) {
        last_minus_hp_by_id_.try_emplace(id, minus_hp);
    }
}

std::size_t BattleEventCapture::read(std::uint64_t after, ArkBattleEvent* out,
                                     std::size_t capacity,
                                     ArkBattleEventReadStatus* status) {
    std::lock_guard lock(events_mutex_);
    if (status) {
        *status = {};
        status->partial = partial_;
        status->ended = ended_;
        if (!events_.empty()) {
            status->oldest_sequence = events_.front().sequence;
            status->newest_sequence = events_.back().sequence;
            status->overrun = after && after + 1 < status->oldest_sequence &&
                              events_.front().kind != ArkBattleEventKind::BattleStart;
        }
    }

    std::size_t written = 0;
    if (!out || !capacity) {
        return written;
    }
    for (const ArkBattleEvent& event : events_) {
        if (event.sequence > after && written < capacity) {
            out[written++] = event;
        }
    }
    return written;
}

double BattleEventCapture::game_clock() {
    return from_fixed_point(
        bindings_->fixed_play_time(bindings_->fixed_play_time_info));
}

std::uint32_t BattleEventCapture::current_frame() {
    return bindings_->fixed_frame_count(bindings_->fixed_frame_count_info);
}

void BattleEventCapture::publish_frame() {
    std::lock_guard lock(events_mutex_);
    frame_ = current_frame();
    frame_valid_ = true;
}

void BattleEventCapture::publish(ArkBattleEvent event) {
    std::lock_guard lock(events_mutex_);
    if (event.kind == ArkBattleEventKind::BattleStart) {
        events_.clear();
        ++battle_;
        partial_ = event.partial;
        ended_ = false;
        frame_event_source_ = event.frame;
        frame_event_index_ = 0;
    }
    if (event.kind == ArkBattleEventKind::BattleEnd) {
        ended_ = true;
    }

    event.sequence = ++sequence_;
    event.battle = battle_;
    if (event.frame != frame_event_source_) {
        frame_event_source_ = event.frame;
        frame_event_index_ = 0;
    }
    event.frame_event_index = frame_event_index_++;
    events_.push_back(std::move(event));
    while (events_.size() > kEventCapacity) {
        events_.pop_front();
        partial_ = true;
    }
}

void BattleEventCapture::begin_battle(void* controller, bool partial) {
    controller_ = controller;
    battle_active_ = true;
    battle_ended_ = false;
    last_clock_ = -1;
    last_hp_by_id_.clear();
    last_minus_hp_by_id_.clear();

    ArkBattleEvent event;
    event.kind = ArkBattleEventKind::BattleStart;
    event.partial = partial;
    event.frame = current_frame();
    event.game_time = game_clock();
    if (controller) {
        event.stage_id = runtime_.string(
            bindings_->level_id(controller, bindings_->level_id_info));
    }
    publish(std::move(event));
    publish_frame();
}

void BattleEventCapture::end_battle() {
    if (!battle_active_ || battle_ended_) {
        return;
    }
    flush_regen();
    publish_frame();
    ArkBattleEvent event;
    event.kind = ArkBattleEventKind::BattleEnd;
    event.frame = current_frame();
    event.game_time = game_clock();
    publish(std::move(event));
    battle_active_ = false;
    battle_ended_ = true;
}

void BattleEventCapture::update_battle(void* controller) {
    int state = 0;
    runtime_.field_get(controller, bindings_->battle_state_field, &state);
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
    if (last_clock_ >= 0 && now - last_clock_ < kClockInterval) {
        return;
    }
    last_clock_ = now;
    flush_regen();
    ArkBattleEvent event;
    event.kind = ArkBattleEventKind::Clock;
    event.frame = current_frame();
    event.game_time = now;
    publish(std::move(event));
}

void BattleEventCapture::record_regen(void* entity, double amount) {
    if (!entity || amount <= 0.0001 || !battle_active_ || battle_ended_) {
        return;
    }
    const std::string id = runtime_.string(
        bindings_->entity_id(entity, bindings_->entity_id_info));
    if (id.empty()) {
        return;
    }
    std::lock_guard lock(regen_mutex_);
    regen_pending_[id] += amount;
}

void BattleEventCapture::flush_regen() {
    std::unordered_map<std::string, double> pending;
    {
        std::lock_guard lock(regen_mutex_);
        pending.swap(regen_pending_);
    }
    if (pending.empty()) {
        return;
    }
    const std::uint32_t frame = current_frame();
    const double now = game_clock();
    for (const auto& [id, amount] : pending) {
        if (amount <= 0.0001) {
            continue;
        }
        ArkBattleEvent event;
        event.kind = ArkBattleEventKind::Regen;
        event.frame = frame;
        event.game_time = now;
        event.calculated_delta = amount;
        event.applied_delta = amount;
        event.source_id = id;
        event.target_id = id;
        event.hp_after = 0;
        event.hp_delta = 0;
        event.hp_effective_after = 0;
        event.hp_effective_delta = 0;
        publish(std::move(event));
    }
}

void BattleEventCapture::record_modifier(void* logger, void* modifier, bool elemental) {
    if (!modifier) {
        return;
    }
    Bindings& il2cpp = *bindings_;
    if (il2cpp.is_cancelled(modifier, il2cpp.is_cancelled_info)) {
        return;
    }

    const bool healing =
        il2cpp.is_heal && il2cpp.is_heal(modifier, il2cpp.is_heal_info);
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
    if (!source || !runtime_.class_assignable(il2cpp.character_class,
                                              runtime_.object_class(source))) {
        if (original_source &&
            runtime_.class_assignable(il2cpp.character_class,
                                      runtime_.object_class(original_source))) {
            source = original_source;
        } else {
            source = nullptr;
        }
    }

    void* controller = nullptr;
    runtime_.field_get(logger, il2cpp.logger_controller_field, &controller);
    if (!controller) {
        return;
    }
    if (!battle_active_) {
        if (battle_ended_) {
            return;
        }
        begin_battle(controller, true);
    }

    ArkBattleEvent event;
    event.kind = healing
                     ? ArkBattleEventKind::Healing
                     : (elemental ? ArkBattleEventKind::ElementalBuildup
                                  : ArkBattleEventKind::Damage);
    event.frame = current_frame();
    event.game_time = game_clock();
    event.calculated_delta =
        std::abs(from_fixed_point(il2cpp.final_delta(modifier, il2cpp.final_delta_info)));
    std::int64_t real_delta = 0;
    std::memcpy(&real_delta,
                static_cast<char*>(modifier) + il2cpp.real_delta_offset,
                sizeof(real_delta));
    event.applied_delta = std::abs(from_fixed_point(real_delta));
    event.damage_type =
        healing ? static_cast<std::int32_t>(ArkDamageType::Healing)
                : il2cpp.damage_type(modifier, il2cpp.damage_type_info);

    if (source) {
        event.source_id = runtime_.string(
            il2cpp.character_id(source, il2cpp.character_id_info));
    }
    void* target = il2cpp.target(modifier, il2cpp.target_info);
    if (target) {
        event.target_id = runtime_.string(
            il2cpp.entity_id(target, il2cpp.entity_id_info));
        {
            std::int64_t hp_raw = 0;
            std::memcpy(&hp_raw,
                        static_cast<const char*>(target) + il2cpp.target_hp_offset,
                        sizeof(hp_raw));
            event.hp_after = from_fixed_point(hp_raw);
            std::int64_t minus_raw = 0;
            std::memcpy(&minus_raw,
                        static_cast<const char*>(target) + il2cpp.minus_hp_offset,
                        sizeof(minus_raw));
            event.minus_hp_after = from_fixed_point(minus_raw);
            const auto previous_minus =
                last_minus_hp_by_id_.find(event.target_id);
            event.minus_hp_delta =
                previous_minus == last_minus_hp_by_id_.end()
                    ? 0.0
                    : event.minus_hp_after - previous_minus->second;
            last_minus_hp_by_id_[event.target_id] = event.minus_hp_after;
            event.max_minus_hp_after = from_fixed_point(
                il2cpp.max_minus_hp(target, il2cpp.max_minus_hp_info));
            event.hp_effective_after = event.hp_after;
            const auto previous = last_hp_by_id_.find(event.target_id);
            event.hp_delta =
                previous == last_hp_by_id_.end()
                    ? std::numeric_limits<double>::quiet_NaN()
                    : event.hp_after - previous->second;
            last_hp_by_id_[event.target_id] = event.hp_after;
            event.hp_effective_delta = event.hp_delta;
            last_effective_hp_by_id_[event.target_id] = event.hp_after;
        }
    }
    if (original_source) {
        event.original_source_id = runtime_.string(
            il2cpp.entity_id(original_source, il2cpp.entity_id_info));
    }

    if (!event.source_id.empty() && il2cpp.is_token && il2cpp.fetch_host) {
        try {
            if (il2cpp.is_token(source, il2cpp.is_token_info)) {
                void* host = source;
                for (int depth = 0; depth < 4; ++depth) {
                    void* next = il2cpp.fetch_host(host, il2cpp.fetch_host_info);
                    if (!next || next == host) {
                        break;
                    }
                    host = next;
                }
                if (host && host != source &&
                    runtime_.class_assignable(il2cpp.character_class,
                                              runtime_.object_class(host))) {
                    std::string owner_id = runtime_.string(
                        il2cpp.character_id(host, il2cpp.character_id_info));
                    if (!owner_id.empty() && owner_id != event.source_id) {
                        event.source_owner_id = std::move(owner_id);
                    }
                }
            }
        } catch (...) {
        }
    }

    if ((!event.source_id.empty() || !event.original_source_id.empty() ||
         !event.target_id.empty()) &&
        std::isfinite(event.calculated_delta) && std::isfinite(event.applied_delta)) {
        publish(std::move(event));
    }
}

void BattleEventCapture::on_log_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(active_capture);
    original_log_modifier(self, modifier, method);
    if (scope.recording()) {
        scope.capture()->record_modifier(self, modifier, false);
    }
}

void BattleEventCapture::on_log_ep_modifier(void* self, void* modifier, void* method) {
    CallbackScope scope(active_capture);
    original_log_ep_modifier(self, modifier, method);
    if (scope.recording()) {
        scope.capture()->record_modifier(self, modifier, true);
    }
}

void BattleEventCapture::on_update_hp_recovery(void* self, std::int64_t delta,
                                               void* method) {
    CallbackScope scope(active_capture);
    bool tracked = false;
    double before = 0;
    if (scope.recording() && self) {
        BattleEventCapture* capture = scope.capture();
        Bindings& il2cpp = *capture->bindings_;
        if (il2cpp.is_mine(self, il2cpp.is_mine_info) &&
            capture->runtime_.class_assignable(
                il2cpp.character_class, capture->runtime_.object_class(self))) {
            std::int64_t hp_raw = 0;
            std::memcpy(&hp_raw,
                        static_cast<const char*>(self) + il2cpp.target_hp_offset,
                        sizeof(hp_raw));
            before = from_fixed_point(hp_raw);
            tracked = true;
        }
    }
    original_update_hp_recovery(self, delta, method);
    if (tracked && scope.recording()) {
        BattleEventCapture* capture = scope.capture();
        Bindings& il2cpp = *capture->bindings_;
        std::int64_t hp_raw = 0;
        std::memcpy(&hp_raw,
                    static_cast<const char*>(self) + il2cpp.target_hp_offset,
                    sizeof(hp_raw));
        const double after = from_fixed_point(hp_raw);
        if (after > before + 0.0001) {
            capture->record_regen(self, after - before);
        }
    }
}

void BattleEventCapture::on_start_game(void* self, void* method) {
    CallbackScope scope(active_capture);
    original_start_game(self, method);
    if (scope.recording()) {
        scope.capture()->begin_battle(self, false);
    }
}

void BattleEventCapture::on_clear_runtime_data(void* self, void* method) {
    CallbackScope scope(active_capture);
    if (scope.recording()) {
        scope.capture()->end_battle();
    }
    original_clear_runtime_data(self, method);
}

void BattleEventCapture::on_destroy(void* self, void* method) {
    CallbackScope scope(active_capture);
    if (scope.recording() && scope.capture()->controller_ == self) {
        scope.capture()->end_battle();
        scope.capture()->controller_ = nullptr;
    }
    original_on_destroy(self, method);
}

void BattleEventCapture::on_update(void* self, void* method) {
    CallbackScope scope(active_capture);
    original_update(self, method);
    if (scope.recording()) {
        scope.capture()->update_battle(self);
    }
}

} // namespace ark::better_battle
