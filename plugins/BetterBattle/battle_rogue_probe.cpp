#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::probe_rogue_extra(const std::string& text) {
    try {
        void* base_class =
            runtime_.find("Torappu.UI.Roguelike", "RoguelikeSquadStatePlugin");
        void* base_ctor = runtime_.method(base_class, ".ctor", 0);
        void* base_plugin = construct_with_args(runtime_, base_class, base_ctor, {});
        il2cpp::Runtime::Root base_root(runtime_, base_plugin);
        void* base_method = runtime_.method(
            base_class, "GetTopicExtraBuffs", 1, {"System.String"});
        void* topic_value = runtime_.string_new(text.c_str());
        il2cpp::Runtime::Root topic_root(runtime_, topic_value);
        void* base_list = runtime_.invoke(base_method, base_plugin, {topic_value});
        il2cpp::Runtime::Root base_list_root(runtime_, base_list);

        void* rl05_class = nullptr;
        void* rl05_plugin = nullptr;
        void* rl05_list = nullptr;
        try {
            rl05_class =
                runtime_.find("Torappu.UI.Roguelike.RL05", "RL05SquadStatePlugin");
            void* rl05_ctor = runtime_.method(rl05_class, ".ctor", 0);
            rl05_plugin = construct_with_args(runtime_, rl05_class, rl05_ctor, {});
            void* rl05_method = runtime_.method(
                rl05_class, "GetTopicExtraBuffs", 1, {"System.String"});
            rl05_list = runtime_.invoke(rl05_method, rl05_plugin, {topic_value});
        } catch (const std::exception& error) {
            log(kLogWarning, std::string("RL05 probe skipped: ") + error.what());
        }

        std::ostringstream output;
        output << "topic " << text << "\n";
        const auto dump_list = [&](const char* label, void* list) {
            output << label << " count=" << runtime_.count(list) << "\n";
            if (!list) {
                return;
            }
            il2cpp::Runtime::Root list_root(runtime_, list);
            const int count = runtime_.count(list);
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
                il2cpp::Runtime::Root item_root(runtime_, item);
                output << "  [" << index << "] "
                       << describe_instance_fields(runtime_, item) << "\n";
                void* buffs = nullptr;
                try {
                    read_field_deep(runtime_, item,
                                    runtime_.object_class(item), "buffs",
                                    &buffs);
                } catch (...) {
                }
                if (!buffs) {
                    continue;
                }
                il2cpp::Runtime::Root buffs_root(runtime_, buffs);
                const int buff_count = runtime_.count(buffs);
                output << "      buffs=" << buff_count << "\n";
                for (int buff_index = 0; buff_index < buff_count; ++buff_index) {
                    void* buff = nullptr;
                    try {
                        buff = runtime_.at(buffs, buff_index);
                    } catch (...) {
                        continue;
                    }
                    if (!buff) {
                        continue;
                    }
                    il2cpp::Runtime::Root buff_root(runtime_, buff);
                    const std::string key = read_string_field(
                        runtime_, buff, runtime_.object_class(buff), "key");
                    output << "        key=" << key << " "
                           << describe_instance_fields(runtime_, buff) << "\n";
                }
            }
        };
        dump_list("base", base_list);
        dump_list("rl05", rl05_list);
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_extra_probe.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("肉鸽机制探测结果写入失败");
        }
        publish_result("肉鸽机制探测已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("肉鸽机制探测失败：") + error.what(), false);
    }
}

