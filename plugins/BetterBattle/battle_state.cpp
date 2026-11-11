#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::refresh_battle() {
    const std::uint64_t refresh_started = perf_now_us();
    BattleSnapshot next;
    std::vector<TraceUnit> new_trace_units;
    try {
        const std::uint64_t discover_started = perf_now_us();
        void* controller = scene_.controller;
        if (controller) {
            void* manager = nullptr;
            read_field_deep(runtime_, controller, classes_.battle_controller,
                            "m_objectManger", &manager);
            if (!manager) {
                clear_scene();
                controller = nullptr;
            }
        }
        if (!controller) {
            std::vector<void*> controllers =
                find_controllers(runtime_, classes_.battle_controller);
            if (controllers.empty()) {
                {
                    std::lock_guard lock(record_mutex_);
                    timeline_controller_absent_ = true;
                }
                clear_scene();
                next.error = "当前不在战斗场景";
                publish_web_snapshot(next);
                publish_snapshot(std::move(next));
                perf_.refresh.add(perf_now_us() - refresh_started);
                return;
            }
            adopt_scene(controllers.front());
            controller = scene_.controller;
        }
        if (card_scene_revision_ != scene_.revision) {
            card_scene_revision_ = scene_.revision;
            card_objects_ready_ = false;
            card_roots_.clear();
        }
        if (scene_.stage.level_id.empty()) {
            scene_.stage.level_id = method_string(
                runtime_, controller, classes_.battle_controller, "get_levelId");
        }
        if (!scene_.side_ready) {
            scene_.player_side = player_side_value(
                runtime_, controller, classes_.battle_controller);
            scene_.side_ready = true;
        }
        update_enemy_focus(controller);
        perf_.discover.add(perf_now_us() - discover_started);

        next.available = true;
        next.state =
            method_int(runtime_, controller, classes_.battle_controller, "get_state", -1);
        next.playing =
            method_bool(runtime_, controller, classes_.battle_controller, "get_isPlaying");
        next.paused =
            method_bool(runtime_, controller, classes_.battle_controller, "get_isPaused");
        if (next.state == 3) {
            next.phase = "ended";
        } else if (replay_active_.load(std::memory_order_acquire)) {
            next.phase = "replay";
        } else if (next.state == 1) {
            next.phase = "starting";
        } else if (next.state == 2) {
            next.phase = next.paused ? "paused" : "playing";
        } else if (next.playing) {
            next.phase = "playing";
        }
        next.stage_id = scene_.stage.level_id;
        next.epoch = static_cast<std::uint32_t>(scene_.revision);
        {
            std::lock_guard lock(mutex_);
            next.stage_input = scene_.stage.id;
            next.stage_code = scene_.stage.code;
            next.stage_name = scene_.stage.name;
        }
        next.frame = static_cast<std::uint32_t>(
            method_int(runtime_, controller, classes_.battle_controller, "get_fixedFrameCnt", 0));
        if (perf_last_frame_valid_ && next.frame >= perf_last_frame_) {
            perf_.last_frame_delta = next.frame - perf_last_frame_;
            perf_.frame_delta_total += perf_.last_frame_delta;
            ++perf_.frame_delta_samples;
        }
        perf_last_frame_ = next.frame;
        perf_last_frame_valid_ = true;
        double controller_game_time = static_cast<double>(next.frame) / 30.0;
        read_fixed_play_time(runtime_, classes_.battle_controller, &controller_game_time);

        const int side = scene_.player_side;
        void* get_cost =
            find_method_optional(runtime_, classes_.battle_controller, "GetCost", 1);
        if (get_cost) {
            int side_value = side;
            void* boxed = runtime_.invoke(get_cost, controller, {&side_value});
            next.cost = boxed ? runtime_.unbox<int>(boxed) : -1;
        }
        sample_world_events(controller, next.frame, next.cost);

        const std::uint64_t cards_started = perf_now_us();
        if (!card_objects_ready_ && classes_.deck_card) {
            void* decks = nullptr;
            read_field_deep(runtime_, controller, classes_.battle_controller, "m_deckDict",
                            &decks);
            if (decks) {
                Root decks_root(runtime_, decks);
                const int deck_count = runtime_.count(decks);
                for (int deck_index = 0; deck_index < deck_count; ++deck_index) {
                    void* deck = listdict_value_at(runtime_, decks, deck_index);
                    if (!deck) {
                        continue;
                    }
                    Root deck_root(runtime_, deck);
                    void* cards = runtime_.call(deck, "get_cards");
                    if (!cards) {
                        continue;
                    }
                    Root cards_root(runtime_, cards);
                    const int card_count = runtime_.count(cards);
                    for (int card_index = 0; card_index < card_count; ++card_index) {
                        if (void* card = runtime_.at(cards, card_index)) {
                            card_roots_.emplace_back(runtime_, card);
                        }
                    }
                }
            }
            card_objects_ready_ = !card_roots_.empty();
        }
        for (const Root& card_root : card_roots_) {
            void* card = card_root.get();
            if (!card) {
                continue;
            }
            try {
                CardInfo info;
                const auto cached = card_static_cache_.find(card);
                if (cached != card_static_cache_.end()) {
                    ++perf_.card_hits;
                    info = cached->second;
                } else {
                    ++perf_.card_misses;
                    void* data = nullptr;
                    read_field_deep(runtime_, card, classes_.deck_card, "m_data", &data);
                    if (data) {
                        Root data_root(runtime_, data);
                        void* data_class = runtime_.object_class(data);
                        info.id = read_string_field(runtime_, data, data_class, "prefabKey");
                        read_field_deep(runtime_, data, data_class, "level", &info.level);
                        read_field_deep(runtime_, data, data_class, "evolvePhase", &info.evolve);
                        read_field_deep(runtime_, data, data_class, "potentialRank",
                                        &info.potential);
                        read_field_deep(runtime_, data, data_class, "mainSkillIndex",
                                        &info.skill_index);
                        read_field_deep(runtime_, data, data_class, "uniqueId", &info.uid);
                        read_talents_cached(data, data_class, info.potential, info.talents);
                        read_modules_from_data(runtime_, data, data_class, info.modules);
                        card_static_cache_.emplace(card, info);
                        if (info.uid) {
                            try {
                                ensure_card_profile(data, info);
                            } catch (...) {
                            }
                        }
                    }
                }
                read_field_deep(runtime_, card, classes_.deck_card, "m_remainingCnt",
                                &info.remaining);
                info.respawn_remaining = method_fp(
                    runtime_, card, classes_.deck_card,
                    "get_respawnRemainingTime");
                info.respawn_progress = method_fp(
                    runtime_, card, classes_.deck_card,
                    "get_respawnProgress");
                read_field_deep(runtime_, card, classes_.deck_card,
                                "<state>k__BackingField", &info.state);
                info.cost =
                    method_int(runtime_, card, classes_.deck_card, "get_cost", -1);
                info.ready =
                    method_bool(runtime_, card, classes_.deck_card, "get_readyToSpawn");
                info.hidden =
                    method_bool(runtime_, card, classes_.deck_card, "get_isHidden");
                info.deployed = !info.ready && !info.hidden && info.remaining == 0;
                try {
                    void* dont_occupy = find_method_optional(
                        runtime_, classes_.deck_card,
                        "get_dontOccupyDeployCnt", 0);
                    if (dont_occupy) {
                        info.dont_occupy_deploy_cnt = runtime_.unbox<bool>(
                            runtime_.invoke(dont_occupy, card));
                    }
                } catch (...) {
                }
                next.cards.push_back(std::move(info));
            } catch (...) {
            }
        }
        perf_.cards.add(perf_now_us() - cards_started);

        const std::uint64_t units_started = perf_now_us();
        void* manager = nullptr;
        read_field_deep(runtime_, controller, classes_.battle_controller, "m_objectManger",
                        &manager);
        Root manager_root(runtime_, manager);
        if (manager) {
            void* manager_class = runtime_.object_class(manager);
            void* list = nullptr;
            read_field_deep(runtime_, manager, manager_class, "m_cachedList", &list);
            Root list_root(runtime_, list);
            ++buff_cache_generation_;
            if (!buff_cache_generation_) {
                buff_cache_generation_ = 1;
            }
            const int count = runtime_.count(list);
            for (int index = 0; index < count && index < 4096; ++index) {
                void* object = runtime_.at(list, index);
                if (!object ||
                    !runtime_.class_assignable(
                        classes_.character,
                        runtime_.object_class(object))) {
                    continue;
                }
                try {
                    UnitInfo info;
                    read_field_deep(runtime_, object, classes_.battle_object,
                                    "<instanceUid>k__BackingField", &info.uid);
                    const auto cached = unit_static_cache_.find(info.uid);
                    if (info.uid && cached != unit_static_cache_.end()) {
                        ++perf_.unit_hits;
                        info = cached->second;
                    } else {
                        ++perf_.unit_misses;
                        info.id =
                            method_string(runtime_, object, classes_.entity, "get_id");
                        info.tmpl_id = method_string(runtime_, object, classes_.entity,
                                                   "get_tmplId");
                        read_field_deep(runtime_, object, classes_.character,
                                        "<cardUid>k__BackingField", &info.card_uid);
                        void* host_uid_method = find_method_optional(
                            runtime_, classes_.character,
                            "get_tokenOrHostUid", 0);
                        if (host_uid_method) {
                            info.owner_uid = runtime_.unbox<std::uint32_t>(
                                runtime_.invoke(host_uid_method, object));
                        }
                        info.mine =
                            method_bool(runtime_, object, classes_.entity, "get_isMine");
                        info.has_skill = method_bool(runtime_, object, classes_.character,
                                                     "get_hasSkill");
                        read_character_talents_cached(object, classes_.character,
                                                      info.talents);
                        read_character_modules(runtime_, object, classes_.character,
                                               info.modules);
                        if (info.uid) {
                            unit_static_cache_.emplace(info.uid, info);
                        }
                    }
                    std::int64_t hp_raw = 0;
                    read_field_deep(runtime_, object, classes_.entity, "m_hp",
                                    &hp_raw);
                    info.hp = static_cast<double>(hp_raw) / kFixedPointScale;
                    info.minus_hp =
                        method_fp(runtime_, object, classes_.entity, "get_minusHp");
                    info.max_minus_hp =
                        method_fp(runtime_, object, classes_.entity, "get_maxMinusHp");
                    info.hp_effective = info.hp;
                    void* max_hp_method =
                        find_method_optional(runtime_, classes_.entity, "get_maxHp", 0);
                    if (max_hp_method) {
                        info.max_hp =
                            unbox_fp(runtime_, runtime_.invoke(max_hp_method, object));
                    }
                    info.shield =
                        method_fp(runtime_, object, classes_.entity,
                                  "get_shieldToShow");
                    if (info.mine && info.shield <= 0) {
                        try {
                            void* container = nullptr;
                            if (read_field_deep(
                                    runtime_, object, classes_.entity,
                                    "<buffContainer>k__BackingField", &container) &&
                                container) {
                                void* calculate = find_method_optional(
                                    runtime_, runtime_.object_class(container),
                                    "CalculateShieldValue", 0);
                                if (calculate) {
                                    info.shield = unbox_fp(
                                        runtime_,
                                        runtime_.invoke(calculate, container));
                                }
                            }
                        } catch (...) {
                        }
                    }
                    info.alive =
                        method_bool(runtime_, object, classes_.entity, "get_alive");
                    info.sp = method_fp(runtime_, object, classes_.entity, "get_sp");
                    info.max_sp =
                        method_int(runtime_, object, classes_.entity, "get_maxSp");
                    info.atk = method_fp(runtime_, object, classes_.entity, "get_atk");
                    info.def = method_fp(runtime_, object, classes_.entity, "get_def");
                    info.magic_resistance =
                        method_fp(runtime_, object, classes_.entity,
                                  "get_magicResistance");
                    info.attack_speed =
                        method_fp(runtime_, object, classes_.entity, "get_attackSpeed");
                    info.move_speed =
                        method_float(runtime_, object, classes_.entity, "get_moveSpeed");
                    info.block_count =
                        method_int(runtime_, object, classes_.entity, "get_blockCnt", 0);
                    info.unit_cost =
                        method_int(runtime_, object, classes_.entity, "get_cost", -1);
                    info.in_skill = method_bool(runtime_, object, classes_.character,
                                                "get_isInSkillState");
                    info.current_state_id =
                        method_int(runtime_, object, classes_.entity,
                                   "get_currentStateId", -1);
                    info.skill_activatable =
                        method_bool(runtime_, object, classes_.entity,
                                    "get_isSkillActivatable");
                    void* trace_skill = nullptr;
                    if (info.has_skill) {
                        void* skill = runtime_.invoke(
                            runtime_.method(classes_.character, "get_skill", 0), object);
                        if (skill) {
                            Root skill_root(runtime_, skill);
                            trace_skill = skill;
                            void* skill_class = runtime_.object_class(skill);
                            info.sp_enough =
                                method_bool(runtime_, skill, skill_class, "get_spEnough");
                            info.skill_affecting =
                                method_bool(runtime_, skill, skill_class, "get_isAffecting");
                            info.skill_begin_anim = method_bool(
                                runtime_, skill, skill_class, "get_hasPlayedBeginAnim");
                            info.skill_remaining = method_fp(
                                runtime_, skill, skill_class, "get_remainingTime");
                            info.skill_trigger_count = method_int(
                                runtime_, skill, skill_class, "get_triggerCnt", 0);
                            info.skill_overload_skill = method_bool(
                                runtime_, skill, skill_class, "get_isOverloadSkill");
                            info.skill_overloaded = method_bool(
                                runtime_, skill, skill_class, "get_isOverloaded");
                            info.skill_retriggerable = method_bool(
                                runtime_, skill, skill_class, "isRetriggerable");
                            info.skill_ammo_remaining = method_int(
                                runtime_, skill, skill_class, "get_remainingCount", -1);
                            info.skill_ammo_max = method_int(
                                runtime_, skill, skill_class, "get_maxCount", -1);
                            void* is_op_triggerable = find_method_optional(
                                runtime_, skill_class, "IsOpTriggerable", 1);
                            if (is_op_triggerable) {
                                int side_value = side;
                                void* result = runtime_.invoke(
                                    is_op_triggerable, skill, {&side_value});
                                info.skill_op_triggerable =
                                    result && runtime_.unbox<bool>(result);
                            }
                        }
                    }
                    info.withdrawable =
                        method_bool(runtime_, object, classes_.character,
                                    "get_withdrawable");
                    info.manually_withdrawable = method_bool(
                        runtime_, object, classes_.character,
                        "get_manuallyWithdrawable");
                    if (info.mine) {
                        // Module selection is already in the squad snapshot. Reading the private
                        // UniqueEquipPair value type back from a live entity is unnecessary here.
                        try {
                            const std::uint64_t buffs_started = perf_now_us();
                            read_entity_buffs(runtime_, object, classes_.entity,
                                              classes_.buff, buff_static_cache_,
                                              buff_cache_generation_, perf_.buff_hits,
                                              perf_.buff_misses, info.buffs,
                                              &info.shield_layers);
                            perf_.buffs.add(perf_now_us() - buffs_started);
                        } catch (...) {
                        }
                        try {
                            ensure_unit_profile(object, info);
                        } catch (...) {
                        }
                    }
                    if (!info.mine) {
                        continue;
                    }
                    next.units.push_back(std::move(info));
                    if (trace_active_) {
                        new_trace_units.emplace_back(
                            runtime_, object, trace_skill, next.units.back().uid,
                            next.units.back().card_uid);
                    }
                } catch (...) {
                }
            }
        }
        prune_buff_static_cache();
        perf_.units.add(perf_now_us() - units_started);
        const std::uint64_t enemies_started = perf_now_us();
        refresh_enemies(controller, next);
        refresh_global_buffs(controller, next);
        refresh_rogue_context(controller, next);
        maybe_queue_auto_rogue_capture(controller);
        update_timeline_rogue_display(next);
        perf_.units.add(perf_now_us() - enemies_started);
        if (trace_active_) {
            trace_units_ = std::move(new_trace_units);
        }
        const std::uint64_t damage_started = perf_now_us();
        for (const UnitInfo& unit : next.units) {
            if (unit.mine) {
                battle_capture_.seed_unit_health(unit.id, unit.hp,
                                                 unit.hp_effective,
                                                 unit.minus_hp);
            }
        }
        poll_battle_events();
        update_unit_health_ledger(next.units, next.frame);
        if (damage_tracker_.started) {
            damage_tracker_.game_time =
                std::max(damage_tracker_.game_time, controller_game_time);
            if (!damage_tracker_.frame_valid ||
                next.frame >= damage_tracker_.current_frame) {
                damage_tracker_.current_frame = next.frame;
                damage_tracker_.frame_valid = true;
            }
        }
        const std::uint64_t damage_now = GetTickCount64();
        const bool include_damage_curves = damage_now >= next_damage_series_tick_;
        next.damage = damage_snapshot(include_damage_curves);
        if (include_damage_curves) {
            next_damage_series_tick_ = damage_now + 250;
            damage_series_published_revision_ = damage_tracker_.series_revision;
        }
        next.damage.series_revision = damage_series_published_revision_;
        perf_.damage.add(perf_now_us() - damage_started);
        next.epoch = static_cast<std::uint32_t>(scene_.revision);
    } catch (const std::exception& error) {
        next.available = false;
        next.error = error.what();
    }
    note_timeline_frame(next.frame);
    publish_web_snapshot(next);
    publish_snapshot(std::move(next));
    const std::uint64_t now = GetTickCount64();
    if (now >= next_status_tick_) {
        next_status_tick_ =
            now + status_interval_ms_.load(std::memory_order_relaxed);
        write_status_file(false);
    }
    perf_.refresh.add(perf_now_us() - refresh_started);
}

