#include "battle_helpers.h"

#include <nlohmann/json.hpp>

namespace ark::better_battle {

void LocalBattle::capture_player_delta(void* delta) {
    if (!delta) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    {
        std::lock_guard lock(player_data_delta_mutex_);
        if (delta == last_captured_delta_ &&
            now - last_captured_delta_tick_ < 100) {
            return;
        }
        last_captured_delta_ = delta;
        last_captured_delta_tick_ = now;
    }
    void* delta_class = runtime_.object_class(delta);
    void* modified = nullptr;
    void* deleted = nullptr;
    read_field_deep(runtime_, delta, delta_class, "modified", &modified);
    read_field_deep(runtime_, delta, delta_class, "deleted", &deleted);
    const auto to_json = [&](void* value) {
        if (!value) {
            return std::string("null");
        }
        il2cpp::Runtime::Root value_root(runtime_, value);
        void* to_string =
            runtime_.method(runtime_.object_class(value), "ToString", 0);
        void* text = runtime_.invoke(to_string, value, {});
        if (!text) {
            return std::string("null");
        }
        il2cpp::Runtime::Root text_root(runtime_, text);
        const int length = runtime_.string_length(text);
        constexpr int kMaxDeltaChars = 32 * 1024 * 1024;
        if (length < 0 || length > kMaxDeltaChars) {
            return std::string("\"<invalid-length>\"");
        }
        const wchar_t* chars = runtime_.string_chars(text);
        const int bytes = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr,
            nullptr);
        if (!bytes && length) {
            return std::string("\"<invalid-utf16>\"");
        }
        std::string json(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
        if (bytes) {
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length,
                                json.data(), bytes, nullptr, nullptr);
        }
        return json;
    };
    const std::string modified_json = to_json(modified);
    const std::string deleted_json = to_json(deleted);
    const auto to_value = [](const std::string& raw) {
        try {
            return nlohmann::ordered_json::parse(raw);
        } catch (...) {
            return nlohmann::ordered_json(raw);
        }
    };
    nlohmann::ordered_json document;
    document["modified"] = to_value(modified_json);
    document["deleted"] = to_value(deleted_json);
    static std::uint64_t sequence = 0;
    const std::filesystem::path directory =
        std::filesystem::path(host_.root_directory) / "rogue_net";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path =
        directory / ("delta-" + std::to_string(now) + "-" +
                     std::to_string(++sequence) + ".json");
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    file << document.dump() << '\n';
    file.flush();
}

void LocalBattle::maybe_queue_pending_capture() {
    if (!player_data_delta_active_.load(std::memory_order_acquire)) {
        return;
    }
    try {
        void* model = player_data_model();
        if (!model) {
            return;
        }
        il2cpp::Runtime::Root model_root(runtime_, model);
        void* rlv2 = nullptr;
        read_field_deep(runtime_, model, runtime_.object_class(model), "rlv2",
                        &rlv2);
        if (!rlv2) {
            return;
        }
        il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
        void* current = runtime_.get_object(rlv2, "current");
        if (!current) {
            return;
        }
        il2cpp::Runtime::Root current_root(runtime_, current);
        void* player = nullptr;
        read_field_deep(runtime_, current, runtime_.object_class(current),
                        "player", &player);
        if (!player) {
            return;
        }
        il2cpp::Runtime::Root player_root(runtime_, player);
        void* pending = nullptr;
        read_field_deep(runtime_, player, runtime_.object_class(player),
                        "pending", &pending);
        if (!pending || runtime_.count(pending) <= 0) {
            last_pending_capture_index_.clear();
            return;
        }
        il2cpp::Runtime::Root pending_root(runtime_, pending);
        void* event = runtime_.at(pending, 0);
        if (!event) {
            return;
        }
        il2cpp::Runtime::Root event_root(runtime_, event);
        void* event_class = runtime_.object_class(event);
        int type = -1;
        read_field_deep(runtime_, event, event_class, "type", &type);
        void* type_class =
            runtime_.find("Torappu", "PlayerRoguelikePlayerEventType");
        if (type != runtime_.enum_value(type_class, "BATTLE")) {
            last_pending_capture_index_.clear();
            return;
        }
        std::string index =
            read_string_field(runtime_, event, event_class, "index");
        if (index.empty()) {
            return;
        }
        if (index == last_pending_capture_index_) {
            return;
        }
        last_pending_capture_index_ = index;
        {
            std::lock_guard lock(mutex_);
            if (std::any_of(pending_.begin(), pending_.end(),
                            [](const Command& command) {
                                return command.name == "rogue-capture";
                            })) {
                return;
            }
            pending_.push_back(Command{"rogue-capture", {}});
        }
        log(kLogInfo, "检测到服务端 BATTLE pending，已排队自动捕获 index=" +
                          index);
    } catch (...) {
    }
}

