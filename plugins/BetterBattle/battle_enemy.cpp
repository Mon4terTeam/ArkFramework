#include "battle_helpers.h"

#include <sstream>

namespace ark::better_battle {
namespace {

struct UnityEngineColor {
    float r = 1;
    float g = 1;
    float b = 1;
    float a = 1;
};

struct BattleOutlineConfigValue {
    float outline_size = 0;
    UnityEngineColor outline_color{};
    bool use_eight_ways = false;
    unsigned char padding[3] = {};
};
static_assert(sizeof(BattleOutlineConfigValue) == 24);

void* find_field_any(il2cpp::Runtime& runtime, void* klass,
                     std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (void* field = find_field_deep(runtime, klass, name)) {
            return field;
        }
    }
    return nullptr;
}

bool read_field_any(il2cpp::Runtime& runtime, void* object, void* klass,
                    std::initializer_list<const char*> names, void* out,
                    std::size_t size) {
    if (!object || !out) {
        return false;
    }
    void* field = find_field_any(runtime, klass, names);
    if (!field) {
        return false;
    }
    auto cached = field_offset_cache().find(field);
    if (cached == field_offset_cache().end()) {
        cached = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
    }
    std::memcpy(out, static_cast<const unsigned char*>(object) + cached->second,
                size);
    return true;
}

template <class T>
bool read_field_any(il2cpp::Runtime& runtime, void* object, void* klass,
                    std::initializer_list<const char*> names, T* out) {
    return read_field_any(runtime, object, klass, names, out, sizeof(T));
}

std::string read_string_any(il2cpp::Runtime& runtime, void* object, void* klass,
                            std::initializer_list<const char*> names) {
    void* text = nullptr;
    if (!read_field_any(runtime, object, klass, names, &text) || !text) {
        return {};
    }
    return runtime.string(text);
}

bool read_number_any(il2cpp::Runtime& runtime, void* object, void* klass,
                     std::initializer_list<const char*> names, double* out) {
    if (!object || !out) {
        return false;
    }
    void* field = find_field_any(runtime, klass, names);
    if (!field) {
        return false;
    }
    const std::string type = runtime.type(runtime.field_type(field));
    auto cached = field_offset_cache().find(field);
    if (cached == field_offset_cache().end()) {
        cached = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
    }
    const void* address =
        static_cast<const unsigned char*>(object) + cached->second;
    if (type == "Torappu.FP") {
        std::int64_t value = 0;
        if (!read_at(address, &value)) {
            return false;
        }
        *out = static_cast<double>(value) / kFixedPointScale;
        return true;
    }
    if (type == "System.Single") {
        float value = 0;
        if (!read_at(address, &value)) {
            return false;
        }
        *out = value;
        return true;
    }
    if (type == "System.Double") {
        double value = 0;
        if (!read_at(address, &value)) {
            return false;
        }
        *out = value;
        return true;
    }
    return false;
}

void read_enemy_skill_definitions(
    il2cpp::Runtime& runtime, void* data, void* data_class,
    std::vector<EnemySkillDefinition>& output) {
    void* skill_list = nullptr;
    if (!read_field_any(runtime, data, data_class, {"skills"}, &skill_list) ||
        !skill_list) {
        return;
    }
    std::unordered_set<std::string> seen;
    for (const EnemySkillDefinition& skill : output) {
        seen.emplace(skill.key);
    }
    const int skill_count = runtime.count(skill_list);
    for (int index = 0; index < skill_count && output.size() < 32; ++index) {
        try {
            void* skill = runtime.at(skill_list, index);
            if (!skill) {
                continue;
            }
            void* skill_class = runtime.object_class(skill);
            EnemySkillDefinition definition;
            definition.key = read_string_any(
                runtime, skill, skill_class, {"prefabKey", "key"});
            if (definition.key.empty() ||
                !seen.emplace(definition.key).second) {
                continue;
            }
            read_field_any(runtime, skill, skill_class, {"priority"},
                           &definition.priority);
            read_field_any(runtime, skill, skill_class, {"spCost"},
                           &definition.sp_cost);
            read_number_any(runtime, skill, skill_class, {"cooldown"},
                            &definition.cooldown);
            read_number_any(runtime, skill, skill_class, {"initCooldown"},
                            &definition.initial_cooldown);
            output.push_back(std::move(definition));
        } catch (...) {
        }
    }
}

std::string field_value_text(il2cpp::Runtime& runtime, void* object,
                             void* field) {
    if (!object || !field) {
        return {};
    }
    try {
        const std::string type = runtime.type(runtime.field_type(field));
        auto cached = field_offset_cache().find(field);
        if (cached == field_offset_cache().end()) {
            cached = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
        }
        const void* address =
            static_cast<const unsigned char*>(object) + cached->second;
        if (type == "System.String") {
            void* text = nullptr;
            if (read_at(address, &text) && text) {
                return runtime.string(text);
            }
            return "null";
        }
        if (type == "System.Boolean") {
            bool value = false;
            read_at(address, &value);
            return value ? "true" : "false";
        }
        if (type == "System.Int32" || type == "System.UInt32") {
            std::int32_t value = 0;
            read_at(address, &value);
            return std::to_string(value);
        }
        if (type == "System.Single") {
            float value = 0;
            read_at(address, &value);
            return std::to_string(value);
        }
        if (type == "Torappu.FP") {
            std::int64_t value = 0;
            read_at(address, &value);
            return std::to_string(
                static_cast<double>(value) / kFixedPointScale);
        }
        return type;
    } catch (...) {
        return "?";
    }
}

std::string read_string_field_any(il2cpp::Runtime& runtime, void* object,
                                  void* klass,
                                  std::initializer_list<const char*> names) {
    return read_string_any(runtime, object, klass, names);
}

std::vector<std::string> read_string_collection(
    il2cpp::Runtime& runtime, void* object, void* klass,
    std::initializer_list<const char*> names) {
    void* collection = nullptr;
    if (!read_field_any(runtime, object, klass, names, &collection) ||
        !collection) {
        return {};
    }
    std::vector<std::string> values;
    const int count = runtime.count(collection);
    values.reserve(static_cast<std::size_t>(std::max(0, std::min(count, 128))));
    for (int index = 0; index < count && index < 128; ++index) {
        try {
            void* item = runtime.at(collection, index);
            if (item) {
                const std::string text = runtime.string(item);
                if (!text.empty()) {
                    values.push_back(text);
                }
            }
        } catch (...) {
        }
    }
    return values;
}

std::vector<int> read_int_collection(il2cpp::Runtime& runtime, void* object,
                                     void* klass,
                                     std::initializer_list<const char*> names) {
    void* collection = nullptr;
    if (!read_field_any(runtime, object, klass, names, &collection) ||
        !collection) {
        return {};
    }
    std::vector<int> values;
    const int count = runtime.count(collection);
    values.reserve(static_cast<std::size_t>(std::max(0, std::min(count, 64))));
    for (int index = 0; index < count && index < 64; ++index) {
        try {
            void* item = runtime.at(collection, index);
            if (!item) {
                continue;
            }
            void* item_class = runtime.object_class(item);
            if (item_class && runtime.class_is_value(item_class)) {
                values.push_back(runtime.unbox<int>(item));
            } else {
                values.push_back(runtime.unbox<int>(item));
            }
        } catch (...) {
        }
    }
    return values;
}

void* find_field_by_class(il2cpp::Runtime& runtime, void* klass,
                          void* wanted, int depth = 0) {
    if (!klass || !wanted || depth > 4) {
        return nullptr;
    }
    void* owner = klass;
    for (int level = 0; owner && level < 6; ++level, owner = runtime.class_parent(owner)) {
        void* iterator = nullptr;
        while (void* field = runtime.class_fields(owner, &iterator)) {
            void* field_class = nullptr;
            try {
                field_class = runtime.class_from_type(runtime.field_type(field));
            } catch (...) {
                continue;
            }
            if (!field_class) {
                continue;
            }
            try {
                if (runtime.class_assignable(wanted, field_class)) {
                    return field;
                }
            } catch (...) {
            }
        }
    }
    return nullptr;
}

void* read_object_field_deep(il2cpp::Runtime& runtime, void* object,
                             void* klass, const char* name) {
    void* field = find_field_deep(runtime, klass, name);
    if (!field || !object) {
        return nullptr;
    }
    try {
        return runtime.field_get_object(field, object);
    } catch (...) {
        return nullptr;
    }
}

void* dictionary_value(il2cpp::Runtime& runtime, void* dictionary,
                       const std::string& key) {
    if (!dictionary) {
        return nullptr;
    }
    try {
        for (const DictionaryPair& pair :
             read_dictionary_pairs(runtime, dictionary)) {
            if (pair.key && runtime.string(pair.key) == key) {
                return pair.value;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

std::string join_ability_fields(il2cpp::Runtime& runtime, void* object) {
    if (!object) {
        return {};
    }
    try {
        void* klass = runtime.object_class(object);
        std::string text;
        void* iterator = nullptr;
        while (void* field = runtime.class_fields(klass, &iterator)) {
            const char* name = runtime.field_name(field);
            if (!name || std::strncmp(name, "__", 2) == 0) {
                continue;
            }
            const std::string type = runtime.type(runtime.field_type(field));
            std::string value;
            if (type == "System.String") {
                void* text_object = nullptr;
                if (!read_field_any(runtime, object, klass, {name}, &text_object) ||
                    !text_object) {
                    continue;
                }
                value = runtime.string(text_object);
            } else if (type == "System.Boolean") {
                bool flag = false;
                read_field_any(runtime, object, klass, {name}, &flag);
                value = flag ? "true" : "false";
            } else if (type == "System.Int32") {
                std::int32_t number = 0;
                read_field_any(runtime, object, klass, {name}, &number);
                value = std::to_string(number);
            } else if (type == "System.Single") {
                float number = 0;
                read_field_any(runtime, object, klass, {name}, &number);
                value = std::to_string(number);
            } else {
                continue;
            }
            if (value.empty()) {
                continue;
            }
            if (!text.empty()) {
                text += " · ";
            }
            if (std::strcmp(name, "text") != 0) {
                text += name;
                text += '=';
            }
            text += value;
        }
        return text;
    } catch (...) {
        return {};
    }
}

std::vector<std::string> read_ability_list(il2cpp::Runtime& runtime,
                                           void* object, void* klass) {
    void* list = nullptr;
    if (!read_field_any(runtime, object, klass,
                        {"abilityList", "abilities"}, &list) ||
        !list) {
        return {};
    }
    std::vector<std::string> values;
    const int count = runtime.count(list);
    for (int index = 0; index < count && index < 32; ++index) {
        try {
            void* item = runtime.at(list, index);
            if (!item) {
                continue;
            }
            const std::string text = join_ability_fields(runtime, item);
            if (!text.empty()) {
                values.push_back(text);
            }
        } catch (...) {
        }
    }
    return values;
}

void dump_object_fields(std::ostream& output, il2cpp::Runtime& runtime,
                        const char* label, void* object) {
    if (!object) {
        output << label << " null\n";
        return;
    }
    void* klass = runtime.object_class(object);
    output << label << " ptr=" << object << " class="
           << runtime.klass_name(klass) << '\n';
    void* iterator = nullptr;
    while (void* field = runtime.class_fields(klass, &iterator)) {
        output << "  " << (runtime.field_name(field) ? runtime.field_name(field) : "?")
               << " : " << runtime.type(runtime.field_type(field))
               << " = " << field_value_text(runtime, object, field) << '\n';
    }
}

} // namespace

std::string LocalBattle::enemy_name(const std::string& id) {
    if (id.empty()) {
        return {};
    }
    const auto cached = enemy_name_cache_.find(id);
    if (cached != enemy_name_cache_.end()) {
        return cached->second;
    }
    std::string name;
    try {
        if (classes_.enemy_db) {
            void* method =
                find_method_optional(runtime_, classes_.enemy_db,
                                     "GetEnemyName", 1);
            if (method) {
                void* argument = runtime_.string_new(id.c_str());
                void* text = runtime_.invoke(method, nullptr, {argument});
                name = runtime_.string(text);
            }
        }
    } catch (...) {
    }
    if (name.empty()) {
        name = id;
    }
    enemy_name_cache_.emplace(id, name);
    return name;
}

const EnemyProfile& LocalBattle::enemy_profile(const std::string& id) {
    const auto cached = enemy_profile_cache_.find(id);
    if (cached != enemy_profile_cache_.end()) {
        return cached->second;
    }

    EnemyProfile profile;
    profile.id = id;
    profile.name = enemy_name(id);
    const auto remember = [&]() -> const EnemyProfile& {
        return enemy_profile_cache_.emplace(id, std::move(profile)).first->second;
    };
    if (id.empty()) {
        return remember();
    }

    void* db_class = classes_.enemy_db;
    if (!db_class) {
        try {
            db_class = runtime_.find("Torappu", "EnemyDB");
        } catch (...) {
            return remember();
        }
    }
    void* db = nullptr;
    try {
        if (void* get_instance =
                find_method_optional(runtime_, db_class, "get_instance", 0)) {
            db = runtime_.invoke(get_instance, nullptr);
        }
    } catch (...) {
    }
    if (!db) {
        try {
            void* field = find_field_deep(runtime_, db_class, "s_instance");
            if (field) {
                runtime_.field_static_get(field, &db);
            }
        } catch (...) {
        }
    }

    void* enemy_data = nullptr;
    try {
        void* method =
            find_method_optional(runtime_, db_class, "TryGet", 3);
        if (method) {
            void* value_class = runtime_.class_from_type(
                runtime_.method_param(method, 2));
            if (value_class) {
                enemy_data = runtime_.object_new(value_class);
                if (enemy_data) {
                    il2cpp::Runtime::Root data_root(runtime_, enemy_data);
                    void* text = runtime_.string_new(id.c_str());
                    il2cpp::Runtime::Root text_root(runtime_, text);
                    bool found = false;
                    for (int level = 0; level < 4 && !found; ++level) {
                        void* result = nullptr;
                        try {
                            result = runtime_.invoke(
                                method, db,
                                {text, &level,
                                 runtime_.object_unbox(enemy_data)});
                        } catch (...) {
                            try {
                                result = runtime_.invoke(
                                    method, nullptr,
                                    {text, &level,
                                     runtime_.object_unbox(enemy_data)});
                            } catch (...) {
                            }
                        }
                        found = result && runtime_.unbox<bool>(result);
                    }
                    if (!found) {
                        enemy_data = nullptr;
                    }
                }
            }
        }
    } catch (...) {
        enemy_data = nullptr;
    }

    if (enemy_data) {
        try {
            void* data_class = runtime_.object_class(enemy_data);
            if (profile.name.empty()) {
                profile.name = read_string_field_any(
                    runtime_, enemy_data, data_class,
                    {"name", "enemyName", "displayName"});
            }
            profile.index = read_string_field_any(
                runtime_, enemy_data, data_class, {"enemyIndex", "index"});
            profile.description = read_string_field_any(
                runtime_, enemy_data, data_class,
                {"description", "enemyDescription", "desc"});
            profile.attack_type = read_string_field_any(
                runtime_, enemy_data, data_class, {"attackType"});
            profile.ability = read_string_field_any(
                runtime_, enemy_data, data_class, {"ability", "abilityDesc"});
            profile.race = read_string_field_any(
                runtime_, enemy_data, data_class, {"race", "raceId"});
            profile.tags = read_string_collection(
                runtime_, enemy_data, data_class, {"enemyTags", "tags"});
            profile.abilities = read_ability_list(runtime_, enemy_data, data_class);
            profile.link_enemies = read_string_collection(
                runtime_, enemy_data, data_class, {"linkEnemies"});
            profile.damage_types = read_int_collection(
                runtime_, enemy_data, data_class, {"damageType", "damageTypes"});
            read_enemy_skill_definitions(runtime_, enemy_data, data_class,
                                         profile.skills);
            int value = 0;
            if (read_field_any(runtime_, enemy_data, data_class,
                               {"enemyLevel", "levelType"}, &value)) {
                profile.level_type = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class, {"sortId"},
                               &value)) {
                profile.sort_id = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class,
                               {"lifePointReduce"}, &value)) {
                profile.life_point_reduce = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class, {"maxHp"},
                               &value)) {
                profile.max_hp = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class, {"atk"},
                               &value)) {
                profile.atk = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class, {"def"},
                               &value)) {
                profile.def = value;
            }
            if (read_field_any(runtime_, enemy_data, data_class, {"massLevel"},
                               &value)) {
                profile.mass_level = value;
            }
            if (profile.sort_id == 0) {
                read_field_any(runtime_, enemy_data, data_class,
                               {"sortId", "sortID"}, &profile.sort_id);
            }
            read_number_any(runtime_, enemy_data, data_class,
                            {"magicResistance", "magicRes"}, &profile.magic_resistance);
            read_number_any(runtime_, enemy_data, data_class, {"moveSpeed"},
                            &profile.move_speed);
            read_number_any(runtime_, enemy_data, data_class,
                            {"baseAttackTime", "attackInterval"},
                            &profile.base_attack_time);
            read_number_any(runtime_, enemy_data, data_class,
                            {"epDamageResistance"}, &profile.ep_damage_resistance);
            read_number_any(runtime_, enemy_data, data_class, {"epResistance"},
                            &profile.ep_resistance);
            profile.source = "EnemyDB.TryGet";
        } catch (...) {
        }
    }

    try {
        void* method =
            find_method_optional(runtime_, db_class,
                                 "TryGetHandbookEnemyData", 3);
        if (method) {
            void* value_class = runtime_.class_from_type(
                runtime_.method_param(method, 2));
            if (value_class) {
                void* stats = runtime_.object_new(value_class);
                if (stats) {
                    il2cpp::Runtime::Root stats_root(runtime_, stats);
                    void* text = runtime_.string_new(id.c_str());
                    il2cpp::Runtime::Root text_root(runtime_, text);
                    bool found = false;
                    for (int level = 0; level < 4 && !found; ++level) {
                        void* result = nullptr;
                        try {
                            result = runtime_.invoke(
                                method, db,
                                {text, &level,
                                 runtime_.object_unbox(stats)});
                        } catch (...) {
                            try {
                                result = runtime_.invoke(
                                    method, nullptr,
                                    {text, &level,
                                     runtime_.object_unbox(stats)});
                            } catch (...) {
                            }
                        }
                        found = result && runtime_.unbox<bool>(result);
                    }
                    if (found) {
                        void* stats_class = runtime_.object_class(stats);
                        int value = 0;
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"maxHp"}, &value)) {
                            profile.max_hp = value;
                        }
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"atk"}, &value)) {
                            profile.atk = value;
                        }
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"def"}, &value)) {
                            profile.def = value;
                        }
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"massLevel"}, &value)) {
                            profile.mass_level = value;
                        }
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"lifePointReduce"}, &value)) {
                            profile.life_point_reduce = value;
                        }
                        if (read_field_any(runtime_, stats, stats_class,
                                           {"levelType"}, &value)) {
                            profile.level_type = value;
                        }
                        read_number_any(runtime_, stats, stats_class,
                                        {"magicResistance"},
                                        &profile.magic_resistance);
                        read_number_any(runtime_, stats, stats_class,
                                        {"moveSpeed"}, &profile.move_speed);
                        read_number_any(runtime_, stats, stats_class,
                                        {"baseAttackTime"},
                                        &profile.base_attack_time);
                        read_number_any(runtime_, stats, stats_class,
                                        {"epDamageResistance"},
                                        &profile.ep_damage_resistance);
                        read_number_any(runtime_, stats, stats_class,
                                        {"epResistance"},
                                        &profile.ep_resistance);
                        read_field_any(runtime_, stats, stats_class,
                                       {"stunImmune"}, &profile.stun_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"sleepImmune"}, &profile.sleep_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"frozenImmune"}, &profile.frozen_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"levitateImmune"},
                                       &profile.levitate_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"disarmedCombatImmune"},
                                       &profile.disarmed_combat_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"fearedImmune"}, &profile.feared_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"palsyImmune"}, &profile.palsy_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"attractImmune"},
                                       &profile.attract_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"teleportImmune"},
                                       &profile.teleport_immune);
                        read_field_any(runtime_, stats, stats_class,
                                       {"groundBoundImmune"},
                                       &profile.ground_bound_immune);
                        if (!profile.tags.empty()) {
                            // The handbook stats may carry tags while TryGet is absent.
                        }
                        if (profile.source.empty()) {
                            profile.source = "EnemyDB.TryGetHandbookEnemyData";
                        } else {
                            profile.source += "+handbook";
                        }
                    }
                }
            }
        }
    } catch (...) {
    }

    if (profile.description.empty() || profile.index.empty() ||
        profile.tags.empty() || profile.abilities.empty()) {
        try {
            void* data_class =
                runtime_.find("Torappu", "EnemyHandBookData");
            void* group_class =
                runtime_.find("Torappu", "EnemyHandBookDataGroup");
            void* handbook_class =
                runtime_.find("Torappu", "EnemyHandBookDB");
            void* handbook = nullptr;
            if (void* get_instance = find_method_optional(
                    runtime_, handbook_class, "get_instance", 0)) {
                handbook = runtime_.invoke(get_instance, nullptr);
            }
            void* dictionary = nullptr;
            if (handbook) {
                il2cpp::Runtime::Root handbook_root(runtime_, handbook);
                if (void* group_field =
                        find_field_by_class(runtime_,
                                            runtime_.object_class(handbook),
                                            group_class)) {
                    void* group = runtime_.field_get_object(group_field, handbook);
                    if (group) {
                        il2cpp::Runtime::Root group_root(runtime_, group);
                        dictionary = read_object_field_deep(
                            runtime_, group, runtime_.object_class(group),
                            "enemyData");
                    }
                }
                if (!dictionary) {
                    if (void* dictionary_field = find_field_by_class(
                            runtime_, runtime_.object_class(handbook),
                            runtime_.find("System.Collections.Generic",
                                          "Dictionary`2"))) {
                        dictionary =
                            runtime_.field_get_object(dictionary_field, handbook);
                    }
                }
            }
            void* data = dictionary_value(runtime_, dictionary, id);
            if (!data) {
                for (void* group :
                     find_live_objects(runtime_, group_class, 8)) {
                    if (!group) {
                        continue;
                    }
                    il2cpp::Runtime::Root group_root(runtime_, group);
                    void* live_dictionary = read_object_field_deep(
                        runtime_, group, runtime_.object_class(group),
                        "enemyData");
                    data = dictionary_value(runtime_, live_dictionary, id);
                    if (data) {
                        break;
                    }
                }
            }
            if (data) {
                il2cpp::Runtime::Root data_root(runtime_, data);
                void* data_object_class = runtime_.object_class(data);
                const std::string handbook_name = read_string_field_any(
                    runtime_, data, data_object_class, {"name", "enemyName"});
                if (!handbook_name.empty() &&
                    (profile.name.empty() || profile.name == id)) {
                    profile.name = handbook_name;
                }
                if (profile.index.empty()) {
                    profile.index = read_string_field_any(
                        runtime_, data, data_object_class, {"enemyIndex"});
                }
                if (profile.description.empty()) {
                    profile.description = read_string_field_any(
                        runtime_, data, data_object_class, {"description"});
                }
                if (profile.attack_type.empty()) {
                    profile.attack_type = read_string_field_any(
                        runtime_, data, data_object_class, {"attackType"});
                }
                if (profile.ability.empty()) {
                    profile.ability = read_string_field_any(
                        runtime_, data, data_object_class, {"ability"});
                }
                if (profile.tags.empty()) {
                    profile.tags = read_string_collection(
                        runtime_, data, data_object_class,
                        {"enemyTags", "tags"});
                }
                if (profile.abilities.empty()) {
                    profile.abilities =
                        read_ability_list(runtime_, data, data_object_class);
                }
                if (profile.link_enemies.empty()) {
                    profile.link_enemies = read_string_collection(
                        runtime_, data, data_object_class, {"linkEnemies"});
                }
                if (profile.damage_types.empty()) {
                    profile.damage_types = read_int_collection(
                        runtime_, data, data_object_class,
                        {"damageType", "damageTypes"});
                }
                int value = 0;
                if (read_field_any(runtime_, data, data_object_class,
                                   {"enemyLevel", "levelType"}, &value)) {
                    profile.level_type = value;
                }
                if (read_field_any(runtime_, data, data_object_class,
                                   {"sortId"}, &value)) {
                    profile.sort_id = value;
                }
                profile.source += profile.source.empty() ? "EnemyHandBookData"
                                                         : "+EnemyHandBookData";
            }
        } catch (...) {
        }
    }

    if (profile.name.empty()) {
        profile.name = id;
    }
    return remember();
}

