#include "battle_helpers.h"

#include <ark/crash_log.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

namespace ark::better_battle {
namespace {

using Json = nlohmann::ordered_json;

struct ScalarPatch {
    std::string field;
    Json value;
};

struct RandomRule {
    std::string group;
    std::string pack;
    std::string key;
    int index = -1;
    bool remove = false;
};

struct ActionPatch {
    int wave = -1;
    int fragment = -1;
    int phase = -1;
    int index = -1;
    std::string enemy;
    std::string hidden_group;
    std::string random_pack;
    std::vector<ScalarPatch> fields;
};

struct ExtraEnemy {
    int wave = 0;
    int fragment = 0;
    std::string enemy;
    int count = 1;
    int route = 0;
    float pre_delay = 0.0f;
    float interval = 1.0f;
};

struct LevelControlConfig {
    bool present = false;
    std::string stage;
    std::string level;
    bool dump_catalog = true;
    bool remove_disabled_hidden = false;
    std::vector<std::string> enable_hidden;
    std::vector<std::string> disable_hidden;
    std::vector<RandomRule> random_rules;
    std::vector<ActionPatch> action_patches;
    std::vector<ExtraEnemy> extra_enemies;
    std::string source_path;
};

struct ActionList {
    void* owner = nullptr;
    void* owner_class = nullptr;
    void* array = nullptr;
    int wave = -1;
    int fragment = -1;
    int phase = -1;
    std::string branch;
    std::vector<void*> actions;
};

struct ActionSite {
    std::size_t list = 0;
    int index = -1;
    void* action = nullptr;
    std::string key;
    std::string hidden_group;
    std::string group;
    std::string pack;
    int weight = 0;
};

std::string json_text(const Json& value) {
    return value.dump();
}

std::string read_optional_string(const Json& object, const char* name) {
    const auto it = object.find(name);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        throw std::runtime_error(std::string("字段 ") + name + " 必须是字符串");
    }
    return it->get<std::string>();
}

int read_optional_int(const Json& object, const char* name, int fallback) {
    const auto it = object.find(name);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        throw std::runtime_error(std::string("字段 ") + name + " 必须是整数");
    }
    return it->get<int>();
}

float read_optional_float(const Json& object, const char* name, float fallback) {
    const auto it = object.find(name);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_number()) {
        throw std::runtime_error(std::string("字段 ") + name + " 必须是数字");
    }
    return it->get<float>();
}

std::vector<std::string> read_string_array(const Json& object, const char* name) {
    std::vector<std::string> result;
    const auto it = object.find(name);
    if (it == object.end() || it->is_null()) {
        return result;
    }
    if (!it->is_array()) {
        throw std::runtime_error(std::string("字段 ") + name + " 必须是数组");
    }
    for (const Json& value : *it) {
        if (!value.is_string()) {
            throw std::runtime_error(std::string("字段 ") + name + " 只能包含字符串");
        }
        result.push_back(value.get<std::string>());
    }
    return result;
}

LevelControlConfig parse_level_control_config(const std::filesystem::path& path) {
    LevelControlConfig config;
    if (!std::filesystem::exists(path)) {
        return config;
    }
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("关卡控制配置无法打开: " + path.string());
    }
    Json document;
    try {
        file >> document;
    } catch (const std::exception& error) {
        throw std::runtime_error("关卡控制配置 JSON 无效: " + path.string() + " " +
                                 error.what());
    }
    if (!document.is_object()) {
        throw std::runtime_error("关卡控制配置根节点必须是对象: " + path.string());
    }
    config.present = true;
    config.source_path = path.string();
    config.stage = read_optional_string(document, "stage");
    config.level = read_optional_string(document, "level");
    config.dump_catalog = document.value("dump_catalog", true);
    config.remove_disabled_hidden =
        document.value("remove_disabled_hidden", false);
    config.enable_hidden = read_string_array(document, "enable_hidden");
    config.disable_hidden = read_string_array(document, "disable_hidden");

    if (const auto it = document.find("random_groups");
        it != document.end() && !it->is_null()) {
        if (!it->is_array()) {
            throw std::runtime_error("random_groups 必须是数组");
        }
        for (const Json& item : *it) {
            if (!item.is_object()) {
                throw std::runtime_error("random_groups[] 必须是对象");
            }
            RandomRule rule;
            rule.group = read_optional_string(item, "group");
            rule.pack = read_optional_string(item, "pack");
            rule.key = read_optional_string(item, "key");
            rule.index = read_optional_int(item, "index", -1);
            rule.remove = item.value("remove", false);
            if (rule.group.empty()) {
                throw std::runtime_error("random_groups[].group 不能为空");
            }
            if (!rule.remove && rule.pack.empty() && rule.key.empty() &&
                rule.index < 0) {
                throw std::runtime_error(
                    "random_groups[] 需要 pack、key、index 或 remove");
            }
            config.random_rules.push_back(std::move(rule));
        }
    }

    if (const auto it = document.find("patch_actions");
        it != document.end() && !it->is_null()) {
        if (!it->is_array()) {
            throw std::runtime_error("patch_actions 必须是数组");
        }
        for (const Json& item : *it) {
            if (!item.is_object()) {
                throw std::runtime_error("patch_actions[] 必须是对象");
            }
            ActionPatch patch;
            patch.wave = read_optional_int(item, "wave", -1);
            patch.fragment = read_optional_int(item, "fragment", -1);
            patch.phase = read_optional_int(item, "phase", -1);
            patch.index = read_optional_int(item, "index", -1);
            patch.enemy = read_optional_string(item, "enemy");
            patch.hidden_group = read_optional_string(item, "hidden_group");
            patch.random_pack = read_optional_string(item, "random_pack");
            for (const auto& [key, value] : item.items()) {
                if (key == "wave" || key == "fragment" || key == "phase" ||
                    key == "index" || key == "enemy" || key == "hidden_group" ||
                    key == "random_pack") {
                    continue;
                }
                patch.fields.push_back({key, value});
            }
            if (patch.fields.empty()) {
                throw std::runtime_error("patch_actions[] 没有字段修改");
            }
            config.action_patches.push_back(std::move(patch));
        }
    }

    if (const auto it = document.find("add_enemies");
        it != document.end() && !it->is_null()) {
        if (!it->is_array()) {
            throw std::runtime_error("add_enemies 必须是数组");
        }
        for (const Json& item : *it) {
            if (!item.is_object()) {
                throw std::runtime_error("add_enemies[] 必须是对象");
            }
            ExtraEnemy extra;
            extra.wave = read_optional_int(item, "wave", 0);
            extra.fragment = read_optional_int(item, "fragment", 0);
            extra.enemy = read_optional_string(item, "enemy");
            extra.count = read_optional_int(item, "count", 1);
            extra.route = read_optional_int(item, "route", 0);
            extra.pre_delay = read_optional_float(item, "preDelay", 0.0f);
            extra.interval = read_optional_float(item, "interval", 1.0f);
            if (extra.enemy.empty()) {
                throw std::runtime_error("add_enemies[].enemy 不能为空");
            }
            if (extra.count <= 0 || extra.route < 0 || extra.wave < 0 ||
                extra.fragment < 0) {
                throw std::runtime_error(
                    "add_enemies[] 的 wave/fragment/route 不能为负，count 必须大于 0");
            }
            config.extra_enemies.push_back(std::move(extra));
        }
    }
    return config;
}