void LocalBattle::update_unit_health_ledger(const std::vector<UnitInfo>& units,
                                            std::uint32_t frame) {
    (void)frame;
    for (const UnitInfo& unit : units) {
        if (!unit.mine || unit.id.empty()) {
            continue;
        }
        DamageActorInfo& actor = damage_tracker_.actors[unit.id];
        actor.id = unit.id;
        if (!actor.hp_tracked || actor.hp_uid != unit.uid) {
            actor.hp_uid = unit.uid;
            actor.hp_start = unit.hp;
            actor.hp_effective_start = unit.hp_effective;
            actor.hp_tracked = true;
            actor.hp_locked = false;
        }
        if (!actor.hp_locked) {
            actor.hp_start = unit.hp;
            actor.hp_effective_start = unit.hp_effective;
        }
        actor.hp_current = unit.hp;
        actor.hp_max = unit.max_hp;
        actor.minus_hp = unit.minus_hp;
        actor.max_minus_hp = unit.max_minus_hp;
        actor.hp_effective_current = unit.hp_effective;
    }
}

void LocalBattle::list_tiles(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* deck = nullptr;
        void* card = find_card(runtime_, controller, controller_class, uid, &deck);
        if (!card) {
            throw std::runtime_error("编队中找不到卡牌 UID " + std::to_string(uid));
        }
        Root card_root(runtime_, card);
        Root deck_root(runtime_, deck);

        void* map = nullptr;
        void* get_map = find_method_optional(runtime_, controller_class, "get_map", 0);
        if (get_map) {
            map = runtime_.invoke(get_map, controller);
        }
        if (!map) {
            read_field_deep(runtime_, controller, controller_class, "_map", &map);
        }
        if (!map) {
            throw std::runtime_error("BattleController 地图不可读");
        }
        Root map_root(runtime_, map);
        void* map_class = runtime_.object_class(map);
        void* tiles_2d = nullptr;
        if (!read_field_deep(runtime_, map, map_class, "_tiles", &tiles_2d) || !tiles_2d) {
            throw std::runtime_error("Map._tiles 不可读");
        }
        Root tiles_root(runtime_, tiles_2d);
        void* tiles_class = runtime_.object_class(tiles_2d);
        int width = 0;
        int height = 0;
        read_field_deep(runtime_, tiles_2d, tiles_class, "_width", &width);
        read_field_deep(runtime_, tiles_2d, tiles_class, "_height", &height);
        if (width <= 0 || height <= 0 || width > 64 || height > 64) {
            throw std::runtime_error("地图尺寸无效：" + std::to_string(width) + "x" +
                                     std::to_string(height));
        }

        void* get_tile = find_method_optional(runtime_, map_class, "GetTile", 2);
        void* deck_class = classes_.deck;
        void* card_class = runtime_.nested(deck_class, "Card");
        void* check_buildable =
            find_method_optional(runtime_, card_class, "CheckBuildable", 4);
        if (!get_tile || !check_buildable) {
            throw std::runtime_error("Map.GetTile 或 Card.CheckBuildable 不存在");
        }

        bool check_host = true;
        bool manually = true;
        bool ignore_mask = false;
        std::string coordinates;
        int count = 0;
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                void* tile = runtime_.invoke(get_tile, map, {&row, &col});
                if (!tile) {
                    continue;
                }
                Root tile_root(runtime_, tile);
                void* result = runtime_.invoke(
                    check_buildable, card,
                    {tile, &check_host, &manually, &ignore_mask});
                if (!result || !runtime_.unbox<bool>(result)) {
                    continue;
                }
                ++count;
                if (count > 48) {
                    continue;
                }
                if (!coordinates.empty()) {
                    coordinates += " ";
                }
                coordinates += std::to_string(row) + "," + std::to_string(col);
            }
        }
        if (!count) {
            throw std::runtime_error(
                "UID " + std::to_string(uid) + " 没有任何 Card.CheckBuildable 可用格");
        }
        publish_result("UID " + std::to_string(uid) + " 可用格 " +
                       std::to_string(count) + " 个" +
                       (count > 48 ? "（只列前 48 个）" : "") + "：" + coordinates);
    } catch (const std::exception& error) {
        publish_result(std::string("格子扫描失败：") + error.what(), false);
    }
}