void LocalBattle::enemy_profile_probe(const std::string& id) {
    try {
        const EnemyProfile& profile = enemy_profile(id);
        std::ostringstream output;
        output << "enemy " << id << " name=" << profile.name
               << " index=" << profile.index
               << " level=" << profile.level_type
               << " description=" << profile.description << '\n';
        output << "stats maxHp=" << profile.max_hp << " atk=" << profile.atk
               << " def=" << profile.def
               << " res=" << profile.magic_resistance
               << " move=" << profile.move_speed
               << " interval=" << profile.base_attack_time
               << " weight=" << profile.mass_level
               << " life=" << profile.life_point_reduce << '\n';
        output << "tags";
        for (const std::string& tag : profile.tags) {
            output << ' ' << tag;
        }
        output << '\n';
        for (const std::string& ability : profile.abilities) {
            output << "ability " << ability << '\n';
        }
        output << "source " << profile.source << '\n';

        void* db_class = classes_.enemy_db;
        if (db_class) {
            void* db = nullptr;
            if (void* get_instance = find_method_optional(
                    runtime_, db_class, "get_instance", 0)) {
                db = runtime_.invoke(get_instance, nullptr);
            }
            if (db) {
                il2cpp::Runtime::Root db_root(runtime_, db);
                output << "EnemyDB instance " << db << " class "
                       << runtime_.klass_name(runtime_.object_class(db))
                       << "\n";
                output << describe_instance_fields(runtime_, db) << '\n';
            }
        }
        void* handbook_class = nullptr;
        try {
            handbook_class = runtime_.find("Torappu", "EnemyHandBookDB");
        } catch (...) {
        }
        if (handbook_class) {
            void* handbook = nullptr;
            if (void* get_instance = find_method_optional(
                    runtime_, handbook_class, "get_instance", 0)) {
                handbook = runtime_.invoke(get_instance, nullptr);
            }
            if (handbook) {
                il2cpp::Runtime::Root handbook_root(runtime_, handbook);
                output << "EnemyHandBookDB instance " << handbook << "\n";
                output << describe_instance_fields(runtime_, handbook) << '\n';
            }
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) /
            "battle_enemy_profile.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("敌人资料探针写入失败");
        }
        publish_result("敌人资料已写入 " + path.string() + "（" +
                       profile.source + "）");
    } catch (const std::exception& error) {
        publish_result(std::string("敌人资料探针失败：") + error.what(), false);
    }
}

