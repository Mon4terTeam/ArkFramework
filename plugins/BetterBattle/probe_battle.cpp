#include "BetterBattle.h"
#include "probe_support.h"

#include <nlohmann/json.hpp>

namespace ark::better_battle {

using namespace probe_support;
using ProbeJson = nlohmann::ordered_json;

void LocalBattle::probe_write(std::string line) {
    std::lock_guard lock(probe_queue_mutex_);
    if (probe_queue_.size() >= 65536) {
        ++probe_dropped_;
        return;
    }
    probe_queue_.push_back(std::move(line));
    probe_queue_ready_.notify_one();
}

void LocalBattle::probe_writer_loop(std::ofstream output) {
    for (;;) {
        std::deque<std::string> pending;
        {
            std::unique_lock lock(probe_queue_mutex_);
            probe_queue_ready_.wait(lock, [&] {
                return probe_writer_stop_ || !probe_queue_.empty();
            });
            if (probe_queue_.empty() && probe_writer_stop_) {
                break;
            }
            pending.swap(probe_queue_);
        }
        for (const std::string& line : pending) {
            output << line << '\n';
        }
        output.flush();
    }
    output.flush();
}

void LocalBattle::start_probe(int interval_frames) {
    const int interval = std::clamp(interval_frames, 1, 60);
    probe_interval_frames_.store(interval);
    probe_enabled_.store(true);
    if (probe_active_.load()) {
        publish_result("探针已运行，采样间隔改为 " + std::to_string(interval) + " 帧");
        return;
    }
    if (probe_writer_.joinable()) {
        close_probe();
    }
    const std::filesystem::path path =
        std::filesystem::path(host_.root_directory) / "battle_probe.jsonl";
    std::ofstream output(path, std::ios::out | std::ios::app);
    if (!output) {
        throw std::runtime_error("无法创建 battle_probe.jsonl");
    }
    {
        std::lock_guard lock(probe_queue_mutex_);
        probe_queue_.clear();
        probe_writer_stop_ = false;
        probe_dropped_ = 0;
    }
    probe_active_.store(true);
    probe_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    probe_has_frame_ = false;
    probe_has_tile_snapshot_ = false;
    probe_last_tile_frame_ = 0;
    probe_entities_.clear();
    probe_tiles_.clear();
    probe_writer_ = std::thread(&LocalBattle::probe_writer_loop, this,
                                std::move(output));
    publish_result("探针已启动：" + path.string() + "，间隔 " +
                   std::to_string(interval) + " 帧");
}

void LocalBattle::stop_probe() {
    probe_enabled_.store(false);
    const bool was_active = probe_active_.load();
    close_probe();
    publish_result(was_active ? "探针已停止" : "探针未运行");
}

void LocalBattle::close_probe() {
    probe_active_.store(false);
    {
        std::lock_guard lock(probe_queue_mutex_);
        probe_writer_stop_ = true;
        probe_queue_.clear();
    }
    probe_queue_ready_.notify_all();
    if (probe_writer_.joinable()) {
        probe_writer_.join();
    }
    probe_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    probe_has_frame_ = false;
    probe_entities_.clear();
    probe_tiles_.clear();
}

ProbeJson probe_entity_value(const ProbeEntity& entity) {
    ProbeJson output;
    output["key"] = entity.key;
    output["class"] = entity.class_name;
    output["id"] = entity.id;
    output["tmpl"] = entity.tmpl_id;
    output["uid"] = entity.uid;
    output["card"] = entity.card_uid;
    output["hp"] = entity.hp;
    output["max_hp"] = entity.max_hp;
    output["atk"] = entity.atk;
    output["def"] = entity.def;
    output["mr"] = entity.magic_resistance;
    output["as"] = entity.attack_speed;
    output["ms"] = entity.move_speed;
    output["sp"] = entity.sp;
    output["max_sp"] = entity.max_sp;
    output["block"] = entity.block_count;
    output["cost"] = entity.unit_cost;
    output["state"] = entity.state_id;
    output["mine"] = entity.mine;
    output["alive"] = entity.alive;
    output["tile"] = entity.tile_key;
    ProbeJson buffs = ProbeJson::array();
    for (const auto& [key, buff] : entity.buffs) {
        (void)key;
        buffs.push_back({
            {"key", buff.key},
            {"uid", buff.uid},
            {"stack", buff.stack_count},
            {"trigger", buff.trigger_count},
            {"remaining", buff.remaining_time},
            {"enabled", buff.enabled},
            {"finished", buff.finished},
            {"source", buff.source_id},
            {"override", buff.override_key},
            {"effect", buff.effect_key},
            {"template", buff.template_key},
            {"blackboard", buff.blackboard},
            {"abnormal", buff.abnormal_flags},
        });
    }
    output["buffs"] = std::move(buffs);
    return output;
}

ProbeJson probe_tile_value(const ProbeTile& tile) {
    return {
        {"key", tile.key},
        {"tile", tile.tile_key},
        {"effect", tile.effect},
        {"tile_types", tile.tile_types},
        {"tile_info", tile.tile_info},
        {"specs", tile.effect_specs},
        {"blackboard", tile.blackboard},
        {"shared", tile.shared_effects},
    };
}

std::string LocalBattle::probe_entity_json(const ProbeEntity& entity) const {
    return probe_entity_value(entity).dump();
}

std::string LocalBattle::probe_tile_json(const ProbeTile& tile) const {
    return probe_tile_value(tile).dump();
}

ProbeEntity LocalBattle::probe_read_entity(void* object) {
    ProbeEntity entity;
    void* klass = runtime_.object_class(object);
    entity.class_name = runtime_.klass_name(klass);
    void* entity_class = classes_.entity ? classes_.entity : klass;
    const auto read_string = [&](const char* field, const char* method) {
        std::string value;
        try {
            value = read_string_field(runtime_, object, entity_class, field);
            if (!value.empty()) {
                return value;
            }
        } catch (...) {
        }
        try {
            if (method) {
                value = method_string(runtime_, object, entity_class, method);
            }
        } catch (...) {
        }
        return value;
    };
    entity.id = read_string("<id>k__BackingField", "get_id");
    entity.tmpl_id = read_string("<tmplId>k__BackingField", "get_tmplId");
    try {
        read_field_deep(runtime_, object, entity_class,
                        "<instanceUid>k__BackingField", &entity.uid);
    } catch (...) {
        try {
            read_field_deep(runtime_, object, entity_class, "m_instanceUid",
                            &entity.uid);
        } catch (...) {
        }
    }
    if (!entity.uid) {
        entity.uid = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(object) & 0xffffffffu);
    }
    entity.key = "u:" + std::to_string(entity.uid);
    try {
        read_field_deep(runtime_, object, entity_class,
                        "<cardUid>k__BackingField", &entity.card_uid);
    } catch (...) {
    }
    entity.hp = method_fp(runtime_, object, entity_class, "get_hp");
    entity.max_hp = method_fp(runtime_, object, entity_class, "get_maxHp");
    entity.atk = method_fp(runtime_, object, entity_class, "get_atk");
    entity.def = method_fp(runtime_, object, entity_class, "get_def");
    entity.magic_resistance =
        method_fp(runtime_, object, entity_class, "get_magicResistance");
    entity.attack_speed =
        method_fp(runtime_, object, entity_class, "get_attackSpeed");
    entity.move_speed =
        method_float(runtime_, object, entity_class, "get_moveSpeed", 0);
    entity.sp = method_fp(runtime_, object, entity_class, "get_sp");
    entity.max_sp =
        method_int(runtime_, object, entity_class, "get_maxSp", 0);
    entity.block_count =
        method_int(runtime_, object, entity_class, "get_blockCnt", 0);
    entity.unit_cost =
        method_int(runtime_, object, entity_class, "get_cost", -1);
    entity.state_id =
        method_int(runtime_, object, entity_class, "get_currentStateId", -1);
    entity.mine = method_bool(runtime_, object, entity_class, "get_isMine");
    entity.alive = method_bool(runtime_, object, entity_class, "get_alive");
    void* origin_tile = nullptr;
    try {
        read_field_deep(runtime_, object, entity_class,
                        "<originTile>k__BackingField", &origin_tile);
    } catch (...) {
    }
    if (origin_tile) {
        try {
            entity.tile_key = read_string_field(
                runtime_, origin_tile, runtime_.object_class(origin_tile),
                "_tileKey");
        } catch (...) {
        }
    }
    if (classes_.buff) {
        entity.buffs = collect_probe_buffs(runtime_, object, entity_class,
                                           classes_.buff);
    }
    return entity;
}

