#include "damage_formula_capture.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include "../common/il2cpp_runtime.h"

namespace ark::better_battle {
namespace {

constexpr double kFixedPointOne = 4294967296.0;
constexpr std::size_t kRecordLimit = 500000;
constexpr std::size_t kPendingLimit = 4096;
// Must match BetterBattle.h kNoSourceActorId; this module cannot include the
// plugin header without a cycle.
constexpr char kNoSourceActorId[] = "__no_source__";

DamageFormulaCapture* active_capture = nullptr;

struct ActiveAttack {
    void* attack_info = nullptr;
    void* source = nullptr;
    void* target = nullptr;
};

std::mutex active_attack_mutex;
std::unordered_map<void*, ActiveAttack> active_attacks;
thread_local std::vector<ActiveAttack> active_attack_stack;

using BuffCalculateFn = void (*)(void* self, void* attack_info, void* method);
using BuffModifierFn = void (*)(void* self, void* modifier, void* method);
using EntityCalculateFn = void (*)(void* self, void* source,
                                   void* attack_info, void* method);
using PenetrateFn = std::int64_t (*)(void* source, void* target,
                                     std::int32_t damage_type, void* method);
using CalculateDamageNoEventFn = std::int64_t (*)(
    void* target, void* source, void* attack_info);
using CalculateDamageFn = std::uint64_t (*)(std::uintptr_t, std::uintptr_t,
                                            std::uintptr_t, std::uintptr_t,
                                            std::uintptr_t);
using CalculateInternalFn = std::int64_t (*)(
    void* attack_info, std::int64_t calc_arg0, std::int64_t calc_arg1,
    void* target);
using CalculateDamageValueFn = std::uint64_t (*)(
    std::uint64_t damage, void* target, std::int32_t damage_type,
    std::uint64_t arg3, std::uint64_t arg4);
using ElementSourceTargetFn = std::uint64_t (*)(void* source, void* target,
                                                std::uint64_t value);
using ElementTargetFn = std::uint64_t (*)(std::uint64_t value, void* target,
                                          std::uint64_t value2);
using ElementValueOnlyFn = std::uint64_t (*)(std::uint64_t value,
                                             void* target);
using HealSourceTargetFn = std::uint64_t (*)(void* source, void* target,
                                             std::uint64_t value);
using HealTargetFn = std::uint64_t (*)(std::uint64_t value, void* target,
                                       std::uint64_t value2);

BuffCalculateFn g_buff_calculate = nullptr;
BuffCalculateFn g_buff_being_calculate = nullptr;
BuffModifierFn g_buff_take_damage = nullptr;
BuffModifierFn g_buff_output_damage = nullptr;
EntityCalculateFn g_entity_calculate = nullptr;
PenetrateFn g_penetrate_ratio = nullptr;
PenetrateFn g_penetrate_fixed = nullptr;
CalculateDamageNoEventFn g_calculate_damage_no_event = nullptr;
CalculateDamageFn g_calculate_damage = nullptr;
CalculateInternalFn g_calculate_internal = nullptr;
CalculateDamageValueFn g_calculate_damage_value = nullptr;
ElementSourceTargetFn g_element_source_target = nullptr;
ElementTargetFn g_element_target = nullptr;
ElementValueOnlyFn g_element_value_only = nullptr;
HealSourceTargetFn g_heal_source_target = nullptr;
HealTargetFn g_heal_target = nullptr;

// Pushes a formula transaction scope. A target-only caller inherits the
// already-active source so the inner CalculateDamageInternal can still bind
// both sides when it is invoked from a healer/elemental wrapper.
bool push_attack_context(void* attack_info, void* source, void* target) {
    auto* capture = active_capture;
    if (!capture || !capture->active() || !target) {
        return false;
    }
    if (!source && !active_attack_stack.empty()) {
        source = active_attack_stack.back().source;
    }
    active_attack_stack.push_back(ActiveAttack{attack_info, source, target});
    return true;
}

} // namespace

DamageFormulaCapture::DamageFormulaCapture(il2cpp::Runtime& runtime,
                                           const ArkPluginApiV1& host)
    : runtime_(runtime), host_(host) {}

DamageFormulaCapture::~DamageFormulaCapture() {
    shutdown();
}

bool DamageFormulaCapture::initialize() {
    if (calculate_hook_ && being_hook_ && entity_calculate_hook_ &&
        buff_take_damage_hook_ && buff_output_damage_hook_ &&
        calculate_damage_no_event_hook_ && calculate_damage_hook_ &&
        calculate_internal_hook_ && calculate_damage_value_hook_ &&
        element_source_target_hook_ && element_target_hook_ &&
        element_value_only_hook_ && heal_source_target_hook_ &&
        heal_target_hook_) {
        return true;
    }
    try {
        void* formula_class = runtime_.find("Torappu.Battle", "BattleFormula");
        void* attack_info_class = runtime_.nested(formula_class, "AttackInfo");
        void* buff_class = runtime_.find("Torappu.Battle", "Buff");
        void* modifier_class = runtime_.find("Torappu.Battle", "Modifier");
        void* entity_class = runtime_.find("Torappu.Battle", "Entity");
        void* attributes_class = runtime_.find("Torappu.Battle", "Attributes");
        void* attribute_type_class =
            runtime_.find("Torappu", "AttributeType");

        const auto adjust_offset = [&](const char* field_name) {
            const std::size_t offset =
                runtime_.field_offset(runtime_.field(attack_info_class,
                                                      field_name));
            if (offset < 2 * sizeof(void*)) {
                throw std::runtime_error("Unsupported AttackInfo field offset");
            }
            return static_cast<std::ptrdiff_t>(offset - 2 * sizeof(void*));
        };
        offsets_.damage_type = adjust_offset("damageType");
        offsets_.origin_atk = adjust_offset("originAtk");
        offsets_.atk = adjust_offset("atk");
        offsets_.atk_scale = adjust_offset("atkScale");
        offsets_.atk_addition = adjust_offset("atkAddition");

        buff_key_offset_ = runtime_.field_offset(
            runtime_.field(buff_class, "<key>k__BackingField"));
        buff_uid_offset_ = runtime_.field_offset(
            runtime_.field(buff_class, "<instanceUid>k__BackingField"));
        const auto adjust_modifier_offset = [&](const char* field_name) {
            const std::size_t offset =
                runtime_.field_offset(runtime_.field(modifier_class, field_name));
            if (offset < 2 * sizeof(void*)) {
                throw std::runtime_error("Unsupported Modifier field offset");
            }
            return static_cast<std::size_t>(offset - 2 * sizeof(void*));
        };
        modifier_value_offset_ = adjust_modifier_offset("m_value");
        modifier_real_delta_offset_ = adjust_modifier_offset("realDelta");
        modifier_extra_data_offset_ = adjust_modifier_offset("extraData");
        modifier_origin_value_offset_ =
            adjust_modifier_offset("<originValue>k__BackingField");
        entity_id_offset_ = runtime_.field_offset(
            runtime_.field(entity_class, "<id>k__BackingField"));
        void* entity_uid_field = nullptr;
        for (void* current = entity_class; current;
             current = runtime_.class_parent(current)) {
            entity_uid_field =
                runtime_.class_field(current, "<instanceUid>k__BackingField");
            if (entity_uid_field) {
                break;
            }
        }
        if (!entity_uid_field) {
            throw std::runtime_error("Missing Entity.instanceUid");
        }
        entity_uid_offset_ = runtime_.field_offset(entity_uid_field);
        entity_attributes_offset_ = runtime_.field_offset(
            runtime_.field(entity_class, "m_attributes"));

        const auto find_unique_method = [&](void* klass, const char* name,
                                            std::uint32_t count) {
            void* iterator = nullptr;
            void* found = nullptr;
            while (void* candidate = runtime_.class_methods(klass, &iterator)) {
                if (std::strcmp(runtime_.method_name(candidate), name) != 0 ||
                    runtime_.method_count(candidate) != count) {
                    continue;
                }
                if (found) {
                    throw std::runtime_error(std::string("Ambiguous ") + name);
                }
                found = candidate;
            }
            if (!found) {
                throw std::runtime_error(std::string("Missing ") + name);
            }
            return found;
        };

        const auto find_buff_method = [&](const char* name) {
            void* iterator = nullptr;
            void* found = nullptr;
            while (void* candidate = runtime_.class_methods(buff_class,
                                                            &iterator)) {
                if (std::strcmp(runtime_.method_name(candidate), name) != 0 ||
                    runtime_.method_count(candidate) != 1) {
                    continue;
                }
                std::string parameter;
                try {
                    parameter =
                        runtime_.type(runtime_.method_param(candidate, 0));
                } catch (...) {
                    continue;
                }
                if (parameter.find("AttackInfo&") == std::string::npos) {
                    continue;
                }
                if (found) {
                    throw std::runtime_error(
                        std::string("Ambiguous Buff.") + name);
                }
                found = candidate;
            }
            if (!found) {
                throw std::runtime_error(std::string("Missing Buff.") + name);
            }
            return found;
        };

        const auto find_buff_modifier_method = [&](const char* name) {
            void* iterator = nullptr;
            void* found = nullptr;
            while (void* candidate = runtime_.class_methods(buff_class,
                                                            &iterator)) {
                if (std::strcmp(runtime_.method_name(candidate), name) != 0 ||
                    runtime_.method_count(candidate) != 1) {
                    continue;
                }
                std::string parameter;
                try {
                    parameter =
                        runtime_.type(runtime_.method_param(candidate, 0));
                } catch (...) {
                    continue;
                }
                if (parameter.find("Modifier&") == std::string::npos) {
                    continue;
                }
                if (found) {
                    throw std::runtime_error(
                        std::string("Ambiguous Buff.") + name);
                }
                found = candidate;
            }
            if (!found) {
                throw std::runtime_error(std::string("Missing Buff.") + name);
            }
            return found;
        };

        attribute_raw_method_ =
            find_unique_method(attributes_class, "GetRawValue", 1);
        attribute_value_method_ =
            find_unique_method(attributes_class, "GetValue", 1);
        attribute_raw_entry_ = *static_cast<void**>(attribute_raw_method_);
        attribute_value_entry_ = *static_cast<void**>(attribute_value_method_);
        attribute_atk_ =
            runtime_.static_value<int>(attribute_type_class, "ATK");
        attribute_defense_ =
            runtime_.static_value<int>(attribute_type_class, "DEF");
        attribute_resistance_ = runtime_.static_value<int>(
            attribute_type_class, "MAGIC_RESISTANCE");
        try {
            attribute_elemental_resistance_ = runtime_.static_value<int>(
                attribute_type_class, "ELEMENTAL_DAMAGE_RESISTANCE");
        } catch (...) {
            attribute_elemental_resistance_ = -1;
        }

        modifier_final_delta_method_ =
            find_unique_method(modifier_class, "get_finalDelta", 0);
        modifier_damage_type_method_ =
            find_unique_method(modifier_class, "get_damageType", 0);
        modifier_source_method_ =
            find_unique_method(modifier_class, "get_source", 0);
        modifier_target_method_ =
            find_unique_method(modifier_class, "get_target", 0);
        modifier_final_delta_entry_ =
            *static_cast<void**>(modifier_final_delta_method_);
        modifier_damage_type_entry_ =
            *static_cast<void**>(modifier_damage_type_method_);
        modifier_source_entry_ = *static_cast<void**>(modifier_source_method_);
        modifier_target_entry_ = *static_cast<void**>(modifier_target_method_);

        void* calculate_method = find_buff_method("OnCalculateDamage");
        void* being_method = find_buff_method("OnBeingCalculateDamage");
        void* take_damage_method =
            find_buff_modifier_method("OnTakeDamage");
        void* output_damage_method =
            find_buff_modifier_method("OnOutputDamage");
        void* entity_calculate_method =
            find_unique_method(entity_class, "OnCalculateDamage", 2);
        void* penetrate_ratio_method =
            find_unique_method(formula_class, "CalculatePenetrateRatio", 3);
        void* penetrate_fixed_method =
            find_unique_method(formula_class, "CalculatePenetrateFixed", 3);
        void* calculate_damage_no_event_method = runtime_.method(
            formula_class, "CalculateDamageNoEvent", 3,
            {"Torappu.Battle.Entity", "Torappu.Battle.Entity",
             "Torappu.Battle.BattleFormula.AttackInfo"});
        void* calculate_damage_method = runtime_.method(
            formula_class, "CalculateDamage", 5,
            {"Torappu.Battle.Entity", "Torappu.Battle.Entity",
             "System.Boolean", "System.Boolean",
             "Torappu.Battle.BattleFormula.AttackInfo&"});
        void* calculate_damage_value_method = runtime_.method(
            formula_class, "CalculateDamage", 5,
            {"Torappu.FP", "Torappu.Battle.Entity",
             "Torappu.Battle.DamageType", "Torappu.FP", "Torappu.FP"});
        void* element_source_target_method = runtime_.method(
            formula_class, "CalculateElementDamage", 3,
            {"Torappu.Battle.Entity", "Torappu.Battle.Entity",
             "Torappu.FP"});
        void* element_target_method = runtime_.method(
            formula_class, "CalculateElementDamage", 3,
            {"Torappu.FP", "Torappu.Battle.Entity", "Torappu.FP"});
        void* element_value_only_method = runtime_.method(
            formula_class, "CalculateElementDamage", 2,
            {"Torappu.FP", "Torappu.Battle.Entity"});
        void* heal_source_target_method = runtime_.method(
            formula_class, "CalculateHeal", 3,
            {"Torappu.Battle.Entity", "Torappu.Battle.Entity",
             "Torappu.FP"});
        void* heal_target_method = runtime_.method(
            formula_class, "CalculateHeal", 3,
            {"Torappu.FP", "Torappu.Battle.Entity", "Torappu.FP"});
        void* calculate_internal_method =
            find_unique_method(formula_class, "CalculateDamageInternal", 4);
        const auto log_method_signature = [&](const char* name, void* method) {
            std::string text = std::string("Damage hook signature ") + name + "(";
            try {
                const std::uint32_t count = runtime_.method_count(method);
                for (std::uint32_t i = 0; i != count; ++i) {
                    if (i) {
                        text += ", ";
                    }
                    const char* parameter =
                        runtime_.method_param_name(method, i);
                    text += parameter ? parameter : "?";
                    text += " ";
                    text += runtime_.type(runtime_.method_param(method, i));
                }
                text += ") -> ";
                text += runtime_.type(runtime_.method_return(method));
            } catch (const std::exception& exception) {
                text += "unavailable: ";
                text += exception.what();
            }
            host_.log(host_.owner, 1, text.c_str());
        };
        log_method_signature("CalculateDamage", calculate_damage_method);
        log_method_signature("CalculateDamageNoEvent",
                             calculate_damage_no_event_method);
        log_method_signature("CalculateDamageInternal",
                             calculate_internal_method);
        log_method_signature("Entity.OnCalculateDamage",
                             entity_calculate_method);
        log_method_signature("CalculateDamage(value)",
                             calculate_damage_value_method);
        log_method_signature("CalculateElementDamage(source,target)",
                             element_source_target_method);
        log_method_signature("CalculateElementDamage(target)",
                             element_target_method);
        log_method_signature("CalculateElementDamage(value-only)",
                             element_value_only_method);
        log_method_signature("CalculateHeal(source,target)",
                             heal_source_target_method);
        log_method_signature("CalculateHeal(target)",
                             heal_target_method);

        g_buff_calculate = reinterpret_cast<BuffCalculateFn>(
            *static_cast<void**>(calculate_method));
        g_buff_being_calculate = reinterpret_cast<BuffCalculateFn>(
            *static_cast<void**>(being_method));
        g_buff_take_damage = reinterpret_cast<BuffModifierFn>(
            *static_cast<void**>(take_damage_method));
        g_buff_output_damage = reinterpret_cast<BuffModifierFn>(
            *static_cast<void**>(output_damage_method));
        g_entity_calculate = reinterpret_cast<EntityCalculateFn>(
            *static_cast<void**>(entity_calculate_method));
        g_penetrate_ratio = reinterpret_cast<PenetrateFn>(
            *static_cast<void**>(penetrate_ratio_method));
        g_penetrate_fixed = reinterpret_cast<PenetrateFn>(
            *static_cast<void**>(penetrate_fixed_method));
        g_calculate_damage_no_event =
            reinterpret_cast<CalculateDamageNoEventFn>(
                *static_cast<void**>(calculate_damage_no_event_method));
        g_calculate_damage = reinterpret_cast<CalculateDamageFn>(
            *static_cast<void**>(calculate_damage_method));
        g_calculate_internal = reinterpret_cast<CalculateInternalFn>(
            *static_cast<void**>(calculate_internal_method));
        g_calculate_damage_value = reinterpret_cast<CalculateDamageValueFn>(
            *static_cast<void**>(calculate_damage_value_method));
        g_element_source_target =
            reinterpret_cast<ElementSourceTargetFn>(
                *static_cast<void**>(element_source_target_method));
        g_element_target = reinterpret_cast<ElementTargetFn>(
            *static_cast<void**>(element_target_method));
        g_element_value_only = reinterpret_cast<ElementValueOnlyFn>(
            *static_cast<void**>(element_value_only_method));
        g_heal_source_target = reinterpret_cast<HealSourceTargetFn>(
            *static_cast<void**>(heal_source_target_method));
        g_heal_target = reinterpret_cast<HealTargetFn>(
            *static_cast<void**>(heal_target_method));

        active_capture = this;
        calculate_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_buff_calculate),
            reinterpret_cast<void*>(&on_buff_calculate), &calculate_original_);
        being_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_buff_being_calculate),
            reinterpret_cast<void*>(&on_buff_being_calculate),
            &being_original_);
        entity_calculate_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_entity_calculate),
            reinterpret_cast<void*>(&on_entity_calculate),
            &entity_calculate_original_);
        buff_take_damage_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_buff_take_damage),
            reinterpret_cast<void*>(&on_buff_take_damage),
            &buff_take_damage_original_);
        buff_output_damage_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_buff_output_damage),
            reinterpret_cast<void*>(&on_buff_output_damage),
            &buff_output_damage_original_);
        penetrate_ratio_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_penetrate_ratio),
            reinterpret_cast<void*>(&on_penetrate_ratio),
            &penetrate_ratio_original_);
        penetrate_fixed_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_penetrate_fixed),
            reinterpret_cast<void*>(&on_penetrate_fixed),
            &penetrate_fixed_original_);
        calculate_damage_no_event_hook_ = host_.create_native_hook(
            host_.owner,
            reinterpret_cast<void*>(g_calculate_damage_no_event),
            reinterpret_cast<void*>(&on_calculate_damage_no_event),
            &calculate_damage_no_event_original_);
        if (!host_.create_native_hook_v2) {
            throw std::runtime_error("Host does not provide native hook v2");
        }
        calculate_damage_hook_ = host_.create_native_hook_v2(
            host_.owner, reinterpret_cast<void*>(g_calculate_damage),
            reinterpret_cast<void*>(&on_calculate_damage),
            &calculate_damage_original_, 5);
        calculate_internal_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_calculate_internal),
            reinterpret_cast<void*>(&on_calculate_internal),
            &calculate_internal_original_);
        calculate_damage_value_hook_ = host_.create_native_hook_v2(
            host_.owner, reinterpret_cast<void*>(g_calculate_damage_value),
            reinterpret_cast<void*>(&on_calculate_damage_value),
            &calculate_damage_value_original_, 5);
        element_source_target_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_element_source_target),
            reinterpret_cast<void*>(&on_element_source_target),
            &element_source_target_original_);
        element_target_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_element_target),
            reinterpret_cast<void*>(&on_element_target),
            &element_target_original_);
        element_value_only_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_element_value_only),
            reinterpret_cast<void*>(&on_element_value_only),
            &element_value_only_original_);
        heal_source_target_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_heal_source_target),
            reinterpret_cast<void*>(&on_heal_source_target),
            &heal_source_target_original_);
        heal_target_hook_ = host_.create_native_hook(
            host_.owner, reinterpret_cast<void*>(g_heal_target),
            reinterpret_cast<void*>(&on_heal_target),
            &heal_target_original_);
        if (!calculate_hook_ || !being_hook_ || !entity_calculate_hook_ ||
            !buff_take_damage_hook_ || !buff_output_damage_hook_ ||
            !penetrate_ratio_hook_ || !penetrate_fixed_hook_ ||
            !calculate_damage_no_event_hook_ ||
            !calculate_damage_hook_ ||
            !calculate_internal_hook_ ||
            !calculate_damage_value_hook_ ||
            !element_source_target_hook_ || !element_target_hook_ ||
            !element_value_only_hook_ || !heal_source_target_hook_ ||
            !heal_target_hook_ || !calculate_original_ ||
            !being_original_ || !entity_calculate_original_ ||
            !buff_take_damage_original_ ||
            !buff_output_damage_original_ ||
            !penetrate_ratio_original_ || !penetrate_fixed_original_ ||
            !calculate_damage_no_event_original_ ||
            !calculate_damage_original_ ||
            !calculate_internal_original_ ||
            !calculate_damage_value_original_ ||
            !element_source_target_original_ ||
            !element_target_original_ || !element_value_only_original_ ||
            !heal_source_target_original_ || !heal_target_original_) {
            throw std::runtime_error("Could not create damage formula hooks");
        }
        // The detours must return through MinHook's trampoline, never through
        // the patched function entry.
        g_buff_calculate = reinterpret_cast<BuffCalculateFn>(
            calculate_original_);
        g_buff_being_calculate = reinterpret_cast<BuffCalculateFn>(
            being_original_);
        g_entity_calculate = reinterpret_cast<EntityCalculateFn>(
            entity_calculate_original_);
        g_buff_take_damage = reinterpret_cast<BuffModifierFn>(
            buff_take_damage_original_);
        g_buff_output_damage = reinterpret_cast<BuffModifierFn>(
            buff_output_damage_original_);
        g_penetrate_ratio = reinterpret_cast<PenetrateFn>(
            penetrate_ratio_original_);
        g_penetrate_fixed = reinterpret_cast<PenetrateFn>(
            penetrate_fixed_original_);
        g_calculate_damage_no_event =
            reinterpret_cast<CalculateDamageNoEventFn>(
                calculate_damage_no_event_original_);
        g_calculate_damage = reinterpret_cast<CalculateDamageFn>(
            calculate_damage_original_);
        g_calculate_internal = reinterpret_cast<CalculateInternalFn>(
            calculate_internal_original_);
        g_calculate_damage_value = reinterpret_cast<CalculateDamageValueFn>(
            calculate_damage_value_original_);
        g_element_source_target = reinterpret_cast<ElementSourceTargetFn>(
            element_source_target_original_);
        g_element_target = reinterpret_cast<ElementTargetFn>(
            element_target_original_);
        g_element_value_only = reinterpret_cast<ElementValueOnlyFn>(
            element_value_only_original_);
        g_heal_source_target = reinterpret_cast<HealSourceTargetFn>(
            heal_source_target_original_);
        g_heal_target = reinterpret_cast<HealTargetFn>(
            heal_target_original_);
        if (!host_.enable_native_hook(host_.owner, calculate_hook_, true) ||
            !host_.enable_native_hook(host_.owner, being_hook_, true) ||
            !host_.enable_native_hook(host_.owner, entity_calculate_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, buff_take_damage_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, buff_output_damage_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, penetrate_ratio_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, penetrate_fixed_hook_,
                                      true) ||
            !host_.enable_native_hook(
                host_.owner, calculate_damage_no_event_hook_, true) ||
            !host_.enable_native_hook(host_.owner, calculate_damage_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, calculate_internal_hook_,
                                      true) ||
            !host_.enable_native_hook(
                host_.owner, calculate_damage_value_hook_, true) ||
            !host_.enable_native_hook(
                host_.owner, element_source_target_hook_, true) ||
            !host_.enable_native_hook(host_.owner, element_target_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, element_value_only_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, heal_source_target_hook_,
                                      true) ||
            !host_.enable_native_hook(host_.owner, heal_target_hook_, true)) {
            throw std::runtime_error("Could not enable damage formula hooks");
        }
        return true;
    } catch (...) {
        shutdown();
        throw;
    }
}

