#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::publish_result(const std::string& result, bool ok) {
    {
        std::lock_guard lock(mutex_);
        last_result_ = result;
        last_ok_ = ok;
    }
    log(ok ? kLogInfo : kLogWarning, result);
    write_status_file();
}

void LocalBattle::publish_snapshot(BattleSnapshot snapshot) {
    std::lock_guard lock(mutex_);
    snapshot_ = std::move(snapshot);
}

void LocalBattle::clear_static_cache() {
    card_static_cache_.clear();
    unit_static_cache_.clear();
    talent_cache_.clear();
    buff_static_cache_.clear();
    buff_cache_generation_ = 0;
}

void LocalBattle::poll_battle_events() {
    constexpr std::size_t kBatchSize = 256;
    std::array<ArkBattleEvent, kBatchSize> events{};
    for (unsigned batch = 0; batch < 16; ++batch) {
        ArkBattleEventReadStatus status;
        const std::size_t count = battle_capture_.read(
            damage_tracker_.sequence, events.data(), events.size(), &status);
        damage_tracker_.overrun = damage_tracker_.overrun || status.overrun;
        damage_tracker_.partial = damage_tracker_.partial || status.partial;

        for (std::size_t index = 0; index < count; ++index) {
            const ArkBattleEvent& event = events[index];
            if (event.kind == ArkBattleEventKind::BattleStart) {
                damage_tracker_ = DamageTracker{};
                damage_tracker_.started = true;
                damage_tracker_.partial = event.partial || status.partial;
                damage_tracker_.battle = event.battle;
                damage_tracker_.start_time = event.partial ? event.game_time : 0.0;
                damage_tracker_.frame_valid = true;
                damage_tracker_.start_frame = event.partial ? event.frame : 0;
                damage_tracker_.current_frame = event.frame;
                damage_tracker_.game_time = event.game_time;
                damage_tracker_.last_event_time = event.game_time;
                damage_tracker_.last_event_kind =
                    static_cast<std::uint32_t>(event.kind);
                damage_tracker_.series.step_frames = 1;
                damage_tracker_.series.frames.push_back(damage_tracker_.start_frame);
                damage_tracker_.series.total.push_back(0);
                damage_tracker_.series.healing.push_back(0);
                damage_tracker_.series.taken.push_back(0);
                for (auto& values : damage_tracker_.series.types) {
                    values.push_back(0);
                }
                damage_tracker_.sequence = event.sequence;
                ++damage_tracker_.revision;
                next_damage_series_tick_ = 0;
                damage_series_published_revision_ = 0;
                continue;
            }
            if (!damage_tracker_.started) {
                if (!event.sequence) {
                    continue;
                }
                damage_tracker_.started = true;
                damage_tracker_.partial = true;
                damage_tracker_.battle = event.battle;
                damage_tracker_.start_time = event.game_time;
                damage_tracker_.frame_valid = true;
                damage_tracker_.start_frame = event.frame;
                damage_tracker_.current_frame = event.frame;
                damage_tracker_.game_time = event.game_time;
                damage_tracker_.series.step_frames = 1;
                damage_tracker_.series.frames.push_back(damage_tracker_.start_frame);
                damage_tracker_.series.total.push_back(0);
                damage_tracker_.series.healing.push_back(0);
                damage_tracker_.series.taken.push_back(0);
                for (auto& values : damage_tracker_.series.types) {
                    values.push_back(0);
                }
            }

            damage_tracker_.sequence = event.sequence;
            if (!damage_tracker_.frame_valid ||
                event.frame >= damage_tracker_.current_frame) {
                damage_tracker_.current_frame = event.frame;
                damage_tracker_.frame_valid = true;
            }
            damage_tracker_.game_time =
                std::max(damage_tracker_.game_time, event.game_time);
            damage_tracker_.last_event_time = event.game_time;
            damage_tracker_.last_event_kind =
                static_cast<std::uint32_t>(event.kind);

            if (event.kind == ArkBattleEventKind::Clock) {
                damage_tracker_.last_clock_time = event.game_time;
                continue;
            }
            if (event.kind == ArkBattleEventKind::BattleEnd) {
                damage_tracker_.ended = true;
                ++damage_tracker_.revision;
                continue;
            }
            if (!std::isfinite(event.calculated_delta) ||
                !std::isfinite(event.applied_delta)) {
                continue;
            }

            if (event.kind == ArkBattleEventKind::Regen) {
                const double regen = std::abs(event.applied_delta);
                if (regen <= 0.009 || event.target_id.empty() ||
                    std::strncmp(event.target_id.c_str(), "char_", 5) != 0) {
                    sync_frame_track(event.frame);
                    continue;
                }
                DamageActorInfo& receiver =
                    damage_tracker_.actors[event.target_id];
                receiver.id = event.target_id;
                if (!receiver.hp_locked) {
                    receiver.hp_start = receiver.hp_current - regen;
                    receiver.hp_effective_start =
                        receiver.hp_effective_current - regen;
                    receiver.hp_locked = true;
                }
                receiver.regen += regen;
                damage_tracker_.total_regen += regen;
                ++damage_tracker_.event_count;
                {
                    std::lock_guard lock(record_mutex_);
                    record_timeline_event_locked(
                        event.frame, event.target_id, event.target_id,
                        kReplayEventRegen, regen, event.target_uid,
                        event.target_uid);
                }
                ++damage_tracker_.revision;
                sync_frame_track(event.frame);
                continue;
            }

            std::string actor_id;
            std::uint32_t actor_uid = 0;
            if (!event.source_owner_id.empty()) {
                actor_id = event.source_owner_id;
                actor_uid = event.source_uid;
            } else if (!event.source_id.empty()) {
                actor_id = event.source_id;
                actor_uid = event.source_uid;
            } else if (!event.original_source_id.empty()) {
                actor_id = event.original_source_id;
                actor_uid = event.original_source_uid;
            } else if (event.kind != ArkBattleEventKind::Healing) {
                actor_id = kNoSourceActorId;
            }
            if (actor_id.empty()) {
                continue;
            }

            DamageActorInfo& actor = damage_tracker_.actors[actor_id];
            actor.id = actor_id;
            if (event.kind == ArkBattleEventKind::ElementalBuildup) {
                const double buildup = std::abs(event.calculated_delta);
                actor.elemental_buildup += buildup;
                damage_tracker_.elemental_buildup += buildup;
                ++damage_tracker_.event_count;
                ++damage_tracker_.revision;
                sync_frame_track(event.frame);
                continue;
            }

            if (event.kind == ArkBattleEventKind::Healing) {
                const double healing = std::abs(event.applied_delta);
                if (event.target_id.empty() || healing <= 0.009 ||
                    std::strncmp(event.target_id.c_str(), "char_", 5) != 0) {
                    sync_frame_track(event.frame);
                    continue;
                }
                DamageActorInfo& healer = damage_tracker_.actors[actor_id];
                healer.id = actor_id;
                healer.healing += healing;
                DamageActorInfo& receiver = damage_tracker_.actors[event.target_id];
                receiver.id = event.target_id;
                if (!receiver.hp_locked) {
                    receiver.hp_start =
                        std::isfinite(event.hp_delta)
                            ? event.hp_after - event.hp_delta
                            : receiver.hp_current;
                    receiver.hp_effective_start =
                        std::isfinite(event.hp_effective_delta)
                            ? event.hp_effective_after -
                                  event.hp_effective_delta
                            : receiver.hp_effective_current;
                    receiver.hp_locked = true;
                }
                receiver.healing_received += healing;
                if (std::isfinite(event.hp_delta)) {
                    receiver.hp_event_delta += event.hp_delta;
                    ++receiver.hp_delta_events;
                } else {
                    ++receiver.hp_delta_fallbacks;
                }
                damage_tracker_.total_healing += healing;
                ++damage_tracker_.event_count;
                {
                    std::lock_guard lock(record_mutex_);
                    record_timeline_event_locked(
                        event.frame, actor_id, event.target_id, kReplayEventHeal,
                        healing, actor_uid, event.target_uid);
                }
                ++damage_tracker_.revision;
                sync_frame_track(event.frame);
                continue;
            }
            if (event.kind != ArkBattleEventKind::Damage) {
                continue;
            }

            const double calculated = std::abs(event.calculated_delta);
            const double actual = std::abs(event.applied_delta);
            const bool target_is_operator =
                std::strncmp(event.target_id.c_str(), "char_", 5) == 0;
            if (target_is_operator) {
                const double taken =
                    actual;
                if (taken > 0.009) {
                    DamageActorInfo& receiver =
                        damage_tracker_.actors[event.target_id];
                    receiver.id = event.target_id;
                    if (!receiver.hp_locked) {
                        receiver.hp_start =
                            std::isfinite(event.hp_delta)
                                ? event.hp_after - event.hp_delta
                                : receiver.hp_current;
                        receiver.hp_effective_start =
                            std::isfinite(event.hp_effective_delta)
                                ? event.hp_effective_after -
                                      event.hp_effective_delta
                                : receiver.hp_effective_current;
                        receiver.hp_locked = true;
                    }
                    receiver.taken += taken;
                    if (std::isfinite(event.hp_delta)) {
                        receiver.hp_event_delta += event.hp_delta;
                        ++receiver.hp_delta_events;
                    } else {
                        ++receiver.hp_delta_fallbacks;
                    }
                    damage_tracker_.total_taken += taken;
                    ++damage_tracker_.event_count;
                    {
                        std::lock_guard lock(record_mutex_);
                        record_timeline_event_locked(
                            event.frame, actor_id, event.target_id,
                            kReplayEventTaken, taken, actor_uid,
                            event.target_uid);
                    }
                    ++damage_tracker_.revision;
                    sync_frame_track(event.frame);
                }
                continue;
            }

            const int type = damage_type_bucket(event.damage_type);
            if (type < 0) {
                continue;
            }
            if (!event.target_id.empty()) {
                std::lock_guard lock(record_mutex_);
                record_timeline_event_locked(
                    event.frame, actor_id, event.target_id,
                    static_cast<std::uint8_t>(type), actual, actor_uid,
                    event.target_uid);
            }
            actor.actual += actual;
            actor.calculated += calculated;
            actor.types[static_cast<std::size_t>(type)].actual += actual;
            actor.types[static_cast<std::size_t>(type)].calculated += calculated;
            damage_tracker_.total_actual += actual;
            damage_tracker_.total_calculated += calculated;
            damage_tracker_.types[static_cast<std::size_t>(type)].actual += actual;
            damage_tracker_.types[static_cast<std::size_t>(type)].calculated += calculated;
            ++damage_tracker_.event_count;
            ++damage_tracker_.revision;
            sync_frame_track(event.frame);
        }
        if (count < events.size()) {
            break;
        }
    }
    verify_replay_events();
}