bool config_matches(const LevelControlConfig& config, const std::string& stage_id,
                    const std::string& level_id) {
    if (!config.present) {
        return false;
    }
    if (config.stage.empty() && config.level.empty()) {
        return true;
    }
    if (!config.stage.empty()) {
        if (config.stage == "*" || config.stage == stage_id ||
            config.stage == level_id ||
            (level_id.size() >= config.stage.size() &&
             level_id.ends_with(config.stage))) {
            return true;
        }
    }
    return !config.level.empty() &&
           (config.level == "*" || config.level == level_id ||
            config.level == stage_id);
}

std::string enum_field_value(il2cpp::Runtime& runtime, void* action,
                             void* action_class, const char* field_name) {
    void* field = find_field_deep(runtime, action_class, field_name);
    if (!field) {
        return {};
    }
    void* enum_class = runtime.class_from_type(runtime.field_type(field));
    if (!enum_class) {
        return {};
    }
    int value = 0;
    if (!read_field_deep(runtime, action, action_class, field_name, &value)) {
        return {};
    }
    void* iterator = nullptr;
    while (void* candidate = runtime.class_fields(enum_class, &iterator)) {
        const char* name = runtime.field_name(candidate);
        if (!name || std::strcmp(name, "value__") == 0) {
            continue;
        }
        int candidate_value = 0;
        try {
            runtime.field_static_get(candidate, &candidate_value);
        } catch (...) {
            continue;
        }
        if (candidate_value == value) {
            return name;
        }
    }
    return std::to_string(value);
}

void set_string_field(il2cpp::Runtime& runtime, void* object, void* klass,
                      const char* name, const std::string& value,
                      bool is_null = false) {
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        throw std::runtime_error(std::string("缺少字段: ") + name);
    }
    void* text = nullptr;
    if (!is_null) {
        text = runtime.string_new(value.c_str());
    }
    if (text) {
        Root text_root(runtime, text);
        runtime.field_set_object(object, field, text);
    } else {
        runtime.field_set_object(object, field, nullptr);
    }
}

void set_enum_field(il2cpp::Runtime& runtime, void* object, void* klass,
                    const char* name, const std::string& value) {
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        throw std::runtime_error(std::string("缺少字段: ") + name);
    }
    void* enum_class = runtime.class_from_type(runtime.field_type(field));
    if (!enum_class) {
        throw std::runtime_error(std::string("字段不是枚举: ") + name);
    }
    int enum_value = runtime.enum_value(enum_class, value.c_str());
    runtime.field_set(object, field, &enum_value);
}

bool set_scalar_field(il2cpp::Runtime& runtime, void* object, void* klass,
                      const ScalarPatch& patch) {
    void* field = find_field_deep(runtime, klass, patch.field.c_str());
    if (!field) {
        return false;
    }
    void* field_class = runtime.class_from_type(runtime.field_type(field));
    if (!field_class) {
        return false;
    }
    const std::string type = runtime.klass_name(field_class);
    if (type == "System.String") {
        const bool is_null = patch.value.is_null();
        set_string_field(runtime, object, klass, patch.field.c_str(),
                         is_null ? std::string() : patch.value.get<std::string>(),
                         is_null);
        return true;
    }
    if (type == "System.Boolean") {
        bool value = patch.value.get<bool>();
        runtime.field_set(object, field, &value);
        return true;
    }
    if (type == "System.Int32") {
        int value = patch.value.get<int>();
        runtime.field_set(object, field, &value);
        return true;
    }
    if (type == "System.Single") {
        float value = patch.value.get<float>();
        runtime.field_set(object, field, &value);
        return true;
    }
    if (type == "System.Double") {
        double value = patch.value.get<double>();
        runtime.field_set(object, field, &value);
        return true;
    }
    if (runtime.class_is_value(field_class) && patch.value.is_string()) {
        const std::string enum_name = patch.value.get<std::string>();
        int value = runtime.enum_value(field_class, enum_name.c_str());
        runtime.field_set(object, field, &value);
        return true;
    }
    if (patch.value.is_null() && !runtime.class_is_value(field_class)) {
        runtime.field_set_object(object, field, nullptr);
        return true;
    }
    throw std::runtime_error("不支持的关卡动作字段类型: " + patch.field + " (" +
                             type + ")");
}

std::vector<void*> array_values(il2cpp::Runtime& runtime, void* array) {
    std::vector<void*> values;
    if (!array) {
        return values;
    }
    const int count = runtime.count(array);
    values.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int index = 0; index < count; ++index) {
        values.push_back(runtime.at(array, index));
    }
    return values;
}

