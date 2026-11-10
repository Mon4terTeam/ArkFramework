#pragma once

#include "BetterBattle.h"

#include <windows.h>

#include <ark/external_bridge.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../common/il2cpp_exports.h"

namespace ark::better_battle {

inline std::vector<std::string> active_path_for(
    const std::map<std::string, ReplayTreeNode>& nodes,
    const std::string& current_id) {
    std::vector<std::string> path;
    const auto current = nodes.find(current_id);
    for (const ReplayTreeNode* cursor =
             current == nodes.end() ? nullptr : &current->second;
         cursor;) {
        path.push_back(cursor->id);
        if (cursor->parent_id.empty()) {
            break;
        }
        const auto parent = nodes.find(cursor->parent_id);
        cursor = parent == nodes.end() ? nullptr : &parent->second;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

inline std::atomic<LocalBattle*> recorder_owner{nullptr};
inline std::atomic<LocalBattle*> replay_owner{nullptr};
inline std::atomic<LocalBattle*> spine_owner{nullptr};

constexpr const char* kAssembly = "Assembly-CSharp.dll";
constexpr double kFixedPointScale = 4294967296.0;
constexpr std::uint32_t kMethodAttributeStatic = 0x10;
constexpr std::uint32_t kTraceState = 1u << 0;
constexpr std::uint32_t kTraceCost = 1u << 1;
constexpr std::uint32_t kTraceHp = 1u << 2;
constexpr std::uint32_t kTraceSp = 1u << 3;
constexpr std::uint32_t kTraceSkill = 1u << 4;
constexpr std::uint32_t kTraceAlive = 1u << 5;
constexpr std::uint32_t kTraceSkillAffect = 1u << 7;
constexpr std::uint32_t kTraceSkillRemaining = 1u << 8;
constexpr std::uint32_t kTraceSkillTrigger = 1u << 9;
constexpr std::uint32_t kTraceSkillBeginAnim = 1u << 10;
constexpr std::uint32_t kTraceStateId = 1u << 12;
constexpr std::uint32_t kTraceDefault =
    kTraceState | kTraceCost | kTraceHp | kTraceSp | kTraceSkill |
    kTraceSkillAffect | kTraceAlive;
constexpr const char* kDamageTypeKeys[] = {"physical", "magical", "true", "elemental"};

void* find_field_deep(il2cpp::Runtime& runtime, void* klass, const char* name);

void* empty_collection_for_field(il2cpp::Runtime& runtime, void* klass,
                                 const char* field_name);
void collection_add(il2cpp::Runtime& runtime, void* collection, void* value);

struct DictionaryPair {
    void* key = nullptr;
    void* value = nullptr;
};
constexpr int damage_type_bucket(std::int32_t type) {
    switch (type) {
    case static_cast<std::int32_t>(ArkDamageType::Physical):
        return 0;
    case static_cast<std::int32_t>(ArkDamageType::Magical):
        return 1;
    case static_cast<std::int32_t>(ArkDamageType::True):
        return 2;
    case static_cast<std::int32_t>(ArkDamageType::Elemental):
        return 3;
    default:
        return -1;
    }
}
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::Physical)) == 0);
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::Magical)) == 1);
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::True)) == 2);
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::Elemental)) == 3);
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::Healing)) == -1);
static_assert(damage_type_bucket(
                  static_cast<std::int32_t>(ArkDamageType::None)) == -1);