void LocalBattle::sync_frame_track(std::uint32_t frame) {
    if (!damage_tracker_.started) {
        return;
    }
    DamageSeries& series = damage_tracker_.series;
    if (series.frames.empty()) {
        series.step_frames = 1;
        series.frames.push_back(damage_tracker_.start_frame);
        series.total.push_back(0);
        series.healing.push_back(damage_tracker_.total_healing);
        series.taken.push_back(damage_tracker_.total_taken);
        for (auto& values : series.types) {
            values.push_back(0);
        }
    }
    if (frame < series.frames.back()) {
        return;
    }
    const bool new_frame = frame > series.frames.back();
    if (new_frame) {
        series.frames.push_back(frame);
        series.total.push_back(damage_tracker_.total_actual);
        series.healing.push_back(damage_tracker_.total_healing);
        series.taken.push_back(damage_tracker_.total_taken);
        for (std::size_t type = 0; type < kDamageTypeCount; ++type) {
            series.types[type].push_back(
                damage_tracker_.types[type].actual);
        }
    } else {
        series.total.back() = damage_tracker_.total_actual;
        series.healing.back() = damage_tracker_.total_healing;
        series.taken.back() = damage_tracker_.total_taken;
        for (std::size_t type = 0; type < kDamageTypeCount; ++type) {
            series.types[type].back() =
                damage_tracker_.types[type].actual;
        }
    }
    const std::size_t target_size = series.frames.size();
    for (auto& [id, actor] : damage_tracker_.actors) {
        (void)id;
        actor.curve.resize(target_size, 0.0);
        actor.healing_curve.resize(target_size, 0.0);
        actor.taken_curve.resize(target_size, 0.0);
        actor.curve.back() = actor.actual;
        actor.healing_curve.back() = actor.healing;
        actor.taken_curve.back() = actor.taken;
    }
    ++damage_tracker_.series_revision;
}