int list_set_count(il2cpp::Runtime& runtime, void* list_set) {
    if (!list_set) {
        return 0;
    }
    void* method = find_method_optional(runtime, runtime.object_class(list_set),
                                        "get_count", 0);
    if (!method) {
        throw std::runtime_error("ListSet.get_count 不可用");
    }
    void* boxed = runtime.invoke(method, list_set);
    if (!boxed) {
        return 0;
    }
    return runtime.unbox<int>(boxed);
}

void* list_set_at(il2cpp::Runtime& runtime, void* list_set, int index) {
    if (index < 0 || index >= list_set_count(runtime, list_set)) {
        throw std::runtime_error("ListSet 索引越界");
    }
    void* method = find_method_optional(runtime, runtime.object_class(list_set),
                                        "get_Item", 1);
    if (!method) {
        throw std::runtime_error("ListSet.get_Item 不可用");
    }
    return runtime.invoke(method, list_set, {&index});
}

void replace_array_field(il2cpp::Runtime& runtime, void* owner,
                         void* owner_class, const char* field_name,
                         const std::vector<void*>& values) {
    void* field = find_field_deep(runtime, owner_class, field_name);
    if (!field) {
        throw std::runtime_error(std::string("缺少数组字段: ") + field_name);
    }
    void* array_class = runtime.class_from_type(runtime.field_type(field));
    void* element_class = array_class ? runtime.class_element(array_class) : nullptr;
    if (!array_class || !element_class) {
        throw std::runtime_error(std::string("字段不是数组: ") + field_name);
    }
    void* array = runtime.array_new(element_class, values.size());
    if (!array) {
        throw std::runtime_error(std::string("数组分配失败: ") + field_name);
    }
    Root array_root(runtime, array);
    void* array_base = runtime.find("System", "Array");
    void* set_value =
        runtime.method(array_base, "SetValue", 2,
                       {"System.Object", "System.Int32"});
    for (std::size_t index = 0; index < values.size(); ++index) {
        int managed_index = static_cast<int>(index);
        runtime.invoke(set_value, array, {values[index], &managed_index});
    }
    runtime.field_set_object(owner, field, array);
}

std::vector<ActionList> collect_action_lists(il2cpp::Runtime& runtime,
                                             void* level_data) {
    std::vector<ActionList> lists;
    if (!level_data) {
        return lists;
    }
    void* level_class = runtime.object_class(level_data);
    void* waves = nullptr;
    if (!read_field_deep(runtime, level_data, level_class, "waves", &waves) ||
        !waves) {
        return lists;
    }
    Root waves_root(runtime, waves);
    const int wave_count = runtime.count(waves);
    for (int wave_index = 0; wave_index < wave_count; ++wave_index) {
        void* wave = runtime.at(waves, wave_index);
        if (!wave) {
            continue;
        }
        Root wave_root(runtime, wave);
        void* wave_class = runtime.object_class(wave);
        void* fragments = nullptr;
        if (!read_field_deep(runtime, wave, wave_class, "fragments", &fragments) ||
            !fragments) {
            continue;
        }
        Root fragments_root(runtime, fragments);
        const int fragment_count = runtime.count(fragments);
        for (int fragment_index = 0; fragment_index < fragment_count;
             ++fragment_index) {
            void* fragment = runtime.at(fragments, fragment_index);
            if (!fragment) {
                continue;
            }
            Root fragment_root(runtime, fragment);
            void* fragment_class = runtime.object_class(fragment);
            void* actions = nullptr;
            read_field_deep(runtime, fragment, fragment_class, "actions", &actions);
            ActionList list;
            list.owner = fragment;
            list.owner_class = fragment_class;
            list.array = actions;
            list.wave = wave_index;
            list.fragment = fragment_index;
            list.actions = array_values(runtime, actions);
            lists.push_back(std::move(list));
        }
    }

    void* branches = nullptr;
    if (!read_field_deep(runtime, level_data, level_class, "branches", &branches) ||
        !branches) {
        return lists;
    }
    Root branches_root(runtime, branches);
    const int branch_count = runtime.count(branches);
    for (int branch_index = 0; branch_index < branch_count; ++branch_index) {
        void* branch = listdict_value_at(runtime, branches, branch_index);
        if (!branch) {
            continue;
        }
        Root branch_root(runtime, branch);
        void* branch_class = runtime.object_class(branch);
        void* phases = nullptr;
        if (!read_field_deep(runtime, branch, branch_class, "phases", &phases) ||
            !phases) {
            continue;
        }
        Root phases_root(runtime, phases);
        const int phase_count = runtime.count(phases);
        for (int phase_index = 0; phase_index < phase_count; ++phase_index) {
            void* phase = runtime.at(phases, phase_index);
            if (!phase) {
                continue;
            }
            Root phase_root(runtime, phase);
            void* phase_class = runtime.object_class(phase);
            void* actions = nullptr;
            read_field_deep(runtime, phase, phase_class, "actions", &actions);
            ActionList list;
            list.owner = phase;
            list.owner_class = phase_class;
            list.array = actions;
            list.phase = phase_index;
            list.branch = "#" + std::to_string(branch_index);
            list.actions = array_values(runtime, actions);
            lists.push_back(std::move(list));
        }
    }
    return lists;
}

std::string action_key(il2cpp::Runtime& runtime, void* action, void* action_class) {
    return read_string_field(runtime, action, action_class, "key");
}

std::string action_hidden_group(il2cpp::Runtime& runtime, void* action,
                                void* action_class) {
    return read_string_field(runtime, action, action_class, "hiddenGroup");
}

std::string action_group(il2cpp::Runtime& runtime, void* action,
                         void* action_class) {
    return read_string_field(runtime, action, action_class,
                             "randomSpawnGroupKey");
}

std::string action_pack(il2cpp::Runtime& runtime, void* action,
                        void* action_class) {
    return read_string_field(runtime, action, action_class,
                             "randomSpawnGroupPackKey");
}

int action_weight(il2cpp::Runtime& runtime, void* action, void* action_class) {
    int weight = 0;
    read_field_deep(runtime, action, action_class, "weight", &weight);
    return weight;
}