void LocalBattle::deploy(std::uint32_t uid, int row, int col, int direction) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* deck_class = classes_.deck;
        void* card_class = runtime_.nested(deck_class, "Card");
        void* deck = nullptr;
        void* card = find_card(runtime_, controller, controller_class, uid, &deck);
        if (!card) {
            throw std::runtime_error("编队中找不到卡牌 UID " + std::to_string(uid));
        }
        Root card_root(runtime_, card);
        Root deck_root(runtime_, deck);
        void* tile = find_tile(runtime_, controller, controller_class, row, col);
        if (!tile) {
            throw std::runtime_error("格子不存在或 Map.GetTile 失败");
        }
        Root tile_root(runtime_, tile);

        int direction = std::clamp(direction, 0, 3);
        bool strict = false;
        bool manually = true;
        bool freely = true;
        bool ignore_mask = true;
        bool force_spawn = false;
        bool ignore_host = false;
        bool force_reduce = false;
        bool dont_update = false;
        void* spawned = nullptr;

        std::string route;
        bool accepted = false;
        std::uint32_t uid = 0;
        void* data = nullptr;
        if (read_field_deep(runtime_, card, card_class, "m_data", &data) && data) {
            Root data_root(runtime_, data);
            read_field_deep(runtime_, data, runtime_.object_class(data), "uniqueId", &uid);
        }
        if (uid) {
            void* player_op = runtime_.method(controller_class, "PlayerOp_Spawn", 3);
            void* op_result =
                runtime_.invoke(player_op, controller, {&uid, &direction, tile});
            accepted = op_result && runtime_.unbox<bool>(op_result);
            if (accepted) {
                route = "PlayerOp_Spawn";
            }
        }

        void* spawn_card = runtime_.method(deck_class, "SpawnCharacterOrToken", 12);
        void* card_result = nullptr;
        if (!accepted) {
            card_result = runtime_.invoke(
                spawn_card, deck,
                {card, &direction, tile, &strict, &manually, &freely, &spawned, &ignore_mask,
                 &force_spawn, &ignore_host, &force_reduce, &dont_update});
            accepted = card_result && runtime_.unbox<bool>(card_result);
        }
        if (accepted) {
            if (route.empty()) {
                route = spawned ? "Card 重载（返回干员）" : "Card 重载";
            }
        }
        if (!accepted && uid) {
            void* uid_spawn = runtime_.method(deck_class, "SpawnCharacterOrToken", 8);
            void* uid_spawned = nullptr;
            void* uid_result = runtime_.invoke(
                uid_spawn, deck,
                {&uid, &direction, tile, &strict, &manually, &freely, &uid_spawned,
                 &ignore_mask});
            accepted = uid_result && runtime_.unbox<bool>(uid_result);
            if (accepted) {
                route = uid_spawned ? "UID 重载（返回干员）" : "UID 重载";
            }
        }
        const bool ready =
            method_bool(runtime_, card, card_class, "get_readyToSpawn");
        const bool ready_no_cost =
            method_bool(runtime_, card, card_class, "get_readyToSpawnWithoutCheckCost");
        const bool hidden = method_bool(runtime_, card, card_class, "get_isHidden");
        const int state = method_int(runtime_, card, card_class, "get_state");
        int remaining = -1;
        read_field_deep(runtime_, card, card_class, "m_remainingCnt", &remaining);
        const int cost = method_int(runtime_, card, card_class, "get_cost");
        if (!accepted) {
            throw std::runtime_error(
                "三种部署入口均被拒绝（ready=" + std::to_string(ready ? 1 : 0) +
                " readyNoCost=" + std::to_string(ready_no_cost ? 1 : 0) +
                " hidden=" + std::to_string(hidden ? 1 : 0) +
                " state=" + std::to_string(state) +
                " remaining=" + std::to_string(remaining) +
                " cost=" + std::to_string(cost) + " uid=" + std::to_string(uid) + "）");
        }
        publish_result("部署 UID " + std::to_string(uid) + " 到 " +
                       std::to_string(row) + "," + std::to_string(col) +
                       "，方向 " + std::to_string(direction) + "，经由 " + route);
    } catch (const std::exception& error) {
        publish_result(std::string("部署失败：") + error.what(), false);
    }
}