ProbeTile LocalBattle::probe_read_tile(void* tile, int row, int col) {
    ProbeTile result;
    result.key = std::to_string(row) + ":" + std::to_string(col);
    void* tile_class = runtime_.object_class(tile);
    try {
        result.tile_key = read_string_field(runtime_, tile, tile_class, "_tileKey");
    } catch (...) {
        try {
            result.tile_key =
                method_string(runtime_, tile, tile_class, "get_tileKey");
        } catch (...) {
        }
    }
    try {
        result.effect = read_string_field(runtime_, tile, tile_class, "_effect");
    } catch (...) {
    }
    try {
        read_field_deep(runtime_, tile, tile_class, "m_tileTypes", &result.tile_types);
    } catch (...) {
    }
    try {
        read_field_deep(runtime_, tile, tile_class, "m_tileInfo", &result.tile_info);
    } catch (...) {
    }
    const auto blackboard = [&](void* board) {
        std::string text;
        if (!board) {
            return text;
        }
        void* pairs = nullptr;
        try {
            read_field_deep(runtime_, board, runtime_.object_class(board), "_items",
                            &pairs);
        } catch (...) {
            return text;
        }
        if (!pairs) {
            return text;
        }
        const int count = runtime_.count(pairs);
        for (int index = 0; index < count && index < 64; ++index) {
            void* pair = nullptr;
            try {
                pair = runtime_.at(pairs, index);
            } catch (...) {
                continue;
            }
            if (!pair) {
                continue;
            }
            void* pair_class = runtime_.object_class(pair);
            std::string key;
            std::string value_str;
            float value = 0.0f;
            try {
                key = read_string_field(runtime_, pair, pair_class, "key");
                read_field_deep(runtime_, pair, pair_class, "value", &value);
                value_str =
                    read_string_field(runtime_, pair, pair_class, "valueStr");
            } catch (...) {
            }
            if (!text.empty()) {
                text += ';';
            }
            text += key + "=" + std::to_string(value);
            if (!value_str.empty()) {
                text += "(" + value_str + ")";
            }
        }
        return text;
    };
    void* bb = nullptr;
    try {
        read_field_deep(runtime_, tile, tile_class, "m_blackboard", &bb);
    } catch (...) {
    }
    result.blackboard = blackboard(bb);
    void* shared = nullptr;
    try {
        read_field_deep(runtime_, tile, tile_class, "m_sharedEffects", &shared);
    } catch (...) {
    }
    if (shared) {
        try {
            for (const auto& pair : read_dictionary_pairs(runtime_, shared)) {
                if (!pair.key) {
                    continue;
                }
                if (!result.shared_effects.empty()) {
                    result.shared_effects += ';';
                }
                result.shared_effects += runtime_.string(pair.key);
            }
        } catch (...) {
        }
    }
    void* specs = nullptr;
    try {
        read_field_deep(runtime_, tile, tile_class, "m_tileEffectSpecs", &specs);
    } catch (...) {
    }
    if (specs) {
        try {
            result.effect_specs = runtime_.count(specs);
        } catch (...) {
            result.effect_specs = 0;
        }
    }
    return result;
}

