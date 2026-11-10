#include "BetterBattle.h"
#include "probe_support.h"

namespace ark::better_battle {

using namespace probe_support;

void LocalBattle::queue_trace_line(std::string line) {
    std::lock_guard lock(trace_queue_mutex_);
    if (trace_queue_.size() >= 32768) {
        ++trace_dropped_lines_;
        return;
    }
    trace_queue_.push_back(std::move(line));
    trace_queue_ready_.notify_one();
}

void LocalBattle::trace_writer_loop(std::ofstream output) {
    for (;;) {
        std::deque<std::string> pending;
        {
            std::unique_lock lock(trace_queue_mutex_);
            trace_queue_ready_.wait(lock, [&] {
                return trace_writer_stop_ || !trace_queue_.empty();
            });
            if (trace_queue_.empty() && trace_writer_stop_) {
                break;
            }
            pending.swap(trace_queue_);
        }
        for (const std::string& line : pending) {
            output << line << '\n';
        }
        output.flush();
    }
    std::uint64_t dropped = 0;
    {
        std::lock_guard lock(trace_queue_mutex_);
        dropped = trace_dropped_lines_;
    }
    if (dropped) {
        output << "trace_dropped=" << dropped << '\n';
    }
    output.flush();
}

void LocalBattle::start_trace(const std::string& fields) {
    if (trace_active_) {
        throw std::runtime_error("帧追踪已经在运行");
    }
    if (trace_writer_.joinable()) {
        close_trace();
    }

    std::uint32_t selected = 0;
    if (fields.empty()) {
        selected = kTraceDefault;
    } else {
        std::istringstream parser(fields);
        std::string token;
        while (std::getline(parser, token, ',')) {
            if (token == "all") {
                selected |= kTraceDefault | kTraceSkillRemaining |
                            kTraceSkillTrigger | kTraceSkillBeginAnim |
                            kTraceStateId;
            }
            else if (token == "state") selected |= kTraceState;
            else if (token == "cost") selected |= kTraceCost;
            else if (token == "hp") selected |= kTraceHp;
            else if (token == "sp") selected |= kTraceSp;
            else if (token == "skill") selected |= kTraceSkill;
            else if (token == "alive") selected |= kTraceAlive;
            else if (token == "affect") selected |= kTraceSkillAffect;
            else if (token == "remain") selected |= kTraceSkillRemaining;
            else if (token == "trigger") selected |= kTraceSkillTrigger;
            else if (token == "anim") selected |= kTraceSkillBeginAnim;
            else if (token == "stateid") selected |= kTraceStateId;
            else {
                throw std::runtime_error(
                    "未知追踪字段：" + token +
                    "（可用 state,cost,hp,sp,skill,affect,remain,"
                    "trigger,anim,stateid,alive,all）");
            }
        }
    }
    if (!selected) {
        throw std::runtime_error("追踪字段为空");
    }

    void* controller_class = classes_.battle_controller;
    std::vector<void*> controllers = find_controllers(runtime_, controller_class);
    if (controllers.empty()) {
        throw std::runtime_error("当前不在战斗场景");
    }
    void* controller = controllers.front();
    adopt_scene(controller);
    void* runtime_class = runtime_.object_class(controller);

    auto find = [&](void* klass, const char* name, int count = 0) {
        void* method = find_method_optional(runtime_, klass, name, count);
        if (method) {
            return method;
        }
        throw std::runtime_error(std::string("缺少帧追踪方法：") + name);
    };
    trace_frame_method_ = find(runtime_class, "get_fixedFrameCnt");
    trace_paused_method_ = find(runtime_class, "get_isPaused");
    trace_playing_method_ = find(runtime_class, "get_isPlaying");
    trace_state_method_ = find(runtime_class, "get_state");
    trace_cost_method_ = find(runtime_class, "GetCost", 1);
    trace_hp_method_ = find(classes_.entity, "get_hp");
    trace_sp_method_ = find(classes_.entity, "get_sp");
    trace_max_sp_method_ = find(classes_.entity, "get_maxSp");
    trace_in_skill_method_ =
        find(classes_.character, "get_isInSkillState");
    trace_state_id_method_ = find(classes_.entity, "get_currentStateId");
    trace_alive_method_ = find(classes_.entity, "get_alive");

    const auto entry = [](void* method) {
        return *static_cast<void**>(method);
    };
    const auto bind = [&](void* method, auto& getter) {
        if (il2cpp::is_executable(entry(method))) {
            getter = reinterpret_cast<std::remove_reference_t<decltype(getter)>>(entry(method));
        }
    };
    bind(trace_frame_method_, trace_frame_get_);
    if (!trace_frame_get_) {
        throw std::runtime_error("get_fixedFrameCnt 没有可执行入口");
    }
    bind(trace_paused_method_, trace_paused_get_);
    bind(trace_playing_method_, trace_playing_get_);
    bind(trace_state_method_, trace_state_get_);
    bind(trace_hp_method_, trace_hp_get_);
    bind(trace_sp_method_, trace_sp_get_);
    bind(trace_max_sp_method_, trace_max_sp_get_);
    bind(trace_in_skill_method_, trace_in_skill_get_);
    bind(trace_state_id_method_, trace_state_id_get_);
    bind(trace_alive_method_, trace_alive_get_);

    const std::filesystem::path path =
        std::filesystem::path(host_.root_directory) / "battle_trace_frames.log";
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法创建 battle_trace_frames.log");
    }
    output << "# fixedFrameCnt trace; logical rate = 30 fps; fields=" << fields
           << '\n';