void LocalBattle::queue_global_buff_capture(void* list) {
    if (!list) {
        return;
    }
    auto root = std::make_unique<il2cpp::Runtime::Root>(runtime_, list);
    {
        std::lock_guard lock(pending_global_mutex_);
        pending_global_list_root_ = std::move(root);
    }
    if (pending_global_capture_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        pending_.push_back(Command{"rogue-global-capture", {}});
    }
}

void LocalBattle::capture_queued_global_buffs() {
    std::unique_ptr<il2cpp::Runtime::Root> root;
    {
        std::lock_guard lock(pending_global_mutex_);
        root = std::move(pending_global_list_root_);
    }
    if (root && root->get()) {
        capture_global_buff_list(root->get());
    }
    pending_global_capture_.store(false, std::memory_order_release);
    bool has_pending = false;
    {
        std::lock_guard lock(pending_global_mutex_);
        has_pending = pending_global_list_root_ != nullptr;
    }
    if (has_pending &&
        !pending_global_capture_.exchange(true, std::memory_order_acq_rel)) {
        std::lock_guard lock(mutex_);
        pending_.push_back(Command{"rogue-global-capture", {}});
    }
}

void LocalBattle::capture_global_buff_list(void* list) {
    if (!list) {
        return;
    }
    il2cpp::Runtime::Attachment attachment(runtime_);
    Root list_root(runtime_, list);
    const int count = runtime_.count(list);
    const std::uint64_t captured_at = GetTickCount64();
    nlohmann::ordered_json document;
    document["capturedAt"] = captured_at;
    document["count"] = count;
    nlohmann::ordered_json buffs = nlohmann::ordered_json::array();
    std::string summary;

    const auto read_blackboard = [&](void* board) {
        nlohmann::ordered_json entries = nlohmann::ordered_json::array();
        if (!board) {
            return entries;
        }
        Root board_root(runtime_, board);
        void* pairs = nullptr;
        try {
            read_field_deep(runtime_, board, runtime_.object_class(board),
                            "_items", &pairs);
        } catch (...) {
            pairs = nullptr;
        }
        if (!pairs) {
            return entries;
        }
        Root pairs_root(runtime_, pairs);
        const int pair_count = runtime_.count(pairs);
        for (int pair_index = 0; pair_index < pair_count && pair_index < 512;
             ++pair_index) {
            void* pair = nullptr;
            try {
                pair = runtime_.at(pairs, pair_index);
            } catch (...) {
                continue;
            }
            if (!pair) {
                continue;
            }
            Root pair_root(runtime_, pair);
            void* pair_class = runtime_.object_class(pair);
            float value = 0.0f;
            try {
                read_field_deep(runtime_, pair, pair_class, "value", &value);
            } catch (...) {
            }
            const std::string key =
                read_string_field(runtime_, pair, pair_class, "key");
            const std::string value_str =
                read_string_field(runtime_, pair, pair_class, "valueStr");
            if (key.empty() && value_str.empty()) {
                continue;
            }
            nlohmann::ordered_json entry = {
                {"key", key},
                {"value", value},
            };
            if (!value_str.empty()) {
                entry["value_str"] = value_str;
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    };

    for (int index = 0; index < count && index < 512; ++index) {
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
        void* item_class = runtime_.object_class(item);
        nlohmann::ordered_json buff;
        buff["class"] = runtime_.klass_name(item_class);

        std::unordered_set<std::string> seen_fields;
        std::string prefab_key;
        for (void* klass = item_class; klass;
             klass = runtime_.class_parent(klass)) {
            void* iterator = nullptr;
            while (void* field = runtime_.class_fields(klass, &iterator)) {
                const char* raw_name = runtime_.field_name(field);
                if (!raw_name || !*raw_name) {
                    continue;
                }
                const std::string name = raw_name;
                if (!seen_fields.insert(name).second) {
                    continue;
                }
                if (runtime_.field_flags(field) & 0x10) {
                    continue;
                }
                if (name.rfind("__Hotfix", 0) == 0 ||
                    name.rfind("_c__Hotfix", 0) == 0) {
                    continue;
                }
                if (name != "prefabKey" && name != "blackboard") {
                    continue;
                }
                void* field_class = nullptr;
                try {
                    field_class =
                        runtime_.class_from_type(runtime_.field_type(field));
                } catch (...) {
                    continue;
                }
                if (!field_class) {
                    continue;
                }
                std::string type_name;
                try {
                    type_name = runtime_.klass_name(field_class);
                } catch (...) {
                    continue;
                }
                if (type_name == "System.String") {
                    void* value = nullptr;
                    try {
                        value = runtime_.field_get_object(field, item);
                    } catch (...) {
                        continue;
                    }
                    if (!value) {
                        continue;
                    }
                    const std::string text = runtime_.string(value);
                    buff[name] = text;
                    if (name == "prefabKey") {
                        prefab_key = text;
                    }
                    continue;
                }
                void* value = nullptr;
                try {
                    value = runtime_.field_get_object(field, item);
                } catch (...) {
                    continue;
                }
                if (!value) {
                    continue;
                }
                if (name == "blackboard" ||
                    type_name.find("Blackboard") != std::string::npos) {
                    buff[name] = read_blackboard(value);
                    break;
                } else {
                    buff[name] = {
                        {"class", runtime_.klass_name(
                                      runtime_.object_class(value))},
                    };
                }
            }
        }
        try {
            void* source = nullptr;
            if (read_field_deep(runtime_, item, item_class, "source",
                                &source) &&
                source) {
                buff["source"] = {
                    {"class",
                     runtime_.klass_name(runtime_.object_class(source))},
                };
            }
        } catch (...) {
        }
        buffs.push_back(std::move(buff));
        if (!prefab_key.empty()) {
            if (summary.size() < 512) {
                if (!summary.empty()) {
                    summary += ',';
                }
                summary += prefab_key;
            }
        }
    }
    document["buffs"] = std::move(buffs);
    const std::string json = document.dump();
    {
        std::lock_guard lock(global_buff_mutex_);
        last_global_buffs_json_ = json;
        last_global_buffs_count_ = count;
        last_global_buffs_tick_ = captured_at;
    }

    if (rogue_global_capture_.load(std::memory_order_acquire)) {
        try {
            const std::filesystem::path directory =
                std::filesystem::path(host_.root_directory) / "rogue_captures";
            std::filesystem::create_directories(directory);
            const std::filesystem::path path =
                directory / ("global-buffs-" + std::to_string(captured_at) +
                             ".json");
            const std::filesystem::path latest_path =
                std::filesystem::path(host_.root_directory) /
                "rogue_global_buffs_latest.json";
            for (const auto& target : {path, latest_path}) {
                std::ofstream file(target, std::ios::out | std::ios::trunc);
                file << json;
                file.flush();
            }
        } catch (...) {
        }
    }
    log(kLogInfo, "PreprocessGlobalBuff captured count=" +
                      std::to_string(count) +
                      (summary.empty() ? std::string{}
                                       : " prefabKeys=" + summary));
}

std::string LocalBattle::last_global_buffs_json() const {
    std::lock_guard lock(global_buff_mutex_);
    return last_global_buffs_json_;
}

void LocalBattle::rogue_dump_global_buffs() {
    try {
        const std::string json = last_global_buffs_json();
        if (json.empty()) {
            throw std::runtime_error(
                "尚未捕获 GlobalBuffData；需要先进入 ROGUELIKE 战斗并触发 "
                "PreprocessGlobalBuff");
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) /
            "rogue_global_buffs_latest.json";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << json;
        file.flush();
        if (!file) {
            throw std::runtime_error("GlobalBuffData 写入失败");
        }
        int count = 0;
        {
            std::lock_guard lock(global_buff_mutex_);
            count = last_global_buffs_count_;
        }
        publish_result("已导出 " + std::to_string(count) +
                       " 条 GlobalBuffData：" + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("导出 GlobalBuffData 失败：") +
                           error.what(),
                       false);
    }
}

void LocalBattle::rogue_net_capture(const std::string& text) {
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        if (text == "stop") {
            player_data_delta_active_.store(false, std::memory_order_release);
            rogue_global_capture_.store(false, std::memory_order_release);
            last_pending_capture_index_.clear();
            publish_result("PlayerDataDelta 捕获已停止");
            return;
        }
        install_preprocess_global_buff_hook();
        if (!install_player_data_delta_hook()) {
            throw std::runtime_error("PlayerDataDelta Hook 安装失败");
        }
        if (!install_player_data_notify_hook()) {
            log(kLogWarning,
                "PlayerData.NotifyDataChanged Hook 不可用，仅保留 delta 回调");
        }
        last_pending_capture_index_.clear();
        player_data_delta_active_.store(true, std::memory_order_release);
        rogue_global_capture_.store(true, std::memory_order_release);
        const std::filesystem::path directory =
            std::filesystem::path(host_.root_directory) / "rogue_net";
        std::filesystem::create_directories(directory);
        publish_result("PlayerDataDelta 捕获已开启：" + directory.string());
    } catch (const std::exception& error) {
        publish_result(std::string("PlayerDataDelta 捕获失败：") + error.what(),
                       false);
    }
}

void LocalBattle::probe_rogue_data(const std::string& text) {
    std::ostringstream output;
    output << "topic " << text << "\n";
    const auto note = [&](const std::string& text) {
        output << text << "\n";
        log(kLogInfo, text);
    };

    void* model = nullptr;
    try {
        void* wrapper = classes_.player_prefs;
        void* getter = runtime_.method(wrapper, "get_s_playerDataLink", 0);
        void* link = runtime_.invoke(getter, nullptr, {});
        output << "s_playerDataLink=" << link;
        if (link) {
            output << " class=" << runtime_.klass_name(runtime_.object_class(link));
        }
        output << "\n";
        if (link) {
            il2cpp::Runtime::Root link_root(runtime_, link);
            output << describe_instance_fields(runtime_, link) << "\n";
            try {
                read_field_deep(runtime_, link, runtime_.object_class(link),
                                "m_data", &model);
            } catch (const std::exception& error) {
                output << "m_data read failed: " << error.what() << "\n";
            }
            if (!model) {
                try {
                    void* data_getter =
                        runtime_.method(runtime_.object_class(link), "get_data", 0);
                    model = runtime_.invoke(data_getter, link, {});
                } catch (const std::exception& error) {
                    output << "get_data failed: " << error.what() << "\n";
                }
            }
        }
    } catch (const std::exception& error) {
        output << "player data link failed: " << error.what() << "\n";
    }
    if (!model) {
        try {
            void* player_data_class = classes_.player_data;
            std::vector<void*> rooted =
                find_rooted_objects(runtime_, player_data_class, 8);
            output << "rooted PlayerData objects " << rooted.size() << "\n";
            for (void* object : rooted) {
                il2cpp::Runtime::Root root(runtime_, object);
                output << "rooted_player_data " << object << "\n"
                       << describe_instance_fields(runtime_, object) << "\n";
                void* candidate = nullptr;
                try {
                    read_field_deep(runtime_, object,
                                    runtime_.object_class(object), "m_data",
                                    &candidate);
                } catch (const std::exception& error) {
                    output << "m_data read failed: " << error.what() << "\n";
                }
                if (candidate) {
                    model = candidate;
                    break;
                }
            }
        } catch (const std::exception& error) {
            output << "rooted scan failed: " << error.what() << "\n";
        }
    }
    if (!model) {
        try {
            void* player_data_class = classes_.player_data;
            std::vector<void*> live =
                find_live_objects(runtime_, player_data_class, 8);
            output << "live PlayerData objects " << live.size() << "\n";
            for (void* object : live) {
                il2cpp::Runtime::Root root(runtime_, object);
                output << "player_data " << object << "\n"
                       << describe_instance_fields(runtime_, object) << "\n";
                void* candidate = nullptr;
                try {
                    read_field_deep(runtime_, object,
                                    runtime_.object_class(object), "m_data",
                                    &candidate);
                } catch (const std::exception& error) {
                    output << "m_data read failed: " << error.what() << "\n";
                }
                if (candidate) {
                    model = candidate;
                    break;
                }
            }
        } catch (const std::exception& error) {
            output << "heap scan failed: " << error.what() << "\n";
        }
    }
    if (!model) {
        try {
            void* model_class =
                classes_.player_data_model;
            std::vector<void*> rooted =
                find_rooted_objects(runtime_, model_class, 4);
            output << "rooted PlayerDataModel objects " << rooted.size()
                   << "\n";
            for (void* object : rooted) {
                il2cpp::Runtime::Root root(runtime_, object);
                output << "rooted_player_data_model " << object << "\n"
                       << describe_instance_fields(runtime_, object) << "\n";
            }
            if (!rooted.empty()) {
                model = rooted.front();
            }
        } catch (const std::exception& error) {
            output << "rooted model scan failed: " << error.what() << "\n";
        }
    }
    if (!model) {
        try {
            void* model_class =
                classes_.player_data_model;
            std::vector<void*> live =
                find_live_objects(runtime_, model_class, 4);
            output << "live PlayerDataModel objects " << live.size() << "\n";
            for (void* object : live) {
                il2cpp::Runtime::Root root(runtime_, object);
                output << "player_data_model " << object << "\n"
                       << describe_instance_fields(runtime_, object) << "\n";
            }
            if (!live.empty()) {
                model = live.front();
            }
        } catch (const std::exception& error) {
            output << "model heap scan failed: " << error.what() << "\n";
        }
    }
    output << "model=" << model;
    if (model) {
        output << " class=" << runtime_.klass_name(runtime_.object_class(model));
    }
    output << "\n";

    void* rlv2 = nullptr;
    void* rlv2_class = nullptr;
    void* current = nullptr;
    if (model) {
        il2cpp::Runtime::Root model_root(runtime_, model);
        try {
            void* rlv2_field = runtime_.field(runtime_.object_class(model), "rlv2");
            rlv2_class = runtime_.class_from_type(runtime_.field_type(rlv2_field));
            rlv2 = runtime_.field_get_object(rlv2_field, model);
        } catch (const std::exception& error) {
            output << "rlv2 read failed: " << error.what() << "\n";
        }
        output << "rlv2=" << rlv2 << "\n";
        if (rlv2) {
            il2cpp::Runtime::Root rlv2_root(runtime_, rlv2);
            output << describe_instance_fields(runtime_, rlv2) << "\n";
            try {
                current = runtime_.get_object(rlv2, "current");
            } catch (const std::exception& error) {
                output << "current read failed: " << error.what() << "\n";
            }
            output << "current=" << current << "\n";
            if (current) {
                il2cpp::Runtime::Root current_root(runtime_, current);
                output << describe_instance_fields(runtime_, current) << "\n";
            }
        }
    }

    try {
        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        output << "squad plugins:";
        for (std::size_t index = 0; assemblies && index < assembly_count; ++index) {
            void* image = runtime_.assembly_image(assemblies[index]);
            if (!image) {
                continue;
            }
            const std::size_t count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < count; ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                const char* name = klass ? runtime_.class_name(klass) : nullptr;
                if (!name || !std::strstr(name, "SquadStatePlugin")) {
                    continue;
                }
                const char* name_space = runtime_.class_namespace(klass);
                output << " " << (name_space ? name_space : "") << "." << name;
            }
        }
        output << "\n";
    } catch (const std::exception& error) {
        output << "class name scan failed: " << error.what() << "\n";
    }

    if (!rlv2 || !rlv2_class) {
        note("未能取得 PlayerRoguelikeV2，跳过 synthetic 测试");
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "rogue_data_probe.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        publish_result("肉鸽数据探测已写入 " + path.string(), false);
        return;
    }

    try {
        void* current_data_class = runtime_.nested(rlv2_class, "CurrentData");
        void* module_class = runtime_.nested(current_data_class, "Module");
        void* current_ctor = runtime_.method(current_data_class, ".ctor", 0);
        void* module_ctor = runtime_.method(module_class, ".ctor", 0);
        void* synthetic = construct_with_args(runtime_, current_data_class,
                                              current_ctor, {});
        il2cpp::Runtime::Root synthetic_root(runtime_, synthetic);
        void* module = construct_with_args(runtime_, module_class, module_ctor, {});
        il2cpp::Runtime::Root module_root(runtime_, module);

        const auto construct = [&](void* enclosing, const char* name) {
            void* klass = runtime_.nested(enclosing, name);
            void* ctor = runtime_.method(klass, ".ctor", 0);
            return std::pair<void*, void*>(
                construct_with_args(runtime_, klass, ctor, {}), klass);
        };

        auto [chaos, chaos_class] = construct(module_class, "Chaos");
        il2cpp::Runtime::Root chaos_root(runtime_, chaos);
        auto [disaster, disaster_class] = construct(module_class, "Disaster");
        il2cpp::Runtime::Root disaster_root(runtime_, disaster);
        auto [wrath, wrath_class] = construct(module_class, "Wrath");
        il2cpp::Runtime::Root wrath_root(runtime_, wrath);
        auto [weather, weather_class] = construct(module_class, "Weather");
        il2cpp::Runtime::Root weather_root(runtime_, weather);

        const std::string topic = text;
        const auto new_string = [&](const std::string& value) {
            return runtime_.string_new(value.c_str());
        };
        if (topic == "rogue_3") {
            void* list = empty_collection_for_field(runtime_, chaos_class,
                                                    "chaosList");
            il2cpp::Runtime::Root list_root(runtime_, list);
            const std::string id = "rogue_3_chaos_1_a";
            void* value = new_string(id);
            il2cpp::Runtime::Root value_root(runtime_, value);
            collection_add(runtime_, list, value);
            runtime_.set<void*>(chaos, "chaosList", list);
            runtime_.set<int>(chaos, "value", 3);
            runtime_.set<int>(chaos, "level", 1);
            runtime_.set<int>(chaos, "curMaxValue", 3);
        } else if (topic == "rogue_4") {
            const std::string id = "rogue_4_disaster_3_a";
            void* value = new_string(id);
            il2cpp::Runtime::Root value_root(runtime_, value);
            runtime_.set<void*>(disaster, "curDisasterId", value);
            runtime_.set<int>(disaster, "disperseStep", 1);
        } else if (topic == "rogue_5") {
            void* string_class =
                runtime_.class_from_name(runtime_.corlib(), "System", "String");
            if (!string_class) {
                throw std::runtime_error("缺少 System.String 类型");
            }
            void* array = runtime_.array_new(string_class, 2);
            il2cpp::Runtime::Root array_root(runtime_, array);
            void* set_value = runtime_.method(
                runtime_.object_class(array), "SetValue", 2,
                {"System.Object", "System.Int32"});
            const char* ids[] = {"rogue_5_wrath_1_a_1",
                                 "rogue_5_wrath_2_a_1"};
            for (int index = 0; index < 2; ++index) {
                void* value = new_string(ids[index]);
                il2cpp::Runtime::Root value_root(runtime_, value);
                int slot = index;
                runtime_.invoke(set_value, array, {value, &slot});
            }
            runtime_.set<void*>(wrath, "wraths", array);
            runtime_.set<int>(wrath, "newWrath", 1);
        } else if (topic == "rogue_6") {
            const std::string main = "rogue_6_weather_1_a";
            const std::string sub = "rogue_6_subweather_1";
            void* main_value = new_string(main);
            il2cpp::Runtime::Root main_root(runtime_, main_value);
            void* sub_value = new_string(sub);
            il2cpp::Runtime::Root sub_root(runtime_, sub_value);
            runtime_.set<void*>(weather, "currentMain", main_value);
            runtime_.set<void*>(weather, "currentSub", sub_value);
            runtime_.set<int>(weather, "weatherStep", 1);
        }

        runtime_.set<void*>(module, "chaos", chaos);
        runtime_.set<void*>(module, "disaster", disaster);
        runtime_.set<void*>(module, "wrath", wrath);
        runtime_.set<void*>(module, "weather", weather);
        runtime_.set<void*>(synthetic, "module", module);

        void* previous = runtime_.get_object(rlv2, "current");
        runtime_.set<void*>(rlv2, "current", synthetic);

        std::vector<std::vector<std::string>> plugin_paths = {
            {"Torappu.UI.Roguelike", "RoguelikeSquadStatePlugin"},
            {"Torappu.UI.Roguelike.RL05", "RL05SquadStatePlugin"},
            {"Torappu.UI.Roguelike.RL06", "RL06SquadStatePlugin"}};
        void* topic_value = new_string(topic);
        il2cpp::Runtime::Root topic_root(runtime_, topic_value);
        for (const auto& path : plugin_paths) {
            output << "plugin " << path[0] << "." << path[1] << "\n";
            try {
                void* klass = runtime_.find(path[0].c_str(), path[1].c_str());
                void* ctor = runtime_.method(klass, ".ctor", 0);
                void* instance =
                    construct_with_args(runtime_, klass, ctor, {});
                il2cpp::Runtime::Root instance_root(runtime_, instance);
                void* method = runtime_.method(
                    klass, "GetTopicExtraBuffs", 1, {"System.String"});
                void* list = runtime_.invoke(method, instance, {topic_value});
                il2cpp::Runtime::Root list_root(runtime_, list);
                const int count = runtime_.count(list);
                output << "  count=" << count << "\n";
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
                }
            } catch (const std::exception& error) {
                output << "  failed: " << error.what() << "\n";
            }
        }
        runtime_.set<void*>(rlv2, "current", previous);
    } catch (const std::exception& error) {
        output << "synthetic current data failed: " << error.what() << "\n";
    }

    const std::filesystem::path path =
        std::filesystem::path(host_.root_directory) / "rogue_data_probe.txt";
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << output.str();
    file.flush();
    if (!file) {
        publish_result("肉鸽数据探测结果写入失败", false);
        return;
    }
    publish_result("肉鸽数据探测已写入 " + path.string());
}