void LocalBattle::rogue_synth(const std::string& text, const std::vector<std::tuple<std::string, std::string, int>>& extra_buffs) {
    std::ostringstream output;
    output << "topic " << text << "\n";
    const auto flush = [&]() {
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_synth.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
    };
    const auto fail = [&](const std::string& message) {
        output << "error " << message << "\n";
        flush();
        publish_result("肉鸽合成失败：" + message, false);
    };
    const auto find_class_pointer = [&](void* candidate) -> void* {
        if (!candidate) {
            return nullptr;
        }
        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        for (std::size_t assembly_index = 0;
             assemblies && assembly_index < assembly_count; ++assembly_index) {
            void* image = runtime_.assembly_image(assemblies[assembly_index]);
            if (!image) {
                continue;
            }
            const std::size_t class_count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < class_count;
                 ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                if (klass == candidate) {
                    return klass;
                }
            }
        }
        return nullptr;
    };

    HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
    if (!module) {
        fail("GameAssembly.dll 未加载");
        return;
    }
    constexpr std::size_t kTypeInfoRva = 0x8bf2f08;
    void* type_info = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(module) + kTypeInfoRva);
    output << "typeinfo=" << type_info;
    void* type_info_class = find_class_pointer(type_info);
    if (type_info_class) {
        output << " class=" << runtime_.klass_name(type_info_class);
    }
    output << "\n";
    flush();
    if (!type_info || !type_info_class) {
        fail("槽位值不是已知类型指针");
        return;
    }
    void* type_info_alias = type_info_class;
    try {
        runtime_.class_init(type_info_alias);
    } catch (...) {
    }

    void* holder_field = nullptr;
    std::string holder_type;
    void* field_iterator = nullptr;
    try {
        while (void* field = runtime_.class_fields(type_info_alias, &field_iterator)) {
            const std::uint32_t flags = runtime_.field_flags
                                            ? runtime_.field_flags(field)
                                            : 0;
            if (!(flags & 0x10)) {
                continue;
            }
            std::string field_type;
            try {
                field_type = runtime_.type(runtime_.field_type(field));
            } catch (...) {
                field_type = "?";
            }
            output << "  static "
                   << (runtime_.field_name(field) ? runtime_.field_name(field) : "?")
                   << " off=0x" << std::hex << runtime_.field_offset(field)
                   << std::dec << " type=" << field_type << "\n";
            try {
                void* field_value = nullptr;
                runtime_.field_static_get(field, &field_value);
                output << "    value=" << field_value;
                if (field_value) {
                    void* value_class = runtime_.object_class(field_value);
                    void* known = find_class_pointer(value_class);
                    if (known) {
                        output << " class=" << runtime_.klass_name(known);
                    }
                }
                output << "\n";
            } catch (const std::exception& error) {
                output << "    value_error=" << error.what() << "\n";
            }
            if (!holder_field && field_type == "Torappu.PlayerRoguelikeV2") {
                holder_field = field;
                holder_type = field_type;
            }
            if (!holder_field && runtime_.field_offset(field) == 0x10 &&
                field_type.find("CurrentData") != std::string::npos) {
                holder_field = field;
                holder_type = field_type;
            }
        }
    } catch (const std::exception& error) {
        fail(std::string("枚举静态字段失败：") + error.what());
        return;
    }
    if (!holder_field) {
        fail("未找到 PlayerRoguelikeV2 静态字段");
        return;
    }
    output << "holder "
           << (runtime_.field_name(holder_field) ? runtime_.field_name(holder_field)
                                                 : "?")
           << " type=" << holder_type << "\n";
    if (holder_type != "Torappu.PlayerRoguelikeV2" &&
        holder_type.find("CurrentData") == std::string::npos) {
        fail("静态字段类型不是 PlayerRoguelikeV2/CurrentData，已放弃写入");
        return;
    }

    void* original = nullptr;
    try {
        runtime_.field_static_get(holder_field, &original);
    } catch (const std::exception& error) {
        fail(std::string("读取静态字段失败：") + error.what());
        return;
    }
    output << "original=" << original << "\n";

    try {
        void* rlv2_class = runtime_.find("Torappu", "PlayerRoguelikeV2");
        void* current_class = runtime_.nested(rlv2_class, "CurrentData");
        void* module_class = runtime_.nested(current_class, "Module");
        const auto make = [&](void* klass) {
            if (runtime_.class_is_value(klass)) {
                return box_zero(runtime_, klass);
            }
            void* ctor = runtime_.method(klass, ".ctor", 0);
            return construct_with_args(runtime_, klass, ctor, {});
        };
        const auto assign = [&](void* owner, const char* name, void* value) {
            void* field = runtime_.field(runtime_.object_class(owner), name);
            void* expected = runtime_.class_from_type(runtime_.field_type(field));
            if (runtime_.class_is_value(expected)) {
                runtime_.set_value(owner, name, value);
            } else {
                runtime_.set<void*>(owner, name, value);
            }
        };

        void* current = make(current_class);
        il2cpp::Runtime::Root current_root(runtime_, current);
        void* module = make(module_class);
        il2cpp::Runtime::Root module_root(runtime_, module);
        assign(current, "module", module);

        for (const char* child :
             {"player", "map", "inventory", "game", "troop", "buff"}) {
            void* field = runtime_.field(current_class, child);
            void* child_class =
                runtime_.class_from_type(runtime_.field_type(field));
            void* value = make(child_class);
            il2cpp::Runtime::Root child_root(runtime_, value);
            assign(current, child, value);
        }

        for (const char* child :
             {"san", "dice", "totem", "vision", "chaos", "fragment", "disaster",
              "nodeUpgrade", "copper", "wrath", "sky", "gridZone", "scrap",
              "weather"}) {
            void* field = runtime_.field(module_class, child);
            void* child_class =
                runtime_.class_from_type(runtime_.field_type(field));
            if (!child_class) {
                continue;
            }
            void* value = make(child_class);
            il2cpp::Runtime::Root child_root(runtime_, value);
            assign(module, child, value);
        }

        void* game = runtime_.get_object(current, "game");
        if (game) {
            il2cpp::Runtime::Root game_root(runtime_, game);
            void* theme = runtime_.string_new(text.c_str());
            il2cpp::Runtime::Root theme_root(runtime_, theme);
            runtime_.set<void*>(game, "theme", theme);
            void* uid = runtime_.string_new("local-synth");
            il2cpp::Runtime::Root uid_root(runtime_, uid);
            runtime_.set<void*>(game, "uid", uid);
        }

        for (const auto& [kind, id, layer] : extra_buffs) {
            output << "apply " << kind << ":" << id << "\n";
            if (kind == "wrath") {
                void* wrath = runtime_.get_object(module, "wrath");
                il2cpp::Runtime::Root wrath_root(runtime_, wrath);
                void* string_class =
                    runtime_.class_from_name(runtime_.corlib(), "System", "String");
                void* array = runtime_.array_new(string_class, 1);
                il2cpp::Runtime::Root array_root(runtime_, array);
                void* set_value = runtime_.method(
                    runtime_.object_class(array), "SetValue", 2,
                    {"System.Object", "System.Int32"});
                void* value = runtime_.string_new(id.c_str());
                il2cpp::Runtime::Root value_root(runtime_, value);
                int index = 0;
                runtime_.invoke(set_value, array, {value, &index});
                runtime_.set<void*>(wrath, "wraths", array);
                runtime_.set<int>(wrath, "newWrath", 0);
            } else if (kind == "chaos") {
                void* chaos = runtime_.get_object(module, "chaos");
                il2cpp::Runtime::Root chaos_root(runtime_, chaos);
                void* list = empty_collection_for_field(
                    runtime_, runtime_.object_class(chaos), "chaosList");
                il2cpp::Runtime::Root list_root(runtime_, list);
                void* value = runtime_.string_new(id.c_str());
                il2cpp::Runtime::Root value_root(runtime_, value);
                collection_add(runtime_, list, value);
                runtime_.set<void*>(chaos, "chaosList", list);
                runtime_.set<int>(chaos, "level", layer);
                runtime_.set<int>(chaos, "value", layer);
            } else if (kind == "disaster") {
                void* disaster = runtime_.get_object(module, "disaster");
                il2cpp::Runtime::Root disaster_root(runtime_, disaster);
                void* value = runtime_.string_new(id.c_str());
                il2cpp::Runtime::Root value_root(runtime_, value);
                runtime_.set<void*>(disaster, "curDisasterId", value);
            } else if (kind == "weather") {
                void* weather = runtime_.get_object(module, "weather");
                il2cpp::Runtime::Root weather_root(runtime_, weather);
                void* value = runtime_.string_new(id.c_str());
                il2cpp::Runtime::Root value_root(runtime_, value);
                runtime_.set<void*>(weather, "currentMain", value);
            } else if (kind == "weather_sub") {
                void* weather = runtime_.get_object(module, "weather");
                il2cpp::Runtime::Root weather_root(runtime_, weather);
                void* value = runtime_.string_new(id.c_str());
                il2cpp::Runtime::Root value_root(runtime_, value);
                runtime_.set<void*>(weather, "currentSub", value);
            } else {
                output << "  unsupported kind\n";
            }
        }

        void* fake_rlv2 = make(rlv2_class);
        il2cpp::Runtime::Root rlv2_root(runtime_, fake_rlv2);
        void* fake_holder = current;
        if (holder_type == "Torappu.PlayerRoguelikeV2") {
            runtime_.set<void*>(fake_rlv2, "current", current);
            fake_holder = fake_rlv2;
        }
        void* field_class = runtime_.class_from_type(
            runtime_.field_type(holder_field));
        if (field_class && runtime_.class_is_value(field_class)) {
            throw std::runtime_error("静态字段是值类型，暂不支持");
        }
        struct RestoreGuard {
            il2cpp::Runtime* runtime = nullptr;
            void* field = nullptr;
            void* value = nullptr;
            ~RestoreGuard() {
                if (runtime && field) {
                    runtime->field_static_set(field, &value);
                }
            }
        } restore{&runtime_, holder_field, original};
        runtime_.field_static_set(holder_field, &fake_holder);

        const std::vector<std::vector<std::string>> plugin_paths = {
            {"Torappu.UI.Roguelike", "RoguelikeSquadStatePlugin"},
            {"Torappu.UI.Roguelike.RL05", "RL05SquadStatePlugin"},
            {"Torappu.UI.Roguelike.RL06", "RL06SquadStatePlugin"}};
        void* topic_value = runtime_.string_new(text.c_str());
        il2cpp::Runtime::Root topic_root(runtime_, topic_value);
        for (const auto& path : plugin_paths) {
            output << "plugin " << path[1] << "\n";
            try {
                void* plugin_class =
                    runtime_.find(path[0].c_str(), path[1].c_str());
                void* plugin = make(plugin_class);
                il2cpp::Runtime::Root plugin_root(runtime_, plugin);
                void* method = runtime_.method(
                    plugin_class, "GetTopicExtraBuffs", 1, {"System.String"});
                void* list = runtime_.invoke(method, plugin, {topic_value});
                il2cpp::Runtime::Root list_root(runtime_, list);
                const int count = runtime_.count(list);
                output << "  count=" << count << "\n";
                for (int index = 0; index < count; ++index) {
                    void* item = runtime_.at(list, index);
                    if (!item) {
                        continue;
                    }
                    il2cpp::Runtime::Root item_root(runtime_, item);
                    output << "  [" << index << "] "
                           << describe_instance_fields(runtime_, item) << "\n";
                }
            } catch (const std::exception& error) {
                output << "  failed: " << error.what() << "\n";
            }
        }

    } catch (const std::exception& error) {
        output << "synthetic failed: " << error.what() << "\n";
    }

    const std::filesystem::path path =
        std::filesystem::path(host_.root_directory) / "rogue_synth.txt";
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << output.str();
    file.flush();
    publish_result("肉鸽合成结果已写入 " + path.string());
}