bool DamageFormulaCapture::shutdown() {
    stop();
    bool removed = true;
    const auto remove = [&](std::uint64_t& hook, void*& original) {
        if (!hook) {
            return;
        }
        host_.enable_native_hook(host_.owner, hook, false);
        removed = host_.remove_native_hook(host_.owner, hook) && removed;
        hook = 0;
        original = nullptr;
    };
    remove(calculate_hook_, calculate_original_);
    remove(being_hook_, being_original_);
    remove(entity_calculate_hook_, entity_calculate_original_);
    remove(buff_take_damage_hook_, buff_take_damage_original_);
    remove(buff_output_damage_hook_, buff_output_damage_original_);
    remove(penetrate_ratio_hook_, penetrate_ratio_original_);
    remove(penetrate_fixed_hook_, penetrate_fixed_original_);
    remove(calculate_damage_no_event_hook_,
           calculate_damage_no_event_original_);
    remove(calculate_damage_hook_, calculate_damage_original_);
    remove(calculate_internal_hook_, calculate_internal_original_);
    remove(calculate_damage_value_hook_, calculate_damage_value_original_);
    remove(element_source_target_hook_, element_source_target_original_);
    remove(element_target_hook_, element_target_original_);
    remove(element_value_only_hook_, element_value_only_original_);
    remove(heal_source_target_hook_, heal_source_target_original_);
    remove(heal_target_hook_, heal_target_original_);
    if (active_capture == this) {
        active_capture = nullptr;
    }
    g_buff_calculate = nullptr;
    g_buff_being_calculate = nullptr;
    g_buff_take_damage = nullptr;
    g_buff_output_damage = nullptr;
    g_entity_calculate = nullptr;
    g_penetrate_ratio = nullptr;
    g_penetrate_fixed = nullptr;
    g_calculate_damage_no_event = nullptr;
    g_calculate_damage = nullptr;
    g_calculate_internal = nullptr;
    g_calculate_damage_value = nullptr;
    g_element_source_target = nullptr;
    g_element_target = nullptr;
    g_element_value_only = nullptr;
    g_heal_source_target = nullptr;
    g_heal_target = nullptr;
    return removed;
}