void LocalBattle::refresh_enemies(void* controller, BattleSnapshot& snapshot) {
    if (!controller || !classes_.enemy) {
        return;
    }
    if (enemy_scene_revision_ != scene_.revision) {
        enemy_scene_revision_ = scene_.revision;
        enemy_serials_.clear();
        next_enemy_serial_ = 1;
        enemy_tile_cache_source_ = nullptr;
        enemy_tile_cache_.clear();
    }

    void* manager = nullptr;
    read_field_deep(runtime_, controller, classes_.battle_controller, "m_objectManger",
                    &manager);
    if (!manager) {
        return;
    }
    void* manager_class = runtime_.object_class(manager);
    void* list = nullptr;
    read_field_deep(runtime_, manager, manager_class, "m_cachedList", &list);
    if (!list) {
        return;
    }

    struct SeenEnemy {
        void* object = nullptr;
        std::uint32_t uid = 0;
        std::int64_t created_raw = 0;
    };
    std::vector<SeenEnemy> seen;
    std::vector<SeenEnemy> unseen;
    const int count = runtime_.count(list);
    seen.reserve(static_cast<std::size_t>(std::max(0, std::min(count, 4096))));
    for (int index = 0; index < count && index < 4096; ++index) {
        void* object = runtime_.at(list, index);
        if (!object || !runtime_.class_assignable(
                           classes_.enemy, runtime_.object_class(object))) {
            continue;
        }
        SeenEnemy entry;
        entry.object = object;
        read_field_deep(runtime_, object, classes_.battle_object,
                        "<instanceUid>k__BackingField", &entry.uid);
        if (!entry.uid) {
            continue;
        }
        read_field_deep(runtime_, object, classes_.enemy, "m_createdTime",
                        &entry.created_raw);
        seen.push_back(entry);
        if (enemy_serials_.find(entry.uid) == enemy_serials_.end()) {
            unseen.push_back(entry);
        }
    }

    std::sort(unseen.begin(), unseen.end(),
              [](const SeenEnemy& left, const SeenEnemy& right) {
                  if (left.created_raw != right.created_raw) {
                      return left.created_raw < right.created_raw;
                  }
                  return left.uid < right.uid;
              });
    for (const SeenEnemy& entry : unseen) {
        enemy_serials_.emplace(entry.uid, next_enemy_serial_++);
    }

    void* tile_map = nullptr;
    if (void* get_map = find_method_optional(
            runtime_, classes_.battle_controller, "get_map", 0)) {
        tile_map = runtime_.invoke(get_map, controller);
    }
    if (!tile_map) {
        read_field_deep(runtime_, controller, classes_.battle_controller,
                        "_map", &tile_map);
    }
    if (tile_map && (tile_map != enemy_tile_cache_source_ ||
                     enemy_tile_cache_.empty())) {
        enemy_tile_cache_source_ = nullptr;
        enemy_tile_cache_.clear();
        bool tile_cache_built = false;
        try {
            Root tile_map_root(runtime_, tile_map);
            void* tile_map_class = runtime_.object_class(tile_map);
            void* tiles_2d = nullptr;
            read_field_deep(runtime_, tile_map, tile_map_class, "_tiles",
                            &tiles_2d);
            int width = 0;
            int height = 0;
            if (tiles_2d) {
                void* tiles_class = runtime_.object_class(tiles_2d);
                read_field_deep(runtime_, tiles_2d, tiles_class, "_width",
                                &width);
                read_field_deep(runtime_, tiles_2d, tiles_class, "_height",
                                &height);
            }
            void* get_tile =
                find_method_optional(runtime_, tile_map_class, "GetTile", 2);
            if (get_tile && width > 0 && height > 0 && width <= 64 &&
                height <= 64) {
                for (int tile_row = 0; tile_row < height; ++tile_row) {
                    for (int tile_col = 0; tile_col < width; ++tile_col) {
                        int row_value = tile_row;
                        int col_value = tile_col;
                        void* tile = runtime_.invoke(
                            get_tile, tile_map, {&row_value, &col_value});
                        if (tile) {
                            enemy_tile_cache_.emplace(
                                tile, std::make_pair(tile_row, tile_col));
                        }
                    }
                }
                tile_cache_built = true;
            }
        } catch (...) {
        }
        if (tile_cache_built) {
            enemy_tile_cache_source_ = tile_map;
        }
    }
    void* root_tile_method =
        find_method_optional(runtime_, classes_.enemy, "get_rootTile", 0);

    snapshot.enemies.reserve(seen.size());
    for (const SeenEnemy& entry : seen) {
        try {
            EnemyInfo info;
            info.uid = entry.uid;
            info.serial = enemy_serials_[entry.uid];
            info.created_time =
                static_cast<double>(entry.created_raw) / kFixedPointScale;
            info.id =
                method_string(runtime_, entry.object, classes_.entity, "get_id");
            const EnemyProfile& profile = enemy_profile(info.id);
            if (profile.skills.empty()) {
                try {
                    if (void* get_data = find_method_optional(
                            runtime_, classes_.enemy, "get_data", 0)) {
                        void* data = runtime_.invoke(get_data, entry.object);
                        if (data) {
                            Root data_root(runtime_, data);
                            read_enemy_skill_definitions(
                                runtime_, data, runtime_.object_class(data),
                                enemy_profile_cache_[info.id].skills);
                        }
                    }
                } catch (...) {
                }
            }
            read_field_deep(runtime_, entry.object, classes_.enemy,
                            "m_hostUid", &info.owner_uid);

            std::int64_t hp_raw = 0;
            read_field_deep(runtime_, entry.object, classes_.entity, "m_hp",
                            &hp_raw);
            info.hp = static_cast<double>(hp_raw) / kFixedPointScale;
            void* max_hp_method =
                find_method_optional(runtime_, classes_.entity, "get_maxHp", 0);
            if (max_hp_method) {
                info.max_hp =
                    unbox_fp(runtime_, runtime_.invoke(max_hp_method, entry.object));
            }
            info.shield =
                method_fp(runtime_, entry.object, classes_.entity,
                          "get_shieldToShow");
            info.atk =
                method_fp(runtime_, entry.object, classes_.entity, "get_atk");
            info.def =
                method_fp(runtime_, entry.object, classes_.entity, "get_def");
            info.magic_resistance =
                method_fp(runtime_, entry.object, classes_.entity,
                          "get_magicResistance");
            info.attack_speed =
                method_fp(runtime_, entry.object, classes_.entity,
                          "get_attackSpeed");
            info.move_speed =
                method_float(runtime_, entry.object, classes_.entity,
                             "get_moveSpeed");
            info.alive =
                method_bool(runtime_, entry.object, classes_.entity, "get_alive");
            info.boss =
                method_bool(runtime_, entry.object, classes_.enemy, "get_isBoss");
            info.giant_boss =
                method_bool(runtime_, entry.object, classes_.enemy,
                            "get_isGiantBoss");
            info.mass_level =
                method_int(runtime_, entry.object, classes_.enemy,
                           "get_massLevel", 0);
            info.block_count =
                method_int(runtime_, entry.object, classes_.entity,
                           "get_blockCnt", 0);
            if (info.block_count <= 0) {
                info.block_count =
                    method_int(runtime_, entry.object, classes_.enemy,
                               "get_blockVolume", 0);
            }
            if (!enemy_tile_cache_.empty()) {
                try {
                    void* tile = root_tile_method
                                     ? runtime_.invoke(root_tile_method,
                                                       entry.object)
                                     : nullptr;
                    if (!tile) {
                        read_field_deep(runtime_, entry.object,
                                        classes_.enemy, "m_currentTile",
                                        &tile);
                    }
                    const auto position = enemy_tile_cache_.find(tile);
                    if (position != enemy_tile_cache_.end()) {
                        info.row = position->second.first;
                        info.col = position->second.second;
                    }
                } catch (...) {
                }
            }

            std::vector<void*> skill_sets;
            for (const char* field_name :
                 {"m_skills", "m_allSkills"}) {
                void* skills = nullptr;
                if (read_field_deep(runtime_, entry.object, classes_.enemy,
                                    field_name, &skills) &&
                    skills) {
                    skill_sets.push_back(skills);
                }
            }
            std::unordered_set<std::string> seen_skills;
            for (void* skills : skill_sets) {
                const int skill_count = runtime_.count(skills);
                for (int index = 0;
                     index < skill_count && info.skills.size() < 24; ++index) {
                    void* skill = runtime_.at(skills, index);
                    if (!skill) {
                        continue;
                    }
                    try {
                        void* skill_class = runtime_.object_class(skill);
                        EnemySkillInfo skill_info;
                        skill_info.key =
                            method_string(runtime_, skill, skill_class,
                                          "get_skillKey");
                        if (skill_info.key.empty() ||
                            !seen_skills.emplace(skill_info.key).second) {
                            continue;
                        }
                        skill_info.priority =
                            method_int(runtime_, skill, skill_class,
                                       "get_priority", 0);
                        skill_info.enabled =
                            method_bool(runtime_, skill, skill_class,
                                        "get_isEnabled");
                        skill_info.used_up =
                            method_bool(runtime_, skill, skill_class,
                                        "get_isUsedUp");
                        read_field_deep(runtime_, skill, skill_class,
                                        "m_triggerCnt",
                                        &skill_info.trigger_count);
                        read_field_deep(runtime_, skill, skill_class,
                                        "_maxTriggerTime",
                                        &skill_info.max_trigger_count);
                        read_field_deep(runtime_, skill, skill_class, "m_spCost",
                                        &skill_info.sp_cost);
                        void* ability = nullptr;
                        if (void* ability_method = find_method_optional(
                                runtime_, skill_class, "get_ability", 0)) {
                            ability =
                                runtime_.invoke(ability_method, skill);
                        }
                        if (ability) {
                            void* ability_class = runtime_.object_class(ability);
                            skill_info.signal_id =
                                method_string(runtime_, ability, ability_class,
                                              "get_signalId");
                            skill_info.range_id =
                                method_string(runtime_, ability, ability_class,
                                              "get_rangeId");
                            skill_info.cooldown =
                                method_fp(runtime_, ability, ability_class,
                                          "get_remainingTime");
                            skill_info.cooldown_total =
                                method_fp(runtime_, ability, ability_class,
                                          "get_periodTime");
                            skill_info.casting =
                                method_bool(runtime_, ability, ability_class,
                                            "get_isCasting");
                            skill_info.affecting =
                                method_bool(runtime_, ability, ability_class,
                                            "get_isAffecting");
                        }
                        info.skills.push_back(std::move(skill_info));
                    } catch (...) {
                    }
                }
            }

            try {
                read_entity_buffs(runtime_, entry.object, classes_.entity,
                                  classes_.buff, buff_static_cache_,
                                  buff_cache_generation_, perf_.buff_hits,
                                  perf_.buff_misses, info.buffs, nullptr);
            } catch (...) {
            }
            snapshot.enemies.push_back(std::move(info));
        } catch (...) {
        }
    }
    std::sort(snapshot.enemies.begin(), snapshot.enemies.end(),
              [](const EnemyInfo& left, const EnemyInfo& right) {
                  return left.serial < right.serial;
              });
    std::unordered_map<std::string, bool> emitted_profiles;
    for (const EnemyInfo& enemy : snapshot.enemies) {
        if (emitted_profiles.emplace(enemy.id, true).second) {
            snapshot.enemy_profiles.push_back(enemy_profile(enemy.id));
        }
    }
    snapshot.enemy_focus_uid =
        focused_enemy_uid_ && GetTickCount64() < focused_enemy_until_
            ? focused_enemy_uid_
            : 0;
    snapshot.enemy_focus_active = snapshot.enemy_focus_uid != 0;
    read_enemy_waves(controller, snapshot);
}