void apply_action_patch(il2cpp::Runtime& runtime, const ActionList& list,
                        void* action, int action_index,
                        const ActionPatch& patch) {
    void* action_class = runtime.object_class(action);
    for (const ScalarPatch& field : patch.fields) {
        if (!set_scalar_field(runtime, action, action_class, field)) {
            throw std::runtime_error("动作不存在字段: " + field.field);
        }
    }
    (void)list;
    (void)action_index;
}

bool patch_matches(il2cpp::Runtime& runtime, const ActionList& list, void* action,
                   int action_index, const ActionPatch& patch) {
    if (patch.wave >= 0 && list.wave != patch.wave) {
        return false;
    }
    if (patch.fragment >= 0 && list.fragment != patch.fragment) {
        return false;
    }
    if (patch.phase >= 0 && list.phase != patch.phase) {
        return false;
    }
    if (patch.index >= 0 && action_index != patch.index) {
        return false;
    }
    void* action_class = runtime.object_class(action);
    if (!patch.enemy.empty() &&
        action_key(runtime, action, action_class) != patch.enemy) {
        return false;
    }
    if (!patch.hidden_group.empty() &&
        action_hidden_group(runtime, action, action_class) != patch.hidden_group) {
        return false;
    }
    if (!patch.random_pack.empty() &&
        action_pack(runtime, action, action_class) != patch.random_pack) {
        return false;
    }
    return true;
}

Json action_record(il2cpp::Runtime& runtime, const ActionList& list, void* action,
                   int action_index) {
    void* action_class = runtime.object_class(action);
    Json item;
    item["path"] = list.branch.empty()
                       ? "wave:" + std::to_string(list.wave) + "/fragment:" +
                             std::to_string(list.fragment)
                       : "branch:" + list.branch + "/phase:" +
                             std::to_string(list.phase);
    item["index"] = action_index;
    item["actionType"] = enum_field_value(runtime, action, action_class,
                                          "actionType");
    item["key"] = action_key(runtime, action, action_class);
    item["hiddenGroup"] = action_hidden_group(runtime, action, action_class);
    item["randomSpawnGroupKey"] = action_group(runtime, action, action_class);
    item["randomSpawnGroupPackKey"] = action_pack(runtime, action, action_class);
    item["weight"] = action_weight(runtime, action, action_class);
    int count = 0;
    int route = -1;
    float pre_delay = 0.0f;
    float interval = 0.0f;
    read_field_deep(runtime, action, action_class, "count", &count);
    read_field_deep(runtime, action, action_class, "routeIndex", &route);
    read_field_deep(runtime, action, action_class, "preDelay", &pre_delay);
    read_field_deep(runtime, action, action_class, "interval", &interval);
    item["count"] = count;
    item["route"] = route;
    item["preDelay"] = pre_delay;
    item["interval"] = interval;
    return item;
}

void dump_catalog(il2cpp::Runtime& runtime, const std::filesystem::path& path,
                  const std::string& stage, const std::string& level,
                  const LevelControlConfig& config,
                  const std::vector<ActionList>& lists) {
    Json root;
    root["stage"] = stage;
    root["level"] = level;
    root["config"] = config.source_path;
    Json hidden = Json::object();
    Json groups = Json::object();
    Json actions = Json::array();
    for (const ActionList& list : lists) {
        for (std::size_t index = 0; index < list.actions.size(); ++index) {
            void* action = list.actions[index];
            if (!action) {
                continue;
            }
            void* action_class = runtime.object_class(action);
            actions.push_back(
                action_record(runtime, list, action, static_cast<int>(index)));
            const std::string group = action_group(runtime, action, action_class);
            const std::string pack = action_pack(runtime, action, action_class);
            const std::string hidden_group =
                action_hidden_group(runtime, action, action_class);
            if (!hidden_group.empty()) {
                hidden[hidden_group].push_back(
                    action_record(runtime, list, action, static_cast<int>(index)));
            }
            if (!group.empty()) {
                const std::string key = group + "|" + pack;
                groups[key].push_back(
                    action_record(runtime, list, action, static_cast<int>(index)));
            }
        }
    }
    root["actions"] = std::move(actions);
    root["hidden_groups"] = std::move(hidden);
    root["random_groups"] = std::move(groups);
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << root.dump(2);
    if (!file) {
        throw std::runtime_error("关卡控制目录写入失败: " + path.string());
    }
}

void ensure_enemy_ref(il2cpp::Runtime& runtime, void* level_data,
                      const std::string& enemy) {
    if (!level_data) {
        return;
    }
    void* level_class = runtime.object_class(level_data);
    void* field = find_field_deep(runtime, level_class, "enemyDbRefs");
    if (!field) {
        throw std::runtime_error("LevelData 缺少 enemyDbRefs");
    }
    void* array = runtime.field_get_object(field, level_data);
    std::vector<void*> refs = array_values(runtime, array);
    for (void* ref : refs) {
        if (!ref) {
            continue;
        }
        void* ref_class = runtime.object_class(ref);
        if (read_string_field(runtime, ref, ref_class, "id") == enemy) {
            return;
        }
    }
    void* array_class = runtime.class_from_type(runtime.field_type(field));
    void* ref_class = array_class ? runtime.class_element(array_class) : nullptr;
    if (!ref_class) {
        throw std::runtime_error("enemyDbRefs 元素类型不可用");
    }
    void* ref = runtime.object_new(ref_class);
    if (!ref) {
        throw std::runtime_error("EnemyDataDbReference 分配失败");
    }
    Root ref_root(runtime, ref);
    bool use_db = true;
    void* use_db_field = find_field_deep(runtime, ref_class, "useDb");
    if (use_db_field) {
        runtime.field_set(ref, use_db_field, &use_db);
    }
    set_string_field(runtime, ref, ref_class, "id", enemy);
    int level = 0;
    void* level_field = find_field_deep(runtime, ref_class, "level");
    if (level_field) {
        runtime.field_set(ref, level_field, &level);
    }
    void* overwritten_field =
        find_field_deep(runtime, ref_class, "overwrittenData");
    if (overwritten_field) {
        runtime.field_set_object(ref, overwritten_field, nullptr);
    }
    refs.push_back(ref);
    replace_array_field(runtime, level_data, level_class, "enemyDbRefs", refs);
}