bool DamageFormulaCapture::start(const std::string& path) {
    stop();
    std::lock_guard lock(mutex_);
    output_.open(path, std::ios::out | std::ios::trunc);
    if (!output_) {
        return false;
    }
    path_ = path;
    records_.store(0, std::memory_order_release);
    calls_.store(0, std::memory_order_release);
    calculate_damage_calls_.store(0, std::memory_order_release);
    calculate_damage_no_event_calls_.store(0, std::memory_order_release);
    calculate_damage_internal_calls_.store(0, std::memory_order_release);
    calculate_damage_value_calls_.store(0, std::memory_order_release);
    element_source_target_calls_.store(0, std::memory_order_release);
    element_target_calls_.store(0, std::memory_order_release);
    element_value_only_calls_.store(0, std::memory_order_release);
    heal_source_target_calls_.store(0, std::memory_order_release);
    heal_target_calls_.store(0, std::memory_order_release);
    limit_reached_.store(false, std::memory_order_release);
    attribution_ = DamageAttributionSnapshot{};
    attribution_.available = true;
    attribution_index_.clear();
    no_source_index_.clear();
    pending_hits_.clear();
    pending_fallback_hits_.clear();
    pending_events_.clear();
    pending_modifiers_.clear();
    active_.store(true, std::memory_order_release);
    return true;
}

void DamageFormulaCapture::stop() {
    active_.store(false, std::memory_order_release);
    std::lock_guard lock(mutex_);
    if (output_) {
        output_.flush();
        output_.close();
    }
}

void DamageFormulaCapture::set_frame(std::uint32_t frame) {
    frame_.store(frame, std::memory_order_release);
}

bool DamageFormulaCapture::active() const {
    return active_.load(std::memory_order_acquire);
}

std::uint64_t DamageFormulaCapture::records() const {
    return records_.load(std::memory_order_acquire);
}

std::uint64_t DamageFormulaCapture::calls() const {
    return calls_.load(std::memory_order_acquire);
}

DamageFormulaCallCounts DamageFormulaCapture::entry_counts() const {
    DamageFormulaCallCounts counts;
    counts.calculate_damage =
        calculate_damage_calls_.load(std::memory_order_acquire);
    counts.calculate_damage_no_event =
        calculate_damage_no_event_calls_.load(std::memory_order_acquire);
    counts.calculate_damage_internal =
        calculate_damage_internal_calls_.load(std::memory_order_acquire);
    counts.calculate_damage_value =
        calculate_damage_value_calls_.load(std::memory_order_acquire);
    counts.element_source_target =
        element_source_target_calls_.load(std::memory_order_acquire);
    counts.element_target =
        element_target_calls_.load(std::memory_order_acquire);
    counts.element_value_only =
        element_value_only_calls_.load(std::memory_order_acquire);
    counts.heal_source_target =
        heal_source_target_calls_.load(std::memory_order_acquire);
    counts.heal_target =
        heal_target_calls_.load(std::memory_order_acquire);
    return counts;
}