void LocalBattle::read_enemy_waves(void* controller, BattleSnapshot& snapshot) {
    if (!controller) {
        return;
    }
    try {
        if (!classes_.scheduler) {
            return;
        }
        void* scheduler = nullptr;
        read_field_deep(runtime_, controller, classes_.battle_controller,
                        "_scheduler", &scheduler);
        if (!scheduler) {
            void* getter =
                find_method_optional(runtime_, classes_.battle_controller,
                                     "get_scheduler", 0);
            if (getter) {
                scheduler = runtime_.invoke(getter, controller);
            }
        }
        if (!scheduler) {
            return;
        }
        void* scheduler_class = runtime_.object_class(scheduler);
        int spawned_wave = -1;
        if (!read_field_deep(runtime_, scheduler, scheduler_class,
                             "m_spawnedWavesCnt", &spawned_wave)) {
            spawned_wave =
                method_int(runtime_, scheduler, scheduler_class,
                           "get_spawnedWavesCnt", -1);
        }
        void* waves = nullptr;
        if (!read_field_deep(runtime_, scheduler, scheduler_class, "m_waves",
                             &waves) ||
            !waves) {
            void* getter =
                find_method_optional(runtime_, scheduler_class, "get_waves", 0);
            if (getter) {
                waves = runtime_.invoke(getter, scheduler);
            }
        }
        if (!waves) {
            return;
        }
        const int wave_total = runtime_.count(waves);
        if (!enemy_wave_plan_cached_ ||
            enemy_wave_plan_source_ != scheduler ||
            enemy_wave_plan_total_ != wave_total) {
            std::vector<EnemyWave> plans;
            plans.reserve(static_cast<std::size_t>(
                std::max(0, std::min(wave_total, 64))));
            for (int wave_index = 0;
                 wave_index < wave_total && wave_index < 64;
                 ++wave_index) {
                void* wave = runtime_.at(waves, wave_index);
                if (!wave) {
                    continue;
                }
                EnemyWave plan;
                plan.index = wave_index + 1;
                try {
                    void* wave_class = runtime_.object_class(wave);
                    float wave_delay = 0;
                    read_field_any(runtime_, wave, wave_class,
                                   {"preDelay", "m_preDelay"}, &wave_delay);
                    plan.pre_delay = wave_delay;
                    void* fragments = nullptr;
                    read_field_any(runtime_, wave, wave_class,
                                   {"fragments", "m_fragments"}, &fragments);
                    if (!fragments) {
                        continue;
                    }
                    const int fragment_count = runtime_.count(fragments);
                    double fragment_offset = wave_delay;
                    for (int fragment_index = 0;
                         fragment_index < fragment_count && fragment_index < 64;
                         ++fragment_index) {
                        void* fragment =
                            runtime_.at(fragments, fragment_index);
                        if (!fragment) {
                            continue;
                        }
                        void* fragment_class =
                            runtime_.object_class(fragment);
                        float fragment_delay = 0;
                        read_field_any(runtime_, fragment, fragment_class,
                                       {"preDelay", "m_preDelay"},
                                       &fragment_delay);
                        void* actions = nullptr;
                        read_field_any(runtime_, fragment, fragment_class,
                                       {"actions", "m_actions"}, &actions);
                        if (!actions) {
                            continue;
                        }
                        const int action_count = runtime_.count(actions);
                        for (int action_index = 0;
                             action_index < action_count && action_index < 128;
                             ++action_index) {
                            void* action = runtime_.at(actions, action_index);
                            if (!action) {
                                continue;
                            }
                            void* action_class =
                                runtime_.object_class(action);
                            EnemyWaveAction item;
                            item.key = read_string_any(
                                runtime_, action, action_class,
                                {"key", "m_key", "enemyId", "m_enemyId",
                                 "prefabKey", "m_prefabKey"});
                            if (item.key.rfind("enemy_", 0) != 0) {
                                continue;
                            }
                            read_field_any(runtime_, action, action_class,
                                           {"count", "m_count"}, &item.count);
                            read_field_any(runtime_, action, action_class,
                                           {"routeIndex", "m_routeIndex"},
                                           &item.route);
                            float action_delay = 0;
                            read_field_any(runtime_, action, action_class,
                                           {"preDelay", "m_preDelay"},
                                           &action_delay);
                            read_number_any(runtime_, action, action_class,
                                            {"interval", "m_interval"},
                                            &item.interval);
                            item.pre_delay = action_delay;
                            item.first_time = fragment_offset +
                                              fragment_delay + action_delay;
                            if (item.count < 0) {
                                item.count = 0;
                            }
                            plan.total += item.count;
                            plan.actions.push_back(std::move(item));
                        }
                        fragment_offset += fragment_delay;
                    }
                } catch (...) {
                }
                plans.push_back(std::move(plan));
            }
            enemy_wave_plan_source_ = scheduler;
            enemy_wave_plan_total_ = wave_total;
            enemy_wave_plan_cache_ = std::move(plans);
            enemy_wave_plan_cached_ = true;
        }

        snapshot.enemy_wave_current = spawned_wave;
        snapshot.enemy_wave_total = enemy_wave_plan_total_;
        snapshot.enemy_waves = enemy_wave_plan_cache_;
        for (EnemyWave& wave : snapshot.enemy_waves) {
            wave.spawned = spawned_wave;
        }

        std::unordered_map<std::string, bool> emitted_profiles;
        for (const EnemyProfile& profile : snapshot.enemy_profiles) {
            emitted_profiles.emplace(profile.id, true);
        }
        for (const EnemyWave& wave : snapshot.enemy_waves) {
            for (const EnemyWaveAction& action : wave.actions) {
                if (action.key.empty() ||
                    !emitted_profiles.emplace(action.key, true).second) {
                    continue;
                }
                snapshot.enemy_profiles.push_back(enemy_profile(action.key));
            }
        }
    } catch (...) {
    }
}