void LocalBattle::probe_battle_start(void* controller) {
    ProbeJson output;
    output["event"] = "battle_start";
    output["time"] = GetTickCount64();
    output["battle"] = probe_battle_id_;
    output["controller"] =
        runtime_.klass_name(runtime_.object_class(controller));
    output["stage"] = scene_.stage.level_id;
    output["code"] = snapshot_.stage_code;
    output["name"] = snapshot_.stage_name;
    output["frame"] = probe_last_frame_;
    probe_write(output.dump());
    void* game_mode = nullptr;
    try {
        read_field_deep(runtime_, controller, classes_.battle_controller,
                        "m_gameMode", &game_mode);
    } catch (...) {
    }
    if (game_mode) {
        const std::string mode_name =
            runtime_.klass_name(runtime_.object_class(game_mode));
        ProbeJson mode_output;
        mode_output["event"] = "game_mode";
        mode_output["time"] = GetTickCount64();
        mode_output["battle"] = probe_battle_id_;
        mode_output["class"] = mode_name;
        int seed = -1;
        try {
            seed = method_int(runtime_, controller, classes_.battle_controller,
                              "get_randomSeed", -1);
        } catch (...) {
        }
        mode_output["seed"] = seed;
        probe_write(mode_output.dump());
    }
}

void LocalBattle::probe_snapshot(void* controller, bool force) {
    if (!controller || !probe_active_.load()) {
        return;
    }
    if (probe_scene_revision_ != scene_.revision) {
        probe_scene_revision_ = scene_.revision;
        ++probe_battle_id_;
        probe_entities_.clear();
        probe_tiles_.clear();
        probe_has_frame_ = false;
        probe_has_tile_snapshot_ = false;
        probe_battle_start(controller);
    }
    if (force) {
        probe_entities_.clear();
        probe_tiles_.clear();
    }
    ensure_frame_clock();
    std::uint32_t frame = probe_last_frame_;
    if (frame_clock_get_ && frame_clock_method_) {
        try {
            frame = frame_clock_get_(frame_clock_method_);
        } catch (...) {
        }
    }
    if (!probe_has_frame_) {
        probe_has_frame_ = true;
    } else {
        const std::uint32_t interval =
            static_cast<std::uint32_t>(std::max(1, probe_interval_frames_.load()));
        if (frame >= probe_last_frame_ && frame - probe_last_frame_ < interval) {
            return;
        }
    }
    probe_last_frame_ = frame;

    std::map<std::string, ProbeEntity> current_entities;
    void* manager = nullptr;
    try {
        read_field_deep(runtime_, controller, classes_.battle_controller,
                        "m_objectManger", &manager);
    } catch (...) {
    }
    if (manager) {
        Root manager_root(runtime_, manager);
        void* list = nullptr;
        try {
            read_field_deep(runtime_, manager_root.get(),
                            runtime_.object_class(manager_root.get()), "m_cachedList",
                            &list);
        } catch (...) {
        }
        if (list) {
            Root list_root(runtime_, list);
            const int count = runtime_.count(list_root.get());
            for (int index = 0; index < count; ++index) {
                void* object = nullptr;
                try {
                    object = runtime_.at(list_root.get(), index);
                } catch (...) {
                    continue;
                }
                if (!object) {
                    continue;
                }
                ProbeEntity entity;
                try {
                    Root object_root(runtime_, object);
                    object = object_root.get();
                    void* klass = runtime_.object_class(object);
                    bool is_entity = false;
                    for (int depth = 0; klass && depth < 64; ++depth) {
                        if (klass == classes_.entity) {
                            is_entity = true;
                            break;
                        }
                        klass = runtime_.class_parent(klass);
                    }
                    if (!is_entity) {
                        continue;
                    }
                    entity = probe_read_entity(object);
                } catch (...) {
                    continue;
                }
                const bool looks_like_entity =
                    !entity.tmpl_id.empty() ||
                    entity.class_name.find("Character") != std::string::npos ||
                    entity.class_name.find("Enemy") != std::string::npos ||
                    entity.class_name.find("Token") != std::string::npos;
                if (!looks_like_entity || entity.key.empty()) {
                    continue;
                }
                current_entities.emplace(entity.key, std::move(entity));
            }
        }
    }

    for (const auto& [key, entity] : current_entities) {
        const auto previous = probe_entities_.find(key);
        if (previous == probe_entities_.end()) {
            ProbeJson event;
            event["event"] = "entity_add";
            event["time"] = GetTickCount64();
            event["frame"] = frame;
            event["entity"] = probe_entity_value(entity);
            probe_write(event.dump());
            continue;
        }
        const ProbeEntity& old = previous->second;
        const bool changed =
            old.hp != entity.hp || old.sp != entity.sp ||
            old.state_id != entity.state_id || old.alive != entity.alive ||
            old.max_hp != entity.max_hp || old.atk != entity.atk ||
            old.def != entity.def || old.move_speed != entity.move_speed ||
            old.attack_speed != entity.attack_speed;
        if (changed) {
            ProbeJson event;
            event["event"] = "entity_change";
            event["time"] = GetTickCount64();
            event["frame"] = frame;
            event["entity"] = probe_entity_value(entity);
            probe_write(event.dump());
        }
        if (old.hp != entity.hp) {
            ProbeJson delta;
            delta["event"] = "hp_delta";
            delta["time"] = GetTickCount64();
            delta["frame"] = frame;
            delta["key"] = entity.key;
            delta["class"] = entity.class_name;
            delta["id"] = entity.id;
            delta["tmpl"] = entity.tmpl_id;
            delta["mine"] = entity.mine;
            delta["from"] = old.hp;
            delta["to"] = entity.hp;
            delta["delta"] = entity.hp - old.hp;
            probe_write(delta.dump());
        }
        for (const auto& [buff_key, buff] : old.buffs) {
            if (!entity.buffs.contains(buff_key)) {
                ProbeJson removed;
                removed["event"] = "buff_remove";
                removed["time"] = GetTickCount64();
                removed["frame"] = frame;
                removed["owner"] = entity.key;
                removed["buff_key"] = buff.key;
                removed["uid"] = buff.uid;
                probe_write(removed.dump());
            }
        }
        for (const auto& [buff_key, buff] : entity.buffs) {
            const auto old_buff = old.buffs.find(buff_key);
            if (old_buff == old.buffs.end()) {
                ProbeJson added;
                added["event"] = "buff_add";
                added["time"] = GetTickCount64();
                added["frame"] = frame;
                added["owner"] = entity.key;
                added["buff_key"] = buff.key;
                added["uid"] = buff.uid;
                added["stack"] = buff.stack_count;
                added["remaining"] = buff.remaining_time;
                added["source"] = buff.source_id;
                probe_write(added.dump());
            } else if (old_buff->second.stack_count != buff.stack_count ||
                       std::abs(old_buff->second.remaining_time -
                                buff.remaining_time) > 0.001) {
                ProbeJson changed_buff;
                changed_buff["event"] = "buff_change";
                changed_buff["time"] = GetTickCount64();
                changed_buff["frame"] = frame;
                changed_buff["owner"] = entity.key;
                changed_buff["buff_key"] = buff.key;
                changed_buff["uid"] = buff.uid;
                changed_buff["stack"] = buff.stack_count;
                changed_buff["remaining"] = buff.remaining_time;
                probe_write(changed_buff.dump());
            }
        }
    }
    for (const auto& [key, entity] : probe_entities_) {
        if (!current_entities.contains(key)) {
            ProbeJson removed;
            removed["event"] = "entity_remove";
            removed["time"] = GetTickCount64();
            removed["frame"] = frame;
            removed["key"] = entity.key;
            removed["class"] = entity.class_name;
            removed["id"] = entity.id;
            removed["tmpl"] = entity.tmpl_id;
            probe_write(removed.dump());
        }
    }
    probe_entities_ = std::move(current_entities);

    void* map = nullptr;
    void* get_map = find_method_optional(runtime_, classes_.battle_controller, "get_map", 0);
    if (get_map) {
        try {
            map = runtime_.invoke(get_map, controller);
        } catch (...) {
        }
    }
    if (!map) {
        try {
            read_field_deep(runtime_, controller, classes_.battle_controller, "_map",
                            &map);
        } catch (...) {
        }
    }
    const bool tile_due =
        force || !probe_has_tile_snapshot_ ||
        (frame >= probe_last_tile_frame_ &&
         frame - probe_last_tile_frame_ >= 30);
    if (map && tile_due) {
        probe_has_tile_snapshot_ = true;
        probe_last_tile_frame_ = frame;
        void* map_class = runtime_.object_class(map);
        void* tiles = nullptr;
        try {
            read_field_deep(runtime_, map, map_class, "_tiles", &tiles);
        } catch (...) {
        }
        if (tiles) {
            void* tiles_class = runtime_.object_class(tiles);
            int width = 0;
            int height = 0;
            try {
                read_field_deep(runtime_, tiles, tiles_class, "_width", &width);
                read_field_deep(runtime_, tiles, tiles_class, "_height", &height);
            } catch (...) {
            }
            void* get_tile =
                find_method_optional(runtime_, map_class, "GetTile", 2);
            std::map<std::string, ProbeTile> current_tiles;
            if (get_tile && width > 0 && height > 0 && width <= 64 && height <= 64) {
                for (int row = 0; row < height; ++row) {
                    for (int col = 0; col < width; ++col) {
                        void* tile = nullptr;
                        try {
                            tile = runtime_.invoke(get_tile, map, {&row, &col});
                        } catch (...) {
                        }
                        if (!tile) {
                            continue;
                        }
                        ProbeTile info;
                        try {
                            info = probe_read_tile(tile, row, col);
                        } catch (...) {
                            continue;
                        }
                        current_tiles.emplace(info.key, std::move(info));
                    }
                }
            }
            for (const auto& [key, tile] : current_tiles) {
                const auto previous = probe_tiles_.find(key);
                if (previous == probe_tiles_.end()) {
                    ProbeJson event;
                    event["event"] = "tile_add";
                    event["time"] = GetTickCount64();
                    event["frame"] = frame;
                    event["tile"] = probe_tile_value(tile);
                    probe_write(event.dump());
                } else if (previous->second.blackboard != tile.blackboard ||
                           previous->second.shared_effects != tile.shared_effects ||
                           previous->second.effect != tile.effect ||
                           previous->second.tile_types != tile.tile_types ||
                           previous->second.tile_info != tile.tile_info ||
                           previous->second.effect_specs != tile.effect_specs) {
                    ProbeJson event;
                    event["event"] = "tile_change";
                    event["time"] = GetTickCount64();
                    event["frame"] = frame;
                    event["tile"] = probe_tile_value(tile);
                    probe_write(event.dump());
                }
            }
            for (const auto& [key, tile] : probe_tiles_) {
                if (!current_tiles.contains(key)) {
                    ProbeJson event;
                    event["event"] = "tile_remove";
                    event["time"] = GetTickCount64();
                    event["frame"] = frame;
                    event["key"] = tile.key;
                    probe_write(event.dump());
                }
            }
            probe_tiles_ = std::move(current_tiles);
        }
    }
}

void LocalBattle::probe_frame(void* controller) {
    if (!probe_enabled_.load() || !controller) {
        return;
    }
    if (!probe_active_.load()) {
        try {
            start_probe(probe_interval_frames_.load());
        } catch (const std::exception& error) {
            log(kLogWarning, std::string("探针启动失败：") + error.what());
            return;
        }
    }
    try {
        probe_snapshot(controller, false);
    } catch (const std::exception& error) {
        static std::uint64_t last_error_tick = 0;
        const std::uint64_t now = GetTickCount64();
        if (now - last_error_tick > 5000) {
            last_error_tick = now;
            log(kLogWarning, std::string("探针采样失败：") + error.what());
        }
    } catch (...) {
    }
}


}  // namespace ark::better_battle
