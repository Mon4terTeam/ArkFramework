#include "battle_helpers.h"

#include <nlohmann/json.hpp>

namespace ark::better_battle {

namespace {

std::vector<void*> find_gchandle_objects(il2cpp::Runtime& runtime, void* klass,
                                        std::size_t limit) {
    if (!klass || !runtime.gchandle_foreach_target) {
        return {};
    }
    struct Context {
        il2cpp::Runtime* runtime = nullptr;
        void* klass = nullptr;
        std::vector<void*>* objects = nullptr;
        std::size_t limit = 0;
    };
    static Context* current = nullptr;
    Context context{&runtime, klass, nullptr, limit};
    std::vector<void*> objects;
    context.objects = &objects;
    current = &context;
    runtime.gchandle_foreach_target(
        [](void* object, void*) {
            Context* ctx = current;
            if (!ctx || !object || ctx->objects->size() >= ctx->limit) {
                return;
            }
            try {
                if (ctx->runtime->object_class(object) == ctx->klass) {
                    ctx->objects->push_back(object);
                }
            } catch (...) {
            }
        },
        &context);
    current = nullptr;
    return objects;
}

bool set_paused_via_ui(il2cpp::Runtime& runtime, void* controller, bool paused) {
    void* ui_class = runtime.find("Torappu.Battle.UI", "UIController");
    if (!ui_class) {
        return false;
    }
    const std::vector<void*> candidates =
        find_gchandle_objects(runtime, ui_class, 16);
    if (candidates.empty()) {
        return false;
    }
    void* behaviour_class = runtime.find("UnityEngine", "Behaviour");
    void* set_paused = runtime.method(ui_class, "SetPaused", 3);
    void* get_controller = runtime.method(ui_class, "get_controller", 0);
    for (void* ui : candidates) {
        void* ui_controller = nullptr;
        try {
            ui_controller = runtime.invoke(get_controller, ui);
        } catch (...) {
            continue;
        }
        if (ui_controller != controller) {
            continue;
        }
        void* top_bar = nullptr;
        try {
            read_field_deep(runtime, ui, ui_class, "_topBar", &top_bar);
        } catch (...) {
        }
        if (!top_bar) {
            continue;
        }
        bool active = true;
        if (behaviour_class) {
            try {
                active = method_bool(runtime, ui, behaviour_class,
                                     "get_isActiveAndEnabled", true);
            } catch (...) {
                active = false;
            }
        }
        if (!active) {
            continue;
        }
        bool paused_value = paused;
        bool preserve_mask = false;
        bool force = false;
        try {
            runtime.invoke(set_paused, ui,
                           {&paused_value, &preserve_mask, &force});
        } catch (...) {
            continue;
        }
        return true;
    }
    return false;
}

}  // namespace

void LocalBattle::on_spine_manual_update(void* self, bool enabled, void* method) {
    LocalBattle* owner = spine_owner.load(std::memory_order_acquire);
    if (!owner) {
        return;
    }
    using SpineManualUpdateFn = void (*)(void*, bool, void*);
    auto original =
        reinterpret_cast<SpineManualUpdateFn>(owner->spine_manual_original_);
    if (original) {
        const bool force_auto =
            owner->tas_mode_.load(std::memory_order_acquire);
        original(self, force_auto ? false : enabled, method);
    }
}

void LocalBattle::enforce_spine_auto(void* controller) {
    if (!tas_mode_.load(std::memory_order_acquire) ||
        !tas_spine_auto_.load(std::memory_order_acquire)) {
        return;
    }
    if (!spine_hook_) {
        try {
            ensure_spine_manual_hook();
        } catch (...) {
        }
    }
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers;
        if (controller) {
            controllers.push_back(controller);
        } else if (scene_.controller) {
            controllers.push_back(scene_.controller);
        } else {
            controllers = find_controllers(runtime_, controller_class);
        }
        if (spine_scene_revision_ != scene_.revision) {
            spine_scene_revision_ = scene_.revision;
            spine_scan_tick_ = 0;
            spine_manual_done_.clear();
        }
        ++spine_scan_tick_;
        if ((spine_scan_tick_ % 6) != 1) {
            return;
        }
        if (spine_scan_tick_ > 600) {
            spine_scan_tick_ = 1;
            spine_manual_done_.clear();
        }
        const std::uint64_t scan_started = perf_now_us();
        for (void* controller : controllers) {
            void* manager = nullptr;
            read_field_deep(runtime_, controller, controller_class, "m_objectManger",
                            &manager);
            if (!manager) {
                continue;
            }
            void* objects = nullptr;
            read_field_deep(runtime_, manager, runtime_.object_class(manager),
                            "m_cachedList", &objects);
            for (int index = 0; objects && index < runtime_.count(objects); ++index) {
                void* object = runtime_.at(objects, index);
                if (!object) {
                    continue;
                }
                void* klass = runtime_.object_class(object);
                const std::string type = runtime_.klass_name(klass);
                void* animator = nullptr;
                if (type == "Torappu.Battle.Character") {
                    void* getter =
                        find_method_optional(runtime_, klass, "get_animator", 0);
                    if (getter) {
                        animator = runtime_.invoke(getter, object);
                    }
                } else if (type == "Torappu.Battle.Enemy") {
                    read_field_deep(runtime_, object, klass, "m_spineAnimator",
                                    &animator);
                }
                if (!animator) {
                    continue;
                }
                std::uint32_t uid = 0;
                try {
                    if (!read_field_deep(
                            runtime_, object, klass,
                            "<instanceUid>k__BackingField", &uid)) {
                        uid = 0;
                    }
                } catch (...) {
                    uid = 0;
                }
                const std::pair<void*, std::uint32_t> key(animator, uid);
                if (spine_manual_done_.find(key) !=
                    spine_manual_done_.end()) {
                    continue;
                }
                void* method = find_method_optional(
                    runtime_, runtime_.object_class(animator),
                    "EnableSkeletonManualUpdate", 1);
                if (!method) {
                    continue;
                }
                bool enabled = false;
                try {
                    runtime_.invoke(method, animator, {&enabled});
                    spine_manual_done_.emplace(key, true);
                } catch (...) {
                }
            }
        }
        perf_.spine_auto.add(perf_now_us() - scan_started);
    } catch (...) {
        perf_.spine_auto.add(0);
    }
}

