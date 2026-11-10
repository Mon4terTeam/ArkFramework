#include "BetterBattle.h"
#include "probe_support.h"

#include <nlohmann/json.hpp>

namespace ark::better_battle {

using namespace probe_support;

void LocalBattle::rogue_capture(const std::string& text) {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        void* model = player_data_model();
        if (!model) {
            throw std::runtime_error("尚未捕获 PlayerData，请先等待主界面或战斗刷新");
        }
        il2cpp::Runtime::Root model_root(runtime_, model);
        void* model_class = runtime_.object_class(model);
        void* rlv2 = nullptr;
        read_field_deep(runtime_, model, model_class, "rlv2", &rlv2);
        if (!rlv2) {
            throw std::runtime_error("PlayerData.rlv2 为空");
        }
        il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
        void* current = nullptr;
        read_field_deep(runtime_, rlv2, runtime_.object_class(rlv2), "current",
                        &current);
        if (!current) {
            throw std::runtime_error("当前没有肉鸽局状态");
        }
        il2cpp::Runtime::Root current_root(runtime_, current);
        void* current_class = runtime_.object_class(current);
        void* game = nullptr;
        void* module = nullptr;
        void* player = nullptr;
        read_field_deep(runtime_, current, current_class, "game", &game);
        read_field_deep(runtime_, current, current_class, "module", &module);
        read_field_deep(runtime_, current, current_class, "player", &player);

        std::string topic = text;
        if (game) {
            il2cpp::Runtime::Root game_root(runtime_, game);
            const std::string current_topic = read_string_field(
                runtime_, game, runtime_.object_class(game), "theme");
            if (!current_topic.empty()) {
                topic = current_topic;
            }
        }

        void* buffs = nullptr;
        std::string source;
        int pending_type = -1;
        if (player) {
            il2cpp::Runtime::Root player_root(runtime_, player);
            void* pending = nullptr;
            read_field_deep(runtime_, player, runtime_.object_class(player),
                            "pending", &pending);
            if (pending) {
                il2cpp::Runtime::Root pending_root(runtime_, pending);
                if (runtime_.count(pending) > 0) {
                    void* event = runtime_.at(pending, 0);
                    if (event) {
                        il2cpp::Runtime::Root event_root(runtime_, event);
                        void* event_class = runtime_.object_class(event);
                        read_field_deep(runtime_, event, event_class, "type",
                                        &pending_type);
                        void* type_class = runtime_.find(
                            "Torappu", "PlayerRoguelikePlayerEventType");
                        const int battle_type =
                            runtime_.enum_value(type_class, "BATTLE");
                        if (pending_type == battle_type) {
                            void* content = nullptr;
                            read_field_deep(runtime_, event, event_class,
                                            "content", &content);
                            if (content) {
                                il2cpp::Runtime::Root content_root(runtime_,
                                                                  content);
                                void* battle = nullptr;
                                read_field_deep(
                                    runtime_, content,
                                    runtime_.object_class(content), "battle",
                                    &battle);
                                if (battle) {
                                    il2cpp::Runtime::Root battle_root(runtime_,
                                                                     battle);
                                    read_field_deep(
                                        runtime_, battle,
                                        runtime_.object_class(battle),
                                        "unKeepBuff", &buffs);
                                    if (buffs) {
                                        il2cpp::Runtime::Root buffs_root(
                                            runtime_, buffs);
                                        source = "pending_battle.unKeepBuff";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!buffs) {
            try {
                void* controller_class =
                    classes_.battle_controller;
                const auto controllers =
                    find_controllers(runtime_, controller_class);
                if (!controllers.empty()) {
                    il2cpp::Runtime::Root controller_root(runtime_,
                                                          controllers.front());
                    void* game_mode = nullptr;
                    read_field_deep(
                        runtime_, controllers.front(),
                        runtime_.object_class(controllers.front()), "m_gameMode",
                        &game_mode);
                    if (game_mode) {
                        il2cpp::Runtime::Root game_mode_root(runtime_, game_mode);
                        void* input = nullptr;
                        read_field_deep(runtime_, game_mode,
                                        runtime_.object_class(game_mode),
                                        "m_input", &input);
                        if (input) {
                            il2cpp::Runtime::Root input_root(runtime_, input);
                            read_field_deep(runtime_, input,
                                            runtime_.object_class(input),
                                            "outerBuffs", &buffs);
                            if (buffs) {
                                il2cpp::Runtime::Root buffs_root(runtime_, buffs);
                                source = "battle.outerBuffs";
                            }
                        }
                    }
                }
            } catch (...) {
            }
        }
        if (!buffs) {
            std::string message =
                "当前 pending 不是 BATTLE，且没有战斗内 outerBuffs";
            if (pending_type >= 0) {
                message += "；pending[0].type=" +
                           std::to_string(pending_type);
            }
            throw std::runtime_error(message);
        }

        void* manager = nullptr;
        try {
            void* controller_class =
                classes_.battle_controller;
            const auto controllers =
                find_controllers(runtime_, controller_class);
            if (!controllers.empty()) {
                il2cpp::Runtime::Root controller_root(runtime_,
                                                      controllers.front());
                void* game_mode = nullptr;
                read_field_deep(
                    runtime_, controllers.front(),
                    runtime_.object_class(controllers.front()), "m_gameMode",
                    &game_mode);
                if (game_mode) {
                    il2cpp::Runtime::Root game_mode_root(runtime_, game_mode);
                    read_field_deep(runtime_, game_mode,
                                    runtime_.object_class(game_mode),
                                    "m_roguelikeManager", &manager);
                }
            }
        } catch (...) {
        }

        const auto array_json = [&](void* list) {
            nlohmann::ordered_json out = nlohmann::ordered_json::array();
            if (list) {
                const int count = runtime_.count(list);
                for (int index = 0; index < count; ++index) {
                    void* value = runtime_.at(list, index);
                    out.push_back(value ? runtime_.string(value) : "");
                }
            }
            return out;
        };
        const auto string_field = [&](void* object, const char* name) {
            if (!object) {
                return std::string{};
            }
            return read_string_field(runtime_, object,
                                     runtime_.object_class(object), name);
        };
        const auto object_field = [&](void* object, const char* name) -> void* {
            void* value = nullptr;
            if (object) {
                read_field_deep(runtime_, object, runtime_.object_class(object),
                                name, &value);
            }
            return value;
        };
        const auto int_field = [&](void* object, const char* name) {
            int value = 0;
            if (object) {
                read_field_deep(runtime_, object, runtime_.object_class(object),
                                name, &value);
            }
            return value;
        };

        const std::uint64_t captured_at = GetTickCount64();
        const bool local_capture =
            local_battle_active_.load(std::memory_order_acquire);
        nlohmann::ordered_json document;
        document["version"] = 1;
        document["topic"] = topic;
        document["source"] = source;
        document["captured_at"] = captured_at;
        document["local"] = local_capture;
        document["pending_type"] = pending_type;
        nlohmann::ordered_json active = nlohmann::ordered_json::object();
        if (module) {
            il2cpp::Runtime::Root module_root(runtime_, module);
            void* chaos = object_field(module, "chaos");
            if (chaos) {
                il2cpp::Runtime::Root root(runtime_, chaos);
                active["chaos"] = {
                    {"level", int_field(chaos, "level")},
                    {"value", int_field(chaos, "value")},
                    {"curMaxValue", int_field(chaos, "curMaxValue")},
                    {"ids", array_json(object_field(chaos, "chaosList"))},
                };
            }
            void* disaster = object_field(module, "disaster");
            if (disaster) {
                il2cpp::Runtime::Root root(runtime_, disaster);
                active["disaster"] = {
                    {"id", string_field(disaster, "curDisasterId")},
                    {"disperseStep", int_field(disaster, "disperseStep")},
                };
            }
            void* wrath = object_field(module, "wrath");
            if (wrath) {
                il2cpp::Runtime::Root root(runtime_, wrath);
                active["wrath"] = {
                    {"newWrath", int_field(wrath, "newWrath")},
                    {"ids", array_json(object_field(wrath, "wraths"))},
                };
            }
            void* weather = object_field(module, "weather");
            if (weather) {
                il2cpp::Runtime::Root root(runtime_, weather);
                active["weather"] = {
                    {"main", string_field(weather, "currentMain")},
                    {"sub", string_field(weather, "currentSub")},
                    {"eye", string_field(weather, "eye")},
                    {"step", int_field(weather, "weatherStep")},
                };
            }
        }
        document["active"] = std::move(active);
        il2cpp::Runtime::Root buffs_root(runtime_, buffs);
        const int buff_count = runtime_.count(buffs);
        nlohmann::ordered_json captured_buffs = nlohmann::ordered_json::array();
        for (int index = 0; index < buff_count; ++index) {
            void* buff = runtime_.at(buffs, index);
            if (!buff) {
                continue;
            }
            il2cpp::Runtime::Root buff_root(runtime_, buff);
            void* buff_class = runtime_.object_class(buff);
            nlohmann::ordered_json item;
            item["key"] = read_string_field(runtime_, buff, buff_class, "key");
            nlohmann::ordered_json blackboard = nlohmann::ordered_json::array();
            void* board = object_field(buff, "blackboard");
            if (board) {
                il2cpp::Runtime::Root board_root(runtime_, board);
                void* pairs = object_field(board, "_items");
                if (pairs) {
                    il2cpp::Runtime::Root pairs_root(runtime_, pairs);
                    const int pair_count = runtime_.count(pairs);
                    for (int pair_index = 0; pair_index < pair_count;
                         ++pair_index) {
                        void* pair = runtime_.at(pairs, pair_index);
                        if (!pair) {
                            continue;
                        }
                        il2cpp::Runtime::Root pair_root(runtime_, pair);
                        void* pair_class = runtime_.object_class(pair);
                        float value = 0.0f;
                        read_field_deep(runtime_, pair, pair_class, "value",
                                        &value);
                        const std::string value_str = read_string_field(
                            runtime_, pair, pair_class, "valueStr");
                        nlohmann::ordered_json entry = {
                            {"key", read_string_field(runtime_, pair, pair_class,
                                                      "key")},
                            {"value", value},
                        };
                        if (!value_str.empty()) {
                            entry["value_str"] = value_str;
                        }
                        blackboard.push_back(std::move(entry));
                    }
                }
            }
            item["blackboard"] = std::move(blackboard);
            captured_buffs.push_back(std::move(item));
        }
        document["buffs"] = std::move(captured_buffs);
        nlohmann::ordered_json handlers = nlohmann::ordered_json::array();
        if (manager) {
            il2cpp::Runtime::Root manager_root(runtime_, manager);
            void* manager_class = runtime_.object_class(manager);
            void* relics = nullptr;
            read_field_deep(runtime_, manager, manager_class, "m_relics",
                            &relics);
            if (relics) {
                il2cpp::Runtime::Root relics_root(runtime_, relics);
                const int relic_count = runtime_.count(relics);
                for (int index = 0; index < relic_count && index < 512;
                     ++index) {
                    void* relic = nullptr;
                    try {
                        relic = runtime_.at(relics, index);
                    } catch (...) {
                        continue;
                    }
                    if (!relic) {
                        continue;
                    }
                    il2cpp::Runtime::Root relic_root(runtime_, relic);
                    void* relic_class = runtime_.object_class(relic);
                    void* data = nullptr;
                    try {
                        void* getter =
                            runtime_.method(relic_class, "get_data", 0);
                        data = runtime_.invoke(getter, relic, {});
                    } catch (...) {
                    }
                    int layer = 0;
                    read_field_deep(runtime_, relic, relic_class,
                                    "<stackLayer>k__BackingField", &layer);
                    nlohmann::ordered_json handler = {
                        {"className", runtime_.klass_name(relic_class)},
                        {"layer", layer},
                        {"key", ""},
                    };
                    if (data) {
                        il2cpp::Runtime::Root data_root(runtime_, data);
                        handler["key"] = read_string_field(
                            runtime_, data, runtime_.object_class(data), "key");
                    }
                    handlers.push_back(std::move(handler));
                }
            }
        }
        document["handlers"] = std::move(handlers);
        const std::string json = document.dump() + "\n";

        const std::filesystem::path directory =
            std::filesystem::path(host_.root_directory) / "rogue_captures";
        std::filesystem::create_directories(directory);
        const std::filesystem::path file_path =
            directory /
            ((local_capture ? "capture-local-" : "capture-") +
             std::to_string(captured_at) + ".json");
        const std::filesystem::path latest_path =
            std::filesystem::path(host_.root_directory) /
            "rogue_capture_latest.json";
        std::vector<std::filesystem::path> paths{file_path};
        if (!local_capture) {
            paths.push_back(latest_path);
        }
        for (const auto& path : paths) {
            std::ofstream file(path, std::ios::out | std::ios::trunc);
            file << json;
            file.flush();
            if (!file) {
                throw std::runtime_error("肉鸽机制捕获写入失败: " +
                                         path.string());
            }
        }
        publish_result("已捕获 " + topic + " 的 " +
                       std::to_string(buff_count) + " 条战斗 Buff（" +
                       source + "）");
    } catch (const std::exception& error) {
        publish_result(std::string("肉鸽机制捕获失败：") + error.what(), false);
    }
}

void LocalBattle::rogue_dump_battle() {
    try {
        void* controller_class = classes_.battle_controller;
        const auto controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前没有 BattleController");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* game_mode = nullptr;
        read_field_deep(runtime_, controller,
                        runtime_.object_class(controller), "m_gameMode",
                        &game_mode);
        if (!game_mode) {
            throw std::runtime_error("BattleController.m_gameMode 为空");
        }
        Root game_mode_root(runtime_, game_mode);
        std::ostringstream output;
        output << "gameMode=" << runtime_.klass_name(runtime_.object_class(game_mode))
               << "\n";
        void* input = nullptr;
        void* manager = nullptr;
        read_field_deep(runtime_, game_mode, runtime_.object_class(game_mode),
                        "m_input", &input);
        read_field_deep(runtime_, game_mode, runtime_.object_class(game_mode),
                        "m_roguelikeManager", &manager);

        const auto dump_data_pair = [&](const std::string& prefix, void* pair) {
            if (!pair) {
                return;
            }
            Root root(runtime_, pair);
            void* klass = runtime_.object_class(pair);
            const std::string key =
                read_string_field(runtime_, pair, klass, "key");
            float value = 0.0f;
            read_field_deep(runtime_, pair, klass, "value", &value);
            const std::string value_str =
                read_string_field(runtime_, pair, klass, "valueStr");
            output << prefix << key << " = " << value;
            if (!value_str.empty()) {
                output << " (" << value_str << ")";
            }
            output << "\n";
        };
        const auto dump_blackboard = [&](const std::string& prefix, void* board) {
            if (!board) {
                return;
            }
            Root root(runtime_, board);
            output << prefix << "blackboard " << board << "\n";
            for (void* klass = runtime_.object_class(board); klass;
                 klass = runtime_.class_parent(klass)) {
                void* iterator = nullptr;
                while (void* field = runtime_.class_fields(klass, &iterator)) {
                    if (runtime_.field_flags(field) & 0x10) {
                        continue;
                    }
                    void* field_class = nullptr;
                    try {
                        field_class =
                            runtime_.class_from_type(runtime_.field_type(field));
                    } catch (...) {
                        continue;
                    }
                    if (!field_class || runtime_.class_is_value(field_class)) {
                        continue;
                    }
                    void* value = nullptr;
                    if (!read_at(
                            static_cast<const unsigned char*>(board) +
                                runtime_.field_offset(field),
                            &value)) {
                        continue;
                    }
                    if (!value) {
                        continue;
                    }
                    Root value_root(runtime_, value);
                    const std::string value_type =
                        runtime_.klass_name(runtime_.object_class(value));
                    const char* field_name = runtime_.field_name(field);
                    output << prefix << "  " << (field_name ? field_name : "?")
                           << " type=" << value_type << "\n";
                    if (runtime_.class_rank(runtime_.object_class(value)) > 0) {
                        const int count = runtime_.count(value);
                        output << prefix << "    count=" << count << "\n";
                        for (int index = 0; index < count && index < 256; ++index) {
                            void* item = nullptr;
                            try {
                                item = runtime_.at(value, index);
                            } catch (...) {
                                continue;
                            }
                            dump_data_pair(prefix + "      ", item);
                        }
                    } else if (value_type.find("List") != std::string::npos) {
                        const int count = runtime_.count(value);
                        output << prefix << "    count=" << count << "\n";
                        for (int index = 0; index < count && index < 256; ++index) {
                            void* item = nullptr;
                            try {
                                item = runtime_.at(value, index);
                            } catch (...) {
                                continue;
                            }
                            dump_data_pair(prefix + "      ", item);
                        }
                    } else if (value_type.find("Dictionary") != std::string::npos) {
                        const auto pairs = read_dictionary_pairs(runtime_, value, 256);
                        output << prefix << "    count=" << pairs.size() << "\n";
                        for (const auto& pair : pairs) {
                            if (pair.key) {
                                output << prefix << "      key="
                                       << runtime_.string(pair.key) << "\n";
                            }
                            dump_data_pair(prefix + "      ", pair.value);
                        }
                    }
                }
            }
        };
        const auto dump_buff_entry = [&](const std::string& prefix, void* buff) {
            if (!buff) {
                return;
            }
            Root root(runtime_, buff);
            void* klass = runtime_.object_class(buff);
            output << prefix << "key="
                   << read_string_field(runtime_, buff, klass, "key") << "\n";
            void* blackboard = nullptr;
            if (read_field_deep(runtime_, buff, klass, "blackboard", &blackboard) &&
                blackboard) {
                dump_blackboard(prefix + "  ", blackboard);
            }
        };
        const auto dump_relic = [&](const std::string& prefix, void* relic) {
            if (!relic) {
                return;
            }
            Root root(runtime_, relic);
            void* klass = runtime_.object_class(relic);
            output << prefix << runtime_.klass_name(klass) << "\n";

            int stack_layer = 0;
            if (read_field_deep(runtime_, relic, klass,
                                "<stackLayer>k__BackingField",
                                &stack_layer)) {
                output << prefix << "  stackLayer=" << stack_layer << "\n";
            }

            void* data = nullptr;
            if (read_field_deep(runtime_, relic, klass,
                                "<data>k__BackingField", &data) &&
                data) {
                Root data_root(runtime_, data);
                dump_buff_entry(prefix + "  data ", data);
            }

            void* relic_data = nullptr;
            if (read_field_deep(runtime_, relic, klass, "relicData",
                                &relic_data) &&
                relic_data) {
                Root relic_data_root(runtime_, relic_data);
                void* relic_class = runtime_.object_class(relic_data);
                output << prefix << "  relicId="
                       << read_string_field(runtime_, relic_data, relic_class,
                                            "id")
                       << "\n";
                void* relic_buffs = nullptr;
                if (read_field_deep(runtime_, relic_data, relic_class, "buffs",
                                    &relic_buffs) &&
                    relic_buffs) {
                    Root relic_buffs_root(runtime_, relic_buffs);
                    const int buff_count = runtime_.count(relic_buffs);
                    output << prefix << "  relicBuffs=" << buff_count << "\n";
                    for (int buff_index = 0;
                         buff_index < buff_count && buff_index < 256;
                         ++buff_index) {
                        void* buff = nullptr;
                        try {
                            buff = runtime_.at(relic_buffs, buff_index);
                        } catch (...) {
                            continue;
                        }
                        if (!buff) {
                            continue;
                        }
                        Root buff_root(runtime_, buff);
                        dump_buff_entry(prefix + "    ", buff);
                    }
                }
            }
        };
        const auto dump_unique_ids = [&](const std::string& label, void* ids) {
            if (!ids) {
                return;
            }
            Root ids_root(runtime_, ids);
            const int count = runtime_.count(ids);
            output << label << "=[";
            for (int index = 0; index < count && index < 64; ++index) {
                std::uint32_t unique_id = 0;
                try {
                    void* boxed = runtime_.at(ids, index);
                    if (boxed) {
                        read_at(runtime_.object_unbox(boxed), &unique_id);
                    }
                } catch (...) {
                }
                if (index) {
                    output << ',';
                }
                output << unique_id;
            }
            output << ']';
        };
        const auto dump_list = [&](const std::string& label, void* list) {
            if (!list) {
                output << label << "=<null>\n";
                return;
            }
            Root root(runtime_, list);
            const int count = runtime_.count(list);
            output << label << " count=" << count << "\n";
            for (int index = 0; index < count; ++index) {
                void* item = nullptr;
                try {
                    item = runtime_.at(list, index);
                } catch (...) {
                    continue;
                }
                if (!item) {
                    continue;
                }
                Root item_root(runtime_, item);
                void* klass = runtime_.object_class(item);
                const std::string class_name = runtime_.klass_name(klass);
                output << "  [" << index << "] " << class_name;
                if (class_name.find("CharBuffBattleData") !=
                    std::string::npos) {
                    output << " charBuffId="
                           << read_string_field(runtime_, item, klass,
                                                "charBuffId");
                    void* unique_ids = nullptr;
                    read_field_deep(runtime_, item, klass, "charUniqueIds",
                                    &unique_ids);
                    dump_unique_ids(" charUniqueIds", unique_ids);
                    void* char_buffs = nullptr;
                    if (read_field_deep(runtime_, item, klass, "buffs",
                                        &char_buffs) &&
                        char_buffs) {
                        Root char_buffs_root(runtime_, char_buffs);
                        const int buff_count = runtime_.count(char_buffs);
                        output << " buffs=" << buff_count << "\n";
                        for (int buff_index = 0;
                             buff_index < buff_count && buff_index < 256;
                             ++buff_index) {
                            void* buff = nullptr;
                            try {
                                buff = runtime_.at(char_buffs, buff_index);
                            } catch (...) {
                                continue;
                            }
                            Root buff_root(runtime_, buff);
                            dump_buff_entry("      ", buff);
                        }
                        output << "    ";
                    }
                }
                if (class_name.find("BasicCharBuff") != std::string::npos) {
                    void* unique_ids = nullptr;
                    if (read_field_deep(runtime_, item, klass, "m_charUniqueIDs",
                                        &unique_ids) &&
                        unique_ids) {
                        dump_unique_ids(" charUniqueIds", unique_ids);
                    }
                    void* relic = nullptr;
                    if (read_field_deep(runtime_, item, klass, "m_relic",
                                        &relic) &&
                        relic) {
                        output << "\n";
                        dump_relic("      ", relic);
                    }
                }
                if (class_name.find("RoguelikeBuff") != std::string::npos) {
                    output << " key="
                           << read_string_field(runtime_, item, klass, "key");
                    void* board = nullptr;
                    if (read_field_deep(runtime_, item, klass, "blackboard",
                                        &board) &&
                        board) {
                        dump_blackboard(std::string("      "), board);
                    }
                }
                if (class_name.find("RelicBuff") != std::string::npos) {
                    void* relic_data = nullptr;
                    if (read_field_deep(runtime_, item, klass, "relicData",
                                        &relic_data) &&
                        relic_data) {
                        Root relic_root(runtime_, relic_data);
                        void* relic_class = runtime_.object_class(relic_data);
                        output << " relicId="
                               << read_string_field(runtime_, relic_data,
                                                    relic_class, "id");
                        void* relic_buffs = nullptr;
                        if (read_field_deep(runtime_, relic_data, relic_class,
                                            "buffs", &relic_buffs) &&
                            relic_buffs) {
                            Root relic_buffs_root(runtime_, relic_buffs);
                            const int relic_count = runtime_.count(relic_buffs);
                            output << " relicBuffs=" << relic_count;
                            for (int relic_index = 0; relic_index < relic_count;
                                 ++relic_index) {
                                void* buff = nullptr;
                                try {
                                    buff = runtime_.at(relic_buffs, relic_index);
                                } catch (...) {
                                    continue;
                                }
                                dump_buff_entry("        ", buff);
                            }
                        }
                    }
                }
                void* id_value = nullptr;
                if (read_field_deep(runtime_, item, klass, "id", &id_value) &&
                    id_value) {
                    output << " id=" << runtime_.string(id_value);
                }
                int layer = 0;
                if (read_field_deep(runtime_, item, klass, "layer", &layer)) {
                    output << " layer=" << layer;
                }
                output << "\n";
                void* nested_buffs = nullptr;
                if (read_field_deep(runtime_, item, klass, "buffs", &nested_buffs) &&
                    nested_buffs) {
                    Root nested_root(runtime_, nested_buffs);
                    const int nested_count = runtime_.count(nested_buffs);
                    output << "      buffs=" << nested_count << "\n";
                    for (int nested_index = 0; nested_index < nested_count;
                         ++nested_index) {
                        void* buff = nullptr;
                        try {
                            buff = runtime_.at(nested_buffs, nested_index);
                        } catch (...) {
                            continue;
                        }
                        dump_buff_entry("        ", buff);
                    }
                }
            }
        };
        const auto dump_global_buff_data = [&](const std::string& label,
                                               void* list) {
            if (!list) {
                output << label << "=<null>\n";
                return;
            }
            Root list_root(runtime_, list);
            const int count = runtime_.count(list);
            output << label << " count=" << count << "\n";
            for (int index = 0; index < count && index < 256; ++index) {
                void* item = nullptr;
                try {
                    item = runtime_.at(list, index);
                } catch (...) {
                    continue;
                }
                if (!item) {
                    continue;
                }
                Root item_root(runtime_, item);
                void* klass = runtime_.object_class(item);
                output << "  [" << index << "] "
                       << runtime_.klass_name(klass);
                void* prefab_key = nullptr;
                read_field_deep(runtime_, item, klass, "prefabKey",
                                &prefab_key);
                if (prefab_key) {
                    output << " prefabKey=" << runtime_.string(prefab_key);
                }
                bool use_extra_data = false;
                if (read_field_deep(runtime_, item, klass, "useExtraData",
                                    &use_extra_data)) {
                    output << " useExtraData="
                           << (use_extra_data ? "true" : "false");
                }
                void* source = nullptr;
                read_field_deep(runtime_, item, klass, "source", &source);
                if (source) {
                    output << " source="
                           << runtime_.klass_name(
                                  runtime_.object_class(source));
                    int source_layer = 0;
                    if (read_field_deep(
                            runtime_, source,
                            runtime_.object_class(source),
                            "<stackLayer>k__BackingField", &source_layer)) {
                        output << " sourceLayer=" << source_layer;
                    }
                }
                void* blackboard = nullptr;
                read_field_deep(runtime_, item, klass, "blackboard",
                                &blackboard);
                if (blackboard) {
                    output << "\n";
                    dump_blackboard(std::string("    "), blackboard);
                } else {
                    output << "\n";
                }
            }
        };
        const auto dump_runtime_global_buffs = [&](const std::string& label,
                                                   void* list) {
            if (!list) {
                output << label << "=<null>\n";
                return;
            }
            Root list_root(runtime_, list);
            const int count = runtime_.count(list);
            output << label << " count=" << count << "\n";
            for (int index = 0; index < count && index < 256; ++index) {
                void* item = nullptr;
                try {
                    item = runtime_.at(list, index);
                } catch (...) {
                    continue;
                }
                if (!item) {
                    continue;
                }
                Root item_root(runtime_, item);
                void* klass = runtime_.object_class(item);
                output << "  [" << index << "] "
                       << runtime_.klass_name(klass);
                const std::string key =
                    read_string_field(runtime_, item, klass, "_key");
                if (!key.empty()) {
                    output << " key=" << key;
                }
                int layer_mask = 0;
                if (read_field_deep(runtime_, item, klass, "m_layerMask",
                                    &layer_mask)) {
                    output << " layerMask=" << layer_mask;
                }
                void* source = nullptr;
                read_field_deep(runtime_, item, klass, "m_source", &source);
                if (source) {
                    try {
                        void* source_class = runtime_.object_class(source);
                        output << " source="
                               << runtime_.klass_name(source_class);
                        int source_layer = 0;
                        if (read_field_deep(
                                runtime_, source, source_class,
                                "<stackLayer>k__BackingField",
                                &source_layer)) {
                            output << " sourceLayer=" << source_layer;
                        }
                        if (read_field_deep(runtime_, source, source_class,
                                            "layer", &source_layer)) {
                            output << " sourceRawLayer=" << source_layer;
                        }
                    } catch (...) {
                    }
                }
                void* buffs = nullptr;
                read_field_deep(runtime_, item, klass, "_buffs", &buffs);
                if (buffs) {
                    const int buff_count = runtime_.count(buffs);
                    output << " buffs=" << buff_count;
                    output << "\n";
                    for (int buff_index = 0;
                         buff_index < buff_count && buff_index < 64;
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
                        Root buff_root(runtime_, buff);
                        void* buff_class = runtime_.object_class(buff);
                        output << "      " << runtime_.klass_name(buff_class);
                        const std::string buff_key =
                            read_string_field(runtime_, buff, buff_class,
                                              "key");
                        const std::string buff_template =
                            read_string_field(runtime_, buff, buff_class,
                                              "templateKey");
                        if (!buff_key.empty()) {
                            output << " key=" << buff_key;
                        }
                        if (!buff_template.empty()) {
                            output << " template=" << buff_template;
                        }
                        void* attributes = nullptr;
                        read_field_deep(runtime_, buff, buff_class,
                                        "attributes", &attributes);
                        if (attributes) {
                            Root attributes_root(runtime_, attributes);
                            void* attribute_modifiers = nullptr;
                            read_field_deep(
                                runtime_, attributes,
                                runtime_.object_class(attributes),
                                "attributeModifiers", &attribute_modifiers);
                            if (attribute_modifiers) {
                                output << " attributes="
                                       << runtime_.count(attribute_modifiers);
                            }
                        }
                        output << "\n";
                    }
                } else {
                    output << "\n";
                }
                void* blackboard = nullptr;
                read_field_deep(runtime_, item, klass, "blackboard",
                                &blackboard);
                if (blackboard) {
                    output << "\n";
                    dump_blackboard(std::string("    "), blackboard);
                } else {
                    output << "\n";
                }
            }
        };
        void* level_data = nullptr;
        read_field_deep(runtime_, controller,
                        runtime_.object_class(controller), "m_levelData",
                        &level_data);
        if (level_data) {
            Root level_data_root(runtime_, level_data);
            void* level_global_buffs = nullptr;
            read_field_deep(runtime_, level_data,
                            runtime_.object_class(level_data), "globalBuffs",
                            &level_global_buffs);
            dump_global_buff_data("m_levelData.globalBuffs",
                                  level_global_buffs);
        } else {
            output << "m_levelData=<null>\n";
        }
        void* controller_global_buffs = nullptr;
        read_field_deep(runtime_, controller,
                        runtime_.object_class(controller), "m_globalBuffs",
                        &controller_global_buffs);
        dump_runtime_global_buffs("m_globalBuffs", controller_global_buffs);

        const auto dump_string_int_dict =
            [&](const std::string& label, void* dict) {
                if (!dict) {
                    output << label << "=<null>\n";
                    return;
                }
                const auto pairs =
                    read_dictionary_pairs(runtime_, dict, 256);
                output << label << " count=" << pairs.size() << "\n";
                for (const auto& pair : pairs) {
                    output << "  ";
                    if (pair.key) {
                        output << runtime_.string(pair.key);
                    } else {
                        output << "<null>";
                    }
                    output << " = "
                           << (pair.value
                                   ? runtime_.unbox<int>(pair.value)
                                   : 0)
                           << "\n";
                }
            };

        if (input) {
            Root input_root(runtime_, input);
            output << "input="
                   << runtime_.klass_name(runtime_.object_class(input)) << "\n";
            int hp = 0;
            int max_hp = 0;
            int shield = 0;
            read_field_deep(runtime_, input, runtime_.object_class(input), "hp",
                            &hp);
            read_field_deep(runtime_, input, runtime_.object_class(input),
                            "maxHp", &max_hp);
            read_field_deep(runtime_, input, runtime_.object_class(input),
                            "shield", &shield);
            output << "hp=" << hp << " maxHp=" << max_hp << " shield=" << shield
                   << "\n";
            void* list = nullptr;
            read_field_deep(runtime_, input, runtime_.object_class(input),
                            "outerBuffs", &list);
            dump_list("outerBuffs", list);
            read_field_deep(runtime_, input, runtime_.object_class(input),
                            "relicBuffs", &list);
            dump_list("relicBuffs", list);
            read_field_deep(runtime_, input, runtime_.object_class(input),
                            "extraBuffs", &list);
            dump_list("extraBuffs", list);
            void* meta = nullptr;
            void* meta_char_buffs = nullptr;
            void* meta_candle_dict = nullptr;
            void* meta_employ_dict = nullptr;
            void* input_class = runtime_.object_class(input);
            void* meta_field = runtime_.field(input_class, "meta");
            if (meta_field) {
                void* meta_class =
                    runtime_.class_from_type(runtime_.field_type(meta_field));
                const int meta_offset = runtime_.field_offset(meta_field);
                output << "metaField type="
                       << (meta_class ? runtime_.klass_name(meta_class) : "?")
                       << " offset=0x" << std::hex << meta_offset << std::dec
                       << " valueType="
                       << (meta_class && runtime_.class_is_value(meta_class) ? 1 : 0)
                       << "\n";
                if (meta_class && runtime_.class_is_value(meta_class)) {
                    std::uint32_t alignment = 0;
                    const int size =
                        runtime_.class_value_size(meta_class, &alignment);
                    output << "metaSize=" << size << "\n";
                    if (size >= 8 && size <= 4096) {
                        std::vector<unsigned char> buffer(
                            static_cast<std::size_t>(size));
                        runtime_.field_get(input, meta_field, buffer.data());
                        constexpr int kValueHeaderSize = 0x10;
                        for (void* klass = meta_class; klass;
                             klass = runtime_.class_parent(klass)) {
                            void* iterator = nullptr;
                            while (void* field =
                                       runtime_.class_fields(klass, &iterator)) {
                                if (runtime_.field_flags(field) & 0x10) {
                                    continue;
                                }
                                const int offset = runtime_.field_offset(field);
                                const int relative = offset - kValueHeaderSize;
                                if (relative < 0 || relative + 8 > size) {
                                    continue;
                                }
                                std::string type_name;
                                try {
                                    type_name =
                                        runtime_.type(runtime_.field_type(field));
                                } catch (...) {
                                    continue;
                                }
                                const char* field_name = runtime_.field_name(field);
                                output << "  meta." << (field_name ? field_name : "?")
                                       << " : " << type_name;
                                if (type_name == "System.Int32") {
                                    output << " = "
                                           << *reinterpret_cast<int*>(
                                                  buffer.data() + relative);
                                } else if (type_name == "System.Boolean") {
                                    output << " = "
                                           << (*reinterpret_cast<bool*>(
                                                   buffer.data() + relative)
                                                   ? "true"
                                                   : "false");
                                } else if (type_name != "System.Single" &&
                                           type_name != "System.Int64" &&
                                           type_name != "System.UInt32") {
                                    void* raw = nullptr;
                                    std::memcpy(&raw, buffer.data() + relative,
                                                sizeof(raw));
                                    if (field_name &&
                                        std::strcmp(field_name, "charBuffs") ==
                                            0) {
                                        meta_char_buffs = raw;
                                    } else if (field_name &&
                                               std::strcmp(
                                                   field_name,
                                                   "hasCandleHolderBuffCharDict") ==
                                                   0) {
                                        meta_candle_dict = raw;
                                    } else if (field_name &&
                                               std::strcmp(field_name,
                                                           "employCharDict") ==
                                                   0) {
                                        meta_employ_dict = raw;
                                    }
                                    output << " ptr=" << raw;
                                    MEMORY_BASIC_INFORMATION info{};
                                    if (VirtualQuery(raw, &info, sizeof(info)) &&
                                        info.State == MEM_COMMIT &&
                                        !(info.Protect & PAGE_NOACCESS) &&
                                        !(info.Protect & PAGE_GUARD)) {
                                        try {
                                            output << " class="
                                                   << runtime_.klass_name(
                                                          runtime_.object_class(
                                                              raw));
                                        } catch (...) {
                                        }
                                    }
                                }
                                output << "\n";
                            }
                        }
                    }
                }
            }
            if (meta_char_buffs) {
                dump_list("meta.charBuffs", meta_char_buffs);
            }
            dump_string_int_dict("meta.hasCandleHolderBuffCharDict",
                                 meta_candle_dict);
            dump_string_int_dict("meta.employCharDict", meta_employ_dict);
        }
        if (manager) {
            Root manager_root(runtime_, manager);
            void* manager_class = runtime_.object_class(manager);
            output << "manager=" << runtime_.klass_name(manager_class) << "\n";
            void* relics = nullptr;
            void* char_buffs = nullptr;
            read_field_deep(runtime_, manager, manager_class, "m_relics", &relics);
            read_field_deep(runtime_, manager, manager_class, "m_charBuffs",
                            &char_buffs);
            if (relics) {
                Root relics_root(runtime_, relics);
                const int relic_count = runtime_.count(relics);
                output << "m_relics count=" << relic_count << "\n";
                for (int index = 0; index < relic_count && index < 256; ++index) {
                    void* item = nullptr;
                    try {
                        item = runtime_.at(relics, index);
                    } catch (...) {
                        continue;
                    }
                    if (!item) {
                        continue;
                    }
                    output << "  [" << index << "]\n";
                    dump_relic("    ", item);
                }
            } else {
                output << "m_relics=<null>\n";
            }
            void* relic_classes = nullptr;
            read_field_deep(runtime_, manager, manager_class, "m_relicClasses",
                            &relic_classes);
            output << "m_relicClasses=" << relic_classes;
            if (!relic_classes) {
                output << "<null>\n";
            } else {
                Root classes_root(runtime_, relic_classes);
                const auto pairs =
                    read_dictionary_pairs(runtime_, relic_classes, 256);
                const auto valid_count = std::count_if(
                    pairs.begin(), pairs.end(), [](const DictionaryPair& pair) {
                        return pair.key && pair.value;
                    });
                output << " count=" << valid_count << "\n";
                for (const auto& pair : pairs) {
                    if (!pair.key || !pair.value) {
                        continue;
                    }
                    output << "  " << runtime_.string(pair.key) << " -> ";
                    Root type_root(runtime_, pair.value);
                    std::string type_name =
                        method_string(runtime_, pair.value,
                                      runtime_.object_class(pair.value),
                                      "ToString");
                    if (type_name.empty()) {
                        type_name = runtime_.klass_name(
                            runtime_.object_class(pair.value));
                    }
                    output << type_name << "\n";
                }
            }
            dump_list("m_charBuffs", char_buffs);
            void* candle_dict = nullptr;
            void* employ_dict = nullptr;
            void* special_used = nullptr;
            int candle_count = 0;
            read_field_deep(runtime_, manager, manager_class,
                            "m_hasCandleHolderBuffCharDict", &candle_dict);
            read_field_deep(runtime_, manager, manager_class,
                            "m_employHolderBuffCharDict", &employ_dict);
            read_field_deep(runtime_, manager, manager_class,
                            "m_specialUsedCharList", &special_used);
            read_field_deep(runtime_, manager, manager_class,
                            "m_characterInCandleHolderCnt", &candle_count);
            dump_string_int_dict("m_hasCandleHolderBuffCharDict",
                                 candle_dict);
            dump_string_int_dict("m_employHolderBuffCharDict",
                                 employ_dict);
            output << "m_characterInCandleHolderCnt=" << candle_count << "\n";
            if (special_used) {
                const auto pairs =
                    read_dictionary_pairs(runtime_, special_used, 64);
                output << "m_specialUsedCharList count=" << pairs.size()
                       << "\n";
                for (const auto& pair : pairs) {
                    output << "  "
                           << (pair.key ? runtime_.string(pair.key)
                                        : "<null>")
                           << " -> ";
                    dump_unique_ids("", pair.value);
                }
            } else {
                output << "m_specialUsedCharList=<null>\n";
            }
        }
        void* entity_class = classes_.entity;
        void* bobject_class = classes_.battle_object;
        void* enemy_class = classes_.enemy;
        void* object_manager = nullptr;
        read_field_deep(runtime_, controller,
                        runtime_.object_class(controller), "m_objectManger",
                        &object_manager);
        output << "enemyUnits\n";
        if (object_manager && entity_class && bobject_class && enemy_class) {
            Root object_manager_root(runtime_, object_manager);
            void* cached = nullptr;
            read_field_deep(runtime_, object_manager,
                            runtime_.object_class(object_manager), "m_cachedList",
                            &cached);
            if (cached) {
                Root cached_root(runtime_, cached);
                const int cached_count = runtime_.count(cached);
                for (int index = 0; index < cached_count && index < 512; ++index) {
                    void* unit = nullptr;
                    try {
                        unit = runtime_.at(cached, index);
                    } catch (...) {
                        continue;
                    }
                    if (!unit || runtime_.object_class(unit) != enemy_class) {
                        continue;
                    }
                    Root unit_root(runtime_, unit);
                    std::uint32_t uid = 0;
                    read_field_deep(runtime_, unit, bobject_class,
                                    "<instanceUid>k__BackingField", &uid);
                    std::int64_t hp_raw = 0;
                    read_field_deep(runtime_, unit, entity_class, "m_hp", &hp_raw);
                    output << "  [" << index << "] uid=" << uid
                           << " id="
                           << method_string(runtime_, unit, entity_class, "get_id")
                           << " tmpl="
                           << method_string(runtime_, unit, entity_class,
                                            "get_tmplId")
                           << " hp="
                           << static_cast<double>(hp_raw) / kFixedPointScale
                           << " maxHp="
                           << method_fp(runtime_, unit, entity_class, "get_maxHp")
                           << " atk="
                           << method_fp(runtime_, unit, entity_class, "get_atk")
                           << " def="
                           << method_fp(runtime_, unit, entity_class, "get_def")
                           << " aspd="
                           << method_fp(runtime_, unit, entity_class,
                                        "get_attackSpeed")
                           << " moveSpeed="
                           << method_float(runtime_, unit, entity_class,
                                           "get_moveSpeed")
                           << " alive="
                           << (method_bool(runtime_, unit, entity_class,
                                           "get_alive")
                                   ? "true"
                                   : "false")
                           << "\n";
                }
            }
        } else {
            output << "  <unavailable>\n";
        }
        const std::string global_buffs = last_global_buffs_json();
        if (global_buffs.empty()) {
            output << "preprocessGlobalBuff=<not captured>\n";
        } else {
            output << "preprocessGlobalBuff " << global_buffs << "\n";
            const std::filesystem::path global_path =
                std::filesystem::path(host_.root_directory) /
                "rogue_global_buffs_latest.json";
            std::ofstream global_file(global_path,
                                      std::ios::out | std::ios::trunc);
            global_file << global_buffs;
            global_file.flush();
        }

        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_rogue_dump.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("肉鸽战斗 dump 写入失败");
        }
        publish_result("肉鸽战斗 dump 已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("肉鸽战斗 dump 失败：") + error.what(), false);
    }
}


}  // namespace ark::better_battle