void append_action(il2cpp::Runtime& runtime, ActionList& list,
                   const ExtraEnemy& extra) {
    void* field = find_field_deep(runtime, list.owner_class, "actions");
    if (!field) {
        throw std::runtime_error("FragmentData 缺少 actions");
    }
    void* array_class = runtime.class_from_type(runtime.field_type(field));
    void* action_class = array_class ? runtime.class_element(array_class) : nullptr;
    if (!action_class) {
        throw std::runtime_error("ActionData 元素类型不可用");
    }
    void* action = runtime.object_new(action_class);
    if (!action) {
        throw std::runtime_error("ActionData 分配失败");
    }
    Root action_root(runtime, action);
    set_enum_field(runtime, action, action_class, "actionType", "SPAWN");
    bool managed = true;
    void* managed_field = find_field_deep(runtime, action_class, "managedByScheduler");
    if (managed_field) {
        runtime.field_set(action, managed_field, &managed);
    }
    set_string_field(runtime, action, action_class, "key", extra.enemy);
    const int count = extra.count;
    int route = extra.route;
    const float pre_delay = extra.pre_delay;
    const float interval = extra.interval;
    const bool false_value = false;
    const bool true_value = true;
    const int zero = 0;
    auto set_int = [&](const char* name, int value) {
        if (void* item = find_field_deep(runtime, action_class, name)) {
            runtime.field_set(action, item, &value);
        }
    };
    auto set_float = [&](const char* name, float value) {
        if (void* item = find_field_deep(runtime, action_class, name)) {
            runtime.field_set(action, item, &value);
        }
    };
    auto set_bool = [&](const char* name, bool value) {
        if (void* item = find_field_deep(runtime, action_class, name)) {
            runtime.field_set(action, item, &value);
        }
    };
    set_int("count", count);
    set_float("preDelay", pre_delay);
    set_float("interval", interval);
    set_int("routeIndex", route);
    set_bool("useExtraRoute", false_value);
    set_bool("blockFragment", false_value);
    set_bool("autoPreviewRoute", true_value);
    set_bool("autoDisplayEnemyInfo", false_value);
    set_bool("isUnharmfulAndAlwaysCountAsKilled", false_value);
    set_bool("dontBlockWave", false_value);
    set_bool("forceBlockWaveInBranch", false_value);
    set_bool("isValid", true_value);
    set_bool("notCountInTotal", false_value);
    set_enum_field(runtime, action, action_class, "randomType", "ALWAYS");
    set_enum_field(runtime, action, action_class, "refreshType", "ALWAYS");
    set_int("weight", zero);
    set_string_field(runtime, action, action_class, "hiddenGroup", "", true);
    set_string_field(runtime, action, action_class, "randomSpawnGroupKey", "",
                     true);
    set_string_field(runtime, action, action_class, "randomSpawnGroupPackKey",
                     "", true);
    std::vector<void*> actions = array_values(runtime, list.array);
    actions.push_back(action);
    replace_array_field(runtime, list.owner, list.owner_class, "actions", actions);
    list.actions = std::move(actions);
}

ActionList* find_action_list(std::vector<ActionList>& lists, int wave,
                             int fragment) {
    for (ActionList& list : lists) {
        if (list.branch.empty() && list.wave == wave &&
            list.fragment == fragment) {
            return &list;
        }
    }
    return nullptr;
}

void ensure_fragment(il2cpp::Runtime& runtime, void* level_data, int wave_index,
                     int fragment_index) {
    void* level_class = runtime.object_class(level_data);
    void* waves = nullptr;
    if (!read_field_deep(runtime, level_data, level_class, "waves", &waves) ||
        !waves) {
        throw std::runtime_error("LevelData 缺少 waves");
    }
    Root waves_root(runtime, waves);
    if (wave_index < 0 || wave_index >= runtime.count(waves)) {
        throw std::runtime_error("add_enemies.wave 超出范围");
    }
    void* wave = runtime.at(waves, wave_index);
    if (!wave) {
        throw std::runtime_error("add_enemies.wave 对应 WaveData 为空");
    }
    Root wave_root(runtime, wave);
    void* wave_class = runtime.object_class(wave);
    void* field = find_field_deep(runtime, wave_class, "fragments");
    if (!field) {
        throw std::runtime_error("WaveData 缺少 fragments");
    }
    void* array = runtime.field_get_object(field, wave);
    void* array_class = runtime.class_from_type(runtime.field_type(field));
    void* fragment_class =
        array_class ? runtime.class_element(array_class) : nullptr;
    if (!fragment_class) {
        throw std::runtime_error("FragmentData 元素类型不可用");
    }
    std::vector<void*> fragments = array_values(runtime, array);
    if (fragment_index < 0) {
        throw std::runtime_error("add_enemies.fragment 不能为负");
    }
    while (static_cast<int>(fragments.size()) <= fragment_index) {
        void* fragment = runtime.object_new(fragment_class);
        if (!fragment) {
            throw std::runtime_error("FragmentData 分配失败");
        }
        Root fragment_root(runtime, fragment);
        float pre_delay = 0.0f;
        if (void* pre_delay_field =
                find_field_deep(runtime, fragment_class, "preDelay")) {
            runtime.field_set(fragment, pre_delay_field, &pre_delay);
        }
        void* actions_field = find_field_deep(runtime, fragment_class, "actions");
        if (!actions_field) {
            throw std::runtime_error("FragmentData 缺少 actions");
        }
        void* actions_array_class =
            runtime.class_from_type(runtime.field_type(actions_field));
        void* action_class =
            actions_array_class ? runtime.class_element(actions_array_class)
                                : nullptr;
        if (!action_class) {
            throw std::runtime_error("ActionData 元素类型不可用");
        }
        void* empty_actions = runtime.array_new(action_class, 0);
        Root empty_actions_root(runtime, empty_actions);
        runtime.field_set_object(fragment, actions_field, empty_actions);
        fragments.push_back(fragment);
    }
    replace_array_field(runtime, wave, wave_class, "fragments", fragments);
}