DamageSnapshot LocalBattle::damage_snapshot(bool include_curves) const {
    DamageSnapshot snapshot;
    snapshot.available = battle_capture_.available();
    snapshot.started = damage_tracker_.started;
    snapshot.ended = damage_tracker_.ended;
    snapshot.partial = damage_tracker_.partial;
    snapshot.overrun = damage_tracker_.overrun;
    snapshot.battle = damage_tracker_.battle;
    snapshot.sequence = damage_tracker_.sequence;
    snapshot.event_count = damage_tracker_.event_count;
    snapshot.revision = damage_tracker_.revision;
    snapshot.series_revision = damage_tracker_.series_revision;
    snapshot.frame_valid = damage_tracker_.frame_valid;
    snapshot.start_frame = damage_tracker_.start_frame;
    snapshot.end_frame = damage_tracker_.current_frame;
    snapshot.duration_frames =
        snapshot.frame_valid && snapshot.end_frame >= snapshot.start_frame
            ? snapshot.end_frame - snapshot.start_frame
            : 0;
    snapshot.duration =
        snapshot.frame_valid
            ? static_cast<double>(snapshot.duration_frames) / 30.0
            : std::max(0.0, damage_tracker_.game_time - damage_tracker_.start_time);
    snapshot.total_actual = damage_tracker_.total_actual;
    snapshot.total_calculated = damage_tracker_.total_calculated;
    snapshot.total_healing = damage_tracker_.total_healing;
    snapshot.total_regen = damage_tracker_.total_regen;
    snapshot.total_taken = damage_tracker_.total_taken;
    snapshot.elemental_buildup = damage_tracker_.elemental_buildup;
    snapshot.types = damage_tracker_.types;
    snapshot.actors.reserve(damage_tracker_.actors.size());
    for (const auto& [id, actor] : damage_tracker_.actors) {
        DamageActorInfo info;
        info.id = id;
        info.name = id == kNoSourceActorId ? "无来源" : operator_name(id);
        info.actual = actor.actual;
        info.calculated = actor.calculated;
        info.healing = actor.healing;
        info.healing_received = actor.healing_received;
        info.regen = actor.regen;
        info.taken = actor.taken;
        info.hp_start = actor.hp_start;
        info.hp_current = actor.hp_current;
        info.hp_max = actor.hp_max;
        info.minus_hp = actor.minus_hp;
        info.max_minus_hp = actor.max_minus_hp;
        info.hp_effective_start = actor.hp_effective_start;
        info.hp_effective_current = actor.hp_effective_current;
        info.hp_tracked = actor.hp_tracked;
        info.hp_locked = actor.hp_locked;
        info.hp_event_delta = actor.hp_event_delta;
        info.hp_delta_events = actor.hp_delta_events;
        info.hp_delta_fallbacks = actor.hp_delta_fallbacks;
        info.elemental_buildup = actor.elemental_buildup;
        info.types = actor.types;
        info.curve_points = actor.curve.size();
        if (include_curves) {
            info.curve = actor.curve;
            info.healing_curve = actor.healing_curve;
            info.taken_curve = actor.taken_curve;
        }
        snapshot.actors.push_back(std::move(info));
    }
    std::sort(snapshot.actors.begin(), snapshot.actors.end(),
              [](const DamageActorInfo& left, const DamageActorInfo& right) {
                  return left.actual == right.actual ? left.id < right.id
                                                     : left.actual > right.actual;
              });
    if (include_curves) {
        snapshot.series = damage_tracker_.series;
        snapshot.curves_included = true;
    }
    snapshot.series_points = damage_tracker_.series.total.size();
    return snapshot;
}