void LocalBattle::focus_enemy(std::uint32_t uid, int duration_ms) {
    try {
        void* controller_class =
            classes_.battle_controller;
        std::vector<void*> controllers =
            find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* manager = nullptr;
        read_field_deep(runtime_, controller, controller_class, "m_objectManger",
                        &manager);
        if (!manager) {
            throw std::runtime_error("BattleController.m_objectManger 不可读");
        }
        Root manager_root(runtime_, manager);
        void* manager_class = runtime_.object_class(manager);
        void* list = nullptr;
        read_field_deep(runtime_, manager, manager_class, "m_cachedList", &list);
        if (!list) {
            throw std::runtime_error("m_cachedList 不可读");
        }
        Root list_root(runtime_, list);
        void* enemy = nullptr;
        const int count = runtime_.count(list);
        for (int index = 0; index < count && index < 4096; ++index) {
            void* object = runtime_.at(list, index);
            if (!object ||
                !runtime_.class_assignable(classes_.enemy,
                                           runtime_.object_class(object))) {
                continue;
            }
            std::uint32_t object_uid = 0;
            read_field_deep(runtime_, object, classes_.battle_object,
                            "<instanceUid>k__BackingField", &object_uid);
            if (object_uid == uid) {
                enemy = object;
                break;
            }
        }
        if (!enemy) {
            throw std::runtime_error("未找到 UID " + std::to_string(uid) +
                                     " 的在场敌人");
        }
        clear_enemy_focus();
        focused_enemy_root_ = std::make_unique<Root>(runtime_, enemy);
        focused_enemy_uid_ = uid;
        const int duration =
            std::clamp(duration_ms > 0 ? duration_ms : 4000, 500, 60000);
        focused_enemy_until_ = GetTickCount64() + static_cast<std::uint64_t>(duration);

        void* animator = nullptr;
        read_field_deep(runtime_, enemy, classes_.enemy, "m_spineAnimator",
                        &animator);
        if (!animator) {
            focused_animator_root_.reset();
            focused_animator_ = nullptr;
            publish_result("敌人 #" + std::to_string(uid) +
                           " 已选中，但 SpineAnimator 不可读");
            return;
        }
        focused_animator_root_ = std::make_unique<Root>(runtime_, animator);
        focused_animator_ = animator;
        int top_sorting_order = 30000;
        std::unordered_set<void*> seen_renderers;
        const auto raise_renderer = [&](void* renderer) {
            if (!renderer || !seen_renderers.emplace(renderer).second) {
                return;
            }
            try {
                void* renderer_class = runtime_.object_class(renderer);
                void* get_order = find_method_optional(
                    runtime_, renderer_class, "get_sortingOrder", 0);
                void* set_order = find_method_optional(
                    runtime_, renderer_class, "set_sortingOrder", 1);
                if (!get_order || !set_order) {
                    return;
                }
                void* boxed = runtime_.invoke(get_order, renderer);
                FocusedRendererState state;
                state.root = std::make_unique<Root>(runtime_, renderer);
                state.renderer = renderer;
                state.sorting_order =
                    boxed ? runtime_.unbox<int>(boxed) : 0;
                runtime_.invoke(set_order, renderer, {&top_sorting_order});
                focused_renderers_.push_back(std::move(state));
            } catch (...) {
            }
        };
        if (void* get_renderer = find_method_optional(
                runtime_, runtime_.object_class(animator),
                "get_meshRenderer", 0)) {
            try {
                raise_renderer(runtime_.invoke(get_renderer, animator));
            } catch (...) {
            }
        }
        bool outline_applied = false;
        try {
            void* outline_manager = nullptr;
            if (void* get_outline = find_method_optional(
                    runtime_, controller_class, "get_spineOutlineManager", 0)) {
                outline_manager =
                    runtime_.invoke(get_outline, controller);
            }
            if (outline_manager) {
                auto outline_manager_root =
                    std::make_unique<Root>(runtime_, outline_manager);
                void* outline_class =
                    runtime_.object_class(outline_manager);
                bool enabled = false;
                if (void* get_enabled = find_method_optional(
                        runtime_, outline_class, "get_isEnable", 0)) {
                    void* boxed =
                        runtime_.invoke(get_enabled, outline_manager);
                    enabled = boxed && runtime_.unbox<bool>(boxed);
                }
                if (!enabled) {
                    if (void* init = find_method_optional(
                            runtime_, outline_class, "Init", 0)) {
                        runtime_.invoke(init, outline_manager);
                    }
                }
                bool enable = true;
                if (void* reset = find_method_optional(
                        runtime_, outline_class, "ResetMeshByAnimator", 2)) {
                    runtime_.invoke(reset, outline_manager,
                                    {animator, &enable});
                }
                void* options = nullptr;
                read_field_deep(runtime_, outline_manager, outline_class,
                                "m_options", &options);
                void* option = nullptr;
                if (options) {
                    Root options_root(runtime_, options);
                    if (void* item = find_method_optional(
                            runtime_, runtime_.object_class(options),
                            "get_Item", 1)) {
                        option = runtime_.invoke(item, options, {animator});
                    }
                }
                if (option) {
                    Root option_root(runtime_, option);
                    void* option_class = runtime_.object_class(option);
                    BattleOutlineConfigValue config;
                    config.outline_size = 0.18f;
                    config.outline_color = {0.8f, 2.4f, 2.4f, 1.0f};
                    config.use_eight_ways = true;
                    write_field_deep(runtime_, option, option_class,
                                     "m_config", config);
                    if (void* draw = find_method_optional(
                            runtime_, outline_class, "DrawOutline", 1)) {
                        runtime_.invoke(draw, outline_manager, {animator});
                    }
                    bool option_enabled = false;
                    if (void* get_option_enabled = find_method_optional(
                            runtime_, option_class, "get_isEnabled", 0)) {
                        void* boxed = runtime_.invoke(
                            get_option_enabled, option);
                        option_enabled =
                            boxed && runtime_.unbox<bool>(boxed);
                    }
                    int materials = 0;
                    void* material_map = nullptr;
                    read_field_deep(runtime_, option, option_class,
                                    "m_materials", &material_map);
                    if (material_map) {
                        materials = runtime_.count(material_map);
                        std::unordered_set<void*> seen_materials;
                        for (const DictionaryPair& pair :
                             read_dictionary_pairs(runtime_, material_map)) {
                            raise_renderer(pair.key);
                            if (!pair.value ||
                                !seen_materials.emplace(pair.value).second) {
                                continue;
                            }
                            try {
                                void* material_class =
                                    runtime_.object_class(pair.value);
                                void* get_queue = find_method_optional(
                                    runtime_, material_class,
                                    "get_renderQueue", 0);
                                void* set_queue = find_method_optional(
                                    runtime_, material_class,
                                    "set_renderQueue", 1);
                                if (!get_queue || !set_queue) {
                                    continue;
                                }
                                void* boxed = runtime_.invoke(
                                    get_queue, pair.value);
                                FocusedMaterialState state;
                                state.root = std::make_unique<Root>(
                                    runtime_, pair.value);
                                state.material = pair.value;
                                state.render_queue =
                                    boxed ? runtime_.unbox<int>(boxed) : 0;
                                int overlay = 4000;
                                runtime_.invoke(set_queue, pair.value,
                                                {&overlay});
                                focused_materials_.push_back(
                                    std::move(state));
                            } catch (...) {
                            }
                        }
                    }
                    outline_applied = option_enabled && materials > 0;
                }
                if (outline_applied) {
                    focused_outline_manager_ = outline_manager;
                    focused_outline_manager_root_ =
                        std::move(outline_manager_root);
                    focused_outline_ = true;
                }
            }
        } catch (...) {
        }
        publish_result("敌人 UID " + std::to_string(uid) +
                       " 高亮 " + std::to_string(duration) + "ms：outline=" +
                       (outline_applied ? "1" : "0"));
    } catch (const std::exception& error) {
        publish_result(std::string("敌人高亮失败：") + error.what(), false);
    }
}