void LocalBattle::set_paused(bool paused) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        if (set_paused_via_ui(runtime_, controller, paused)) {
            const bool actual = method_bool(runtime_, controller, controller_class,
                                            "get_isPaused", !paused);
            if (actual == paused) {
                publish_result(paused ? "战斗已暂停" : "战斗已继续");
                return;
            }
            if (!paused) {
                publish_result("继续失败：仍有其他暂停来源生效", false);
                return;
            }
        }
        void* pause_key = runtime_.nested(runtime_.find("Torappu.Battle", "Consts"),
                                          "BattlePauseKey");
        const int key = runtime_.static_value<int>(pause_key, "UI_CONTROLLER");
        void* method = runtime_.method(controller_class, "SetPaused", 2);
        bool paused_value = paused;
        int key_value = key;
        runtime_.invoke(method, controller, {&paused_value, &key_value});
        publish_result(paused ? "战斗已暂停" : "战斗已继续");
    } catch (const std::exception& error) {
        publish_result(std::string(paused ? "暂停失败：" : "继续失败：") + error.what(), false);
    }
}

void LocalBattle::close_battle_session() {
    cancel_timeline_fast_forward();
    battle_session_closed_.store(true, std::memory_order_release);
    remove_replay_hook();
    replay_active_.store(false, std::memory_order_release);
    replay_dispatched_ = 0;
    replay_next_step_ = 0;
    replay_frame_ = 0;
    replay_steps_.clear();
    replay_file_.clear();
    replay_stop_frame_.store(0, std::memory_order_release);
    replay_stop_enabled_.store(false, std::memory_order_release);
    pending_replay_seek_fast_.store(false, std::memory_order_release);
    pending_replay_seek_target_.store(0, std::memory_order_release);
    pending_timeline_jump_ = false;
    pending_timeline_fast_ = false;
    pending_timeline_play_ = false;
    pending_timeline_parent_id_.clear();
    record_active_.store(false, std::memory_order_release);
    record_dirty_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(record_mutex_);
        frame_actual_operations_.clear();
        frame_prepared_ = std::numeric_limits<std::uint32_t>::max();
        frame_policy_ = TimelineFramePolicy::Record;
    }

    BattleSnapshot idle;
    idle.available = false;
    idle.playing = false;
    idle.paused = false;
    idle.phase = "idle";
    idle.state = -1;
    publish_web_snapshot(idle);
    publish_snapshot(std::move(idle));
    write_status_file(false);
}