constexpr const char* kBuffAttributeNames[] = {
    "MAX_HP", "ATK", "DEF", "MAGIC_RESISTANCE", "COST", "BLOCK_CNT",
    "MOVE_SPEED", "ATTACK_SPEED", "BASE_ATTACK_TIME", "RESERVED_0",
    "RESERVED_1", "RESERVED_2", "RESERVED_3", "HP_RECOVERY_PER_SEC",
    "SP_RECOVERY_PER_SEC", "ABILITY_RANGE_FORWARD_EXTEND", "MAX_DEPLOY_COUNT",
    "DEF_PENETRATE", "MAGIC_RESIST_PENETRATE",
    "HP_RECOVERY_PER_SEC_BY_MAX_HP_RATIO", "TAUNT_LEVEL", "RESPAWN_TIME",
    "MAX_DECK_STACK_CNT", "MASS_LEVEL", "BASE_FORCE_LEVEL",
    "DEF_PENETRATE_FIXED", "ONE_MINUS_STATUS_RESISTANCE",
    "MAGIC_RESIST_PENETRATE_FIXED", "MAX_EP", "EP_RECOVERY_PER_SEC",
    "SP_RECOVER_RATIO", "EP_DAMAGE_RESISTANCE", "EP_RESISTANCE",
    "DAMAGE_HITRATE_PHYSICAL", "DAMAGE_HITRATE_MAGICAL",
    "EP_BREAK_RECOVER_SPEED", "SLOW_DOWN", "BLOCK_RADIUS_SCALE",
};
constexpr const char* kBuffAbnormalNames[] = {
    "STUNNED", "SP_RECOVER_STOPPED", "TARGET_FREE", "BLOCK_FREE", "HIDDEN",
    "INVINCIBLE", "UNDEADABLE", "HEAL_FREE", "UNBALANCE_IMMUNE", "INVISIBLE",
    "UNUSED_10", "DISARMED", "SILENCED", "UNMOVABLE", "UNUSED_14",
    "ALLY_TARGET_FREE", "FROZEN", "CAMOUFLAGE", "FORCE_DISARMED",
    "STUNNED_NO_AMPLIFY_DAMAGE", "DISABLE_COMBAT", "ELEMENT_FREE_ALL",
    "UNMOVABLE_PRIVATE", "COLD", "SKILL_NOT_ACTIVATABLE", "LEVITATE",
    "DURANCE", "NOT_WITHDRAWABLE", "OUT_OF_GROUND", "SP_MODIFY_STOPPED",
    "ANTI_STATUS_RESISTABLE", "DISARMED_COMBAT", "TOWER_TARGET_FREE", "FEARED",
    "SKILL_ACTIVABLE_IN_ABNORMAL", "MOTION_TARGET_FREE", "FORCE_LEVITATE",
    "BUFF_ADD_CAN_BE_CANCELED_IF_DEFENSE",
    "DEFENSE_BUFF_ADD_IF_CANCELABLE_BUFF", "PALSY", "PALSYING", "ATTRACTED",
    "FEARED_PRIVATE", "DOZE", "TELEPORTED", "GROUND_BOUND",
};
constexpr const char* kBuffAbnormalComboNames[] = {
    "SLEEPING", "SHELTERING",
};
using Root = il2cpp::Runtime::Root;
using RecordSpawnFn = bool (*)(void*, std::uint32_t, int, void*, void*);
using RecordSkillFn = bool (*)(void*, void*, void*, void*);
using RecordWithdrawFn = bool (*)(void*, void*, void*);
using RecordLocateFn = void (*)(void*, void*, void*);
template <class T>
bool read_at(const void* pointer, T* out) {
    if (!pointer || !out) {
        return false;
    }
    std::memcpy(out, pointer, sizeof(T));
    return true;
}
struct FieldCacheKey {
    void* runtime = nullptr;
    void* klass = nullptr;
    const char* name = nullptr;

    bool operator==(const FieldCacheKey& other) const {
        return runtime == other.runtime && klass == other.klass &&
               std::strcmp(name, other.name) == 0;
    }
};
struct FieldCacheHash {
    std::size_t operator()(const FieldCacheKey& key) const {
        std::size_t value = std::hash<void*>{}(key.runtime);
        value ^= std::hash<void*>{}(key.klass) + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= std::hash<const char*>{}(key.name) + 0x9e3779b9u + (value << 6) +
                 (value >> 2);
        return value;
    }
};
struct MethodCacheKey {
    void* runtime = nullptr;
    void* klass = nullptr;
    const char* name = nullptr;
    int count = 0;