void LocalBattle::test_enemy_outline(std::uint32_t uid) {
    try {
        void* controller_class = classes_.battle_controller;
        std::vector<void*> controllers =
            find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        Root controller_root(runtime_, controllers.front());
        void* manager_outline = nullptr;
        if (void* get_outline = find_method_optional(
                runtime_, controller_class, "get_spineOutlineManager", 0)) {
            manager_outline =
                runtime_.invoke(get_outline, controllers.front());
        }
        if (!manager_outline) {
            throw std::runtime_error("spineOutlineManager 不可读");
        }
        Root manager_outline_root(runtime_, manager_outline);
        void* outline_class = runtime_.object_class(manager_outline);

        const auto options_count = [&]() {
            void* options = nullptr;
            if (!read_field_deep(runtime_, manager_outline, outline_class,
                                 "m_options", &options) ||
                !options) {
                return 0;
            }
            return runtime_.count(options);
        };
        const auto manager_enabled = [&]() {
            bool enabled = false;
            if (void* get_enabled = find_method_optional(
                    runtime_, outline_class, "get_isEnable", 0)) {
                void* boxed =
                    runtime_.invoke(get_enabled, manager_outline);
                enabled = boxed && runtime_.unbox<bool>(boxed);
            }
            return enabled;
        };
        const auto shader_name = [&]() {
            void* shader = nullptr;
            if (!read_field_deep(runtime_, manager_outline, outline_class,
                                 "m_outlineShader", &shader) ||
                !shader) {
                return std::string("none");
            }
            try {
                void* get_name =
                    find_method_optional(runtime_, classes_.unity_object,
                                         "get_name", 0);
                void* name = get_name ? runtime_.invoke(get_name, shader)
                                      : nullptr;
                return name ? runtime_.string(name)
                            : std::string("unnamed");
            } catch (...) {
                return std::string("unreadable");
            }
        };

        const bool before_enabled = manager_enabled();
        const int before_options = options_count();
        const std::string before_shader = shader_name();
        bool init_called = false;
        if ((!before_enabled || before_options == 0) &&
            find_method_optional(runtime_, outline_class, "Init", 0)) {
            runtime_.invoke(find_method_optional(runtime_, outline_class, "Init", 0),
                            manager_outline);
            init_called = true;
        }
        const bool after_init_enabled = manager_enabled();
        const int after_init_options = options_count();
        const std::string after_init_shader = shader_name();

        void* enemy = nullptr;
        void* object_manager = nullptr;
        read_field_deep(runtime_, controllers.front(), controller_class,
                        "m_objectManger", &object_manager);
        if (object_manager) {
            Root object_manager_root(runtime_, object_manager);
            void* object_manager_class = runtime_.object_class(object_manager);
            void* list = nullptr;
            read_field_deep(runtime_, object_manager, object_manager_class,
                            "m_cachedList", &list);
            if (list) {
                Root list_root(runtime_, list);
                const int count = runtime_.count(list);
                for (int index = 0; index < count; index++) {
                    void* object = runtime_.at(list, index);
                    if (!object ||
                        !runtime_.class_assignable(
                            classes_.enemy, runtime_.object_class(object))) {
                        continue;
                    }
                    std::uint32_t object_uid = 0;
                    read_field_deep(runtime_, object, classes_.battle_object,
                                    "<instanceUid>k__BackingField",
                                    &object_uid);
                    if (object_uid == uid) {
                        enemy = object;
                        break;
                    }
                }
            }
        }
        void* animator = nullptr;
        if (enemy) {
            Root enemy_root(runtime_, enemy);
            read_field_deep(runtime_, enemy, classes_.enemy,
                            "m_spineAnimator", &animator);
        }

        bool draw_called = false;
        bool reset_called = false;
        bool drawn_ready = false;
        int drawn_materials = 0;
        bool drawn_enabled = false;
        BattleOutlineConfigValue drawn_config{};
        if (after_init_enabled && animator) {
            Root animator_root(runtime_, animator);
            if (void* reset = find_method_optional(
                    runtime_, outline_class, "ResetMeshByAnimator", 2)) {
                bool enable = true;
                runtime_.invoke(reset, manager_outline, {animator, &enable});
                reset_called = true;
            }
            if (void* draw = find_method_optional(
                    runtime_, outline_class, "DrawOutline", 1)) {
                runtime_.invoke(draw, manager_outline, {animator});
                draw_called = true;
            }
            try {
                void* options = nullptr;
                if (read_field_deep(runtime_, manager_outline, outline_class,
                                    "m_options", &options) &&
                    options) {
                    Root options_root(runtime_, options);
                    void* item_method = find_method_optional(
                        runtime_, runtime_.object_class(options),
                        "get_Item", 1);
                    void* option =
                        item_method
                            ? runtime_.invoke(item_method, options, {animator})
                            : nullptr;
                    if (option) {
                        Root option_root(runtime_, option);
                        void* option_class = runtime_.object_class(option);
                        if (void* get_option_enabled =
                                find_method_optional(
                                    runtime_, option_class,
                                    "get_isEnabled", 0)) {
                            void* boxed = runtime_.invoke(
                                get_option_enabled, option);
                            drawn_enabled =
                                boxed && runtime_.unbox<bool>(boxed);
                        }
                        void* material_map = nullptr;
                        if (read_field_deep(runtime_, option, option_class,
                                            "m_materials", &material_map) &&
                            material_map) {
                            Root material_root(runtime_, material_map);
                            drawn_materials =
                                runtime_.count(material_map);
                        }
                        read_field_deep(runtime_, option, option_class,
                                        "m_config", &drawn_config);
                        drawn_ready = drawn_enabled && drawn_materials > 0;
                    }
                }
            } catch (...) {
            }
        }

        std::ostringstream output;
        output << "outline-probe uid=" << uid
               << " enemy=" << (enemy ? "1" : "0")
               << " animator=" << (animator ? "1" : "0")
               << " before=" << (before_enabled ? "1" : "0")
               << "/" << before_options
               << " shader=" << before_shader
               << " init=" << (init_called ? "1" : "0")
               << " afterInit=" << (after_init_enabled ? "1" : "0")
               << "/" << after_init_options
               << " shader2=" << after_init_shader
               << " reset=" << (reset_called ? "1" : "0")
               << " draw=" << (draw_called ? "1" : "0")
               << " option=" << (drawn_enabled ? "1" : "0")
               << " materials=" << drawn_materials
               << " size=" << drawn_config.outline_size
               << " rgba=" << drawn_config.outline_color.r << ","
               << drawn_config.outline_color.g << ","
               << drawn_config.outline_color.b << ","
               << drawn_config.outline_color.a
               << " eight=" << (drawn_config.use_eight_ways ? "1" : "0")
               << " ready=" << (drawn_ready ? "1" : "0");
        publish_result(output.str());
    } catch (const std::exception& error) {
        publish_result(std::string("描边探测失败：") + error.what(), false);
    }
}