bool DamageFormulaCapture::readable(const void* address, std::size_t size) {
    if (!address || !size) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) == 0) {
        return false;
    }
    if (info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = begin + size;
    const auto region_end =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return begin < end && end <= region_end;
}

std::string DamageFormulaCapture::read_string(void* value) {
    if (!readable(value, 0x14)) {
        return {};
    }
    std::int32_t length = 0;
    std::memcpy(&length, static_cast<const char*>(value) + 0x10,
                sizeof(length));
    if (length <= 0 || length > 4096) {
        return {};
    }
    const auto* chars = reinterpret_cast<const wchar_t*>(
        static_cast<const char*>(value) + 0x14);
    if (!readable(chars, static_cast<std::size_t>(length) * sizeof(wchar_t))) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars,
                                         length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length,
                        result.data(), size, nullptr, nullptr);
    return result;
}

DamageFormulaCapture::AttackInfoValues DamageFormulaCapture::read_attack_info(
    void* attack_info, const AttackInfoOffsets& offsets) {
    AttackInfoValues values;
    if (!readable(attack_info, 0x28)) {
        return values;
    }
    const char* base = static_cast<const char*>(attack_info);
    std::memcpy(&values.damage_type, base + offsets.damage_type,
                sizeof(values.damage_type));
    std::memcpy(&values.origin_atk, base + offsets.origin_atk,
                sizeof(values.origin_atk));
    std::memcpy(&values.atk, base + offsets.atk, sizeof(values.atk));
    std::memcpy(&values.atk_scale, base + offsets.atk_scale,
                sizeof(values.atk_scale));
    std::memcpy(&values.atk_addition, base + offsets.atk_addition,
                sizeof(values.atk_addition));
    return values;
}

DamageFormulaCapture::ModifierValues
DamageFormulaCapture::read_modifier_values(void* modifier) const {
    ModifierValues values;
    if (!readable(modifier, std::max(
                                {modifier_value_offset_ + sizeof(std::int64_t),
                                 modifier_real_delta_offset_ +
                                     sizeof(std::int64_t),
                                 modifier_extra_data_offset_ +
                                     sizeof(std::int64_t),
                                 modifier_origin_value_offset_ +
                                     sizeof(std::int64_t)}))) {
        return values;
    }
    const char* base = static_cast<const char*>(modifier);
    std::memcpy(&values.value, base + modifier_value_offset_,
                sizeof(values.value));
    std::memcpy(&values.real_delta, base + modifier_real_delta_offset_,
                sizeof(values.real_delta));
    std::memcpy(&values.extra_data, base + modifier_extra_data_offset_,
                sizeof(values.extra_data));
    std::memcpy(&values.origin_value, base + modifier_origin_value_offset_,
                sizeof(values.origin_value));
    if (modifier_final_delta_entry_) {
        using IntGetter = std::int64_t (*)(void*, void*);
        values.final_delta =
            reinterpret_cast<IntGetter>(modifier_final_delta_entry_)(
                modifier, modifier_final_delta_method_);
    }
    if (modifier_damage_type_entry_) {
        using IntGetter = std::int32_t (*)(void*, void*);
        values.damage_type =
            reinterpret_cast<IntGetter>(modifier_damage_type_entry_)(
                modifier, modifier_damage_type_method_);
    }
    return values;
}

DamageFormulaCapture::EntityAttributes
DamageFormulaCapture::read_entity_attributes(void* entity) const {
    EntityAttributes values;
    if (!entity || !attribute_raw_entry_ || !attribute_value_entry_ ||
        !readable(static_cast<const char*>(entity) + entity_attributes_offset_,
                  sizeof(void*))) {
        return values;
    }
    void* attributes = nullptr;
    std::memcpy(&attributes,
                static_cast<const char*>(entity) + entity_attributes_offset_,
                sizeof(attributes));
    if (!attributes) {
        return values;
    }
    using AttributeFn = std::int64_t (*)(void*, std::int32_t, void*);
    const auto raw = reinterpret_cast<AttributeFn>(attribute_raw_entry_);
    const auto value = reinterpret_cast<AttributeFn>(attribute_value_entry_);
    const auto read = [&](int attribute, std::int64_t& target,
                          bool raw_value) {
        if (attribute < 0) {
            return;
        }
        target = raw_value
                     ? raw(attributes, attribute, attribute_raw_method_)
                     : value(attributes, attribute,
                             attribute_value_method_);
    };
    read(attribute_atk_, values.atk_raw, true);
    read(attribute_atk_, values.atk, false);
    read(attribute_defense_, values.defense_raw, true);
    read(attribute_defense_, values.defense, false);
    read(attribute_resistance_, values.resistance_raw, true);
    read(attribute_resistance_, values.resistance, false);
    read(attribute_elemental_resistance_, values.elemental_resistance, false);
    return values;
}

void DamageFormulaCapture::update_entity(
    void* entity, const std::string& id, std::uint32_t uid,
    const std::vector<AttributeContribution>& contributions) {
    if (!entity || id.empty()) {
        return;
    }
    EntitySnapshot snapshot;
    snapshot.valid = true;
    snapshot.id = id;
    snapshot.attributes = read_entity_attributes(entity);
    snapshot.attack.raw =
        static_cast<double>(snapshot.attributes.atk_raw) / kFixedPointOne;
    snapshot.defense.raw =
        static_cast<double>(snapshot.attributes.defense_raw) / kFixedPointOne;
    snapshot.resistance.raw =
        static_cast<double>(snapshot.attributes.resistance_raw) /
        kFixedPointOne;

    for (const AttributeContribution& contribution : contributions) {
        AttributeStage* stage = nullptr;
        if (contribution.attribute == "ATK") {
            stage = &snapshot.attack;
        } else if (contribution.attribute == "DEF") {
            stage = &snapshot.defense;
        } else if (contribution.attribute == "MAGIC_RESISTANCE") {
            stage = &snapshot.resistance;
        } else if (contribution.attribute ==
                   "ELEMENTAL_DAMAGE_RESISTANCE") {
            stage = &snapshot.elemental_resistance;
        }
        if (stage) {
            stage->contributions.push_back(contribution);
        }
    }

    const auto add_residual = [&](AttributeStage& stage,
                                  std::int64_t raw_value,
                                  const char* attribute) {
        std::vector<std::size_t> all(stage.contributions.size());
        for (std::size_t index = 0; index < all.size(); ++index) {
            all[index] = index;
        }
        const double modeled = evaluate_attribute_stage(stage, all);
        const double actual =
            static_cast<double>(raw_value) / kFixedPointOne;
        const double residual = actual - modeled;
        if (std::abs(residual) <= 1e-7) {
            return;
        }
        AttributeContribution contribution;
        contribution.attribute = attribute;
        contribution.key = std::string("system.unmodeled-") + attribute;
        contribution.label = "未建模属性修饰";
        contribution.class_name = "system";
        contribution.final_addition = residual;
        stage.contributions.push_back(std::move(contribution));
    };
    add_residual(snapshot.attack, snapshot.attributes.atk, "ATK");
    add_residual(snapshot.defense, snapshot.attributes.defense, "DEF");
    add_residual(snapshot.resistance, snapshot.attributes.resistance,
                 "MAGIC_RESISTANCE");

    std::lock_guard lock(mutex_);
    entities_[id] = std::move(snapshot);
    if (uid != 0) {
        entities_by_uid_[uid] = entities_[id];
    }
    entity_ids_[entity] = id;
}

DamageAttributionSnapshot DamageFormulaCapture::snapshot() const {
    std::lock_guard lock(mutex_);
    DamageAttributionSnapshot result = attribution_;
    result.unmatched_events =
        result.observed_events > result.hits
            ? result.observed_events - result.hits
            : 0;
    result.entries = entry_counts();
    return result;
}

bool DamageFormulaCapture::same_fixed_value(double left, double right) {
    constexpr double kScale = 4294967296.0;
    const auto raw = [](double value) {
        return std::llround(std::abs(value) * kScale);
    };
    return raw(left) == raw(right);
}

std::vector<std::size_t> DamageFormulaCapture::find_modifier_chain(
    const PendingEvent& event, bool* ambiguous) const {
    if (ambiguous) {
        *ambiguous = false;
    }
    std::vector<std::size_t> chain;
    std::vector<bool> used(pending_modifiers_.size(), false);
    double current = event.calculated;
    for (std::size_t depth = 0; depth < 32; ++depth) {
        std::size_t matches = 0;
        std::size_t matched = 0;
        for (std::size_t index = 0; index < pending_modifiers_.size();
             ++index) {
            if (used[index]) {
                continue;
            }
            const PendingModifier& change = pending_modifiers_[index];
            if (change.damage_type != event.damage_type ||
                (event.target_uid != 0 && change.target_uid != 0 &&
                 change.target_uid != event.target_uid) ||
                (event.source_uid != 0 && change.source_uid != 0 &&
                 change.source_uid != event.source_uid) ||
                !same_fixed_value(change.after_final, current)) {
                continue;
            }
            ++matches;
            matched = index;
        }
        if (matches == 0) {
            break;
        }
        if (matches > 1) {
            if (ambiguous) {
                *ambiguous = true;
            }
            return {};
        }
        chain.push_back(matched);
        used[matched] = true;
        current = pending_modifiers_[matched].before_final;
    }
    return chain;
}

bool DamageFormulaCapture::hit_matches_value(
    const PendingHit& hit, const PendingEvent& event, double value) const {
    return hit.target == event.target &&
           hit.damage_type == event.damage_type &&
           (hit.target_uid == 0 || event.target_uid == 0 ||
            hit.target_uid == event.target_uid) &&
           same_fixed_value(hit.calculated, value);
}