    trace_scene_revision_ = scene_.revision;
    trace_fields_ = selected;
    trace_side_ = player_side_value(runtime_, controller, runtime_class);
    trace_has_frame_ = false;
    trace_last_units_.clear();
    trace_last_state_.clear();
    trace_units_.clear();
    trace_skill_methods_.clear();
    trace_changes_ = 0;
    trace_dropped_lines_ = 0;
    {
        std::lock_guard lock(trace_queue_mutex_);
        trace_queue_.clear();
        trace_writer_stop_ = false;
    }
    trace_writer_ = std::thread(
        &LocalBattle::trace_writer_loop, this, std::move(output));
    trace_active_ = true;
    trace_fields_text_ = fields;
    refresh_battle();
    trace_frame();
    publish_result("逻辑帧追踪已开始：" + path.string() +
                   "，字段=" + (fields.empty() ? "default" : fields));
}

void LocalBattle::stop_trace() {
    if (!trace_active_ && !trace_writer_.joinable()) {
        publish_result("逻辑帧追踪未运行");
        return;
    }
    const std::uint64_t changes = trace_changes_;
    const std::uint64_t dropped = trace_dropped_lines_;
    close_trace();
    publish_result("逻辑帧追踪已停止，记录 " + std::to_string(changes) +
                   " 次状态变化，丢弃 " + std::to_string(dropped) + " 行");
}

void LocalBattle::close_trace() {
    trace_active_ = false;
    {
        std::lock_guard lock(trace_queue_mutex_);
        trace_writer_stop_ = true;
    }
    trace_queue_ready_.notify_all();
    if (trace_writer_.joinable()) {
        trace_writer_.join();
    }
    trace_units_.clear();
    trace_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    trace_frame_method_ = nullptr;
    trace_state_method_ = nullptr;
    trace_paused_method_ = nullptr;
    trace_playing_method_ = nullptr;
    trace_cost_method_ = nullptr;
    trace_hp_method_ = nullptr;
    trace_sp_method_ = nullptr;
    trace_max_sp_method_ = nullptr;
    trace_in_skill_method_ = nullptr;
    trace_state_id_method_ = nullptr;
    trace_alive_method_ = nullptr;
    trace_frame_get_ = nullptr;
    trace_paused_get_ = nullptr;
    trace_playing_get_ = nullptr;
    trace_state_get_ = nullptr;
    trace_hp_get_ = nullptr;
    trace_sp_get_ = nullptr;
    trace_max_sp_get_ = nullptr;
    trace_in_skill_get_ = nullptr;
    trace_state_id_get_ = nullptr;
    trace_alive_get_ = nullptr;
    trace_has_frame_ = false;
    trace_last_units_.clear();
    trace_last_state_.clear();
    trace_skill_methods_.clear();
    trace_fields_text_.clear();
}