void LocalBattle::withdraw(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character = find_character(runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        bool ok = false;
        std::string route;
        if (void* player_method =
                find_method_optional(runtime_, controller_class, "PlayerOp_Withdraw", 1)) {
            void* accepted =
                runtime_.invoke(player_method, controllers.front(), {character});
            ok = accepted && runtime_.unbox<bool>(accepted);
            if (ok) {
                route = "BattleController.PlayerOp_Withdraw";
            }
        }
        void* character_class = classes_.character;
        void* method = runtime_.method(character_class, "Withdraw", 4);
        bool arguments[4] = {false, true, true, false};
        for (int index = 0; index < 4; ++index) {
            const char* raw_name =
                runtime_.method_param_name(method, static_cast<std::uint32_t>(index));
            if (!raw_name) {
                continue;
            }
            std::string name = raw_name;
            for (char& c : name) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (name.find("manual") != std::string::npos ||
                name.find("force") != std::string::npos) {
                arguments[index] = true;
            } else if (name.find("dead") != std::string::npos ||
                       name.find("auto") != std::string::npos) {
                arguments[index] = false;
            }
        }
        if (!ok) {
            void* accepted = runtime_.invoke(
                method, character,
                {&arguments[0], &arguments[1], &arguments[2], &arguments[3]});
            ok = accepted && runtime_.unbox<bool>(accepted);
            if (ok) {
                route = "Character.Withdraw";
            }
        }
        if (!ok) {
            throw std::runtime_error("PlayerOp_Withdraw / Character.Withdraw 返回 false");
        }
        publish_result("已撤退 UID " + std::to_string(uid) + "，经由 " + route);
    } catch (const std::exception& error) {
        publish_result(std::string("撤退失败：") + error.what(), false);
    }
}

void LocalBattle::trigger_skill(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* character = find_character(runtime_, controller, controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* character_class = classes_.character;
        void* entity_class = classes_.entity;
        if (!method_bool(runtime_, character, character_class, "get_hasSkill")) {
            throw std::runtime_error("该干员当前没有可用技能");
        }
        void* skill = runtime_.invoke(
            runtime_.method(character_class, "get_skill", 0), character);
        if (!skill) {
            throw std::runtime_error("Character.get_skill 返回 null");
        }
        Root skill_root(runtime_, skill);
        void* skill_class = runtime_.object_class(skill);
        const int side = player_side_value(runtime_, controller, controller_class);
        int side_value = side;

        const double current_sp = method_fp(runtime_, character, entity_class, "get_sp");
        const int max_sp = method_int(runtime_, character, entity_class, "get_maxSp", -1);
        const bool sp_enough =
            method_bool(runtime_, skill, skill_class, "get_spEnough");
        bool op_triggerable = false;
        if (void* is_op_triggerable =
                find_method_optional(runtime_, skill_class, "IsOpTriggerable", 1)) {
            void* result = runtime_.invoke(
                is_op_triggerable, skill, {&side_value});
            op_triggerable = result && runtime_.unbox<bool>(result);
        }
        const bool activatable =
            method_bool(runtime_, character, entity_class, "get_isSkillActivatable");

        bool ok = false;
        std::string route = "Character.OpTrigSkill";
        if (op_triggerable) {
            void* skill_id = runtime_.string_new("");
            Root skill_id_root(runtime_, skill_id);
            if (void* player_method =
                    find_method_optional(runtime_, controller_class, "PlayerOp_TrigSkill", 2)) {
                void* accepted =
                    runtime_.invoke(player_method, controller, {character, skill_id});
                ok = accepted && runtime_.unbox<bool>(accepted);
                if (ok) {
                    route = "BattleController.PlayerOp_TrigSkill";
                }
            }
            if (!ok) {
                void* method = runtime_.method(character_class, "OpTrigSkill", 2);
                void* accepted =
                    runtime_.invoke(method, character, {&side_value, skill_id});
                ok = accepted && runtime_.unbox<bool>(accepted);
            }
        }
        if (!ok) {
            throw std::runtime_error(
                "技能入口拒绝（spEnough=" + std::to_string(sp_enough ? 1 : 0) +
                " IsOpTriggerable=" + std::to_string(op_triggerable ? 1 : 0) +
                " isSkillActivatable=" + std::to_string(activatable ? 1 : 0) +
                " SP=" + std::to_string(current_sp) + "/" +
                std::to_string(max_sp) + "）");
        }
        publish_result("已触发 UID " + std::to_string(uid) + " 的技能，经由 " +
                       route + "，SP=" + std::to_string(current_sp) + "/" +
                       std::to_string(max_sp));
    } catch (const std::exception& error) {
        publish_result(std::string("技能失败：") + error.what(), false);
    }
}