void remove_actions(il2cpp::Runtime& runtime,
                    const std::unordered_set<void*>& remove,
                    std::vector<ActionList>& lists) {
    if (remove.empty()) {
        return;
    }
    for (ActionList& list : lists) {
        std::vector<void*> kept;
        kept.reserve(list.actions.size());
        for (void* action : list.actions) {
            if (!remove.contains(action)) {
                kept.push_back(action);
            }
        }
        if (kept.size() != list.actions.size()) {
            replace_array_field(runtime, list.owner, list.owner_class, "actions",
                                kept);
            list.actions = std::move(kept);
        }
    }
}

void add_enemy_to_level(il2cpp::Runtime& runtime, void* level_data,
                        const ExtraEnemy& extra) {
    void* level_class = runtime.object_class(level_data);
    void* routes = nullptr;
    read_field_deep(runtime, level_data, level_class, "routes", &routes);
    const int route_count = runtime.count(routes);
    if (extra.route >= route_count) {
        throw std::runtime_error(
            "add_enemies.route 超出范围: " + std::to_string(extra.route) +
            "/" + std::to_string(route_count));
    }
    ensure_enemy_ref(runtime, level_data, extra.enemy);
    ensure_fragment(runtime, level_data, extra.wave, extra.fragment);
    std::vector<ActionList> lists = collect_action_lists(runtime, level_data);
    ActionList* list = find_action_list(lists, extra.wave, extra.fragment);
    if (!list) {
        throw std::runtime_error("无法定位 add_enemies 的目标 fragment");
    }
    append_action(runtime, *list, extra);
}

void apply_hidden_removal(il2cpp::Runtime& runtime,
                          const LevelControlConfig& config,
                          std::vector<ActionList>& lists) {
    if (!config.remove_disabled_hidden || config.disable_hidden.empty()) {
        return;
    }
    std::unordered_set<std::string> disabled(config.disable_hidden.begin(),
                                             config.disable_hidden.end());
    std::unordered_set<void*> remove;
    for (const ActionList& list : lists) {
        for (void* action : list.actions) {
            if (!action) {
                continue;
            }
            void* action_class = runtime.object_class(action);
            if (disabled.contains(action_hidden_group(runtime, action,
                                                      action_class))) {
                remove.insert(action);
            }
        }
    }
    remove_actions(runtime, remove, lists);
}

} // namespace