void LocalBattle::inspect_field_type(const std::string& text, const std::string& path, const std::string& name) {
    try {
        void* klass = runtime_.find(text.c_str(), path.c_str());
        if (!klass) {
            throw std::runtime_error("类型不存在: " + text + "." + path);
        }
        void* field = runtime_.field(klass, name.c_str());
        if (!field) {
            throw std::runtime_error("字段不存在: " + name);
        }
        void* field_class = runtime_.class_from_type(runtime_.field_type(field));
        if (!field_class) {
            throw std::runtime_error("字段不是类类型: " + name);
        }
        if (field_class == klass) {
            throw std::runtime_error("字段类型与声明类型相同，可能是枚举或值类型");
        }
        std::string file_name = text + "_" + path + "_" + name;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) /
            ("battle_type_" + file_name + ".txt");
        write_type_dump(runtime_, path, field_class);
        publish_result("字段类型信息已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("读取字段类型失败：") + error.what(), false);
    }
}

void LocalBattle::rogue_catalog(const std::string& path) {
    try {
        void* db_class = classes_.roguelike_topic_db;
        void* db = runtime_.singleton(db_class);
        Root db_root(runtime_, db);

        bool inited = false;
        void* get_inited = runtime_.method(db_class, "get_inited", 0);
        void* inited_value = runtime_.invoke(get_inited, db);
        if (inited_value) {
            inited = runtime_.unbox<bool>(inited_value);
        }

        void* get_data = runtime_.method(db_class, "get_data", 0);
        void* table = runtime_.invoke(get_data, db);
        if (!table) {
            throw std::runtime_error("RoguelikeTopicDB.get_data() 返回 null");
        }
        Root table_root(runtime_, table);
        void* table_class = runtime_.object_class(table);
        void* topics = nullptr;
        void* details = nullptr;
        read_field_deep(runtime_, table, table_class, "topics", &topics);
        read_field_deep(runtime_, table, table_class, "details", &details);

        std::ostringstream output;
        output << "RoguelikeTopicDB inited=" << (inited ? 1 : 0)
               << " table=" << runtime_.klass_name(table_class) << "\n";
        const auto topic_pairs = read_dictionary_pairs(runtime_, topics);
        const auto detail_pairs = read_dictionary_pairs(runtime_, details);
        output << "topics=" << topic_pairs.size() << " details=" << detail_pairs.size() << "\n";

        for (const auto& pair : detail_pairs) {
            const std::string topic_id =
                pair.key ? runtime_.string(pair.key) : std::string{};
            output << "\n[topic] " << topic_id << "\n";
            void* detail = pair.value;
            if (!detail) {
                output << "  <null detail>\n";
                continue;
            }
            Root detail_root(runtime_, detail);
            void* detail_class = runtime_.object_class(detail);

            void* difficulties = nullptr;
            read_field_deep(runtime_, detail, detail_class, "difficulties", &difficulties);
            if (difficulties) {
                Root difficulties_root(runtime_, difficulties);
                const int count = runtime_.count(difficulties);
                for (int index = 0; index < count; ++index) {
                    void* difficulty = nullptr;
                    try {
                        difficulty = runtime_.at(difficulties, index);
                    } catch (...) {
                        continue;
                    }
                    if (!difficulty) {
                        continue;
                    }
                    Root difficulty_root(runtime_, difficulty);
                    void* difficulty_class = runtime_.object_class(difficulty);
                    int mode = -1;
                    int grade = -1;
                    read_field_deep(runtime_, difficulty, difficulty_class, "modeDifficulty", &mode);
                    read_field_deep(runtime_, difficulty, difficulty_class, "grade", &grade);
                    const std::string name = read_string_field(
                        runtime_, difficulty, difficulty_class, "name");
                    output << "  difficulty mode=" << mode << " grade=" << grade
                           << " name=" << name << "\n";
                }
            }

            void* stages = nullptr;
            read_field_deep(runtime_, detail, detail_class, "stages", &stages);
            const auto stage_pairs = read_dictionary_pairs(runtime_, stages);
            output << "  stages=" << stage_pairs.size() << "\n";
            for (const auto& stage_pair : stage_pairs) {
                const std::string stage_id =
                    stage_pair.key ? runtime_.string(stage_pair.key) : std::string{};
                void* stage = stage_pair.value;
                if (!stage) {
                    continue;
                }
                Root stage_root(runtime_, stage);
                void* stage_class = runtime_.object_class(stage);
                output << "    " << stage_id
                       << " code=" << read_string_field(
                              runtime_, stage, stage_class, "code")
                       << " name=" << read_string_field(
                              runtime_, stage, stage_class, "name")
                       << " level=" << read_string_field(
                              runtime_, stage, stage_class, "levelId")
                       << " linked=" << read_string_field(
                              runtime_, stage, stage_class, "linkedStageId")
                       << "\n";
            }
        }

        std::string output_path = path;
        while (!output_path.empty() &&
               (output_path.front() == ' ' || output_path.front() == '\t' ||
                output_path.front() == '\r' || output_path.front() == '\n')) {
            output_path.erase(output_path.begin());
        }
        while (!output_path.empty() &&
               (output_path.back() == ' ' || output_path.back() == '\t' ||
                output_path.back() == '\r' || output_path.back() == '\n')) {
            output_path.pop_back();
        }
        if (output_path.empty()) {
            output_path = "battle_rogue_catalog.txt";
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / output_path;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("肉鸽目录写入失败");
        }
        publish_result("肉鸽目录已写入 " + path.string() + "，主题 " +
                       std::to_string(detail_pairs.size()) + " 个");
    } catch (const std::exception& error) {
        publish_result(std::string("读取肉鸽目录失败：") + error.what(), false);
    }
}

}  // namespace ark::better_battle