void LocalBattle::debug_buff(const std::string& text, int value) {
    try {
        void* controller_class = classes_.battle_controller;
        const auto controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前没有 BattleController");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* manager = nullptr;
        read_field_deep(runtime_, controller, controller_class, "m_objectManger",
                        &manager);
        if (!manager) {
            throw std::runtime_error("BattleController.m_objectManger 为空");
        }
        Root manager_root(runtime_, manager);
        void* manager_class = runtime_.object_class(manager);
        void* list = nullptr;
        read_field_deep(runtime_, manager, manager_class, "m_cachedList", &list);
        if (!list) {
            throw std::runtime_error("对象管理器 m_cachedList 为空");
        }
        Root list_root(runtime_, list);

        void* entity_class = classes_.entity;
        void* character_class = classes_.character;
        void* attr_type_class = runtime_.find("Torappu", "AttributeType");
        void* attributes_class = runtime_.find("Torappu.Battle", "Attributes");
        std::string key = text;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        const char* attr_name =
            key == "atk" ? "ATK" : key == "def" ? "DEF" : key == "hp" ? "MAX_HP"
                                                                     : nullptr;
        if (!attr_name) {
            throw std::runtime_error("只支持 atk / def / hp");
        }
        int attr = runtime_.static_value<int>(attr_type_class, attr_name);
        void* get_raw = runtime_.method(attributes_class, "GetRawValue", 1,
                                        {"Torappu.AttributeType"});
        void* overwrite = runtime_.method(
            attributes_class, "OverwriteRawData", 3,
            {"Torappu.AttributeType", "Torappu.FP", "System.Boolean"});
        const int count = runtime_.count(list);
        int applied = 0;
        std::string sample;
        for (int index = 0; index < count && index < 4096; ++index) {
            void* entity = nullptr;
            try {
                entity = runtime_.at(list, index);
            } catch (...) {
                continue;
            }
            if (!entity) {
                continue;
            }
            void* klass = runtime_.object_class(entity);
            bool is_character = false;
            for (void* cursor = klass; cursor; cursor = runtime_.class_parent(cursor)) {
                if (cursor == character_class) {
                    is_character = true;
                    break;
                }
            }
            if (!is_character) {
                continue;
            }
            if (!method_bool(runtime_, entity, entity_class, "get_isMine")) {
                continue;
            }
            void* attributes = nullptr;
            if (!read_field_deep(runtime_, entity, entity_class, "m_attributes",
                                 &attributes) ||
                !attributes) {
                continue;
            }
            void* raw_box = runtime_.invoke(get_raw, attributes, {&attr});
            if (!raw_box) {
                continue;
            }
            std::int64_t raw = 0;
            std::memcpy(&raw, runtime_.object_unbox(raw_box), sizeof(raw));
            std::int64_t boosted = raw * static_cast<std::int64_t>(value);
            constexpr std::int64_t kLimit = std::int64_t{1} << 58;
            if (boosted > kLimit) boosted = kLimit;
            if (boosted < -kLimit) boosted = -kLimit;
            bool mark_dirty = true;
            runtime_.invoke(overwrite, attributes, {&attr, &boosted, &mark_dirty});
            if (sample.empty()) {
                std::int64_t check = 0;
                try {
                    void* check_box =
                        runtime_.invoke(get_raw, attributes, {&attr});
                    if (check_box) {
                        std::memcpy(&check, runtime_.object_unbox(check_box),
                                    sizeof(check));
                    }
                } catch (...) {
                }
                sample = std::to_string(raw) + " -> " + std::to_string(check);
            }
            ++applied;
        }
        publish_result("调试增益已应用：属性 " + std::string(attr_name) + " ×" +
                       std::to_string(value) + "，单位 " +
                       std::to_string(applied) +
                       (sample.empty() ? "" : "，样例 " + sample));
    } catch (const std::exception& error) {
        publish_result(std::string("调试增益失败：") + error.what(), false);
    }
}