void DamageFormulaCapture::queue_hit(const PendingHit& hit) {
    if (!active_.load(std::memory_order_acquire)) {
        return;
    }
    PendingEvent matched;
    std::vector<PendingModifier> modifier_chain;
    bool found = false;
    {
        std::lock_guard lock(mutex_);
        for (auto iterator = pending_events_.begin();
             iterator != pending_events_.end(); ++iterator) {
            if (!hit_matches_value(hit, *iterator,
                                   iterator->calculated)) {
                continue;
            }
            matched = std::move(*iterator);
            pending_events_.erase(iterator);
            found = true;
            break;
        }
        if (!found) {
            for (auto iterator = pending_events_.begin();
                 iterator != pending_events_.end(); ++iterator) {
                bool ambiguous = false;
                const std::vector<std::size_t> indices =
                    find_modifier_chain(*iterator, &ambiguous);
                if (ambiguous || indices.empty()) {
                    continue;
                }
                const double root =
                    pending_modifiers_[indices.back()].before_final;
                if (!hit_matches_value(hit, *iterator, root)) {
                    continue;
                }
                matched = std::move(*iterator);
                modifier_chain.reserve(indices.size());
                for (std::size_t index : indices) {
                    modifier_chain.push_back(pending_modifiers_[index]);
                }
                for (auto index = indices.rbegin(); index != indices.rend();
                     ++index) {
                    pending_modifiers_.erase(
                        pending_modifiers_.begin() +
                        static_cast<std::ptrdiff_t>(*index));
                }
                pending_events_.erase(iterator);
                found = true;
                break;
            }
        }
        if (!found) {
            std::vector<PendingHit>& queue =
                hit.source_uid != 0 ? pending_hits_ : pending_fallback_hits_;
            if (queue.size() >= kPendingLimit) {
                queue.erase(queue.begin());
            }
            queue.push_back(hit);
        }
    }
    if (found) {
        attribute_hit(matched.actor, matched.source_uid, matched.target,
                      matched.target_uid, hit.frame, hit.target_attributes,
                      hit.target_attributes_valid, hit.attack_info,
                      hit.calculated, matched.calculated, modifier_chain);
    }
}

void DamageFormulaCapture::observe_damage_event(
    std::uint32_t frame, const std::string& actor,
    const std::string& target, std::uint32_t source_uid,
    std::uint32_t target_uid, std::int32_t damage_type,
    double calculated) {
    if (!active_.load(std::memory_order_acquire) || actor.empty() ||
        target.empty()) {
        return;
    }
    PendingEvent event{frame, actor, target, source_uid, target_uid,
                       damage_type, std::abs(calculated)};
    PendingHit matched;
    std::vector<PendingModifier> modifier_chain;
    bool found = false;
    {
        std::lock_guard lock(mutex_);
        ++attribution_.observed_events;
        switch (static_cast<DamageKind>(damage_type)) {
        case DamageKind::Physical:
            ++attribution_.physical_events;
            break;
        case DamageKind::Magical:
            ++attribution_.magical_events;
            break;
        case DamageKind::True:
            ++attribution_.true_events;
            break;
        case DamageKind::Healing:
            ++attribution_.healing_events;
            break;
        case DamageKind::Elemental:
            ++attribution_.elemental_events;
            break;
        default:
            break;
        }
        if (actor == kNoSourceActorId) {
            ++attribution_.no_source_events;
        }
        const auto consume = [&](std::vector<PendingHit>& queue,
                                 double value) {
            for (auto iterator = queue.begin(); iterator != queue.end();
                 ++iterator) {
                if (!hit_matches_value(*iterator, event, value)) {
                    continue;
                }
                matched = std::move(*iterator);
                queue.erase(iterator);
                found = true;
                return true;
            }
            return false;
        };
        // Sourced formula transactions always win over target-only fallback
        // hits. The latter come from value overloads and preview calculations;
        // they are only used when no sourced hit closes the same event.
        found = consume(pending_hits_, event.calculated) ||
                consume(pending_fallback_hits_, event.calculated);
        if (!found) {
            bool ambiguous = false;
            const std::vector<std::size_t> indices =
                find_modifier_chain(event, &ambiguous);
            if (!ambiguous && !indices.empty()) {
                const double root =
                    pending_modifiers_[indices.back()].before_final;
                found = consume(pending_hits_, root) ||
                        consume(pending_fallback_hits_, root);
                if (found) {
                    modifier_chain.reserve(indices.size());
                    for (std::size_t index : indices) {
                        modifier_chain.push_back(pending_modifiers_[index]);
                    }
                    for (auto index = indices.rbegin();
                         index != indices.rend(); ++index) {
                        pending_modifiers_.erase(
                            pending_modifiers_.begin() +
                            static_cast<std::ptrdiff_t>(*index));
                    }
                }
            }
        }
        if (!found) {
            if (pending_events_.size() >= kPendingLimit) {
                pending_events_.erase(pending_events_.begin());
            }
            pending_events_.push_back(std::move(event));
        }
    }
    if (found) {
        attribute_hit(actor, source_uid, target, target_uid, frame,
                      matched.target_attributes,
                      matched.target_attributes_valid,
                      matched.attack_info, matched.calculated,
                      event.calculated, modifier_chain);
    }
}