void LocalBattle::load_catalog() {
    try {
        void* db_class = runtime_.find("Torappu", "CharacterDB");
        void* database = runtime_.singleton(db_class);
        Root database_root(runtime_, database);
        void* get_ids = runtime_.method(db_class, "get_allCharacterIds", 0);
        void* get_value = runtime_.method(db_class, "GetValueOrDefault", 1);
        void* ids = runtime_.invoke(get_ids, database);
        Root ids_root(runtime_, ids);
        const int count = runtime_.count(ids);
        std::vector<OperatorEntry> catalog;
        catalog.reserve(static_cast<std::size_t>(std::max(count, 0)));

        void* try_get_char_name = nullptr;
        try {
            void* character_util = classes_.character_util;
            if (character_util) {
                try_get_char_name = runtime_.method(
                    character_util, "TryGetCharName", 2,
                    {"System.String", "System.String&"});
            }
        } catch (...) {
            // Fall back to the token and patch databases below.
        }

        void* token_instance = nullptr;
        try {
            token_instance = runtime_.singleton(runtime_.find("Torappu", "TokenDB"));
        } catch (...) {
        }
        Root token_root(runtime_, token_instance);

        void* patch_chars = nullptr;
        try {
            void* patch_db = runtime_.singleton(runtime_.find("Torappu", "CharPatchDB"));
            if (patch_db) {
                void* patch_data = runtime_.call(patch_db, "get_data");
                if (patch_data) {
                    patch_chars = runtime_.get<void*>(patch_data, "patchChars");
                }
            }
        } catch (...) {
        }
        Root patch_root(runtime_, patch_chars);

        const auto resolve_name = [&](void* key) {
            void* value = nullptr;
            if (try_get_char_name) {
                try {
                    if (runtime_.unbox<bool>(
                            runtime_.invoke(try_get_char_name, nullptr, {key, &value})) &&
                        value) {
                        return runtime_.string(value);
                    }
                } catch (...) {
                }
            }
            if (token_root.get()) {
                value = nullptr;
                try {
                    if (runtime_.unbox<bool>(
                            runtime_.call(token_root.get(), "TryGetName", {key, &value})) &&
                        value) {
                        return runtime_.string(value);
                    }
                } catch (...) {
                }
            }
            if (patch_root.get()) {
                value = nullptr;
                try {
                    if (runtime_.unbox<bool>(
                            runtime_.call(patch_root.get(), "TryGetValue", {key, &value})) &&
                        value) {
                        void* name = runtime_.get<void*>(value, "name");
                        if (name) {
                            return runtime_.string(name);
                        }
                    }
                } catch (...) {
                }
            }
            return std::string{};
        };

        for (int index = 0; index < count; ++index) {
            void* id_object = runtime_.at(ids, index);
            if (!id_object) {
                continue;
            }
            Root id_root(runtime_, id_object);
            const std::string id = runtime_.string(id_object);
            if (id.empty()) {
                continue;
            }
            OperatorEntry entry;
            entry.id = id;
            entry.name = resolve_name(id_object);
            try {
                void* data = runtime_.invoke(get_value, database, {id_object});
                if (data) {
                    Root data_root(runtime_, data);
                    void* data_class = runtime_.object_class(data);
                    read_field_deep(runtime_, data, data_class, "maxPotentialLevel",
                                    &entry.max_potential);
                    void* skills = nullptr;
                    if (read_field_deep(runtime_, data, data_class, "skills", &skills) && skills) {
                        entry.skill_count = std::max(runtime_.count(skills), 1);
                    }
                }
            } catch (...) {
                // One bad data row must not discard the rest of the operator catalog.
            }
            if (entry.name.empty()) {
                entry.name = entry.id;
            }
            catalog.push_back(std::move(entry));
        }

        std::sort(catalog.begin(), catalog.end(), [](const OperatorEntry& left,
                                                     const OperatorEntry& right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.id < right.id;
        });

        {
            std::lock_guard lock(mutex_);
            operators_ = std::move(catalog);
            operator_index_.clear();
            for (std::size_t index = 0; index < operators_.size(); ++index) {
                operator_index_[operators_[index].id] = index;
            }
            catalog_error_.clear();
            last_result_ = "干员目录已加载：" + std::to_string(operators_.size()) + " 条";
            last_ok_ = true;
        }
        publish_catalog();
        write_status_file();
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        catalog_error_ = error.what();
        last_result_ = std::string("干员目录加载失败：") + error.what();
        last_ok_ = false;
        log(kLogWarning, last_result_);
    }
    publish_catalog();
    write_status_file();
}