void LocalBattle::update_enemy_focus(void* controller) {
    (void)controller;
    if (!focused_enemy_uid_ && !focused_enemy_root_) {
        return;
    }
    if (!focused_enemy_until_ ||
        GetTickCount64() >= focused_enemy_until_) {
        clear_enemy_focus();
        return;
    }
    if (focused_enemy_root_) {
        void* enemy = focused_enemy_root_->get();
        if (!enemy) {
            clear_enemy_focus();
            return;
        }
        try {
            if (!method_bool(runtime_, enemy, classes_.entity, "get_alive")) {
                clear_enemy_focus();
                return;
            }
        } catch (...) {
        }
    }
    if (focused_outline_ && focused_outline_manager_ &&
        focused_animator_) {
        try {
            void* outline_class =
                runtime_.object_class(focused_outline_manager_);
            if (void* draw = find_method_optional(
                    runtime_, outline_class, "DrawOutline", 1)) {
                runtime_.invoke(draw, focused_outline_manager_,
                                {focused_animator_});
            }
        } catch (...) {
        }
    }
}

void LocalBattle::clear_enemy_focus() {
    if (focused_outline_ && focused_outline_manager_ &&
        focused_animator_ && focused_animator_root_) {
        try {
            void* outline_class =
                runtime_.object_class(focused_outline_manager_);
            if (void* disable = find_method_optional(
                    runtime_, outline_class, "DisableOutlineByAnimator", 1)) {
                runtime_.invoke(disable, focused_outline_manager_,
                                {focused_animator_});
            }
        } catch (...) {
        }
    }
    focused_outline_manager_root_.reset();
    focused_outline_manager_ = nullptr;
    focused_outline_ = false;
    for (FocusedRendererState& state : focused_renderers_) {
        try {
            if (!state.renderer) {
                continue;
            }
            void* set_order = find_method_optional(
                runtime_, runtime_.object_class(state.renderer),
                "set_sortingOrder", 1);
            if (set_order) {
                int order = state.sorting_order;
                runtime_.invoke(set_order, state.renderer, {&order});
            }
        } catch (...) {
        }
    }
    focused_renderers_.clear();
    for (FocusedMaterialState& state : focused_materials_) {
        try {
            if (!state.material) {
                continue;
            }
            void* set_queue = find_method_optional(
                runtime_, runtime_.object_class(state.material),
                "set_renderQueue", 1);
            if (set_queue) {
                int queue = state.render_queue;
                runtime_.invoke(set_queue, state.material, {&queue});
            }
        } catch (...) {
        }
    }
    focused_materials_.clear();
    focused_animator_root_.reset();
    focused_enemy_root_.reset();
    focused_animator_ = nullptr;
    focused_enemy_uid_ = 0;
    focused_enemy_until_ = 0;
}