void DamageFormulaCapture::attribute_hit(
    const std::string& source_id, std::uint32_t source_uid,
    const std::string& target_id,
    std::uint32_t target_uid, std::uint32_t frame,
    const EntityAttributes& target_attributes,
    bool target_attributes_valid,
    const AttackInfoValues& attack_info,
    double formula_calculated, double final_calculated,
    const std::vector<PendingModifier>& modifier_chain) {
    (void)frame;
    constexpr double kScale = 4294967296.0;
    std::lock_guard lock(mutex_);
    if (!attribution_.available) {
        return;
    }
    const auto source_it = entities_.find(source_id);
    const auto target_uid_it = entities_by_uid_.find(target_uid);
    const auto target_it = entities_.find(target_id);
    const EntitySnapshot* source_snapshot =
        source_it == entities_.end() ? nullptr : &source_it->second;
    const EntitySnapshot* target_snapshot = nullptr;
    if (target_uid_it != entities_by_uid_.end()) {
        target_snapshot = &target_uid_it->second;
    } else if (target_it != entities_.end()) {
        target_snapshot = &target_it->second;
    }

    DamageAttributionInput input;
    input.kind = static_cast<DamageKind>(attack_info.damage_type);
    const std::string metric =
        input.kind == DamageKind::Healing ? "healing" : "damage";
    input.attack_scale = attack_info.atk_scale / kScale;
    input.attack_addition = attack_info.atk_addition / kScale;
    input.expected_formula_damage = std::abs(formula_calculated);
    input.has_expected_final_damage = true;
    input.expected_final_damage = std::abs(final_calculated);

    struct ObservedFactor {
        std::string key;
        std::string source_id;
        std::uint32_t buff_uid = 0;
        std::int64_t delta_raw = 0;
    };
    std::map<std::string, ObservedFactor> observed_factors;
    for (const PendingModifier& change : modifier_chain) {
        const std::int64_t delta_raw =
            change.after_final_raw - change.before_final_raw;
        if (delta_raw != 0) {
            const std::string identity =
                change.key + '\x1f' + std::to_string(change.uid) + '\x1f' +
                change.source_id;
            auto& factor = observed_factors[identity];
            factor.key = change.key;
            factor.source_id = change.source_id;
            factor.buff_uid = change.uid;
            factor.delta_raw += delta_raw;
        }
    }
    std::int64_t observed_delta_raw = 0;
    for (const auto& [identity, factor] : observed_factors) {
        (void)identity;
        input.post_damage_factors.push_back(
            DamageFactor{factor.key, factor.key, factor.source_id,
                         factor.buff_uid, factor.delta_raw});
        observed_delta_raw += factor.delta_raw;
    }
    const std::int64_t expected_delta_raw =
        fixed_from_double(std::abs(final_calculated)) -
        fixed_from_double(std::abs(formula_calculated));
    const std::int64_t residual_delta_raw =
        expected_delta_raw - observed_delta_raw;
    if (residual_delta_raw != 0) {
        input.post_damage_factors.push_back(
            DamageFactor{"system.post-defense-delta",
                         "其余防御后伤判增量（待归因）", source_id, 0,
                         residual_delta_raw});
    }

    const double actual_attack = attack_info.atk / kScale;
    if (source_snapshot) {
        input.source_attack = source_snapshot->attack;
    } else {
        input.source_attack.raw = actual_attack;
    }
    const auto add_stage_residual =
        [](AttributeStage& stage, double actual, const std::string& attribute,
           const std::string& key, const std::string& label) {
            std::vector<std::size_t> all(stage.contributions.size());
            for (std::size_t index = 0; index < all.size(); ++index) {
                all[index] = index;
            }
            const double modeled = evaluate_attribute_stage(stage, all);
            const double residual = actual - modeled;
            if (std::abs(residual) <= 1e-7) {
                return;
            }
            double final_scaler = 1;
            for (const AttributeContribution& contribution :
                 stage.contributions) {
                final_scaler *= contribution.final_scaler;
            }
            if (std::abs(final_scaler) <= 1e-9) {
                final_scaler = 1;
            }
            AttributeContribution contribution;
            contribution.attribute = attribute;
            contribution.key = key;
            contribution.label = label;
            contribution.class_name = "system";
            contribution.final_addition = residual / final_scaler;
            stage.contributions.push_back(std::move(contribution));
        };
    add_stage_residual(input.source_attack, actual_attack,
                       "ATK", "system.unmodeled-hit-atk",
                       "命中时未建模攻击力");

    if (target_snapshot) {
        if (input.kind == DamageKind::Physical) {
            input.target_mitigation = target_snapshot->defense;
        } else if (input.kind == DamageKind::Magical) {
            input.target_mitigation = target_snapshot->resistance;
        } else if (input.kind == DamageKind::Elemental) {
            input.target_mitigation = target_snapshot->elemental_resistance;
        }
    }
    if (target_attributes_valid) {
        const auto apply_target_value =
            [&](std::int64_t value, const std::string& attribute) {
                const double actual = value / kScale;
                input.target_mitigation.raw = actual;
                add_stage_residual(input.target_mitigation, actual, attribute,
                                   "system.unmodeled-target-mitigation",
                                   "命中时未建模目标减伤");
            };
        if (input.kind == DamageKind::Physical) {
            apply_target_value(target_attributes.defense, "DEF");
        } else if (input.kind == DamageKind::Magical) {
            apply_target_value(target_attributes.resistance,
                               "MAGIC_RESISTANCE");
        } else if (input.kind == DamageKind::Elemental) {
            apply_target_value(target_attributes.elemental_resistance,
                               "ELEMENTAL_DAMAGE_RESISTANCE");
        }
    }

    const DamageAttributionResult attributed =
        attribute_damage_formula(input);
    ++attribution_.hits;
    attribution_.base_damage += attributed.base_damage;
    attribution_.modeled_damage += attributed.modeled_damage;
    attribution_.unallocated += attributed.unallocated;
    attribution_.exact_zones =
        attribution_.exact_zones && attributed.exact_zones;
    if (output_) {
        std::ostringstream trace;
        trace << std::setprecision(17)
              << "{\"kind\":\"attribution\",\"frame\":" << frame
              << ",\"actor\":\"" << source_id << "\",\"target\":\""
              << target_id << "\",\"target_uid\":" << target_uid
              << ",\"damage_type\":" << attack_info.damage_type
              << ",\"formula\":" << formula_calculated
              << ",\"final\":" << final_calculated
              << ",\"modeled\":" << attributed.modeled_damage
              << ",\"unallocated\":" << attributed.unallocated
              << ",\"chain\":[";
        bool first_factor = true;
        for (const PendingModifier& change : modifier_chain) {
            if (!first_factor) {
                trace << ',';
            }
            first_factor = false;
            trace << "{\"key\":\"" << change.key << "\",\"uid\":"
                  << change.uid << ",\"before\":" << change.before_final
                  << ",\"after\":" << change.after_final << '}';
        }
        trace << "]}\n";
        output_ << trace.str();
        records_.fetch_add(1, std::memory_order_release);
    }

    const auto add_total =
        [&](const std::string& zone,
            const DamageAttributionSource& source,
            double shapley, double counterfactual) {
            if (std::abs(shapley) <= 1e-9 &&
                std::abs(counterfactual) <= 1e-9) {
                return;
            }
            const std::string key =
                metric + "\x1f" + source_id + "\x1f" + target_id + "\x1f" +
                std::to_string(target_uid) + "\x1f" + zone + "\x1f" +
                source.kind + "\x1f" + source.key + "\x1f" +
                std::to_string(source.buff_uid);
            auto found = attribution_index_.find(key);
            if (found == attribution_index_.end()) {
                DamageAttributionTotal total;
                total.actor = source_id;
                total.target = target_id;
                total.target_uid = target_uid;
                total.metric = metric;
                total.zone = zone;
                total.source_kind = source.kind;
                total.source_key = source.key;
                total.source_label = source.label;
                total.source_id = source.source;
                total.buff_uid = source.buff_uid;
                found = attribution_index_
                            .emplace(key, attribution_.totals.size())
                            .first;
                attribution_.totals.push_back(std::move(total));
            }
            DamageAttributionTotal& total =
                attribution_.totals[found->second];
            total.shapley += shapley;
            total.counterfactual += counterfactual;
            ++total.hits;
        };

    for (const DamageAttributionAllocation& allocation :
         attributed.allocations) {
        add_total(allocation.zone, allocation.source, allocation.shapley,
                  allocation.counterfactual);
    }
    if (source_id == kNoSourceActorId &&
        std::abs(attributed.base_damage) > 1e-9) {
        // __no_source__ is a real damage category in this client, not a
        // failure. Keep its base damage in a separate ledger so selecting
        // 无来源 in the panel still shows data without breaking the Buff-total
        // conservation invariant.
        const std::string key =
            metric + "\x1f" + target_id + "\x1f" +
            std::to_string(target_uid) + "\x1f" +
            std::to_string(attack_info.damage_type);
        auto found = no_source_index_.find(key);
        if (found == no_source_index_.end()) {
            DamageAttributionTotal total;
            total.actor = source_id;
            total.target = target_id;
            total.target_uid = target_uid;
            total.metric = metric;
            total.zone = "base";
            total.source_kind = "base";
            total.source_key = "base.no-source";
            total.source_label = "无来源伤害";
            total.source_id = source_id;
            found = no_source_index_
                        .emplace(key, attribution_.no_source_totals.size())
                        .first;
            attribution_.no_source_totals.push_back(std::move(total));
        }
        DamageAttributionTotal& total =
            attribution_.no_source_totals[found->second];
        total.shapley += attributed.base_damage;
        total.counterfactual += attributed.base_damage;
        ++total.hits;
    }
    if (std::abs(attributed.unallocated) > 1e-9) {
        DamageAttributionSource residual;
        residual.kind = "system";
        residual.key = "system.unallocated";
        residual.label = "未归因残差";
        add_total("residual", residual, attributed.unallocated,
                  attributed.unallocated);
    }
}