void* LocalBattle::player_data_model() {
    try {
        void* wrapper = classes_.player_prefs;
        void* getter = runtime_.method(wrapper, "get_s_playerDataLink", 0);
        void* link = runtime_.invoke(getter, nullptr, {});
        if (link) {
            il2cpp::Runtime::Root link_root(runtime_, link);
            void* model = nullptr;
            if (read_field_deep(runtime_, link, runtime_.object_class(link), "m_data",
                                &model) &&
                model) {
                return model;
            }
        }
    } catch (...) {
    }
    void* player = player_data_instance_.load(std::memory_order_acquire);
    if (player) {
        void* model = nullptr;
        if (read_field_deep(runtime_, player, runtime_.object_class(player), "m_data",
                             &model) &&
            model) {
            return model;
        }
    }
    return nullptr;
}

void LocalBattle::rogue_out_buffs(const std::string& text) {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        std::ostringstream output;
        output << "topic " << (text.empty() ? "<current>" : text) << "\n";
        bool pending_battle = false;
        int pending_type = -1;
        void* model = player_data_model();
        if (model) {
            il2cpp::Runtime::Root model_root(runtime_, model);
            void* rlv2 = nullptr;
            read_field_deep(runtime_, model, runtime_.object_class(model),
                            "rlv2", &rlv2);
            if (rlv2) {
                il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
                void* current = runtime_.get_object(rlv2, "current");
                if (current) {
                    il2cpp::Runtime::Root current_root(runtime_, current);
                    void* player = nullptr;
                    read_field_deep(runtime_, current,
                                    runtime_.object_class(current), "player",
                                    &player);
                    if (player) {
                        il2cpp::Runtime::Root player_root(runtime_, player);
                        void* pending = nullptr;
                        read_field_deep(runtime_, player,
                                        runtime_.object_class(player), "pending",
                                        &pending);
                        if (pending && runtime_.count(pending) > 0) {
                            il2cpp::Runtime::Root pending_root(runtime_, pending);
                            void* event = runtime_.at(pending, 0);
                            if (event) {
                                il2cpp::Runtime::Root event_root(runtime_, event);
                                read_field_deep(
                                    runtime_, event,
                                    runtime_.object_class(event), "type",
                                    &pending_type);
                                void* type_class = runtime_.find(
                                    "Torappu", "PlayerRoguelikePlayerEventType");
                                pending_battle =
                                    pending_type == runtime_.enum_value(
                                                        type_class, "BATTLE");
                            }
                        }
                    }
                }
            }
        }
        output << "pending[0].type=" << pending_type
               << " isBattle=" << (pending_battle ? 1 : 0) << "\n";
        void* list = nullptr;
        std::string source;
        if (pending_battle) {
            void* util = classes_.roguelike_data_util;
            void* method = runtime_.method(util, "GetOutBuffs", 0);
            list = runtime_.invoke(method, nullptr, {});
            source = "GetOutBuffs";
        } else {
            try {
                void* controller_class =
                    classes_.battle_controller;
                const auto controllers =
                    find_controllers(runtime_, controller_class);
                if (!controllers.empty()) {
                    il2cpp::Runtime::Root controller_root(runtime_,
                                                          controllers.front());
                    void* game_mode =
                        runtime_.get_object(controllers.front(), "m_gameMode");
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
                                            "outerBuffs", &list);
                            if (list) {
                                source = "battle.outerBuffs";
                            }
                        }
                    }
                }
            } catch (...) {
            }
            if (!list) {
                throw std::runtime_error(
                    "GetOutBuffs 需要 pending[0] 为 BATTLE；当前 type=" +
                    std::to_string(pending_type) +
                    "，且没有战斗内 outerBuffs 可读");
            }
        }
        output << source << "=" << list;
        if (list) {
            output << " class=" << runtime_.klass_name(runtime_.object_class(list));
        }
        output << "\n";
        if (!list) {
            const std::filesystem::path path =
                std::filesystem::path(host_.root_directory) / "rogue_out_buffs.txt";
            std::ofstream file(path, std::ios::out | std::ios::trunc);
            file << output.str();
            file.flush();
            publish_result("肉鸽局外 Buff 结果为空，已写入 " + path.string(), false);
            return;
        }
        il2cpp::Runtime::Root list_root(runtime_, list);
        const int count = runtime_.count(list);
        output << "count=" << count << "\n";
        for (int index = 0; index < count; ++index) {
            void* item = runtime_.at(list, index);
            if (!item) {
                output << "[" << index << "] null\n";
                continue;
            }
            il2cpp::Runtime::Root item_root(runtime_, item);
            void* klass = runtime_.object_class(item);
            output << "[" << index << "] "
                   << runtime_.klass_name(klass) << "\n";
            output << "  key=" << read_string_field(runtime_, item, klass, "key")
                   << "\n";
            void* board = nullptr;
            if (!read_field_deep(runtime_, item, klass, "blackboard", &board) ||
                !board) {
                output << "  blackboard=<null>\n";
                continue;
            }
            il2cpp::Runtime::Root board_root(runtime_, board);
            void* board_class = runtime_.object_class(board);
            output << "  blackboard=" << runtime_.klass_name(board_class) << "\n";
            void* items = nullptr;
            if (!read_field_deep(runtime_, board, board_class, "_items", &items) ||
                !items) {
                output << "    _items=<null>\n";
                continue;
            }
            il2cpp::Runtime::Root items_root(runtime_, items);
            const int item_count = runtime_.count(items);
            output << "    _items count=" << item_count << "\n";
            for (int pair_index = 0; pair_index < item_count; ++pair_index) {
                void* pair = runtime_.at(items, pair_index);
                if (!pair) {
                    continue;
                }
                il2cpp::Runtime::Root pair_root(runtime_, pair);
                void* pair_class = runtime_.object_class(pair);
                const std::string key =
                    read_string_field(runtime_, pair, pair_class, "key");
                float value = 0.0f;
                read_field_deep(runtime_, pair, pair_class, "value", &value);
                const std::string value_str =
                    read_string_field(runtime_, pair, pair_class, "valueStr");
                output << "      " << key << " = " << value;
                if (!value_str.empty()) {
                    output << " (" << value_str << ")";
                }
                output << "\n";
            }
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_out_buffs.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("肉鸽局外 Buff 结果写入失败");
        }
        publish_result("肉鸽局外 Buff 已写入 " + path.string() + "，共 " +
                       std::to_string(count) + " 项");
    } catch (const std::exception& error) {
        publish_result(std::string("读取肉鸽局外 Buff 失败：") + error.what(), false);
    }
}