void LocalBattle::leave_battle() {
    try {
        cancel_timeline_fast_forward();
        player_data_instance_.store(nullptr, std::memory_order_release);
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (!controllers.empty()) {
            Root controller_root(runtime_, controllers.front());
            if (void* method = find_method_optional(runtime_, controller_class, "GiveUpGame", 0)) {
                try {
                    runtime_.invoke(method, controllers.front());
                } catch (...) {
                }
            }
        }
        void* flow_class = classes_.game_flow_controller;
        void* flow = runtime_.singleton(flow_class);
        Root flow_root(runtime_, flow);
        void* options_class = runtime_.nested(flow_class, "Options");
        void* options = box_zero(runtime_, options_class);
        Root options_root(runtime_, options);
        runtime_.set<bool>(options, "stopMusic", false);
        std::string scene = scene_constant(runtime_, "HOME");
        if (scene.empty()) {
            scene = scene_constant(runtime_, "EMPTY_SCENE");
        }
        if (scene.empty()) {
            throw std::runtime_error("HOME/EMPTY_SCENE 场景名为空");
        }
        void* text = runtime_.string_new(scene.c_str());
        Root text_root(runtime_, text);
        void* start_scene = runtime_.method(flow_class, "StartScene", 2);
        void* accepted =
            runtime_.invoke(start_scene, flow, {text, runtime_.object_unbox(options)});
        if (!accepted || !runtime_.unbox<bool>(accepted)) {
            throw std::runtime_error("StartScene(" + scene + ") 返回 false");
        }
        local_battle_active_.store(false, std::memory_order_release);
        clear_scene();
        scene_.stage_pending = false;
        scene_.stage = StageInfo{};
        scene_.stage_request.clear();
        clear_static_cache();
        close_battle_session();
        publish_result("已离开本地战斗，返回 " + scene);
    } catch (const std::exception& error) {
        publish_result(std::string("离开战斗失败：") + error.what(), false);
    }
}

bool LocalBattle::route_home_from_finish() {
    try {
        player_data_instance_.store(nullptr, std::memory_order_release);
        void* flow_class = classes_.game_flow_controller;
        void* flow = runtime_.singleton(flow_class);
        Root flow_root(runtime_, flow);
        void* options_class = runtime_.nested(flow_class, "Options");
        void* options = box_zero(runtime_, options_class);
        Root options_root(runtime_, options);
        runtime_.set<bool>(options, "stopMusic", false);
        std::string scene = scene_constant(runtime_, "HOME");
        if (scene.empty()) {
            scene = scene_constant(runtime_, "EMPTY_SCENE");
        }
        if (scene.empty()) {
            throw std::runtime_error("HOME/EMPTY_SCENE 场景名为空");
        }
        void* text = runtime_.string_new(scene.c_str());
        Root text_root(runtime_, text);
        void* start_scene = runtime_.method(flow_class, "StartScene", 2);
        void* accepted =
            runtime_.invoke(start_scene, flow, {text, runtime_.object_unbox(options)});
        if (!accepted || !runtime_.unbox<bool>(accepted)) {
            throw std::runtime_error("StartScene(" + scene + ") 返回 false");
        }
        local_battle_active_.store(false, std::memory_order_release);
        clear_scene();
        scene_.stage_pending = false;
        scene_.stage = StageInfo{};
        scene_.stage_request.clear();
        clear_static_cache();
        close_battle_session();
        publish_result("战斗结束，已跳过服务器同步并返回 " + scene);
        log(kLogInfo, "本地战斗结束，已直接返回 " + scene);
        return true;
    } catch (const std::exception& error) {
        publish_result(std::string("战斗结束返回 HOME 失败：") + error.what(), false);
        log(kLogWarning, std::string("战斗结束返回 HOME 失败：") + error.what());
        return false;
    }
}