void DamageFormulaCapture::record_buff(
    const char* phase, std::uint32_t frame, const std::string& key,
    std::uint32_t uid, const AttackInfoValues& before,
    const AttackInfoValues& after) {
    if (before.damage_type == after.damage_type &&
        before.origin_atk == after.origin_atk && before.atk == after.atk &&
        before.atk_scale == after.atk_scale &&
        before.atk_addition == after.atk_addition) {
        return;
    }
    if (
        !active_.load(std::memory_order_acquire) ||
        limit_reached_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (!output_) {
        return;
    }
    if (records_.load(std::memory_order_relaxed) >= kRecordLimit) {
        limit_reached_.store(true, std::memory_order_release);
        return;
    }
    std::ostringstream line;
    line << std::setprecision(17)
         << "{\"kind\":\"buff\",\"phase\":\"" << phase
         << "\",\"frame\":" << frame << ",\"buff\":\"" << key
         << "\",\"uid\":" << uid << ",\"before\":{\"damage_type\":"
         << before.damage_type << ",\"origin_atk\":"
         << before.origin_atk / kFixedPointOne << ",\"atk\":"
         << before.atk / kFixedPointOne << ",\"atk_scale\":"
         << before.atk_scale / kFixedPointOne << ",\"atk_addition\":"
         << before.atk_addition / kFixedPointOne
         << "},\"after\":{\"damage_type\":" << after.damage_type
         << ",\"origin_atk\":" << after.origin_atk / kFixedPointOne
         << ",\"atk\":" << after.atk / kFixedPointOne
         << ",\"atk_scale\":" << after.atk_scale / kFixedPointOne
         << ",\"atk_addition\":" << after.atk_addition / kFixedPointOne
         << "}}\n";
    output_ << line.str();
    records_.fetch_add(1, std::memory_order_release);
}

void DamageFormulaCapture::record_modifier_change(
    const char* phase, std::uint32_t frame, const std::string& key,
    std::uint32_t uid, const ModifierValues& before,
    const ModifierValues& after, const std::string& source_id,
    std::uint32_t source_uid, const std::string& target_id,
    std::uint32_t target_uid) {
    if (before.damage_type == after.damage_type &&
        before.final_delta == after.final_delta &&
        before.value == after.value && before.real_delta == after.real_delta &&
        before.extra_data == after.extra_data &&
        before.origin_value == after.origin_value) {
        return;
    }
    if (!active_.load(std::memory_order_acquire) ||
        limit_reached_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (!output_) {
        return;
    }
    if (records_.load(std::memory_order_relaxed) >= kRecordLimit) {
        limit_reached_.store(true, std::memory_order_release);
        return;
    }
    std::ostringstream line;
    line << std::setprecision(17)
         << "{\"kind\":\"modifier\",\"phase\":\"" << phase
         << "\",\"frame\":" << frame << ",\"buff\":\"" << key
         << "\",\"uid\":" << uid << ",\"damage_type\":"
         << after.damage_type << ",\"source\":\"" << source_id
         << "\",\"source_uid\":" << source_uid << ",\"target\":\""
         << target_id << "\",\"target_uid\":" << target_uid
         << ",\"before\":{\"final\":" << before.final_delta / kFixedPointOne
         << ",\"value\":" << before.value / kFixedPointOne
         << ",\"real_delta\":" << before.real_delta / kFixedPointOne
         << ",\"extra\":" << before.extra_data / kFixedPointOne
         << ",\"origin_value\":" << before.origin_value / kFixedPointOne
         << "},\"after\":{\"final\":" << after.final_delta / kFixedPointOne
         << ",\"value\":" << after.value / kFixedPointOne
         << ",\"real_delta\":" << after.real_delta / kFixedPointOne
         << ",\"extra\":" << after.extra_data / kFixedPointOne
         << ",\"origin_value\":" << after.origin_value / kFixedPointOne
         << "}}\n";
    output_ << line.str();
    records_.fetch_add(1, std::memory_order_release);

    PendingModifier pending;
    pending.frame = frame;
    pending.key = key;
    pending.uid = uid;
    pending.source_id = source_id;
    pending.source_uid = source_uid;
    pending.target_id = target_id;
    pending.target_uid = target_uid;
    pending.damage_type = after.damage_type;
    pending.before_final =
        std::abs(before.final_delta / kFixedPointOne);
    pending.after_final = std::abs(after.final_delta / kFixedPointOne);
    pending.before_final_raw = std::abs(before.final_delta);
    pending.after_final_raw = std::abs(after.final_delta);
    pending.before_value = std::abs(before.value / kFixedPointOne);
    pending.after_value = std::abs(after.value / kFixedPointOne);
    if (pending_modifiers_.size() >= kPendingLimit) {
        pending_modifiers_.erase(pending_modifiers_.begin());
    }
    pending_modifiers_.push_back(std::move(pending));
}

void DamageFormulaCapture::record_attack_info(
    const char* phase, std::uint32_t frame, void* self, void* source,
    const AttackInfoValues& before, const AttackInfoValues& after,
    const EntityAttributes& source_attributes,
    const EntityAttributes& target_attributes) {
    if (!active_.load(std::memory_order_acquire) ||
        limit_reached_.load(std::memory_order_acquire)) {
        return;
    }
    const auto entity_id = [&](void* entity) {
        if (!readable(entity, entity_id_offset_ + sizeof(void*))) {
            return std::string();
        }
        void* id = nullptr;
        std::memcpy(&id,
                    static_cast<const char*>(entity) + entity_id_offset_,
                    sizeof(id));
        return read_string(id);
    };
    std::lock_guard lock(mutex_);
    if (!output_) {
        return;
    }
    if (records_.load(std::memory_order_relaxed) >= kRecordLimit) {
        limit_reached_.store(true, std::memory_order_release);
        return;
    }
    std::ostringstream line;
    line << std::setprecision(17)
         << "{\"kind\":\"attack_info\",\"phase\":\"" << phase
         << "\",\"frame\":" << frame << ",\"self\":\"" << entity_id(self)
         << "\",\"source\":\"" << entity_id(source)
         << "\",\"damage_type\":" << before.damage_type
         << ",\"before\":{\"origin_atk\":"
         << before.origin_atk / kFixedPointOne << ",\"atk\":"
         << before.atk / kFixedPointOne << ",\"atk_scale\":"
         << before.atk_scale / kFixedPointOne << ",\"atk_addition\":"
         << before.atk_addition / kFixedPointOne
         << "},\"after\":{\"origin_atk\":"
         << after.origin_atk / kFixedPointOne << ",\"atk\":"
         << after.atk / kFixedPointOne << ",\"atk_scale\":"
         << after.atk_scale / kFixedPointOne << ",\"atk_addition\":"
         << after.atk_addition / kFixedPointOne
         << "},\"source_attr\":{\"atk_raw\":"
         << source_attributes.atk_raw / kFixedPointOne << ",\"atk\":"
         << source_attributes.atk / kFixedPointOne
         << "},\"target_attr\":{\"def_raw\":"
         << target_attributes.defense_raw / kFixedPointOne << ",\"def\":"
         << target_attributes.defense / kFixedPointOne << ",\"res_raw\":"
         << target_attributes.resistance_raw / kFixedPointOne << ",\"res\":"
         << target_attributes.resistance / kFixedPointOne << ",\"ep\":"
         << target_attributes.elemental_resistance / kFixedPointOne
         << "}}\n";
    output_ << line.str();
    records_.fetch_add(1, std::memory_order_release);
}

void DamageFormulaCapture::record_formula_internal(
    std::uint32_t frame, void* source, void* target, std::int64_t calc_arg0,
    std::int64_t calc_arg1, const AttackInfoValues& attack_info,
    std::int64_t result, bool matched) {
    if (!active_.load(std::memory_order_acquire) ||
        limit_reached_.load(std::memory_order_acquire)) {
        return;
    }
    const auto entity_id = [&](void* entity) {
        if (!readable(entity, entity_id_offset_ + sizeof(void*))) {
            return std::string();
        }
        void* id = nullptr;
        std::memcpy(&id,
                    static_cast<const char*>(entity) + entity_id_offset_,
                    sizeof(id));
        return read_string(id);
    };
    const std::string target_id = entity_id(target);
    std::uint32_t source_uid = 0;
    if (readable(source, entity_uid_offset_ + sizeof(source_uid))) {
        std::memcpy(&source_uid,
                    static_cast<const char*>(source) + entity_uid_offset_,
                    sizeof(source_uid));
    }
    std::uint32_t target_uid = 0;
    if (readable(target, entity_uid_offset_ + sizeof(target_uid))) {
        std::memcpy(&target_uid,
                    static_cast<const char*>(target) + entity_uid_offset_,
                    sizeof(target_uid));
    }
    const bool target_attributes_valid =
        readable(target, entity_attributes_offset_ + sizeof(void*));
    const EntityAttributes target_attributes =
        target_attributes_valid ? read_entity_attributes(target)
                                : EntityAttributes{};
    {
        std::lock_guard lock(mutex_);
        if (!output_) {
            return;
        }
        if (records_.load(std::memory_order_relaxed) >= kRecordLimit) {
            limit_reached_.store(true, std::memory_order_release);
            return;
        }
        std::ostringstream line;
        line << std::setprecision(17)
             << "{\"kind\":\"internal\",\"frame\":" << frame
             << ",\"matched\":" << (matched ? 1 : 0)
             << ",\"source\":\"" << entity_id(source)
             << "\",\"source_uid\":" << source_uid
             << ",\"target\":\"" << target_id
             << "\",\"target_uid\":" << target_uid
             << ",\"damage_type\":" << attack_info.damage_type
             << ",\"attack_info\":{\"origin_atk\":"
             << attack_info.origin_atk / kFixedPointOne << ",\"atk\":"
             << attack_info.atk / kFixedPointOne << ",\"atk_scale\":"
             << attack_info.atk_scale / kFixedPointOne
             << ",\"atk_addition\":"
             << attack_info.atk_addition / kFixedPointOne
             << "},\"calc_arg0\":" << calc_arg0 / kFixedPointOne
             << ",\"calc_arg1\":" << calc_arg1 / kFixedPointOne
             << ",\"result\":" << result / kFixedPointOne << "}\n";
        output_ << line.str();
        records_.fetch_add(1, std::memory_order_release);
    }
    // CalculateDamageInternal has no source parameter; a call outside an outer
    // CalculateDamage/CalculateDamageNoEvent scope is a preview or probe. It
    // may produce a plausible value, but it must never seed a pending hit that
    // a later real event could consume twice.
    if (matched && target_uid != 0 && !target_id.empty()) {
        queue_hit(PendingHit{frame, target_id, source_uid, target_uid,
                             attack_info.damage_type, target_attributes,
                             target_attributes_valid, attack_info,
                             result / kFixedPointOne});
    }
}

void DamageFormulaCapture::record_penetration(
    const char* phase, std::uint32_t frame, void* source, void* target,
    std::int32_t damage_type, std::int64_t result) {
    if (!active_.load(std::memory_order_acquire) ||
        limit_reached_.load(std::memory_order_acquire)) {
        return;
    }
    const auto entity_id = [&](void* entity) {
        if (!readable(entity, entity_id_offset_ + sizeof(void*))) {
            return std::string();
        }
        void* id = nullptr;
        std::memcpy(&id,
                    static_cast<const char*>(entity) + entity_id_offset_,
                    sizeof(id));
        return read_string(id);
    };
    std::lock_guard lock(mutex_);
    if (!output_) {
        return;
    }
    if (records_.load(std::memory_order_relaxed) >= kRecordLimit) {
        limit_reached_.store(true, std::memory_order_release);
        return;
    }
    std::ostringstream line;
    line << std::setprecision(17)
         << "{\"kind\":\"penetration\",\"phase\":\"" << phase
         << "\",\"frame\":" << frame << ",\"source\":\"" << entity_id(source)
         << "\",\"target\":\"" << entity_id(target)
         << "\",\"damage_type\":" << damage_type
         << ",\"result\":" << result / kFixedPointOne << "}\n";
    output_ << line.str();
    records_.fetch_add(1, std::memory_order_release);
}

void DamageFormulaCapture::on_buff_calculate(void* self, void* attack_info,
                                             void* method) {
    if (!g_buff_calculate) {
        return;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
    }
    AttackInfoValues before;
    std::string key;
    std::uint32_t uid = 0;
    std::uint32_t frame = 0;
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        frame = capture->frame_.load(std::memory_order_acquire);
        if (readable(self, capture->buff_key_offset_ + sizeof(void*))) {
            void* key_object = nullptr;
            std::memcpy(&key_object,
                        static_cast<const char*>(self) +
                            capture->buff_key_offset_,
                        sizeof(key_object));
            key = read_string(key_object);
        }
        if (readable(self, capture->buff_uid_offset_ + sizeof(uid))) {
            std::memcpy(&uid,
                        static_cast<const char*>(self) +
                            capture->buff_uid_offset_,
                        sizeof(uid));
        }
        before = read_attack_info(attack_info, capture->offsets_);
    }
    g_buff_calculate(self, attack_info, method);
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        const auto after = read_attack_info(attack_info, capture->offsets_);
        capture->record_buff("source", frame, key, uid, before, after);
    }
}

void DamageFormulaCapture::on_buff_being_calculate(void* self,
                                                   void* attack_info,
                                                   void* method) {
    if (!g_buff_being_calculate) {
        return;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
    }
    AttackInfoValues before;
    std::string key;
    std::uint32_t uid = 0;
    std::uint32_t frame = 0;
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        frame = capture->frame_.load(std::memory_order_acquire);
        if (readable(self, capture->buff_key_offset_ + sizeof(void*))) {
            void* key_object = nullptr;
            std::memcpy(&key_object,
                        static_cast<const char*>(self) +
                            capture->buff_key_offset_,
                        sizeof(key_object));
            key = read_string(key_object);
        }
        if (readable(self, capture->buff_uid_offset_ + sizeof(uid))) {
            std::memcpy(&uid,
                        static_cast<const char*>(self) +
                            capture->buff_uid_offset_,
                        sizeof(uid));
        }
        before = read_attack_info(attack_info, capture->offsets_);
    }
    g_buff_being_calculate(self, attack_info, method);
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        const auto after = read_attack_info(attack_info, capture->offsets_);
        capture->record_buff("target", frame, key, uid, before, after);
    }
}