void LocalBattle::rogue_player_dump() {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        std::ostringstream output;
        void* model = player_data_model();
        output << "player_data=" << player_data_instance_.load()
               << " model=" << model << "\n";
        if (!model) {
            throw std::runtime_error(
                "尚未捕获 PlayerData；请等待主界面刷新后重试");
        }
        il2cpp::Runtime::Root model_root(runtime_, model);
        void* model_class = runtime_.object_class(model);
        void* rlv2 = nullptr;
        read_field_deep(runtime_, model, model_class, "rlv2", &rlv2);
        output << "rlv2=" << rlv2 << "\n";
        void* current = nullptr;
        void* rlv2_class = nullptr;
        if (rlv2) {
            il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
            rlv2_class = runtime_.object_class(rlv2);
            output << "rlv2 fields " << describe_instance_fields(runtime_, rlv2) << "\n";
            current = runtime_.get_object(rlv2, "current");
        }
        output << "current=" << current << "\n";
        if (!current) {
            const std::filesystem::path path =
                std::filesystem::path(host_.root_directory) / "rogue_player_dump.txt";
            std::ofstream file(path, std::ios::out | std::ios::trunc);
            file << output.str();
            file.flush();
            publish_result("当前没有肉鸽局，状态已写入 " + path.string(), false);
            return;
        }
        il2cpp::Runtime::Root current_root(runtime_, current);
        void* current_class = runtime_.object_class(current);
        output << "current fields "
               << describe_instance_fields(runtime_, current) << "\n";

        void* game = nullptr;
        void* module = nullptr;
        void* player = nullptr;
        read_field_deep(runtime_, current, current_class, "game", &game);
        read_field_deep(runtime_, current, current_class, "module", &module);
        read_field_deep(runtime_, current, current_class, "player", &player);
        if (player) {
            il2cpp::Runtime::Root player_root(runtime_, player);
            void* player_class = runtime_.object_class(player);
            output << "player " << describe_instance_fields(runtime_, player) << "\n";
            void* pending = nullptr;
            read_field_deep(runtime_, player, player_class, "pending", &pending);
            if (pending) {
                il2cpp::Runtime::Root pending_root(runtime_, pending);
                const int count = runtime_.count(pending);
                output << "  pending count=" << count << "\n";
                for (int index = 0; index < count; ++index) {
                    void* event = runtime_.at(pending, index);
                    if (!event) {
                        continue;
                    }
                    il2cpp::Runtime::Root event_root(runtime_, event);
                    void* event_class = runtime_.object_class(event);
                    int type = -1;
                    read_field_deep(runtime_, event, event_class, "type", &type);
                    output << "    [" << index << "] type=" << type
                           << " index="
                           << read_string_field(runtime_, event, event_class,
                                                "index")
                           << " " << describe_instance_fields(runtime_, event)
                           << "\n";
                    void* content = nullptr;
                    if (read_field_deep(runtime_, event, event_class, "content",
                                        &content) &&
                        content) {
                        il2cpp::Runtime::Root content_root(runtime_, content);
                        output << "      content "
                               << describe_instance_fields(runtime_, content)
                               << "\n";
                        void* battle = nullptr;
                        if (read_field_deep(runtime_, content,
                                            runtime_.object_class(content),
                                            "battle", &battle) &&
                            battle) {
                            il2cpp::Runtime::Root battle_root(runtime_, battle);
                            output << "        battle "
                                   << describe_instance_fields(runtime_, battle)
                                   << "\n";
                        }
                    }
                }
            }
        }
        if (game) {
            il2cpp::Runtime::Root game_root(runtime_, game);
            void* game_class = runtime_.object_class(game);
            output << "game " << describe_instance_fields(runtime_, game) << "\n";
            const std::string theme =
                read_string_field(runtime_, game, game_class, "theme");
            const std::string uid =
                read_string_field(runtime_, game, game_class, "uid");
            int mode = -1;
            int difficult = -1;
            int grade = -1;
            int eq_grade = -1;
            int64_t start = 0;
            read_field_deep(runtime_, game, game_class, "mode", &mode);
            read_field_deep(runtime_, game, game_class, "difficult", &difficult);
            read_field_deep(runtime_, game, game_class, "modeGrade", &grade);
            read_field_deep(runtime_, game, game_class, "equivalentGrade",
                             &eq_grade);
            read_field_deep(runtime_, game, game_class, "start", &start);
            output << "  theme=" << theme << " uid=" << uid
                   << " mode=" << mode << " difficult=" << difficult
                   << " modeGrade=" << grade << " equivalentGrade=" << eq_grade
                   << " start=" << start << "\n";
            void* outer = nullptr;
            if (read_field_deep(runtime_, game, game_class, "outerBuff", &outer) &&
                outer) {
                il2cpp::Runtime::Root outer_root(runtime_, outer);
                output << "  outerBuff "
                       << describe_instance_fields(runtime_, outer) << "\n";
            }
        }
        if (!module) {
            throw std::runtime_error("current.module 为空");
        }
        il2cpp::Runtime::Root module_root(runtime_, module);
        void* module_class = runtime_.object_class(module);
        output << "module " << describe_instance_fields(runtime_, module) << "\n";
        const auto child = [&](const char* name) -> void* {
            void* value = nullptr;
            read_field_deep(runtime_, module, module_class, name, &value);
            return value;
        };
        const auto dump_string_list = [&](const char* label, void* list) {
            output << "  " << label << "=";
            if (!list) {
                output << "<null>\n";
                return;
            }
            il2cpp::Runtime::Root root(runtime_, list);
            const int count = runtime_.count(list);
            output << "count=" << count << "\n";
            for (int index = 0; index < count; ++index) {
                void* value = runtime_.at(list, index);
                output << "    [" << index << "] "
                       << (value ? runtime_.string(value) : "<null>") << "\n";
            }
        };
        if (void* chaos = child("chaos")) {
            il2cpp::Runtime::Root root(runtime_, chaos);
            void* klass = runtime_.object_class(chaos);
            int value = 0;
            int level = 0;
            int max_value = 0;
            read_field_deep(runtime_, chaos, klass, "value", &value);
            read_field_deep(runtime_, chaos, klass, "level", &level);
            read_field_deep(runtime_, chaos, klass, "curMaxValue", &max_value);
            output << "  chaos value=" << value << " level=" << level
                   << " curMaxValue=" << max_value
                   << " predict=" << read_string_field(runtime_, chaos, klass, "predict")
                   << "\n";
            void* list = nullptr;
            read_field_deep(runtime_, chaos, klass, "chaosList", &list);
            dump_string_list("chaosList", list);
        }
        if (void* disaster = child("disaster")) {
            il2cpp::Runtime::Root root(runtime_, disaster);
            void* klass = runtime_.object_class(disaster);
            int step = 0;
            read_field_deep(runtime_, disaster, klass, "disperseStep", &step);
            output << "  disaster id="
                   << read_string_field(runtime_, disaster, klass, "curDisasterId")
                   << " disperseStep=" << step << "\n";
        }
        if (void* wrath = child("wrath")) {
            il2cpp::Runtime::Root root(runtime_, wrath);
            void* klass = runtime_.object_class(wrath);
            int new_wrath = 0;
            read_field_deep(runtime_, wrath, klass, "newWrath", &new_wrath);
            output << "  wrath newWrath=" << new_wrath << "\n";
            void* list = nullptr;
            read_field_deep(runtime_, wrath, klass, "wraths", &list);
            dump_string_list("wraths", list);
        }
        if (void* weather = child("weather")) {
            il2cpp::Runtime::Root root(runtime_, weather);
            void* klass = runtime_.object_class(weather);
            int step = 0;
            read_field_deep(runtime_, weather, klass, "weatherStep", &step);
            output << "  weather main="
                   << read_string_field(runtime_, weather, klass, "currentMain")
                   << " sub="
                   << read_string_field(runtime_, weather, klass, "currentSub")
                   << " eye=" << read_string_field(runtime_, weather, klass, "eye")
                   << " step=" << step << "\n";
            void* areas = nullptr;
            read_field_deep(runtime_, weather, klass, "effectArea", &areas);
            if (areas) {
                il2cpp::Runtime::Root areas_root(runtime_, areas);
                const auto pairs = read_dictionary_pairs(runtime_, areas, 128);
                output << "    effectArea count=" << pairs.size() << "\n";
                for (const auto& pair : pairs) {
                    int value = 0;
                    if (pair.value) {
                        std::memcpy(&value, runtime_.object_unbox(pair.value),
                                    sizeof(value));
                    }
                    output << "      "
                           << (pair.key ? runtime_.string(pair.key) : "<null>")
                           << "=" << value << "\n";
                }
            }
        }
        if (void* buff = child("buff")) {
            il2cpp::Runtime::Root root(runtime_, buff);
            void* klass = runtime_.object_class(buff);
            int tmp_hp = 0;
            read_field_deep(runtime_, buff, klass, "tmpHP", &tmp_hp);
            output << "  buff tmpHP=" << tmp_hp << "\n";
            void* list = nullptr;
            read_field_deep(runtime_, buff, klass, "squadBuff", &list);
            dump_string_list("squadBuff", list);
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_player_dump.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("肉鸽玩家状态写入失败");
        }
        publish_result("肉鸽玩家状态已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("读取肉鸽玩家状态失败：") + error.what(), false);
    }
}