void LocalBattle::describe_withdraw() {
    try {
        void* character_class = classes_.character;
        void* method = runtime_.method(character_class, "Withdraw", 4);
        std::string detail = "Character.Withdraw(";
        for (int index = 0; index < 4; ++index) {
            if (index) {
                detail += ", ";
            }
            const char* name = runtime_.method_param_name(method, static_cast<std::uint32_t>(index));
            const std::string type = runtime_.type(runtime_.method_param(method, index));
            detail += (name && *name ? name : "arg" + std::to_string(index));
            detail += ":" + type;
        }
        detail += ")";
        publish_result(detail);
    } catch (const std::exception& error) {
        publish_result(std::string("读取 Withdraw 签名失败：") + error.what(), false);
    }
}

bool LocalBattle::ensure_spine_manual_hook() {
    if (spine_hook_) {
        return true;
    }
    void* spine_class = runtime_.find("Torappu.Battle", "SpineAnimator");
    void* method = find_method_optional(
        runtime_, spine_class, "EnableSkeletonManualUpdate", 1);
    if (!method) {
        throw std::runtime_error("SpineAnimator.EnableSkeletonManualUpdate 缺失");
    }
    void* entry = *static_cast<void**>(method);
    if (!il2cpp::is_executable(entry)) {
        throw std::runtime_error("SpineAnimator 手动更新入口不可执行");
    }
    spine_hook_ = host_.create_native_hook(
        host_.owner, entry,
        reinterpret_cast<void*>(&LocalBattle::on_spine_manual_update),
        &spine_manual_original_);
    if (!spine_hook_ || !spine_manual_original_) {
        spine_hook_ = 0;
        spine_manual_original_ = nullptr;
        throw std::runtime_error("创建 Spine 自动更新 Hook 失败");
    }
    spine_owner.store(this, std::memory_order_release);
    if (!host_.enable_native_hook(host_.owner, spine_hook_, true)) {
        spine_owner.store(nullptr, std::memory_order_release);
        host_.remove_native_hook(host_.owner, spine_hook_);
        spine_hook_ = 0;
        spine_manual_original_ = nullptr;
        throw std::runtime_error("启用 Spine 自动更新 Hook 失败");
    }
    return true;
}