void LocalBattle::apply_level_control(void* level_data, const std::string& stage_id,
                                      const std::string& level_id) {
    level_control_enabled_hidden_groups_.clear();
    level_control_disabled_hidden_groups_.clear();
    level_control_runtime_applied_ = false;
    level_control_applied_scheduler_ = nullptr;
    if (!level_data || !host_.root_directory) {
        return;
    }
    const std::filesystem::path root(host_.root_directory);
    LevelControlConfig config =
        parse_level_control_config(root / "level_control.json");
    if (!config.present) {
        return;
    }
    if (!config_matches(config, stage_id, level_id)) {
        log(kLogInfo, "关卡控制配置不匹配当前关卡: " + config.stage + "/" +
                          config.level);
        return;
    }
    ark::crash_log::set_stage("bb.level_control.collect");
    std::vector<ActionList> lists = collect_action_lists(runtime_, level_data);
    if (config.dump_catalog) {
        ark::crash_log::set_stage("bb.level_control.dump_catalog");
        dump_catalog(runtime_, root / "level_control_catalog.json", stage_id,
                     level_id, config, lists);
    }

    ark::crash_log::set_stage("bb.level_control.patch_actions");
    log(kLogInfo, "关卡控制阶段: patch_actions");
    for (const ActionPatch& patch : config.action_patches) {
        for (ActionList& list : lists) {
            for (std::size_t index = 0; index < list.actions.size(); ++index) {
                void* action = list.actions[index];
                if (!action || !patch_matches(runtime_, list, action,
                                              static_cast<int>(index), patch)) {
                    continue;
                }
                apply_action_patch(runtime_, list, action,
                                   static_cast<int>(index), patch);
                break;
            }
        }
    }

    std::map<std::string, RandomRule> rules;
    for (const RandomRule& rule : config.random_rules) {
        rules[rule.group] = rule;
    }
    std::map<std::string, std::vector<ActionSite>> grouped;
    for (std::size_t list_index = 0; list_index < lists.size(); ++list_index) {
        const ActionList& list = lists[list_index];
        for (std::size_t index = 0; index < list.actions.size(); ++index) {
            void* action = list.actions[index];
            if (!action) {
                continue;
            }
            void* action_class = runtime_.object_class(action);
            const std::string group = action_group(runtime_, action, action_class);
            if (group.empty()) {
                continue;
            }
            ActionSite site;
            site.list = list_index;
            site.index = static_cast<int>(index);
            site.action = action;
            site.key = action_key(runtime_, action, action_class);
            site.hidden_group =
                action_hidden_group(runtime_, action, action_class);
            site.group = group;
            site.pack = action_pack(runtime_, action, action_class);
            site.weight = action_weight(runtime_, action, action_class);
            grouped[group].push_back(std::move(site));
        }
    }
    std::unordered_set<void*> remove;
    ark::crash_log::set_stage("bb.level_control.random_groups");
    log(kLogInfo, "关卡控制阶段: random_groups");
    for (const auto& [group, members] : grouped) {
        const auto rule_it = rules.find(group);
        if (rule_it == rules.end()) {
            continue;
        }
        const RandomRule& rule = rule_it->second;
        std::unordered_set<void*> keep;
        if (!rule.remove) {
            std::string selected_pack = rule.pack;
            std::vector<void*> selected;
            if (!rule.pack.empty()) {
                for (const ActionSite& site : members) {
                    if (site.pack == rule.pack) {
                        selected.push_back(site.action);
                    }
                }
                if (selected.empty()) {
                    throw std::runtime_error("权重组 " + group + " 不存在 pack: " +
                                             rule.pack);
                }
            } else if (!rule.key.empty()) {
                for (const ActionSite& site : members) {
                    if (site.key == rule.key) {
                        selected.push_back(site.action);
                        selected_pack = site.pack;
                    }
                }
                if (selected.empty()) {
                    throw std::runtime_error("权重组 " + group + " 不存在 key: " +
                                             rule.key);
                }
            } else if (rule.index >= 0) {
                for (std::size_t member_index = 0;
                     member_index < members.size(); ++member_index) {
                    if (static_cast<int>(member_index) == rule.index) {
                        selected.push_back(members[member_index].action);
                        selected_pack = members[member_index].pack;
                        break;
                    }
                }
                if (selected.empty()) {
                    throw std::runtime_error("权重组 " + group + " 不存在 index: " +
                                             std::to_string(rule.index));
                }
            }
            if (!selected_pack.empty()) {
                for (const ActionSite& site : members) {
                    if (site.pack == selected_pack) {
                        selected.push_back(site.action);
                    }
                }
            }
            for (void* action : selected) {
                keep.insert(action);
            }
        }
        for (const ActionSite& site : members) {
            if (!keep.contains(site.action)) {
                remove.insert(site.action);
            }
        }
    }
    for (const auto& [group, rule] : rules) {
        if (!grouped.contains(group)) {
            log(kLogInfo, "关卡控制权重组不存在: " + group +
                              (rule.remove ? " (remove)" : ""));
        }
    }
    ark::crash_log::set_stage("bb.level_control.remove_actions");
    log(kLogInfo, "关卡控制阶段: remove_actions count=" +
                      std::to_string(remove.size()));
    remove_actions(runtime_, remove, lists);
    lists = collect_action_lists(runtime_, level_data);
    ark::crash_log::set_stage("bb.level_control.hidden_removal");
    log(kLogInfo, "关卡控制阶段: hidden_removal");
    apply_hidden_removal(runtime_, config, lists);
    ark::crash_log::set_stage("bb.level_control.add_enemies");
    log(kLogInfo, "关卡控制阶段: add_enemies");
    for (const ExtraEnemy& extra : config.extra_enemies) {
        add_enemy_to_level(runtime_, level_data, extra);
    }
    if (config.dump_catalog) {
        ark::crash_log::set_stage("bb.level_control.dump_applied_catalog");
        lists = collect_action_lists(runtime_, level_data);
        dump_catalog(runtime_, root / "level_control_catalog_applied.json",
                     stage_id, level_id, config, lists);
    }
    for (const std::string& group : config.enable_hidden) {
        if (std::find(config.disable_hidden.begin(), config.disable_hidden.end(),
                      group) == config.disable_hidden.end()) {
            level_control_enabled_hidden_groups_.push_back(group);
        }
    }
    for (const std::string& group : config.disable_hidden) {
        level_control_disabled_hidden_groups_.push_back(group);
    }
    log(kLogInfo,
        "关卡控制已应用: " + stage_id + " -> " + level_id +
            " random=" + std::to_string(config.random_rules.size()) +
            " patch=" + std::to_string(config.action_patches.size()) +
            " add=" + std::to_string(config.extra_enemies.size()) +
            " hidden+=" + std::to_string(level_control_enabled_hidden_groups_.size()) +
            " hidden-=" +
            std::to_string(level_control_disabled_hidden_groups_.size()));
}

void LocalBattle::apply_runtime_level_control(void* controller) {
    if (!controller || (level_control_enabled_hidden_groups_.empty() &&
                        level_control_disabled_hidden_groups_.empty())) {
        return;
    }
    void* scheduler = nullptr;
    if (!read_field_deep(runtime_, controller, classes_.battle_controller,
                         "_scheduler", &scheduler) ||
        !scheduler) {
        return;
    }
    Root scheduler_root(runtime_, scheduler);
    void* scheduler_class = runtime_.object_class(scheduler);
    void* list_set = nullptr;
    if (!read_field_deep(runtime_, scheduler, scheduler_class,
                         "m_enabledHiddenGroups", &list_set) ||
        !list_set) {
        return;
    }
    Root list_root(runtime_, list_set);
    void* list_class = runtime_.object_class(list_set);
    void* add = find_method_optional(runtime_, list_class, "Add", 1);
    void* remove = find_method_optional(runtime_, list_class, "Remove", 1);
    if (!add || !remove) {
        return;
    }
    for (const std::string& group : level_control_disabled_hidden_groups_) {
        void* text = runtime_.string_new(group.c_str());
        if (text) {
            Root text_root(runtime_, text);
            runtime_.invoke(remove, list_set, {text});
        }
    }
    for (const std::string& group : level_control_enabled_hidden_groups_) {
        void* text = runtime_.string_new(group.c_str());
        if (text) {
            Root text_root(runtime_, text);
            runtime_.invoke(add, list_set, {text});
        }
    }
    const bool first_application = !level_control_runtime_applied_;
    level_control_runtime_applied_ = true;
    level_control_applied_scheduler_ = scheduler;
    if (first_application) {
        log(kLogInfo, "关卡控制隐藏组已同步到 Scheduler");
    }
}