void LocalBattle::rogue_player_json() {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        void* model = player_data_model();
        if (!model) {
            throw std::runtime_error("尚未捕获 PlayerData");
        }
        il2cpp::Runtime::Root model_root(runtime_, model);
        void* serialize =
            runtime_.method(runtime_.object_class(model), "Serialize", 0);
        void* text = runtime_.invoke(serialize, model, {});
        if (!text) {
            throw std::runtime_error("PlayerDataModel.Serialize 返回空");
        }
        il2cpp::Runtime::Root text_root(runtime_, text);
        const int length = runtime_.string_length(text);
        constexpr int kMaxPlayerJsonChars = 64 * 1024 * 1024;
        if (length < 0 || length > kMaxPlayerJsonChars) {
            throw std::runtime_error("PlayerData JSON 长度异常");
        }
        const wchar_t* chars = runtime_.string_chars(text);
        const int bytes = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr,
            nullptr);
        if (!bytes && length) {
            throw std::runtime_error("PlayerData JSON UTF-16 转换失败");
        }
        std::string json(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
        if (bytes) {
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length,
                                json.data(), bytes, nullptr, nullptr);
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_player_data.json";
        std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
        file << json;
        file.flush();
        if (!file) {
            throw std::runtime_error("PlayerData JSON 写入失败");
        }
        publish_result("PlayerData JSON 已写入 " + path.string() + "，大小 " +
                       std::to_string(json.size()) + " 字节");
    } catch (const std::exception& error) {
        publish_result(std::string("导出 PlayerData JSON 失败：") + error.what(),
                       false);
    }
}