void DamageFormulaCapture::on_entity_calculate(void* self, void* source,
                                               void* attack_info,
                                               void* method) {
    if (!g_entity_calculate) {
        return;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
    }
    AttackInfoValues before;
    EntityAttributes source_attributes;
    EntityAttributes target_attributes;
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        before = read_attack_info(attack_info, capture->offsets_);
        source_attributes = capture->read_entity_attributes(self);
        target_attributes = capture->read_entity_attributes(source);
    }
    {
        std::lock_guard lock(active_attack_mutex);
        active_attacks[attack_info] = ActiveAttack{attack_info, self, source};
    }
    active_attack_stack.push_back(ActiveAttack{attack_info, self, source});
    g_entity_calculate(self, source, attack_info, method);
    active_attack_stack.pop_back();
    {
        std::lock_guard lock(active_attack_mutex);
        active_attacks.erase(attack_info);
    }
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        const auto after = read_attack_info(attack_info, capture->offsets_);
        capture->record_attack_info(
            "entity-source", capture->frame_.load(std::memory_order_acquire),
            self, source, before, after, source_attributes,
            target_attributes);
    }
}

void DamageFormulaCapture::observe_buff_modifier(
    const char* phase, void* self, void* modifier, void* method,
    void (*original)(void*, void*, void*)) {
    if (!original) {
        return;
    }
    auto* capture = active_capture;
    if (!capture || !capture->active_.load(std::memory_order_acquire) ||
        !modifier) {
        original(self, modifier, method);
        return;
    }
    capture->calls_.fetch_add(1, std::memory_order_relaxed);
    std::string key;
    std::uint32_t uid = 0;
    if (readable(self, capture->buff_key_offset_ + sizeof(void*))) {
        void* key_object = nullptr;
        std::memcpy(&key_object,
                    static_cast<const char*>(self) +
                        capture->buff_key_offset_,
                    sizeof(key_object));
        key = read_string(key_object);
    }
    if (readable(self, capture->buff_uid_offset_ + sizeof(uid))) {
        std::memcpy(&uid,
                    static_cast<const char*>(self) +
                        capture->buff_uid_offset_,
                    sizeof(uid));
    }
    const ModifierValues before = capture->read_modifier_values(modifier);
    original(self, modifier, method);
    const ModifierValues after = capture->read_modifier_values(modifier);

    using RefGetter = void* (*)(void*, void*);
    void* source = capture->modifier_source_entry_
                       ? reinterpret_cast<RefGetter>(
                             capture->modifier_source_entry_)(
                             modifier, capture->modifier_source_method_)
                       : nullptr;
    void* target = capture->modifier_target_entry_
                       ? reinterpret_cast<RefGetter>(
                             capture->modifier_target_entry_)(
                             modifier, capture->modifier_target_method_)
                       : nullptr;
    const auto identity = [&](void* entity, std::uint32_t& entity_uid) {
        if (!readable(entity, capture->entity_id_offset_ + sizeof(void*))) {
            return std::string();
        }
        void* id = nullptr;
        std::memcpy(&id,
                    static_cast<const char*>(entity) +
                        capture->entity_id_offset_,
                    sizeof(id));
        if (readable(entity,
                     capture->entity_uid_offset_ + sizeof(entity_uid))) {
            std::memcpy(&entity_uid,
                        static_cast<const char*>(entity) +
                            capture->entity_uid_offset_,
                        sizeof(entity_uid));
        }
        return read_string(id);
    };
    std::uint32_t source_uid = 0;
    std::uint32_t target_uid = 0;
    const std::string source_id = identity(source, source_uid);
    const std::string target_id = identity(target, target_uid);
    capture->record_modifier_change(
        phase, capture->frame_.load(std::memory_order_acquire), key, uid,
        before, after, source_id, source_uid, target_id, target_uid);
}

void DamageFormulaCapture::on_buff_take_damage(void* self, void* modifier,
                                               void* method) {
    if (active_capture) {
        active_capture->observe_buff_modifier("take", self, modifier, method,
                                              g_buff_take_damage);
    } else if (g_buff_take_damage) {
        g_buff_take_damage(self, modifier, method);
    }
}

void DamageFormulaCapture::on_buff_output_damage(void* self, void* modifier,
                                                 void* method) {
    if (active_capture) {
        active_capture->observe_buff_modifier(
            "output", self, modifier, method, g_buff_output_damage);
    } else if (g_buff_output_damage) {
        g_buff_output_damage(self, modifier, method);
    }
}

std::int64_t DamageFormulaCapture::on_penetrate_ratio(
    void* source, void* target, std::int32_t damage_type, void* method) {
    if (!g_penetrate_ratio) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
    }
    const std::int64_t result =
        g_penetrate_ratio(source, target, damage_type, method);
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        capture->record_penetration(
            "ratio", capture->frame_.load(std::memory_order_acquire), source,
            target, damage_type, result);
    }
    return result;
}

std::int64_t DamageFormulaCapture::on_penetrate_fixed(
    void* source, void* target, std::int32_t damage_type, void* method) {
    if (!g_penetrate_fixed) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
    }
    const std::int64_t result =
        g_penetrate_fixed(source, target, damage_type, method);
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        capture->record_penetration(
            "fixed", capture->frame_.load(std::memory_order_acquire), source,
            target, damage_type, result);
    }
    return result;
}

std::int64_t DamageFormulaCapture::on_calculate_damage_no_event(
    void* target, void* source, void* attack_info) {
    if (!g_calculate_damage_no_event) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->calculate_damage_no_event_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    // CalculateDamageNoEvent(target, source, atkInfo): the first native
    // argument is the target, the second is the attacker. Some wrappers pass a
    // null source and rely on the surrounding Heal/Element scope.
    const bool pushed = push_attack_context(attack_info, source, target);
    const std::int64_t result = g_calculate_damage_no_event(
        target, source, attack_info);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_calculate_damage(
    std::uintptr_t arg0, std::uintptr_t arg1, std::uintptr_t arg2,
    std::uintptr_t arg3, std::uintptr_t arg4) {
    if (!g_calculate_damage) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->calculate_damage_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    // The managed signature is CalculateDamage(target, source,
    // triggerTargetEvent, triggerSourceEvent, atkInfo), confirmed from the
    // runtime parameter names. Preview calls have no target and must not seed a
    // pending hit; CalculateDamageInternal only attributes inside this scope.
    const bool pushed = push_attack_context(
        reinterpret_cast<void*>(arg4),
        /*source=*/reinterpret_cast<void*>(arg1),
        /*target=*/reinterpret_cast<void*>(arg0));
    const std::uint64_t result =
        g_calculate_damage(arg0, arg1, arg2, arg3, arg4);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::int64_t DamageFormulaCapture::on_calculate_internal(
    void* attack_info, std::int64_t calc_arg0, std::int64_t calc_arg1,
    void* target) {
    if (!g_calculate_internal) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->calculate_damage_internal_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    ActiveAttack context;
    if (!active_attack_stack.empty()) {
        context = active_attack_stack.back();
    } else {
        std::lock_guard lock(active_attack_mutex);
        const auto found = active_attacks.find(attack_info);
        if (found != active_attacks.end()) {
            context = found->second;
        }
    }
    // The target is enough to bind a hit to the observed target event; the
    // source may be genuinely absent for shared-fate/environment true damage.
    const bool matched = context.target != nullptr;
    void* source = matched ? context.source : nullptr;
    void* actual_target = matched ? context.target : target;
    AttackInfoValues values;
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        values = read_attack_info(attack_info, capture->offsets_);
    }
    const std::int64_t result =
        g_calculate_internal(attack_info, calc_arg0, calc_arg1, target);
    if (capture && capture->active_.load(std::memory_order_acquire)) {
        capture->record_formula_internal(
            capture->frame_.load(std::memory_order_acquire), source,
            actual_target, calc_arg0, calc_arg1, values, result, matched);
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_calculate_damage_value(
    std::uint64_t damage, void* target, std::int32_t damage_type,
    std::uint64_t arg3, std::uint64_t arg4) {
    if (!g_calculate_damage_value) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->calculate_damage_value_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, nullptr, target);
    const std::uint64_t result = g_calculate_damage_value(
        damage, target, damage_type, arg3, arg4);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_element_source_target(
    void* source, void* target, std::uint64_t value) {
    if (!g_element_source_target) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->element_source_target_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, source, target);
    const std::uint64_t result =
        g_element_source_target(source, target, value);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_element_target(
    std::uint64_t value, void* target, std::uint64_t value2) {
    if (!g_element_target) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->element_target_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, nullptr, target);
    const std::uint64_t result = g_element_target(value, target, value2);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_element_value_only(
    std::uint64_t value, void* target) {
    if (!g_element_value_only) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->element_value_only_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, nullptr, target);
    const std::uint64_t result = g_element_value_only(value, target);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_heal_source_target(
    void* source, void* target, std::uint64_t value) {
    if (!g_heal_source_target) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->heal_source_target_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, source, target);
    const std::uint64_t result = g_heal_source_target(source, target, value);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

std::uint64_t DamageFormulaCapture::on_heal_target(
    std::uint64_t value, void* target, std::uint64_t value2) {
    if (!g_heal_target) {
        return 0;
    }
    auto* capture = active_capture;
    if (capture) {
        capture->calls_.fetch_add(1, std::memory_order_relaxed);
        capture->heal_target_calls_.fetch_add(
            1, std::memory_order_relaxed);
    }
    const bool pushed = push_attack_context(nullptr, nullptr, target);
    const std::uint64_t result = g_heal_target(value, target, value2);
    if (pushed) {
        active_attack_stack.pop_back();
    }
    return result;
}

} // namespace ark::better_battle