    bool operator==(const MethodCacheKey& other) const {
        return runtime == other.runtime && klass == other.klass && count == other.count &&
               std::strcmp(name, other.name) == 0;
    }
};
struct MethodCacheHash {
    std::size_t operator()(const MethodCacheKey& key) const {
        std::size_t value = std::hash<void*>{}(key.runtime);
        value ^= std::hash<void*>{}(key.klass) + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= std::hash<const char*>{}(key.name) + 0x9e3779b9u + (value << 6) +
                 (value >> 2);
        value ^= std::hash<int>{}(key.count) + 0x9e3779b9u + (value << 6) + (value >> 2);
        return value;
    }
};
std::unordered_map<FieldCacheKey, void*, FieldCacheHash>& field_cache();
std::unordered_map<MethodCacheKey, void*, MethodCacheHash>& method_cache();
std::unordered_map<void*, std::size_t>& field_offset_cache();
struct UniqueEquipPairLayout {
    void* klass = nullptr;
    void* id_field = nullptr;
    void* level_field = nullptr;
    std::string id_name;
    std::string level_name;

    bool valid() const { return klass && id_field && level_field; }
};
// UniqueEquipPair is a private value type in this build, so it is absent from the IL2CPP
// index and its field names cannot be assumed. Identify its string and int32 fields by
// their managed types. This also keeps working if a future build renames the fields.
UniqueEquipPairLayout resolve_unique_equip_pair_class(il2cpp::Runtime& runtime,
                                                      void* pair_class);
template <class T>
bool read_field_deep(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                     T* out) {
    if (!object || !out) {
        return false;
    }
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return false;
    }
    auto offset = field_offset_cache().find(field);
    if (offset == field_offset_cache().end()) {
        offset = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
    }
    return read_at(static_cast<const unsigned char*>(object) + offset->second, out);
}
template <class T>
bool write_field_deep(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                      const T& value) {
    if (!object) {
        return false;
    }
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return false;
    }
    auto offset = field_offset_cache().find(field);
    if (offset == field_offset_cache().end()) {
        offset = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
    }
    void* destination = static_cast<unsigned char*>(object) + offset->second;
    std::memcpy(destination, &value, sizeof(T));
    return true;
}

std::vector<void*> find_live_objects(il2cpp::Runtime& runtime, void* klass,
                                     std::size_t limit);

std::vector<void*> find_rooted_objects(il2cpp::Runtime& runtime, void* klass,
                                       std::size_t limit);

std::uint64_t perf_now_us();

void* find_field_deep(il2cpp::Runtime& runtime, void* klass, const char* name);

UniqueEquipPairLayout resolve_unique_equip_pair(il2cpp::Runtime& runtime, void* inst_class);

void* find_method_optional(il2cpp::Runtime& runtime, void* klass, const char* name, int count);

std::string read_string_field(il2cpp::Runtime& runtime, void* object, void* klass,
                              const char* name);

bool write_character_inst_trust(il2cpp::Runtime& runtime, void* instance,
                                       void* instance_class,
                                       int trust_percent);

std::string describe_instance_fields(il2cpp::Runtime& runtime, void* object);

bool method_bool(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                 bool fallback = false);

int method_int(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
               int fallback = -1);

bool read_fixed_play_time(il2cpp::Runtime& runtime, void* klass, double* value);

std::string method_string(il2cpp::Runtime& runtime, void* object, void* klass, const char* name);

double unbox_fp(il2cpp::Runtime& runtime, void* boxed);

double method_fp(il2cpp::Runtime& runtime, void* object, void* klass, const char* name);

float method_float(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                   float fallback = 0);

std::string trim_float_text(std::string text);

std::string format_blackboard_number(float value, const std::string& format);