void LocalBattle::resolve_stage(const std::string& stage_id) {
    StageInfo info;
    info.id = stage_id;
    try {
        void* util = classes_.stage_data_util;
        void* stage_class = classes_.stage_data;
        void* method = runtime_.method(util, "GetStageOrNull", 1);
        void* text = runtime_.string_new(stage_id.c_str());
        Root text_root(runtime_, text);
        void* stage = runtime_.invoke(method, nullptr, {text});
        if (!stage) {
            throw std::runtime_error("关卡不存在或当前不可解析");
        }
        Root stage_root(runtime_, stage);
        info.code = read_string_field(runtime_, stage, stage_class, "code");
        info.name = read_string_field(runtime_, stage, stage_class, "name");
        info.level_id = read_string_field(runtime_, stage, stage_class, "levelId");
        info.valid = true;
        publish_result("关卡 " + (info.code.empty() ? stage_id : info.code) +
                       (info.name.empty() ? "" : " " + info.name) + "，level=" + info.level_id);
    } catch (const std::exception& error) {
        publish_result(std::string("关卡解析失败：") + error.what(), false);
    }
    std::lock_guard lock(mutex_);
    scene_.stage = std::move(info);
    scene_.stage_request = scene_.stage.id;
}

void LocalBattle::start_battle(const BattleStart& start) {
    if (start.stage.empty()) {
        publish_result("关卡 ID 为空", false);
        return;
    }
    if (start.squad.empty() || start.squad.size() > 12) {
        publish_result("编队必须包含 1 到 12 名干员", false);
        return;
    }
    try {
        remember_previous_controller_for_restart();
        void* stage_util = classes_.stage_data_util;
        void* stage_class = classes_.stage_data;
        void* get_stage = runtime_.method(stage_util, "GetStageOrNull", 1);
        void* stage_id = runtime_.string_new(start.stage.c_str());
        Root stage_id_root(runtime_, stage_id);
        void* stage = runtime_.invoke(get_stage, nullptr, {stage_id});
        if (!stage) {
            throw std::runtime_error("StageDataUtil.GetStageOrNull 返回 null");
        }
        Root stage_root(runtime_, stage);
        const std::string stage_code =
            read_string_field(runtime_, stage, stage_class, "code");
        const std::string stage_name =
            read_string_field(runtime_, stage, stage_class, "name");
        const std::string level_id = read_string_field(runtime_, stage, stage_class, "levelId");
        if (level_id.empty()) {
            throw std::runtime_error("StageData.levelId 为空");
        }
        {
            std::lock_guard lock(mutex_);
            scene_.stage.id = start.stage;
            scene_.stage.code = stage_code;
            scene_.stage.name = stage_name;
            scene_.stage.level_id = level_id;
            scene_.stage.valid = true;
            scene_.stage_request = start.stage;
            scene_.stage_pending = true;
        }

        void* level_utils = classes_.level_utils;
        void* load_level = runtime_.method(level_utils, "LoadLevel", 1);
        void* level_text = runtime_.string_new(level_id.c_str());
        Root level_text_root(runtime_, level_text);
        void* level_data = runtime_.invoke(load_level, nullptr, {level_text});
        if (!level_data) {
            throw std::runtime_error("LevelUtils.LoadLevel 返回 null");
        }
        Root level_root(runtime_, level_data);

        void* query_class = classes_.char_query;
        void* inst_class = classes_.advanced_character_inst;
        void* simple_char = runtime_.method(query_class, "SimpleChar", 1);
        void* inst_ctor = runtime_.method(inst_class, ".ctor", 5);
        void* pair_list_class = runtime_.class_from_type(runtime_.method_param(inst_ctor, 4));
        if (!pair_list_class) {
            throw std::runtime_error("AdvancedCharacterInst 模组列表类型不可用");
        }
        void* pair_list_ctor = runtime_.method(pair_list_class, ".ctor", 0);
        void* pair_list_add = runtime_.method(pair_list_class, "Add", 1);
        const UniqueEquipPairLayout pair_layout =
            resolve_unique_equip_pair(runtime_, inst_class);
        if (!pair_layout.valid()) {
            throw std::runtime_error("UniqueEquipPair 字段不可用");
        }

        void* converter = classes_.battle_data_converter;
        void* convert = runtime_.method(converter, "ConvertToBattlePlayerData", 8);
        void* runtime_convert =
            runtime_.method(converter, "ConvertToRuntimeCharacterData", 4,
                            {"System.Collections.Generic.List<Torappu.AdvancedCharacterInst>",
                             "System.Boolean", "System.Boolean", "Torappu.PlayerSide"});
        void* inst_list_class =
            runtime_.class_from_type(runtime_.method_param(runtime_convert, 0));
        void* inst_list_ctor = runtime_.method(inst_list_class, ".ctor", 0);
        void* inst_list_add = runtime_.method(inst_list_class, "Add", 1);
        void* instances = construct_with_args(runtime_, inst_list_class, inst_list_ctor, {});
        Root instances_root(runtime_, instances);

        for (const SquadSlot& slot : start.squad) {
            void* char_text = runtime_.string_new(slot.id.c_str());
            Root char_text_root(runtime_, char_text);
            void* query = runtime_.invoke(simple_char, nullptr, {char_text});
            if (!query) {
                throw std::runtime_error("CharQuery.SimpleChar 返回 null: " + slot.id);
            }
            Root query_root(runtime_, query);
            void* pair_list =
                construct_with_args(runtime_, pair_list_class, pair_list_ctor, {});
            if (!pair_list) {
                throw std::runtime_error("模组列表分配失败: " + slot.id);
            }
            Root pair_list_root(runtime_, pair_list);
            if (!slot.module_id.empty()) {
                void* module_text = runtime_.string_new(slot.module_id.c_str());
                Root module_text_root(runtime_, module_text);
                void* pair = box_zero(runtime_, pair_layout.klass);
                if (!pair) {
                    throw std::runtime_error("模组条目分配失败: " + slot.id);
                }
                Root pair_root(runtime_, pair);
                runtime_.field_set_object(pair, pair_layout.id_field, module_text);
                int module_level = std::clamp(slot.module_level, 1, 3);
                runtime_.field_set(pair, pair_layout.level_field, &module_level);
                runtime_.invoke(pair_list_add, pair_list,
                                {runtime_.object_unbox(pair)});
            }
            int level = slot.level;
            int evolve = slot.evolve;
            int potential = slot.potential;
            void* query_value = runtime_.object_unbox(query);
            if (!query_value) {
                throw std::runtime_error("CharQuery 无法拆箱: " + slot.id);
            }
            void* instance = runtime_.object_new(inst_class);
            if (!instance) {
                throw std::runtime_error("AdvancedCharacterInst 分配失败: " + slot.id);
            }
            Root instance_root(runtime_, instance);
            runtime_.invoke(inst_ctor, instance,
                            {query_value, &level, &evolve, &potential, pair_list});
            const int skill_index = std::clamp(slot.skill_index, 0, 2);
            const int skill_level = std::clamp(slot.skill_level, 1, 10);
            write_field_deep(runtime_, instance, inst_class, "skillIndex", skill_index);
            write_field_deep(runtime_, instance, inst_class, "mainSkillLvl", skill_level);
            if (!write_character_inst_trust(runtime_, instance, inst_class,
                                            slot.trust)) {
                throw std::runtime_error("写入干员信赖失败: " + slot.id);
            }
            runtime_.invoke(inst_list_add, instances, {instance});
        }

        void* stage_info_class = classes_.battle_stage_info;
        void* create_stage_info = runtime_.method(stage_info_class, "Create", 1);
        void* stage_info = runtime_.invoke(create_stage_info, nullptr, {stage});
        if (!stage_info) {
            throw std::runtime_error("BattleStageInfo.Create 返回 null");
        }
        Root stage_info_root(runtime_, stage_info);

        void* in_out_class = classes_.battle_in_out;
        void* in_params_class = runtime_.nested(in_out_class, "InParams");
        void* in_params = box_zero(runtime_, in_params_class);
        Root in_params_root(runtime_, in_params);
        if (!in_params) {
            throw std::runtime_error("BattleInOut.InParams 分配失败");
        }

        void* player_list_field = runtime_.field(in_params_class, "playerDataList");
        void* player_list_class =
            runtime_.class_from_type(runtime_.field_type(player_list_field));
        void* player_list_ctor = runtime_.method(player_list_class, ".ctor", 0);
        void* player_list_add = runtime_.method(player_list_class, "Add", 1);
        void* player_list = construct_with_args(runtime_, player_list_class, player_list_ctor, {});
        Root player_list_root(runtime_, player_list);

        int difficulty = 0;
        int diff_group = 0;
        read_field_deep(runtime_, stage, stage_class, "difficulty", &difficulty);
        read_field_deep(runtime_, stage, stage_class, "diffGroup", &diff_group);
        void* menu_style_class = classes_.battle_sys_menu_style;
        void* side_class = classes_.player_side;
        const int menu_style = runtime_.static_value<int>(menu_style_class, "NO_REWARD");
        int player_side = runtime_.static_value<int>(side_class, "DEFAULT");
        if (start.game_mode.valid) {
            player_side = start.game_mode.player_side;
        }

        void* rune_table = classes_.rune_table;
        void* rune_input_class = runtime_.nested(rune_table, "PackedRuneInput");
        void* rune_input_ctor = runtime_.method(rune_input_class, ".ctor", 0);
        void* rune_input =
            construct_with_args(runtime_, rune_input_class, rune_input_ctor, {});
        Root rune_input_root(runtime_, rune_input);
        void* rune_list_class =
            runtime_.class_from_type(runtime_.field_type(runtime_.field(rune_input_class, "runes")));
        void* rune_list_ctor = runtime_.method(rune_list_class, ".ctor", 0);
        void* rune_list = construct_with_args(runtime_, rune_list_class, rune_list_ctor, {});
        Root rune_list_root(runtime_, rune_list);
        runtime_.set<void*>(rune_input, "runes", rune_list);

        void* mode_class = classes_.game_mode_meta;
        void* mode_default = runtime_.field(mode_class, "DEFAULT");
        void* mode_value = box_zero(runtime_, mode_class);
        Root mode_value_root(runtime_, mode_value);
        if (!mode_value || !runtime_.object_unbox) {
            throw std::runtime_error("GameModeMeta.DEFAULT 分配失败");
        }
        runtime_.field_static_get(mode_default, runtime_.object_unbox(mode_value));
        if (start.game_mode.valid) {
            const std::uint8_t mode_type =
                static_cast<std::uint8_t>(start.game_mode.mode_type);
            write_field_deep(runtime_, mode_value, mode_class, "modeType",
                             mode_type);
            runtime_.set<int>(mode_value, "playerSide",
                              start.game_mode.player_side);
        }
        if (tas_mode_.load(std::memory_order_acquire) ||
            replay_active_.load(std::memory_order_acquire)) {
            runtime_.set<bool>(mode_value, "allowManualTick", false);
            runtime_.set<bool>(mode_value, "isDeterministic", true);
        } else if (start.game_mode.valid) {
            runtime_.set<bool>(mode_value, "allowManualTick",
                               start.game_mode.allow_manual_tick);
            runtime_.set<bool>(mode_value, "isDeterministic",
                               start.game_mode.is_deterministic);
        }

        static unsigned counter = 0;
        const std::string battle_id = "local-trial-" + std::to_string(GetCurrentProcessId()) +
                                      "-" + std::to_string(++counter);
        void* battle_id_string = runtime_.string_new(battle_id.c_str());
        Root battle_id_root(runtime_, battle_id_string);

        runtime_.set<void*>(in_params, "battleId", battle_id_string);
        runtime_.set<void*>(in_params, "levelData", level_data);
        runtime_.set<void*>(in_params, "playerDataList", player_list);
        runtime_.set_value(in_params, "stageInfo", stage_info);
        runtime_.set<bool>(in_params, "isPractise", true);
        runtime_.set<int>(in_params, "difficulty", difficulty);
        runtime_.set<int>(in_params, "diffGroup", diff_group);
        runtime_.set<int>(in_params, "sysMenuStyle", menu_style);
        runtime_.set<void*>(in_params, "runeInput", rune_input);
        runtime_.set_nullable_value(in_params, "gameModeMeta", mode_value);

        unsigned char reserved = 0;
        unsigned char add_player = 1;
        unsigned char tail = 0;
        void* player_data = runtime_.invoke(
            convert, nullptr,
            {instances, level_data, &difficulty, nullptr, &reserved, &player_side,
             &add_player, &tail});
        if (!player_data) {
            throw std::runtime_error("ConvertToBattlePlayerData 返回 null");
        }
        Root player_data_root(runtime_, player_data);
        runtime_.invoke(player_list_add, player_list, {player_data});

        void* in_out = runtime_.singleton(in_out_class);
        Root in_out_root(runtime_, in_out);
        void* set_in_params = runtime_.method(in_out_class, "SetInParams", 1);
        void* in_params_value = runtime_.object_unbox(in_params);
        if (!in_params_value) {
            throw std::runtime_error("InParams 无法拆箱");
        }
        runtime_.invoke(set_in_params, in_out, {in_params_value});

        void* flow_class = classes_.game_flow_controller;
        void* options_class = runtime_.nested(flow_class, "Options");
        void* flow = runtime_.singleton(flow_class);
        Root flow_root(runtime_, flow);
        void* options = box_zero(runtime_, options_class);
        Root options_root(runtime_, options);
        runtime_.set<bool>(options, "stopMusic", true);
        void* mode_field = runtime_.field(options_class, "mode");
        void* mode_enum = runtime_.class_from_type(runtime_.field_type(mode_field));
        const int loading_mode = runtime_.static_value<int>(mode_enum, "LOADING_MODE");
        runtime_.set<int>(options, "mode", loading_mode);

        const std::string scene = scene_constant(runtime_, "BATTLE_LOADER");
        if (scene.empty()) {
            throw std::runtime_error("SceneConsts.BATTLE_LOADER 为空");
        }
        void* scene_text = runtime_.string_new(scene.c_str());
        Root scene_root(runtime_, scene_text);
        void* start_scene = runtime_.method(flow_class, "StartScene", 2);
        void* options_value = runtime_.object_unbox(options);
        void* started = runtime_.invoke(start_scene, flow, {scene_text, options_value});
        const bool accepted = started && runtime_.unbox<bool>(started);
        if (!accepted) {
            throw std::runtime_error("GameFlowController.StartScene(" + scene + ") 返回 false");
        }
        finish_bypassed_ = false;
        finish_home_pending_.store(false, std::memory_order_release);
        local_battle_active_.store(true, std::memory_order_release);
        activate_pending_timeline();
        clear_static_cache();
        perf_ = PerfStats{};
        {
            std::lock_guard lock(mutex_);
            profiles_.clear();
            profiles_dirty_ = true;
        }
        publish_result("已提交本地战斗：" + start.stage + "，干员 " +
                       std::to_string(start.squad.size()) + " 名，battleId=" + battle_id);
    } catch (const std::exception& error) {
        publish_result(std::string("启动战斗失败：") + error.what(), false);
    }
}

}  // namespace ark::better_battle