void LocalBattle::enemy_wave_probe() {
    try {
        void* controller_class =
            classes_.battle_controller;
        std::vector<void*> controllers =
            find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        Root controller_root(runtime_, controllers.front());
        void* scheduler = nullptr;
        read_field_deep(runtime_, controllers.front(), controller_class,
                        "_scheduler", &scheduler);
        std::ostringstream output;
        output << "controller=" << controllers.front()
               << " scheduler=" << scheduler << '\n';
        dump_object_fields(output, runtime_, "scheduler", scheduler);
        if (!scheduler) {
            throw std::runtime_error("Scheduler 不可读");
        }
        Root scheduler_root(runtime_, scheduler);
        void* scheduler_class = runtime_.object_class(scheduler);
        void* waves = nullptr;
        read_field_deep(runtime_, scheduler, scheduler_class, "m_waves", &waves);
        output << "waves=" << waves;
        if (waves) {
            output << " count=" << runtime_.count(waves);
        }
        output << '\n';
        if (waves) {
            Root waves_root(runtime_, waves);
            const int wave_count = std::min(runtime_.count(waves), 8);
            for (int wave_index = 0; wave_index < wave_count; ++wave_index) {
                void* wave = runtime_.at(waves, wave_index);
                output << "\n=== wave " << wave_index << " ===\n";
                dump_object_fields(output, runtime_, "wave", wave);
                if (!wave) {
                    continue;
                }
                Root wave_root(runtime_, wave);
                void* wave_class = runtime_.object_class(wave);
                void* fragments = nullptr;
                read_field_any(runtime_, wave, wave_class,
                               {"fragments", "m_fragments"}, &fragments);
                output << "fragments=" << fragments;
                if (fragments) {
                    output << " count=" << runtime_.count(fragments);
                }
                output << '\n';
                if (!fragments) {
                    continue;
                }
                Root fragments_root(runtime_, fragments);
                const int fragment_count =
                    std::min(runtime_.count(fragments), 8);
                for (int fragment_index = 0;
                     fragment_index < fragment_count; ++fragment_index) {
                    void* fragment =
                        runtime_.at(fragments, fragment_index);
                    output << "\n--- fragment " << fragment_index
                           << " ---\n";
                    dump_object_fields(output, runtime_, "fragment", fragment);
                    if (!fragment) {
                        continue;
                    }
                    Root fragment_root(runtime_, fragment);
                    void* fragment_class =
                        runtime_.object_class(fragment);
                    void* actions = nullptr;
                    read_field_any(runtime_, fragment, fragment_class,
                                   {"actions", "m_actions"}, &actions);
                    output << "actions=" << actions;
                    if (actions) {
                        output << " count=" << runtime_.count(actions);
                    }
                    output << '\n';
                    if (!actions) {
                        continue;
                    }
                    Root actions_root(runtime_, actions);
                    const int action_count =
                        std::min(runtime_.count(actions), 16);
                    for (int action_index = 0;
                         action_index < action_count; ++action_index) {
                        void* action =
                            runtime_.at(actions, action_index);
                        output << "\n  action " << action_index << '\n';
                        dump_object_fields(output, runtime_, "  action",
                                           action);
                    }
                }
            }
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) /
            "battle_enemy_waves.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("波次探针写入失败");
        }
        publish_result("敌人波次字段已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("敌人波次探针失败：") + error.what(), false);
    }
}

} // namespace ark::better_battle