void LocalBattle::dump_level_control() {
    std::vector<void*> controllers =
        find_controllers(runtime_, classes_.battle_controller);
    if (controllers.empty()) {
        throw std::runtime_error("当前不在战斗场景");
    }
    void* controller = controllers.front();
    Root controller_root(runtime_, controller);
    void* scheduler = nullptr;
    if (!read_field_deep(runtime_, controller, classes_.battle_controller,
                         "_scheduler", &scheduler) ||
        !scheduler) {
        throw std::runtime_error("Scheduler 不可读");
    }
    Root scheduler_root(runtime_, scheduler);
    void* scheduler_class = runtime_.object_class(scheduler);
    void* waves = nullptr;
    read_field_deep(runtime_, scheduler, scheduler_class, "m_waves", &waves);
    std::vector<ActionList> lists;
    if (waves) {
        Root waves_root(runtime_, waves);
        const int wave_count = runtime_.count(waves);
        for (int wave_index = 0; wave_index < wave_count; ++wave_index) {
            void* wave = runtime_.at(waves, wave_index);
            if (!wave) {
                continue;
            }
            Root wave_root(runtime_, wave);
            void* wave_class = runtime_.object_class(wave);
            void* fragments = nullptr;
            read_field_deep(runtime_, wave, wave_class, "fragments", &fragments);
            if (!fragments) {
                continue;
            }
            Root fragments_root(runtime_, fragments);
            const int fragment_count = runtime_.count(fragments);
            for (int fragment_index = 0; fragment_index < fragment_count;
                 ++fragment_index) {
                void* fragment = runtime_.at(fragments, fragment_index);
                if (!fragment) {
                    continue;
                }
                Root fragment_root(runtime_, fragment);
                void* fragment_class = runtime_.object_class(fragment);
                void* actions = nullptr;
                read_field_deep(runtime_, fragment, fragment_class, "actions",
                                &actions);
                ActionList list;
                list.owner = fragment;
                list.owner_class = fragment_class;
                list.array = actions;
                list.wave = wave_index;
                list.fragment = fragment_index;
                list.actions = array_values(runtime_, actions);
                lists.push_back(std::move(list));
            }
        }
    }
    Json root;
    root["stage"] = scene_.stage.level_id;
    root["stage_input"] = scene_.stage.id;
    root["hidden_enabled"] = Json::array();
    void* list_set = nullptr;
    read_field_deep(runtime_, scheduler, scheduler_class, "m_enabledHiddenGroups",
                    &list_set);
    if (list_set) {
        Root list_root(runtime_, list_set);
        const int count = list_set_count(runtime_, list_set);
        for (int index = 0; index < count; ++index) {
            void* item = list_set_at(runtime_, list_set, index);
            if (item) {
                root["hidden_enabled"].push_back(runtime_.string(item));
            }
        }
    }
    Json actions = Json::array();
    for (const ActionList& list : lists) {
        for (std::size_t index = 0; index < list.actions.size(); ++index) {
            if (void* action = list.actions[index]) {
                actions.push_back(action_record(runtime_, list, action,
                                                static_cast<int>(index)));
            }
        }
    }
    root["actions"] = std::move(actions);
    const std::filesystem::path path =
        std::filesystem::path(host_.root_directory) / "level_control_live.json";
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << root.dump(2);
    if (!file) {
        throw std::runtime_error("关卡控制运行态导出失败: " + path.string());
    }
    publish_result("关卡控制运行态已写入 " + path.string());
}

void LocalBattle::cmd_level_control(Args& args) {
    try {
        const std::string action = args.empty() ? "dump" : args.next();
        if (action == "dump" || action == "groups" || action == "status") {
            dump_level_control();
            return;
        }
        if (action != "hidden") {
            throw std::runtime_error(
                "level-control 需要 dump/groups/status 或 hidden");
        }
        const std::string sub = args.next();
        std::vector<void*> controllers =
            find_controllers(runtime_, classes_.battle_controller);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* scheduler = nullptr;
        if (!read_field_deep(runtime_, controller, classes_.battle_controller,
                             "_scheduler", &scheduler) ||
            !scheduler) {
            throw std::runtime_error("Scheduler 不可读");
        }
        Root scheduler_root(runtime_, scheduler);
        void* scheduler_class = runtime_.object_class(scheduler);
        void* list_set = nullptr;
        if (!read_field_deep(runtime_, scheduler, scheduler_class,
                             "m_enabledHiddenGroups", &list_set) ||
            !list_set) {
            throw std::runtime_error("Scheduler.m_enabledHiddenGroups 不可读");
        }
        Root list_root(runtime_, list_set);
        if (sub == "list") {
            Json groups = Json::array();
            const int count = list_set_count(runtime_, list_set);
            for (int index = 0; index < count; ++index) {
                if (void* item = list_set_at(runtime_, list_set, index)) {
                    groups.push_back(runtime_.string(item));
                }
            }
            publish_result("Scheduler 已启用隐藏组: " + groups.dump());
            return;
        }
        if (sub == "clear") {
            void* clear = find_method_optional(
                runtime_, runtime_.object_class(list_set), "Clear", 0);
            if (!clear) {
                throw std::runtime_error("ListSet.Clear 不可用");
            }
            runtime_.invoke(clear, list_set);
            level_control_enabled_hidden_groups_.clear();
            level_control_disabled_hidden_groups_.clear();
            publish_result("Scheduler 隐藏组已清空");
            return;
        }
        if (sub != "enable" && sub != "disable") {
            throw std::runtime_error(
                "level-control hidden 需要 list/enable/disable/clear");
        }
        const std::string group = args.next();
        if (group.empty()) {
            throw std::runtime_error("hidden 需要组名");
        }
        void* text = runtime_.string_new(group.c_str());
        Root text_root(runtime_, text);
        void* method = find_method_optional(
            runtime_, runtime_.object_class(list_set),
            sub == "enable" ? "Add" : "Remove", 1);
        if (!method) {
            throw std::runtime_error("ListSet 操作不可用");
        }
        runtime_.invoke(method, list_set, {text});
        if (sub == "enable") {
            level_control_enabled_hidden_groups_.push_back(group);
            std::erase(level_control_disabled_hidden_groups_, group);
        } else {
            level_control_disabled_hidden_groups_.push_back(group);
            std::erase(level_control_enabled_hidden_groups_, group);
        }
        level_control_runtime_applied_ = true;
        publish_result("隐藏组 " + group +
                       (sub == "enable" ? " 已启用" : " 已禁用"));
    } catch (const std::exception& error) {
        publish_result(std::string("关卡控制失败：") + error.what(), false);
    }
}

} // namespace ark::better_battle