void LocalBattle::inspect_skill(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character =
            find_character(runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* character_class = classes_.character;
        void* entity_class = classes_.entity;
        void* skill = runtime_.invoke(
            runtime_.method(character_class, "get_skill", 0), character);
        if (!skill) {
            throw std::runtime_error("Character.get_skill 返回 null");
        }
        Root skill_root(runtime_, skill);
        void* skill_class = runtime_.object_class(skill);
        const auto method_presence = [&](const char* name) {
            void* method = find_method_optional(runtime_, skill_class, name, 0);
            char text[32];
            std::snprintf(text, sizeof(text), "%s", method ? "yes" : "no");
            return std::string(text);
        };

        std::string detail =
            "UID " + std::to_string(uid) +
            " skill_type=" +
            (skill_class ? runtime_.klass_name(skill_class) : std::string("<null>")) +
            " m_cast=" + method_presence("get_isCasting") +
            " m_affect=" + method_presence("get_isAffecting") +
            " m_remaining=" + method_presence("get_remainingTime") +
            " m_trigger=" + method_presence("get_triggerCnt") +
            " m_anim=" + method_presence("get_hasPlayedBeginAnim") +
            " m_ready=" + method_presence("get_isReady") +
            " in_skill=" +
            std::to_string(method_bool(runtime_, character, character_class,
                                       "get_isInSkillState")
                               ? 1
                               : 0) +
            " state_id=" +
            std::to_string(method_int(runtime_, character, entity_class,
                                      "get_currentStateId", -1)) +
            " affecting=" +
            std::to_string(method_bool(runtime_, skill, skill_class, "get_isAffecting") ? 1 : 0) +
            " begin_anim=" +
            std::to_string(method_bool(runtime_, skill, skill_class,
                                       "get_hasPlayedBeginAnim")
                               ? 1
                               : 0) +
            " remaining=" + std::to_string(
                                method_fp(runtime_, skill, skill_class, "get_remainingTime")) +
            " trigger_cnt=" +
            std::to_string(method_int(runtime_, skill, skill_class, "get_triggerCnt", 0)) +
            " sp=" + std::to_string(method_fp(runtime_, character, entity_class, "get_sp")) +
            "/" + std::to_string(method_int(runtime_, character, entity_class, "get_maxSp", -1));

        void* skill_data = runtime_.invoke(
            runtime_.method(character_class, "get_skillData", 0), character);
        if (skill_data) {
            Root skill_data_root(runtime_, skill_data);
            void* data_class = runtime_.object_class(skill_data);
            const std::string skill_id =
                read_string_field(runtime_, skill_data, data_class, "skillId");
            const std::string skill_name =
                read_string_field(runtime_, skill_data, data_class, "name");
            float duration = 0;
            int skill_type = -1;
            int duration_type = -1;
            read_field_deep(runtime_, skill_data, data_class, "duration", &duration);
            read_field_deep(runtime_, skill_data, data_class, "skillType", &skill_type);
            read_field_deep(runtime_, skill_data, data_class, "durationType", &duration_type);
            detail += " skill_id=" + skill_id + " skill_name=" + skill_name +
                      " duration=" + std::to_string(duration) +
                      " type=" + std::to_string(skill_type) +
                      " duration_type=" + std::to_string(duration_type);
        }
        publish_result(detail);
    } catch (const std::exception& error) {
        publish_result(std::string("读取技能状态失败：") + error.what(), false);
    }
}

void LocalBattle::list_skill_methods(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character =
            find_character(runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* character_class = classes_.character;
        void* skill = runtime_.invoke(
            runtime_.method(character_class, "get_skill", 0), character);
        if (!skill) {
            throw std::runtime_error("Character.get_skill 返回 null");
        }
        Root skill_root(runtime_, skill);

        std::string detail;
        for (void* current = runtime_.object_class(skill); current;
             current = runtime_.class_parent(current)) {
            detail += "\nclass " + runtime_.klass_name(current);
            void* iterator = nullptr;
            while (void* method = runtime_.class_methods(current, &iterator)) {
                const char* raw_name = runtime_.method_name(method);
                if (!raw_name) {
                    continue;
                }
                const std::string name = raw_name;
                if (name.find("kill") == std::string::npos &&
                    name.find("ast") == std::string::npos &&
                    name.find("ffect") == std::string::npos &&
                    name.find("emain") == std::string::npos &&
                    name.find("rigger") == std::string::npos &&
                    name.find("eady") == std::string::npos &&
                    name.find("nim") == std::string::npos) {
                    continue;
                }
                detail += "\n  ";
                detail += name;
                detail += "(";
                for (std::uint32_t index = 0; index < runtime_.method_count(method); ++index) {
                    if (index) {
                        detail += ", ";
                    }
                    try {
                        detail += runtime_.type(runtime_.method_param(method, index));
                    } catch (...) {
                        detail += "?";
                    }
                }
                detail += ") -> ";
                try {
                    detail += runtime_.type(runtime_.method_return(method));
                } catch (...) {
                    detail += "?";
                }
                detail += " entry=";
                char pointer[32];
                std::snprintf(pointer, sizeof(pointer), "%p", *static_cast<void**>(method));
                detail += pointer;
            }
        }
        publish_result(detail);
    } catch (const std::exception& error) {
        publish_result(std::string("列出技能方法失败：") + error.what(), false);
    }
}