void LocalBattle::trace_frame() {
    if (!trace_active_ || !scene_.controller || !trace_frame_get_) {
        return;
    }
    if (trace_scene_revision_ != scene_.revision) {
        return;
    }
    try {
        void* controller = scene_.controller;
        if (!controller) {
            throw std::runtime_error("BattleController 已失效");
        }
        const auto get_bool = [&](void* object, void* method, InstanceBoolGetter getter) {
            if (getter) {
                return getter(object, method);
            }
            void* boxed = runtime_.invoke(method, object);
            return boxed && runtime_.unbox<bool>(boxed);
        };
        const auto get_int = [&](void* object, void* method, InstanceIntGetter getter) {
            if (getter) {
                return getter(object, method);
            }
            void* boxed = runtime_.invoke(method, object);
            return boxed ? runtime_.unbox<int>(boxed) : -1;
        };
        const auto get_fixed_raw = [&](void* object, void* method, InstanceFixedGetter getter) {
            if (getter) {
                return getter(object, method);
            }
            std::int64_t raw = 0;
            void* boxed = runtime_.invoke(method, object);
            if (boxed) {
                read_at(runtime_.object_unbox(boxed), &raw);
            }
            return raw;
        };
        const std::uint32_t frame = trace_frame_get_(trace_frame_method_);
        bool paused = false;
        bool playing = false;
        int state = -1;
        if (trace_fields_ & kTraceState) {
            paused = get_bool(controller, trace_paused_method_, trace_paused_get_);
            playing = get_bool(controller, trace_playing_method_, trace_playing_get_);
            state = get_int(controller, trace_state_method_, trace_state_get_);
        }
        const bool same_frame = trace_has_frame_ && frame == trace_last_frame_;
        if (same_frame) {
            const bool header_changed =
                (trace_fields_ & kTraceState) &&
                (paused != trace_last_paused_ ||
                 playing != trace_last_playing_ ||
                 state != trace_last_state_value_);
            if (!header_changed) {
                return;
            }
        }
        int cost = -1;
        if (trace_fields_ & kTraceCost) {
            int side = trace_side_;
            void* value = runtime_.invoke(trace_cost_method_, controller, {&side});
            cost = value ? runtime_.unbox<int>(value) : -1;
        }

        const bool header_changed =
            !trace_has_frame_ ||
            ((trace_fields_ & kTraceState) &&
             (paused != trace_last_paused_ || playing != trace_last_playing_ ||
              state != trace_last_state_value_)) ||
            ((trace_fields_ & kTraceCost) && cost != trace_last_cost_);
        if (same_frame && !header_changed) {
            return;
        }

        constexpr std::uint32_t kTraceUnitFields =
            kTraceHp | kTraceSp | kTraceSkill | kTraceAlive | kTraceSkillAffect |
            kTraceSkillRemaining | kTraceSkillTrigger |
            kTraceSkillBeginAnim | kTraceStateId;
        if (!same_frame && (trace_fields_ & kTraceUnitFields)) {
            std::string units;
            units.reserve(trace_units_.size() * 96);
            char buffer[160];
            for (const TraceUnit& unit : trace_units_) {
                void* object = unit.root.get();
                if (!object) {
                    continue;
                }
                const int prefix = std::snprintf(
                    buffer, sizeof(buffer), " U%u:c%u", unit.uid, unit.card_uid);
                units.append(buffer, static_cast<std::size_t>(prefix));
                if (trace_fields_ & kTraceHp) {
                    const std::int64_t raw =
                        get_fixed_raw(object, trace_hp_method_, trace_hp_get_);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":hp%lld",
                        static_cast<long long>(raw / 4294967296LL));
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceSp) {
                    const std::int64_t raw =
                        get_fixed_raw(object, trace_sp_method_, trace_sp_get_);
                    const int max_sp =
                        get_int(object, trace_max_sp_method_, trace_max_sp_get_);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":sp%lld/%d",
                        static_cast<long long>(raw / 4294967296LL), max_sp);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceSkill) {
                    const bool in_skill =
                        get_bool(object, trace_in_skill_method_, trace_in_skill_get_);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":in%d", in_skill ? 1 : 0);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                void* skill = unit.skill.get();
                const TraceSkillMethods* skill_methods = nullptr;
                void* skill_class = skill ? runtime_.object_class(skill) : nullptr;
                if (skill && skill_class) {
                    const auto found = trace_skill_methods_.find(skill_class);
                    if (found == trace_skill_methods_.end()) {
                        const auto require = [&](const char* name) {
                            void* method =
                                find_method_optional(runtime_, skill_class, name, 0);
                            if (!method) {
                                throw std::runtime_error(
                                    std::string("技能类型缺少帧追踪方法：") + name);
                            }
                            return method;
                        };
                        TraceSkillMethods methods;
                        if (trace_fields_ & kTraceSkillAffect) {
                            methods.affecting = require("get_isAffecting");
                        }
                        if (trace_fields_ & kTraceSkillRemaining) {
                            methods.remaining = require("get_remainingTime");
                        }
                        if (trace_fields_ & kTraceSkillTrigger) {
                            methods.trigger_count = require("get_triggerCnt");
                        }
                        if (trace_fields_ & kTraceSkillBeginAnim) {
                            methods.begin_anim = require("get_hasPlayedBeginAnim");
                        }
                        skill_methods =
                            &trace_skill_methods_.emplace(skill_class, methods)
                                 .first->second;
                    } else {
                        skill_methods = &found->second;
                    }
                }
                if (trace_fields_ & kTraceSkillAffect) {
                    const bool affecting = skill && get_bool(
                        skill, skill_methods ? skill_methods->affecting : nullptr, nullptr);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":aff%d", affecting ? 1 : 0);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceSkillRemaining) {
                    const std::int64_t raw = skill
                        ? get_fixed_raw(
                              skill, skill_methods ? skill_methods->remaining : nullptr, nullptr)
                        : 0;
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":rem%.3f",
                        static_cast<double>(raw) / kFixedPointScale);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceSkillTrigger) {
                    const int trigger_count = skill
                        ? get_int(
                              skill,
                              skill_methods ? skill_methods->trigger_count : nullptr, nullptr)
                        : 0;
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":trig%d", trigger_count);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceSkillBeginAnim) {
                    const bool begin_anim = skill && get_bool(
                        skill, skill_methods ? skill_methods->begin_anim : nullptr, nullptr);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":anim%d", begin_anim ? 1 : 0);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceStateId) {
                    const int state_id =
                        get_int(object, trace_state_id_method_, trace_state_id_get_);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":st%d", state_id);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
                if (trace_fields_ & kTraceAlive) {
                    const bool alive =
                        get_bool(object, trace_alive_method_, trace_alive_get_);
                    const int n = std::snprintf(
                        buffer, sizeof(buffer), ":alive%d", alive ? 1 : 0);
                    units.append(buffer, static_cast<std::size_t>(n));
                }
            }
            trace_last_units_ = std::move(units);
        }

        std::string current;
        current.reserve(96 + trace_last_units_.size());
        char buffer[160];
        if (trace_fields_ & kTraceState) {
            std::snprintf(buffer, sizeof(buffer),
                          "paused=%d playing=%d state=%d",
                          paused ? 1 : 0, playing ? 1 : 0, state);
            current += buffer;
        }
        if (trace_fields_ & kTraceCost) {
            std::snprintf(buffer, sizeof(buffer), "%scost=%d",
                          current.empty() ? "" : " ", cost);
            current += buffer;
        }
        if (!trace_last_units_.empty()) {
            current += " |";
            current += trace_last_units_;
        }

        trace_has_frame_ = true;
        trace_last_frame_ = frame;
        trace_last_paused_ = paused;
        trace_last_playing_ = playing;
        trace_last_state_value_ = state;
        trace_last_cost_ = cost;
        if (current != trace_last_state_) {
            char prefix[64];
            std::snprintf(prefix, sizeof(prefix), "frame=%u ", frame);
            queue_trace_line(std::string(prefix) + current);
            trace_last_state_ = std::move(current);
            ++trace_changes_;
        }
    } catch (const std::exception& error) {
        trace_active_ = false;
        queue_trace_line(std::string("trace_error=") + error.what());
        log(kLogWarning, std::string("逻辑帧追踪已停止：") + error.what());
    }
}


}  // namespace ark::better_battle