std::string skill_description_text(il2cpp::Runtime& runtime, void* skill, void* skill_class,
                                   const std::string& raw);

void* box_zero(il2cpp::Runtime& runtime, void* klass);

void* construct_with_args(il2cpp::Runtime& runtime, void* klass, void* constructor,
                          std::initializer_list<void*> arguments);

std::string scene_constant(il2cpp::Runtime& runtime, const char* name);

std::string sanitize_cache_token(std::string value);

std::vector<void*> find_controllers(il2cpp::Runtime& runtime, void* controller_class);

void* listdict_value_at(il2cpp::Runtime& runtime, void* listdict, int index);

void* player_side_of(il2cpp::Runtime& runtime, void* controller, void* controller_class);

int player_side_value(il2cpp::Runtime& runtime, void* controller, void* controller_class);

void* find_tile(il2cpp::Runtime& runtime, void* controller, void* controller_class, int row,
                int col);

void* find_character(il2cpp::Runtime& runtime, void* controller, void* controller_class,
                     std::uint32_t uid);

void* find_card(il2cpp::Runtime& runtime, void* controller, void* controller_class,
                std::uint32_t uid, void** deck_out);

std::string describe_class_layout(il2cpp::Runtime& runtime, void* klass, bool include_methods);

void collect_buff_uid_map(il2cpp::Runtime& runtime, void* map, void* buff_class,
                          std::vector<void*>& output, std::unordered_set<void*>& seen,
                          int& budget);

void collect_buff_internal_list(il2cpp::Runtime& runtime, void* list, void* buff_class,
                                std::vector<void*>& output, std::unordered_set<void*>& seen);

std::string join_nonempty(const std::vector<std::string>& values, const char* separator);

std::string format_buff_modifiers(il2cpp::Runtime& runtime, void* buff, void* buff_class);

std::int64_t buff_modifier_time_bucket(std::int64_t raw);

std::string format_buff_blackboard(il2cpp::Runtime& runtime, void* buff,
                                   void* buff_class);

std::string format_buff_blackboard(
    const std::vector<BuffBlackboardEntry>& entries);

void read_blackboard_entries(il2cpp::Runtime& runtime, void* board,
                             std::vector<BuffBlackboardEntry>& output);

void fill_buff_static(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                      void* entity_class, BuffStaticInfo& info);

void fill_buff_dynamic(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                       std::uint32_t uid, BuffStaticInfo& static_info,
                       UnitInfo::BuffInfo& info);

void read_talents_from_data(il2cpp::Runtime& runtime, void* data, void* data_class,
                            int potential, std::vector<TalentInfo>& talents);

void read_modules_from_data(il2cpp::Runtime& runtime, void* data, void* data_class,
                            std::vector<ModuleInfo>& modules);

void read_character_modules(il2cpp::Runtime& runtime, void* character, void* character_class,
                            std::vector<ModuleInfo>& modules);

void read_entity_buffs(il2cpp::Runtime& runtime, void* entity, void* entity_class,
                       void* buff_class,
                       std::map<std::pair<void*, std::uint32_t>, BuffStaticInfo>&
                           static_cache,
                       std::uint64_t generation, std::uint64_t& cache_hits,
                       std::uint64_t& cache_misses,
                       std::vector<UnitInfo::BuffInfo>& output,
                       int* shield_layers = nullptr);

void write_type_dump(il2cpp::Runtime& runtime, const std::filesystem::path& path,
                     void* klass);

std::vector<DictionaryPair> read_dictionary_pairs(il2cpp::Runtime& runtime, void* dictionary,
                                                  int limit = 100000);

void* dictionary_value_for_key(il2cpp::Runtime& runtime, void* dictionary,
                               const std::string& key);

void* empty_collection_for_field(il2cpp::Runtime& runtime, void* klass,
                                 const char* field_name);

void collection_add(il2cpp::Runtime& runtime, void* collection, void* value);

} // namespace ark::better_battle