void LocalBattle::inspect_buff_fields(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character =
            find_character(runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* entity_class = classes_.entity;
        void* container = nullptr;
        if (!read_field_deep(runtime_, character, entity_class,
                             "<buffContainer>k__BackingField", &container) ||
            !container) {
            throw std::runtime_error("Entity.<buffContainer> 为空");
        }
        Root container_root(runtime_, container);
        void* container_class = runtime_.object_class(container);

        std::ostringstream output;
        output << "character=" << method_string(runtime_, character, entity_class, "get_id")
               << " cardUid=" << uid << '\n';
        output << "\n[BuffContainer]\n" << describe_class_layout(runtime_, container_class, true);
        for (const char* field_name : {"m_buffs", "m_buffUidMap", "m_buffOverrideMap"}) {
            void* field_object = nullptr;
            if (!read_field_deep(runtime_, container, container_class, field_name,
                                 &field_object) ||
                !field_object) {
                output << "\n[" << field_name << "] <null>\n";
                continue;
            }
            Root field_root(runtime_, field_object);
            output << "\n[" << field_name << "] "
                   << runtime_.klass_name(runtime_.object_class(field_object)) << '\n'
                   << describe_class_layout(runtime_, runtime_.object_class(field_object), true);
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_buff_fields.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("无法写入 battle_buff_fields.txt");
        }
        publish_result("Buff 字段布局已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("读取 Buff 字段失败：") + error.what(), false);
    }
}

void LocalBattle::dump_buffs(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character =
            find_character(runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error("找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* entity_class = classes_.entity;
        void* buff_class = classes_.buff;
        void* container = nullptr;
        if (!read_field_deep(runtime_, character, entity_class,
                             "<buffContainer>k__BackingField", &container) ||
            !container) {
            throw std::runtime_error("Entity.<buffContainer> 为空");
        }
        Root container_root(runtime_, container);
        void* container_class = runtime_.object_class(container);
        void* buffs = nullptr;
        read_field_deep(runtime_, container, container_class, "m_buffs", &buffs);
        std::vector<void*> objects;
        std::unordered_set<void*> seen;
        int budget = 512;
        int list_count = 0;
        if (buffs) {
            Root buffs_root(runtime_, buffs);
            void* buffs_class = runtime_.object_class(buffs);
            void* internal_list = nullptr;
            read_field_deep(runtime_, buffs, buffs_class, "m_internalList", &internal_list);
            if (internal_list) {
                Root internal_root(runtime_, internal_list);
                std::unordered_set<void*> list_seen;
                collect_buff_internal_list(runtime_, internal_list, buff_class, objects,
                                           list_seen);
            }
            list_count = static_cast<int>(objects.size());
        }
        void* uid_map = nullptr;
        read_field_deep(runtime_, container, container_class, "m_buffUidMap", &uid_map);
        if (uid_map) {
            Root uid_map_root(runtime_, uid_map);
            collect_buff_uid_map(runtime_, uid_map, buff_class, objects, seen, budget);
        }
        std::sort(objects.begin(), objects.end());
        objects.erase(std::unique(objects.begin(), objects.end()), objects.end());

        static constexpr const char* kAttributeNames[] = {
            "MAX_HP", "ATK", "DEF", "MAGIC_RESISTANCE", "COST", "BLOCK_CNT",
            "MOVE_SPEED", "ATTACK_SPEED", "BASE_ATTACK_TIME", "RESERVED_0",
            "RESERVED_1", "RESERVED_2", "RESERVED_3", "HP_RECOVERY_PER_SEC",
            "SP_RECOVERY_PER_SEC", "ABILITY_RANGE_FORWARD_EXTEND", "MAX_DEPLOY_COUNT",
            "DEF_PENETRATE", "MAGIC_RESIST_PENETRATE", "HP_RECOVERY_PER_SEC_BY_MAX_HP_RATIO",
            "TAUNT_LEVEL", "RESPAWN_TIME", "MAX_DECK_STACK_CNT", "MASS_LEVEL",
            "BASE_FORCE_LEVEL", "DEF_PENETRATE_FIXED", "ONE_MINUS_STATUS_RESISTANCE",
            "MAGIC_RESIST_PENETRATE_FIXED", "MAX_EP", "EP_RECOVERY_PER_SEC",
            "SP_RECOVER_RATIO", "EP_DAMAGE_RESISTANCE", "EP_RESISTANCE",
            "DAMAGE_HITRATE_PHYSICAL", "DAMAGE_HITRATE_MAGICAL", "EP_BREAK_RECOVER_SPEED",
            "SLOW_DOWN", "BLOCK_RADIUS_SCALE",
        };
        static constexpr const char* kAbnormalNames[] = {
            "STUNNED", "SP_RECOVER_STOPPED", "TARGET_FREE", "BLOCK_FREE", "HIDDEN",
            "INVINCIBLE", "UNDEADABLE", "HEAL_FREE", "UNBALANCE_IMMUNE", "INVISIBLE",
            "UNUSED_10", "DISARMED", "SILENCED", "UNMOVABLE", "UNUSED_14",
            "ALLY_TARGET_FREE", "FROZEN", "CAMOUFLAGE", "FORCE_DISARMED",
            "STUNNED_NO_AMPLIFY_DAMAGE", "DISABLE_COMBAT", "ELEMENT_FREE_ALL",
            "UNMOVABLE_PRIVATE", "COLD", "SKILL_NOT_ACTIVATABLE", "LEVITATE",
            "DURANCE", "NOT_WITHDRAWABLE", "OUT_OF_GROUND", "SP_MODIFY_STOPPED",
            "ANTI_STATUS_RESISTABLE", "DISARMED_COMBAT", "TOWER_TARGET_FREE", "FEARED",
            "SKILL_ACTIVABLE_IN_ABNORMAL", "MOTION_TARGET_FREE", "FORCE_LEVITATE",
            "BUFF_ADD_CAN_BE_CANCELED_IF_DEFENSE", "DEFENSE_BUFF_ADD_IF_CANCELABLE_BUFF",
            "PALSY", "PALSYING", "ATTRACTED", "FEARED_PRIVATE", "DOZE", "TELEPORTED",
            "GROUND_BOUND",
        };

        std::ostringstream output;
        const std::string character_id =
            method_string(runtime_, character, entity_class, "get_id");
        output << "cardUid=" << uid << " character=" << character_id
               << " buffs=" << objects.size() << " list=" << list_count << '\n';
        for (void* buff : objects) {
            Root buff_root(runtime_, buff);
            void* buff_class_runtime = runtime_.object_class(buff);
            auto raw_fp = [&](const char* name) {
                std::int64_t raw = 0;
                read_field_deep(runtime_, buff, buff_class, name, &raw);
                return static_cast<double>(raw) / kFixedPointScale;
            };
            auto read_bool = [&](const char* name) {
                bool value = false;
                read_field_deep(runtime_, buff, buff_class, name, &value);
                return value;
            };
            auto read_int = [&](const char* name) {
                int value = 0;
                read_field_deep(runtime_, buff, buff_class, name, &value);
                return value;
            };
            std::string key = read_string_field(
                runtime_, buff, buff_class, "<key>k__BackingField");
            std::string override_key = read_string_field(
                runtime_, buff, buff_class, "<overrideKey>k__BackingField");
            std::string effect_key = read_string_field(
                runtime_, buff, buff_class, "<effectKey>k__BackingField");
            std::string template_key;
            int max_stack_count = 0;
            void* data = nullptr;
            if (read_field_deep(runtime_, buff, buff_class, "m_data", &data) && data) {
                void* data_class = runtime_.object_class(data);
                template_key =
                    read_string_field(runtime_, data, data_class, "templateKey");
                read_field_deep(runtime_, data, data_class, "maxStackCnt",
                                &max_stack_count);
            }
            const double life_time = raw_fp("m_lifeTime");
            const double remaining_time = raw_fp("m_remainingTime");
            const double existing_time = raw_fp("m_existingTime");
            const double remaining_ratio =
                method_fp(runtime_, buff, buff_class_runtime, "get_remainingRatio");
            const double trigger_interval =
                method_fp(runtime_, buff, buff_class_runtime, "get_triggerInterval");
            const int stack_count = read_int("m_stackCnt");
            const int valid_stack_count =
                method_int(runtime_, buff, buff_class_runtime, "get_validStackCnt",
                           stack_count);
            const int trigger_count =
                method_int(runtime_, buff, buff_class_runtime, "get_triggerCnt");
            const bool enabled =
                method_bool(runtime_, buff, buff_class_runtime, "get_isEnabled");
            const bool finished =
                method_bool(runtime_, buff, buff_class_runtime, "get_isFinished");
            const bool valid =
                method_bool(runtime_, buff, buff_class_runtime, "get_isValid", true);
            const bool triggerable =
                method_bool(runtime_, buff, buff_class_runtime, "get_triggerable");
            const bool status_resistable =
                method_bool(runtime_, buff, buff_class_runtime,
                            "get_isStatusResistable");
            const bool has_shield =
                read_bool("<hasShield>k__BackingField");
            std::vector<std::string> modifier_parts;
            const auto collect_modifiers =
                [&](const char* field_name, const char* format, double identity,
                    auto value_formatter) {
                    void* values = nullptr;
                    if (!read_field_deep(runtime_, buff, buff_class, field_name,
                                         &values) ||
                        !values) {
                        return;
                    }
                    Root values_root(runtime_, values);
                    int count = 0;
                    try {
                        count = runtime_.count(values);
                    } catch (...) {
                        return;
                    }
                    for (int index = 0;
                         index < count &&
                         index < static_cast<int>(std::size(kAttributeNames));
                         ++index) {
                        void* boxed = nullptr;
                        try {
                            boxed = runtime_.at(values, index);
                        } catch (...) {
                            continue;
                        }
                        const double value = unbox_fp(runtime_, boxed);
                        if (std::abs(value - identity) < 1e-9) {
                            continue;
                        }
                        char text[96]{};
                        std::snprintf(text, sizeof(text), format,
                                      kAttributeNames[index], value_formatter(value));
                        modifier_parts.push_back(text);
                    }
                };
            collect_modifiers("m_attributeAdditions", "%s+%.4f", 0.0,
                              [](double value) { return value; });
            collect_modifiers("m_attributeMultipliers", "%s%.2f%%", 0.0,
                              [](double value) { return value * 100.0; });
            collect_modifiers("m_attributeFinalAdditions", "%s+%.4f(final)", 0.0,
                              [](double value) { return value; });
            collect_modifiers(
                "m_attributeFinalScalers", "%s*%.4f(final)", 1.0,
                [](double value) { return value >= 0 ? value : 1.0 + value; });

            const auto mask_names = [&](std::int64_t mask,
                                        const char* const* names,
                                        std::size_t count) {
                std::vector<std::string> parts;
                const std::uint64_t bits = static_cast<std::uint64_t>(mask);
                for (std::size_t index = 0; index < count; ++index) {
                    if (bits & (std::uint64_t{1} << index)) {
                        parts.emplace_back(names[index]);
                    }
                }
                return join_nonempty(parts, ", ");
            };
            const auto read_mask = [&](const char* field) {
                std::int64_t mask = 0;
                read_field_deep(runtime_, buff, buff_class, field, &mask);
                return mask;
            };
            const std::int64_t abnormal_flag_mask =
                read_mask("<abnormalFlagMask>k__BackingField");
            const std::int64_t abnormal_immune_mask =
                read_mask("<abnormalImmuneMask>k__BackingField");
            const std::int64_t abnormal_anti_mask =
                read_mask("<abnormalAntiMask>k__BackingField");
            const std::int64_t abnormal_combo_mask =
                read_mask("<abnormalComboMask>k__BackingField");
            const std::int64_t abnormal_combo_immune_mask =
                read_mask("<abnormalComboImmuneMask>k__BackingField");
            const std::string abnormal_flags =
                mask_names(abnormal_flag_mask, kAbnormalNames,
                           std::size(kAbnormalNames));
            const std::string abnormal_immune =
                mask_names(abnormal_immune_mask, kAbnormalNames,
                           std::size(kAbnormalNames));
            const std::string abnormal_anti =
                mask_names(abnormal_anti_mask, kAbnormalNames,
                           std::size(kAbnormalNames));
            const std::string abnormal_combo =
                mask_names(abnormal_combo_mask, kBuffAbnormalComboNames,
                           std::size(kBuffAbnormalComboNames));
            const std::string abnormal_combo_immune =
                mask_names(abnormal_combo_immune_mask,
                           kBuffAbnormalComboNames,
                           std::size(kBuffAbnormalComboNames));
            const std::string blackboard =
                format_buff_blackboard(runtime_, buff, buff_class);
            const std::uint32_t instance_uid = static_cast<std::uint32_t>(
                read_int("<instanceUid>k__BackingField"));
            const int priority = read_int("<priority>k__BackingField");
            const int event_priority = read_int("<onEventPriority>k__BackingField");
            char header[1024]{};
            std::snprintf(header, sizeof(header),
                          "uid=%u key=%s template=%s override=%s priority=%d event=%d "
                          "enabled=%d finished=%d valid=%d triggerable=%d resistable=%d "
                          "life=%.4f remaining=%.4f ratio=%.4f existing=%.4f "
                          "stack=%d validStack=%d maxStack=%d triggerCnt=%d "
                          "triggerInterval=%.4f hasShield=%d effect=%s",
                          instance_uid, key.c_str(), template_key.c_str(),
                          override_key.c_str(), priority, event_priority, enabled ? 1 : 0,
                          finished ? 1 : 0, valid ? 1 : 0, triggerable ? 1 : 0,
                          status_resistable ? 1 : 0, life_time, remaining_time,
                          remaining_ratio, existing_time, stack_count, valid_stack_count,
                          max_stack_count, trigger_count, trigger_interval,
                          has_shield ? 1 : 0, effect_key.c_str());
            output << header << '\n';
            if (!modifier_parts.empty()) {
                output << "  modifiers: " << join_nonempty(modifier_parts, ", ") << '\n';
            }
            if (!abnormal_flags.empty()) {
                output << "  abnormal: " << abnormal_flags << '\n';
            }
            if (!abnormal_immune.empty()) {
                output << "  abnormalImmune: " << abnormal_immune << '\n';
            }
            if (!abnormal_anti.empty()) {
                output << "  abnormalAnti: " << abnormal_anti << '\n';
            }
            if (!abnormal_combo.empty()) {
                output << "  abnormalCombo: " << abnormal_combo << '\n';
            }
            if (!abnormal_combo_immune.empty()) {
                output << "  abnormalComboImmune: " << abnormal_combo_immune
                       << '\n';
            }
            if (!blackboard.empty()) {
                output << "  blackboard: " << blackboard << '\n';
            }
        }

        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_buffs.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("无法写入 battle_buffs.txt");
        }
        publish_result("Buff 状态已写入 " + path.string() + "，共 " +
                       std::to_string(objects.size()) + " 个");
    } catch (const std::exception& error) {
        publish_result(std::string("读取 Buff 状态失败：") + error.what(), false);
    }
}

void LocalBattle::dispatch_buff_post_born(std::uint32_t uid, const std::string& path) {
    try {
        void* controller_class =
            classes_.battle_controller;
        std::vector<void*> controllers =
            find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* character = find_character(
            runtime_, controllers.front(), controller_class, uid);
        if (!character) {
            throw std::runtime_error(
                "找不到 UID " + std::to_string(uid) + " 的干员");
        }
        Root character_root(runtime_, character);
        void* entity_class = classes_.entity;
        void* buff_class = classes_.buff;
        void* container = nullptr;
        if (!read_field_deep(runtime_, character, entity_class,
                             "<buffContainer>k__BackingField", &container) ||
            !container) {
            throw std::runtime_error("Entity.<buffContainer> 为空");
        }
        Root container_root(runtime_, container);
        void* container_class = runtime_.object_class(container);
        void* buffs = nullptr;
        read_field_deep(runtime_, container, container_class, "m_buffs",
                        &buffs);
        std::vector<void*> objects;
        std::unordered_set<void*> seen;
        int budget = 512;
        if (buffs) {
            Root buffs_root(runtime_, buffs);
            void* buffs_class = runtime_.object_class(buffs);
            void* internal_list = nullptr;
            read_field_deep(runtime_, buffs, buffs_class, "m_internalList",
                            &internal_list);
            if (internal_list) {
                Root internal_root(runtime_, internal_list);
                std::unordered_set<void*> list_seen;
                collect_buff_internal_list(
                    runtime_, internal_list, buff_class, objects, list_seen);
            }
        }
        void* uid_map = nullptr;
        read_field_deep(runtime_, container, container_class, "m_buffUidMap",
                        &uid_map);
        if (uid_map) {
            Root uid_map_root(runtime_, uid_map);
            collect_buff_uid_map(runtime_, uid_map, buff_class, objects, seen,
                                 budget);
        }
        std::sort(objects.begin(), objects.end());
        objects.erase(std::unique(objects.begin(), objects.end()),
                      objects.end());

        std::size_t matched = 0;
        std::size_t dispatched = 0;
        for (void* buff : objects) {
            Root buff_root(runtime_, buff);
            void* buff_runtime_class = runtime_.object_class(buff);
            const std::string key = read_string_field(
                runtime_, buff, buff_class, "<key>k__BackingField");
            if (!path.empty() && key != path) {
                continue;
            }
            ++matched;
            void* method =
                runtime_.method(buff_runtime_class, "OnOwnerPostBorn", 0);
            if (!method) {
                continue;
            }
            runtime_.invoke(method, buff, {});
            ++dispatched;
        }
        publish_result("Buff OnOwnerPostBorn 派发 " +
                       std::to_string(dispatched) + "/" +
                       std::to_string(matched) +
                       (path.empty() ? "" : " key=" + path));
    } catch (const std::exception& error) {
        publish_result(
            std::string("Buff OnOwnerPostBorn 派发失败：") + error.what(),
            false);
    }
}

void LocalBattle::refresh_global_buffs(void* controller,
                                       BattleSnapshot& snapshot) {
    snapshot.global_buffs.clear();
    if (!controller) {
        return;
    }
    try {
        void* list = nullptr;
        if (!read_field_deep(runtime_, controller,
                             runtime_.object_class(controller), "m_globalBuffs",
                             &list) ||
            !list) {
            return;
        }
        Root list_root(runtime_, list);
        const int count = std::min(runtime_.count(list), 64);
        for (int index = 0; index < count; ++index) {
            void* group = nullptr;
            try {
                group = runtime_.at(list, index);
            } catch (...) {
                continue;
            }
            if (!group) {
                continue;
            }
            Root group_root(runtime_, group);
            void* group_class = runtime_.object_class(group);
            GlobalBuffGroupInfo info;
            info.key = read_string_field(runtime_, group, group_class, "_key");
            if (info.key.empty()) {
                info.key = method_string(runtime_, group, group_class, "get_key");
            }
            read_field_deep(runtime_, group, group_class, "m_layerMask",
                            &info.layer_mask);
            read_field_deep(runtime_, group, group_class,
                            "<instanceUid>k__BackingField", &info.uid);
            void* source = nullptr;
            if (read_field_deep(runtime_, group, group_class, "m_source",
                                &source) &&
                source) {
                try {
                    info.source = runtime_.klass_name(
                        runtime_.object_class(source));
                } catch (...) {
                }
            }
            void* board = nullptr;
            if (read_field_deep(runtime_, group, group_class, "blackboard",
                                &board) &&
                board) {
                read_blackboard_entries(runtime_, board,
                                        info.blackboard_entries);
                info.blackboard =
                    format_buff_blackboard(info.blackboard_entries);
            }
            void* buffs = nullptr;
            if (read_field_deep(runtime_, group, group_class, "_buffs",
                                &buffs) &&
                buffs) {
                Root buffs_root(runtime_, buffs);
                const int buff_count = std::min(runtime_.count(buffs), 64);
                for (int buff_index = 0; buff_index < buff_count;
                     ++buff_index) {
                    void* data = nullptr;
                    try {
                        data = runtime_.at(buffs, buff_index);
                    } catch (...) {
                        continue;
                    }
                    if (!data) {
                        continue;
                    }
                    Root data_root(runtime_, data);
                    void* data_class = runtime_.object_class(data);
                    GlobalBuffEntryInfo entry;
                    entry.key = read_string_field(runtime_, data, data_class,
                                                  "buffKey");
                    entry.template_key = read_string_field(
                        runtime_, data, data_class, "templateKey");
                    entry.override_key = read_string_field(
                        runtime_, data, data_class, "overrideKey");
                    read_field_deep(runtime_, data, data_class, "maxStackCnt",
                                    &entry.max_stack);
                    read_field_deep(runtime_, data, data_class, "priority",
                                    &entry.priority);
                    float life_time = 0.0f;
                    read_field_deep(runtime_, data, data_class, "lifeTime",
                                    &life_time);
                    entry.life_time = static_cast<double>(life_time);
                    void* entry_board = nullptr;
                    if (read_field_deep(runtime_, data, data_class,
                                        "blackboard", &entry_board) &&
                        entry_board) {
                        read_blackboard_entries(runtime_, entry_board,
                                                entry.blackboard_entries);
                        entry.blackboard =
                            format_buff_blackboard(entry.blackboard_entries);
                    }
                    if (!entry.key.empty() || !entry.template_key.empty()) {
                        info.buffs.push_back(std::move(entry));
                    }
                }
            }
            if (!info.key.empty() || !info.buffs.empty()) {
                snapshot.global_buffs.push_back(std::move(info));
            }
        }
    } catch (...) {
        snapshot.global_buffs.clear();
    }
}

void LocalBattle::refresh_rogue_context(void* controller,
                                        BattleSnapshot& snapshot) {
    if (!scene_.rogue_checked ||
        (scene_.rogue &&
         (!timeline_rogue_capture_.captured ||
          !timeline_rogue_capture_.input_ready))) {
        capture_rogue_battle_meta(controller);
    }
    if (rogue_context_ready_ &&
        rogue_context_revision_ == snapshot.epoch) {
        snapshot.rogue = rogue_context_cache_;
        return;
    }
    snapshot.rogue = {};
    rogue_context_ready_ = false;
    if (!controller) {
        return;
    }
    try {
        void* mode = nullptr;
        if (!read_field_deep(runtime_, controller,
                             runtime_.object_class(controller), "m_gameMode",
                             &mode) ||
            !mode) {
            rogue_context_cache_ = snapshot.rogue;
            rogue_context_revision_ = snapshot.epoch;
            rogue_context_ready_ = true;
            return;
        }
        Root mode_root(runtime_, mode);
        void* input = nullptr;
        if (!read_field_deep(runtime_, mode, runtime_.object_class(mode),
                             "m_input", &input) ||
            !input) {
            rogue_context_cache_ = snapshot.rogue;
            rogue_context_revision_ = snapshot.epoch;
            rogue_context_ready_ = true;
            return;
        }
        Root input_root(runtime_, input);
        void* input_class = runtime_.object_class(input);
        void* meta = nullptr;
        if (read_field_deep(runtime_, input, input_class, "meta", &meta) &&
            meta) {
            snapshot.rogue.topic_id = read_string_field(
                runtime_, meta, runtime_.object_class(meta), "topicId");
        }
        void* relic_buffs = nullptr;
        if (!read_field_deep(runtime_, input, input_class, "relicBuffs",
                             &relic_buffs) ||
            !relic_buffs) {
            snapshot.rogue.valid = !snapshot.rogue.topic_id.empty();
            rogue_context_cache_ = snapshot.rogue;
            rogue_context_revision_ = snapshot.epoch;
            rogue_context_ready_ = true;
            return;
        }
        Root relic_buffs_root(runtime_, relic_buffs);
        const int count = std::min(runtime_.count(relic_buffs), 512);
        for (int index = 0; index < count; ++index) {
            void* relic_buff = nullptr;
            try {
                relic_buff = runtime_.at(relic_buffs, index);
            } catch (...) {
                continue;
            }
            if (!relic_buff) {
                continue;
            }
            Root relic_root(runtime_, relic_buff);
            void* relic_class = runtime_.object_class(relic_buff);
            void* relic_data = nullptr;
            if (!read_field_deep(runtime_, relic_buff, relic_class, "relicData",
                                 &relic_data) ||
                !relic_data) {
                continue;
            }
            Root data_root(runtime_, relic_data);
            void* data_class = runtime_.object_class(relic_data);
            RogueRelicInfo relic;
            relic.id = read_string_field(runtime_, relic_data, data_class, "id");
            read_field_deep(runtime_, relic_buff, relic_class, "layer",
                            &relic.layer);
            void* buffs = nullptr;
            if (read_field_deep(runtime_, relic_data, data_class, "buffs",
                                &buffs) &&
                buffs) {
                Root buffs_root(runtime_, buffs);
                const int buff_count = std::min(runtime_.count(buffs), 64);
                for (int buff_index = 0; buff_index < buff_count;
                     ++buff_index) {
                    void* buff = nullptr;
                    try {
                        buff = runtime_.at(buffs, buff_index);
                    } catch (...) {
                        continue;
                    }
                    if (!buff) {
                        continue;
                    }
                    void* buff_class = runtime_.object_class(buff);
                    const std::string key =
                        read_string_field(runtime_, buff, buff_class, "key");
                    if (!key.empty()) {
                        relic.buff_keys.push_back(key);
                    }
                    void* board = nullptr;
                    if (read_field_deep(runtime_, buff, buff_class,
                                        "blackboard", &board) &&
                        board) {
                        std::vector<BuffBlackboardEntry> entries;
                        read_blackboard_entries(runtime_, board, entries);
                        for (const BuffBlackboardEntry& entry : entries) {
                            if (entry.key == "key" && !entry.value_str.empty()) {
                                relic.buff_keys.push_back(entry.value_str);
                            }
                        }
                    }
                }
            }
            if (!relic.id.empty()) {
                snapshot.rogue.relics.push_back(std::move(relic));
            }
        }
        snapshot.rogue.valid =
            !snapshot.rogue.topic_id.empty() || !snapshot.rogue.relics.empty();
        rogue_context_cache_ = snapshot.rogue;
        rogue_context_revision_ = snapshot.epoch;
        rogue_context_ready_ = true;
    } catch (...) {
        snapshot.rogue = {};
        rogue_context_ready_ = false;
    }
}

}  // namespace ark::better_battle