void LocalBattle::tas_bench(const std::string& text, const std::string& path, int value) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        if (text == "stop-at") {
            const std::uint32_t target =
                value > 0 ? static_cast<std::uint32_t>(value) : 3000;
            replay_stop_frame_.store(target, std::memory_order_release);
            publish_result("Timeline replay stop frame=" + std::to_string(target));
            return;
        }
        if (text == "damage-series") {
            const DamageSnapshot damage = damage_snapshot(true);
            std::uint32_t controller_frame = snapshot_.frame;
            double controller_time = 0;
            bool has_controller_time = false;
            int random_seed = -1;
            try {
                void* frame_method =
                    find_method_optional(runtime_, controller_class, "get_fixedFrameCnt", 0);
                if (frame_method) {
                    auto get_frame =
                        reinterpret_cast<StaticFrameGetter>(*static_cast<void**>(frame_method));
                    controller_frame = get_frame(frame_method);
                }
                has_controller_time =
                    read_fixed_play_time(runtime_, controller_class, &controller_time);
                random_seed =
                    method_int(runtime_, controller, controller_class,
                               "get_randomSeed", -1);
            } catch (...) {
            }
            nlohmann::ordered_json document;
            document["frame"] = controller_frame;
            document["stage"] = snapshot_.stage_id;
            document["duration"] = damage.duration;
            document["damage_game_time"] = damage_tracker_.game_time;
            document["last_event_time"] = damage_tracker_.last_event_time;
            document["last_event_kind"] = damage_tracker_.last_event_kind;
            document["last_clock_time"] = damage_tracker_.last_clock_time;
            document["controller_time"] = controller_time;
            document["has_controller_time"] = has_controller_time;
            document["random_seed"] = random_seed;
            document["battle"] = damage.battle;
            document["sequence"] = damage.sequence;
            document["revision"] = damage.revision;
            document["series_revision"] = damage.series_revision;
            document["total"] = {
                {"actual", damage.total_actual},
                {"calculated", damage.total_calculated},
            };
            nlohmann::ordered_json types = nlohmann::ordered_json::object();
            for (std::size_t index = 0; index < kDamageTypeCount; ++index) {
                types[kDamageTypeKeys[index]] = {
                    {"actual", damage.types[index].actual},
                    {"calculated", damage.types[index].calculated},
                };
            }
            document["types"] = std::move(types);
            document["start_frame"] = damage.start_frame;
            document["end_frame"] = damage.end_frame;
            document["duration_frames"] = damage.duration_frames;
            nlohmann::ordered_json series;
            series["frame_step"] = damage.series.step_frames;
            series["frames"] = damage.series.frames;
            series["total"] = damage.series.total;
            for (std::size_t index = 0; index < kDamageTypeCount; ++index) {
                series[kDamageTypeKeys[index]] = damage.series.types[index];
            }
            document["series"] = std::move(series);
            nlohmann::ordered_json operators = nlohmann::ordered_json::array();
            for (const DamageActorInfo& actor : damage.actors) {
                operators.push_back({
                    {"id", actor.id},
                    {"actual", actor.actual},
                    {"calculated", actor.calculated},
                    {"curve", actor.curve},
                });
            }
            document["operators"] = std::move(operators);
            std::ofstream output(
                std::filesystem::path(host_.root_directory) / "battle_damage_series.json",
                std::ios::out | std::ios::trunc);
            output << document.dump();
            output.close();
            if (!output) {
                throw std::runtime_error("伤害曲线导出失败");
            }
            publish_result("Timeline damage series frame=" +
                           std::to_string(snapshot_.frame) +
                           " points=" + std::to_string(damage.series_points) +
                           " actors=" + std::to_string(damage.actors.size()));
            return;
        }
        void* manager = nullptr;
        read_field_deep(runtime_, controller, controller_class, "m_objectManger", &manager);
        void* objects = nullptr;
        if (manager) {
            read_field_deep(runtime_, manager, runtime_.object_class(manager), "m_cachedList",
                            &objects);
        }
        const auto animator_for = [&](void* object, const std::string& type) -> void* {
            void* klass = runtime_.object_class(object);
            if (type == "Torappu.Battle.Character") {
                void* getter = find_method_optional(runtime_, klass, "get_animator", 0);
                return getter ? runtime_.invoke(getter, object) : nullptr;
            }
            if (type == "Torappu.Battle.Enemy") {
                void* animator = nullptr;
                read_field_deep(runtime_, object, klass, "m_spineAnimator", &animator);
                return animator;
            }
            return nullptr;
        };
        const auto skeleton_for = [&](void* animator) -> void* {
            if (!animator) {
                return nullptr;
            }
            void* method = find_method_optional(runtime_, runtime_.object_class(animator),
                                                "get_skeleton", 0);
            return method ? runtime_.invoke(method, animator) : nullptr;
        };
        const auto log_spine = [&](void* object, const std::string& type) {
            void* animator = animator_for(object, type);
            if (!animator) {
                log(kLogInfo, "Timeline spine " + type + " animator=null");
                return;
            }
            void* skeleton = skeleton_for(animator);
            bool manual = false;
            std::int64_t skip = 0;
            if (skeleton) {
                read_field_deep(runtime_, skeleton, runtime_.object_class(skeleton),
                                "<EnableManualUpdate>k__BackingField", &manual);
            }
            read_field_deep(runtime_, animator, runtime_.object_class(animator),
                            "m_skipTickTime", &skip);
            std::ostringstream output;
            output << "Timeline spine " << type << " host=" << object
                   << " animator=" << animator << " skeleton=" << skeleton
                   << " manual=" << manual << " skip="
                   << static_cast<double>(skip) / kFixedPointScale;
            if (skeleton) {
                void* state = nullptr;
                read_field_deep(runtime_, skeleton, runtime_.object_class(skeleton), "state",
                                &state);
                if (state) {
                    void* tracks = nullptr;
                    read_field_deep(runtime_, state, runtime_.object_class(state), "tracks",
                                    &tracks);
                    if (tracks) {
                        void* items = nullptr;
                        int count = 0;
                        read_field_deep(runtime_, tracks, runtime_.object_class(tracks), "Items",
                                        &items);
                        read_field_deep(runtime_, tracks, runtime_.object_class(tracks), "Count",
                                        &count);
                        output << " tracks=" << count;
                        for (int track_index = 0;
                             items && track_index < std::min(count, 3); ++track_index) {
                            void* track = runtime_.at(items, track_index);
                            if (!track) {
                                continue;
                            }
                            float track_time = 0;
                            float alpha = 0;
                            float time_scale = 0;
                            int loop = 0;
                            read_field_deep(runtime_, track, runtime_.object_class(track),
                                            "trackTime", &track_time);
                            read_field_deep(runtime_, track, runtime_.object_class(track),
                                            "alpha", &alpha);
                            read_field_deep(runtime_, track, runtime_.object_class(track),
                                            "timeScale", &time_scale);
                            read_field_deep(runtime_, track, runtime_.object_class(track),
                                            "loop", &loop);
                            output << " track[" << track_index << "]=" << track_time
                                   << "/a" << alpha << "/s" << time_scale << "/l" << loop;
                        }
                    }
                }
            }
            log(kLogInfo, output.str());
        };
        const auto log_projectiles = [&]() {
            void* projectiles = nullptr;
            read_field_deep(runtime_, manager, runtime_.object_class(manager),
                            "<projectiles>k__BackingField", &projectiles);
            if (!projectiles) {
                log(kLogInfo, "Timeline projectiles=null");
                return;
            }
            void* items = nullptr;
            int count = 0;
            read_field_deep(runtime_, projectiles, runtime_.object_class(projectiles), "m_items",
                            &items);
            read_field_deep(runtime_, projectiles, runtime_.object_class(projectiles),
                            "<count>k__BackingField", &count);
            log(kLogInfo, "Timeline projectiles count=" + std::to_string(count));
            struct RawObjectPtr {
                void* object;
                std::uint32_t uid;
                std::uint32_t padding;
            };
            for (int index = 0; items && index < count; ++index) {
                void* projectile = runtime_.at(items, index);
                if (!projectile) {
                    continue;
                }
                void* projectile_class = runtime_.object_class(projectile);
                const std::string key =
                    read_string_field(runtime_, projectile, projectile_class,
                                      "<key>k__BackingField");
                const std::string original =
                    read_string_field(runtime_, projectile, projectile_class,
                                      "<originalKey>k__BackingField");
                std::int64_t lifetime = 0;
                int type = -1;
                bool managed = false;
                RawObjectPtr source{};
                read_field_deep(runtime_, projectile, projectile_class, "m_lifeTime", &lifetime);
                read_field_deep(runtime_, projectile, projectile_class, "_projectileType", &type);
                read_field_deep(runtime_, projectile, projectile_class, "_managedBySource",
                                &managed);
                read_field_deep(runtime_, projectile, projectile_class, "m_source", &source);
                std::string source_id;
                if (source.object) {
                    source_id = read_string_field(runtime_, source.object,
                                                  runtime_.object_class(source.object),
                                                  "<id>k__BackingField");
                }
                const double remaining =
                    method_fp(runtime_, projectile, projectile_class, "get_remainingTime");
                const double current =
                    method_fp(runtime_, projectile, projectile_class, "get_currentTime");
                std::ostringstream output;
                output << "Timeline projectile[" << index << "] class="
                       << runtime_.klass_name(projectile_class) << " key=" << key
                       << " original=" << original << " source=" << source_id << " pid="
                       << source.uid << " uid=" << source.uid << " life="
                       << static_cast<double>(lifetime) / kFixedPointScale << " remaining="
                       << remaining << " current=" << current << " type=" << type
                       << " managed=" << managed << " ptr=" << projectile;
                log(kLogInfo, output.str());
            }
        };
        const auto install_spine_hook = [&]() -> bool {
            return ensure_spine_manual_hook();
        };

        if (text == "framedata") {
            void* method = runtime_.method(controller_class, "ManualFrameTick", 2);
            void* frame_class =
                runtime_.class_from_type(runtime_.method_param(method, 0));
            void* iterator = nullptr;
            while (void* field = runtime_.class_fields(frame_class, &iterator)) {
                void* field_class =
                    runtime_.class_from_type(runtime_.field_type(field));
                log(kLogInfo,
                    "Timeline FrameData field " +
                        std::string(runtime_.field_name(field) ? runtime_.field_name(field)
                                                              : "?") +
                        " type=" +
                        (field_class ? runtime_.klass_name(field_class) : "unknown") +
                        " offset=" + std::to_string(runtime_.field_offset(field)));
            }
            publish_result("Timeline FrameData dumped");
            return;
        }

        if (text == "seek") {
            const std::uint32_t target =
                value >= 0 ? static_cast<std::uint32_t>(value) : 3000;
            const std::string plan_file =
                path.empty()
                    ? "E:\\Games\\ArkTest\\ArkFramework\\recordings\\sk5_9op_complete_20261103_010140.json"
                    : path;
            if (!load_replay_plan(plan_file)) {
                throw std::runtime_error("录像装载失败");
            }
            void* game_mode = nullptr;
            read_field_deep(runtime_, controller, controller_class, "m_gameMode", &game_mode);
            if (game_mode) {
                runtime_.set<bool>(game_mode, "m_allowManualTick", true);
            }
            if (!method_bool(runtime_, controller, controller_class,
                             "get_isDeterministic")) {
                throw std::runtime_error("当前战斗不是确定性战斗，无法手动回放");
            }
            tas_spine_auto_.store(true, std::memory_order_release);
            install_spine_hook();
            bool deterministic = true;
            runtime_.invoke(runtime_.method(controller_class, "set_isDeterministic", 1),
                            controller, {&deterministic});
            void* method = runtime_.method(controller_class, "ManualFrameTick", 2);
            void* frame_method =
                runtime_.method(controller_class, "get_fixedFrameCnt", 0);
            auto frame_get = reinterpret_cast<StaticFrameGetter>(
                *static_cast<void**>(frame_method));
            void* frame_class =
                runtime_.class_from_type(runtime_.method_param(method, 0));
            void* frame_value = box_zero(runtime_, frame_class);
            Root frame_value_root(runtime_, frame_value);
            void* queue_field = runtime_.field(frame_class, "opQueue");
            void* queue_class =
                runtime_.class_from_type(runtime_.field_type(queue_field));
            void* queue = runtime_.object_new(queue_class);
            Root queue_root(runtime_, queue);
            runtime_.invoke(runtime_.method(queue_class, ".ctor", 0), queue);
            runtime_.field_set_object(frame_value, queue_field, queue);
            bool flag = true;
            replay_next_step_ = 0;
            replay_dispatched_ = 0;
            std::size_t failures = 0;
            const auto started = std::chrono::steady_clock::now();
            while (frame_get(frame_method) < target) {
                replay_frame_ = frame_get(frame_method);
                while (replay_next_step_ < replay_steps_.size() &&
                       replay_steps_[replay_next_step_].first <= replay_frame_) {
                    for (const RecordedOperation& operation :
                         replay_steps_[replay_next_step_].second) {
                        try {
                            dispatch_replay_operation(operation);
                        } catch (...) {
                            ++failures;
                        }
                        ++replay_dispatched_;
                    }
                    ++replay_next_step_;
                }
                runtime_.invoke(method, controller, {frame_value, &flag});
            }
            const auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started);
            publish_result("Timeline seek target=" + std::to_string(target) +
                           " frame=" + std::to_string(frame_get(frame_method)) +
                           " ops=" + std::to_string(replay_dispatched_) +
                           " fail=" + std::to_string(failures) +
                           " seconds=" + std::to_string(elapsed.count()));
            return;
        }

        if (text == "continue-replay") {
            if (replay_steps_.empty()) {
                throw std::runtime_error("录像未装载");
            }
            void* game_mode = nullptr;
            read_field_deep(runtime_, controller, controller_class, "m_gameMode", &game_mode);
            if (game_mode) {
                runtime_.set<bool>(game_mode, "m_allowManualTick", false);
            }
            tas_spine_auto_.store(true, std::memory_order_release);
            install_spine_hook();
            if (!install_replay_hook()) {
                throw std::runtime_error("回放 Hook 未就绪");
            }
            replay_active_.store(true, std::memory_order_release);
            set_paused(false);
            publish_result("Timeline continue replay remaining=" +
                           std::to_string(replay_steps_.size() - replay_next_step_));
            return;
        }

        if (text == "spine-auto") {
            tas_spine_auto_.store(true, std::memory_order_release);
            install_spine_hook();
            int changed = 0;
            for (int index = 0; objects && index < runtime_.count(objects); ++index) {
                void* object = runtime_.at(objects, index);
                if (!object) {
                    continue;
                }
                const std::string type = runtime_.klass_name(runtime_.object_class(object));
                void* animator = animator_for(object, type);
                if (!animator) {
                    continue;
                }
                void* method = find_method_optional(runtime_, runtime_.object_class(animator),
                                                    "EnableSkeletonManualUpdate", 1);
                if (!method) {
                    continue;
                }
                bool enabled = false;
                runtime_.invoke(method, animator, {&enabled});
                ++changed;
            }
            publish_result("Timeline spine-auto changed=" + std::to_string(changed));
            return;
        }

        if (text == "spine") {
            int manual = 0;
            int auto_count = 0;
            int total = 0;
            for (int index = 0; objects && index < runtime_.count(objects); ++index) {
                void* object = runtime_.at(objects, index);
                if (!object) {
                    continue;
                }
                const std::string type = runtime_.klass_name(runtime_.object_class(object));
                if (type != "Torappu.Battle.Character" && type != "Torappu.Battle.Enemy") {
                    continue;
                }
                ++total;
                void* animator = animator_for(object, type);
                void* skeleton = skeleton_for(animator);
                bool is_manual = false;
                if (skeleton) {
                    read_field_deep(runtime_, skeleton, runtime_.object_class(skeleton),
                                    "<EnableManualUpdate>k__BackingField", &is_manual);
                }
                if (is_manual) {
                    ++manual;
                } else if (skeleton) {
                    ++auto_count;
                }
                log_spine(object, type);
            }
            log(kLogInfo, "Timeline spine summary total=" + std::to_string(total) +
                              " manual=" + std::to_string(manual) +
                              " auto=" + std::to_string(auto_count));
            log_projectiles();
            publish_result("Timeline spine dumped");
            return;
        }

        if (text == "projectiles") {
            log_projectiles();
            publish_result("Timeline projectiles dumped");
            return;
        }

        throw std::runtime_error(
            "tas-bench 需要 seek [frame]、continue-replay、stop-at [frame]、"
            "damage-series、spine-auto、spine 或 projectiles");
    } catch (const std::exception& error) {
        publish_result(std::string("Timeline bench 失败：") + error.what(), false);
    }
}

}  // namespace ark::better_battle