void LocalBattle::rogue_pending_detail() {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        void* model = player_data_model();
        if (!model) {
            throw std::runtime_error("尚未捕获 PlayerData");
        }
        il2cpp::Runtime::Root model_root(runtime_, model);
        void* rlv2 = nullptr;
        read_field_deep(runtime_, model, runtime_.object_class(model), "rlv2",
                        &rlv2);
        if (!rlv2) {
            throw std::runtime_error("PlayerData.rlv2 为空");
        }
        il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
        void* current = runtime_.get_object(rlv2, "current");
        if (!current) {
            throw std::runtime_error("当前没有肉鸽局状态");
        }
        il2cpp::Runtime::Root current_root(runtime_, current);
        void* player = nullptr;
        read_field_deep(runtime_, current, runtime_.object_class(current),
                        "player", &player);
        if (!player) {
            throw std::runtime_error("current.player 为空");
        }
        il2cpp::Runtime::Root player_root(runtime_, player);
        void* pending = nullptr;
        read_field_deep(runtime_, player, runtime_.object_class(player),
                        "pending", &pending);
        if (!pending) {
            throw std::runtime_error("current.player.pending 为空");
        }
        il2cpp::Runtime::Root pending_root(runtime_, pending);
        void* zip = runtime_.find("Torappu", "ZipUtils");
        void* decompress = runtime_.method(
            zip, "DecompressStrFromBase64", 1, {"System.String"});
        const std::filesystem::path directory =
            std::filesystem::path(host_.root_directory) / "rogue_pending";
        std::filesystem::create_directories(directory);
        std::ostringstream manifest;
        const int count = runtime_.count(pending);
        manifest << "pending count=" << count << "\n";
        for (int index = 0; index < count; ++index) {
            void* event = runtime_.at(pending, index);
            if (!event) {
                continue;
            }
            il2cpp::Runtime::Root event_root(runtime_, event);
            void* event_class = runtime_.object_class(event);
            void* content = nullptr;
            read_field_deep(runtime_, event, event_class, "content", &content);
            std::string event_index =
                read_string_field(runtime_, event, event_class, "index");
            if (event_index.empty()) {
                event_index = std::to_string(index);
            }
            int type = -1;
            read_field_deep(runtime_, event, event_class, "type", &type);
            std::string detail;
            if (content) {
                il2cpp::Runtime::Root content_root(runtime_, content);
                detail = read_string_field(runtime_, content,
                                           runtime_.object_class(content),
                                           "detailStr");
            }
            manifest << "[" << index << "] type=" << type
                     << " index=" << event_index
                     << " detail_chars=" << detail.size() << "\n";
            if (detail.empty()) {
                continue;
            }
            std::string safe_index;
            for (const char ch : event_index) {
                safe_index.push_back(std::isalnum(static_cast<unsigned char>(ch))
                                         ? ch
                                         : '_');
            }
            const std::filesystem::path raw_path =
                directory / ("pending-" + std::to_string(index) + "-" +
                             safe_index + ".b64");
            {
                std::ofstream file(raw_path, std::ios::out | std::ios::trunc |
                                                 std::ios::binary);
                file << detail;
            }
            void* source = runtime_.string_new(detail.c_str());
            if (!source) {
                manifest << "  decompress skipped: string_new null\n";
                continue;
            }
            il2cpp::Runtime::Root source_root(runtime_, source);
            void* text = runtime_.invoke(decompress, nullptr, {source});
            if (!text) {
                manifest << "  decompress empty\n";
                continue;
            }
            il2cpp::Runtime::Root text_root(runtime_, text);
            const std::string decoded = runtime_.string(text);
            const std::filesystem::path json_path =
                directory / ("pending-" + std::to_string(index) + "-" +
                             safe_index + ".json");
            std::ofstream file(json_path, std::ios::out | std::ios::trunc |
                                              std::ios::binary);
            file << decoded;
            file.flush();
            manifest << "  decoded=" << json_path.filename().string()
                     << " bytes=" << decoded.size() << "\n";
        }
        const std::filesystem::path manifest_path =
            directory / "manifest.txt";
        std::ofstream manifest_file(manifest_path,
                                    std::ios::out | std::ios::trunc);
        manifest_file << manifest.str();
        manifest_file.flush();
        publish_result("pending detail 已写入 " + directory.string());
    } catch (const std::exception& error) {
        publish_result(std::string("导出 pending detail 失败：") + error.what(),
                       false);
    }
}


}  // namespace ark::better_battle
