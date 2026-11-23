#pragma once

#include <ark/plugin.h>

#include "damage_attribution.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ark::il2cpp {
class Runtime;
}

namespace ark::better_battle {

struct DamageAttributionTotal {
    std::string actor;
    std::string target;
    std::uint32_t target_uid = 0;
    std::string metric;
    std::string zone;
    std::string source_kind;
    std::string source_key;
    std::string source_label;
    std::string source_id;
    std::uint32_t buff_uid = 0;
    double shapley = 0;
    double counterfactual = 0;
    std::uint64_t hits = 0;
};

struct DamageFormulaCallCounts {
    std::uint64_t calculate_damage = 0;
    std::uint64_t calculate_damage_no_event = 0;
    std::uint64_t calculate_damage_internal = 0;
    std::uint64_t calculate_damage_value = 0;
    std::uint64_t element_source_target = 0;
    std::uint64_t element_target = 0;
    std::uint64_t element_value_only = 0;
    std::uint64_t heal_source_target = 0;
    std::uint64_t heal_target = 0;
};

struct DamageAttributionSnapshot {
    bool available = false;
    bool exact_zones = true;
    std::uint64_t hits = 0;
    std::uint64_t observed_events = 0;
    std::uint64_t unmatched_events = 0;
    std::uint64_t physical_events = 0;
    std::uint64_t magical_events = 0;
    std::uint64_t true_events = 0;
    std::uint64_t healing_events = 0;
    std::uint64_t elemental_events = 0;
    // True/environment damage can reach the formula without any source entity.
    // It is still matched by target/type/value and reported with the event's
    // __no_source__ actor.
    std::uint64_t no_source_events = 0;
    double base_damage = 0;
    double modeled_damage = 0;
    double unallocated = 0;
    std::vector<DamageAttributionTotal> totals;
    // Base damage for the legitimate __no_source__ category. Kept separate so
    // the Buff totals still satisfy base_damage + sum(totals.shapley) ==
    // modeled_damage.
    std::vector<DamageAttributionTotal> no_source_totals;
    DamageFormulaCallCounts entries;
};

// Observation-only hooks on the native damage formula. Attribution never
// changes the returned formula value or any live battle state.
class DamageFormulaCapture {
public:
    DamageFormulaCapture(il2cpp::Runtime& runtime, const ArkPluginApiV1& host);
    ~DamageFormulaCapture();

    DamageFormulaCapture(const DamageFormulaCapture&) = delete;
    DamageFormulaCapture& operator=(const DamageFormulaCapture&) = delete;

    bool initialize();
    bool shutdown();
    bool start(const std::string& path);
    void stop();
    void set_frame(std::uint32_t frame);
    bool active() const;
    std::uint64_t records() const;
    std::uint64_t calls() const;
    DamageFormulaCallCounts entry_counts() const;
    void update_entity(
        void* entity, const std::string& id, std::uint32_t uid,
        const std::vector<AttributeContribution>& contributions);
    void observe_damage_event(std::uint32_t frame, const std::string& actor,
                              const std::string& target,
                              std::uint32_t source_uid,
                              std::uint32_t target_uid,
                              std::int32_t damage_type,
                              double calculated);
    DamageAttributionSnapshot snapshot() const;

private:
    struct AttackInfoOffsets {
        std::ptrdiff_t damage_type = 0;
        std::ptrdiff_t origin_atk = 0;
        std::ptrdiff_t atk = 0;
        std::ptrdiff_t atk_scale = 0;
        std::ptrdiff_t atk_addition = 0;
    };

    struct AttackInfoValues {
        std::int32_t damage_type = 0;
        std::int64_t origin_atk = 0;
        std::int64_t atk = 0;
        std::int64_t atk_scale = 0;
        std::int64_t atk_addition = 0;
    };

    struct EntityAttributes {
        std::int64_t atk_raw = 0;
        std::int64_t atk = 0;
        std::int64_t defense_raw = 0;
        std::int64_t defense = 0;
        std::int64_t resistance_raw = 0;
        std::int64_t resistance = 0;
        std::int64_t elemental_resistance = 0;
    };

    struct ModifierValues {
        std::int32_t damage_type = 0;
        std::int64_t final_delta = 0;
        std::int64_t value = 0;
        std::int64_t real_delta = 0;
        std::int64_t extra_data = 0;
        std::int64_t origin_value = 0;
    };

    struct EntitySnapshot {
        bool valid = false;
        std::string id;
        EntityAttributes attributes;
        AttributeStage attack;
        AttributeStage defense;
        AttributeStage resistance;
        AttributeStage elemental_resistance;
    };

    static void on_buff_calculate(void* self, void* attack_info, void* method);
    static void on_buff_being_calculate(void* self, void* attack_info,
                                        void* method);
    static void on_entity_calculate(void* self, void* source,
                                    void* attack_info, void* method);
    static void on_buff_take_damage(void* self, void* modifier, void* method);
    static void on_buff_output_damage(void* self, void* modifier, void* method);
    static std::int64_t on_penetrate_ratio(void* source, void* target,
                                           std::int32_t damage_type,
                                           void* method);
    static std::int64_t on_penetrate_fixed(void* source, void* target,
                                           std::int32_t damage_type,
                                           void* method);
    static std::int64_t on_calculate_damage_no_event(
        void* target, void* source, void* attack_info);
    static std::uint64_t on_calculate_damage(
        std::uintptr_t arg0, std::uintptr_t arg1, std::uintptr_t arg2,
        std::uintptr_t arg3, std::uintptr_t arg4);
    static std::int64_t on_calculate_internal(
        void* attack_info, std::int64_t calc_arg0, std::int64_t calc_arg1,
        void* target);
    static std::uint64_t on_calculate_damage_value(
        std::uint64_t damage, void* target, std::int32_t damage_type,
        std::uint64_t arg3, std::uint64_t arg4);
    static std::uint64_t on_element_source_target(void* source, void* target,
                                                  std::uint64_t value);
    static std::uint64_t on_element_target(std::uint64_t value, void* target,
                                           std::uint64_t value2);
    static std::uint64_t on_element_value_only(std::uint64_t value,
                                               void* target);
    static std::uint64_t on_heal_source_target(void* source, void* target,
                                               std::uint64_t value);
    static std::uint64_t on_heal_target(std::uint64_t value, void* target,
                                        std::uint64_t value2);
    static bool readable(const void* address, std::size_t size);
    static std::string read_string(void* value);
    static AttackInfoValues read_attack_info(void* attack_info,
                                             const AttackInfoOffsets& offsets);
    EntityAttributes read_entity_attributes(void* entity) const;
    ModifierValues read_modifier_values(void* modifier) const;

    void record_buff(const char* phase, std::uint32_t frame,
                     const std::string& key, std::uint32_t uid,
                     const AttackInfoValues& before,
                     const AttackInfoValues& after);
    void record_formula_internal(std::uint32_t frame, void* source,
                                 void* target, std::int64_t calc_arg0,
                                 std::int64_t calc_arg1,
                                 const AttackInfoValues& attack_info,
                                 std::int64_t result, bool matched);
    void record_attack_info(const char* phase, std::uint32_t frame,
                            void* self, void* source,
                            const AttackInfoValues& before,
                            const AttackInfoValues& after,
                            const EntityAttributes& source_attributes,
                            const EntityAttributes& target_attributes);
    void record_modifier_change(
        const char* phase, std::uint32_t frame, const std::string& key,
        std::uint32_t uid, const ModifierValues& before,
        const ModifierValues& after, const std::string& source_id,
        std::uint32_t source_uid, const std::string& target_id,
        std::uint32_t target_uid);
    void observe_buff_modifier(const char* phase, void* self, void* modifier,
                               void* method,
                               void (*original)(void*, void*, void*));
    void record_penetration(const char* phase, std::uint32_t frame,
                            void* source, void* target,
                            std::int32_t damage_type, std::int64_t result);
    struct PendingModifier {
        std::uint32_t frame = 0;
        std::string key;
        std::uint32_t uid = 0;
        std::string source_id;
        std::uint32_t source_uid = 0;
        std::string target_id;
        std::uint32_t target_uid = 0;
        std::int32_t damage_type = 0;
        double before_final = 0;
        double after_final = 0;
        std::int64_t before_final_raw = 0;
        std::int64_t after_final_raw = 0;
        double before_value = 0;
        double after_value = 0;
    };
    struct PendingHit {
        std::uint32_t frame = 0;
        std::string target;
        std::uint32_t source_uid = 0;
        std::uint32_t target_uid = 0;
        std::int32_t damage_type = 0;
        EntityAttributes target_attributes;
        bool target_attributes_valid = false;
        AttackInfoValues attack_info;
        double calculated = 0;
    };

    struct PendingEvent {
        std::uint32_t frame = 0;
        std::string actor;
        std::string target;
        std::uint32_t source_uid = 0;
        std::uint32_t target_uid = 0;
        std::int32_t damage_type = 0;
        double calculated = 0;
    };

    void queue_hit(const PendingHit& hit);
    void attribute_hit(const std::string& source_id,
                       std::uint32_t source_uid,
                       const std::string& target_id,
                       std::uint32_t target_uid, std::uint32_t frame,
                       const EntityAttributes& target_attributes,
                       bool target_attributes_valid,
                       const AttackInfoValues& attack_info,
                       double formula_calculated, double final_calculated,
                       const std::vector<PendingModifier>& modifier_chain);
    static bool same_fixed_value(double left, double right);
    std::vector<std::size_t> find_modifier_chain(
        const PendingEvent& event, bool* ambiguous) const;
    bool hit_matches_value(const PendingHit& hit, const PendingEvent& event,
                           double value) const;

    il2cpp::Runtime& runtime_;
    const ArkPluginApiV1& host_;
    AttackInfoOffsets offsets_;
    std::size_t buff_key_offset_ = 0;
    std::size_t buff_uid_offset_ = 0;
    std::size_t modifier_value_offset_ = 0;
    std::size_t modifier_real_delta_offset_ = 0;
    std::size_t modifier_extra_data_offset_ = 0;
    std::size_t modifier_origin_value_offset_ = 0;
    void* modifier_final_delta_entry_ = nullptr;
    void* modifier_final_delta_method_ = nullptr;
    void* modifier_damage_type_entry_ = nullptr;
    void* modifier_damage_type_method_ = nullptr;
    void* modifier_source_entry_ = nullptr;
    void* modifier_source_method_ = nullptr;
    void* modifier_target_entry_ = nullptr;
    void* modifier_target_method_ = nullptr;
    std::size_t entity_id_offset_ = 0;
    std::size_t entity_uid_offset_ = 0;
    std::size_t entity_attributes_offset_ = 0;
    void* attribute_raw_method_ = nullptr;
    void* attribute_value_method_ = nullptr;
    void* attribute_raw_entry_ = nullptr;
    void* attribute_value_entry_ = nullptr;
    int attribute_atk_ = -1;
    int attribute_defense_ = -1;
    int attribute_resistance_ = -1;
    int attribute_elemental_resistance_ = -1;
    void* calculate_original_ = nullptr;
    void* being_original_ = nullptr;
    void* entity_calculate_original_ = nullptr;
    void* buff_take_damage_original_ = nullptr;
    void* buff_output_damage_original_ = nullptr;
    void* penetrate_ratio_original_ = nullptr;
    void* penetrate_fixed_original_ = nullptr;
    void* calculate_damage_no_event_original_ = nullptr;
    void* calculate_damage_original_ = nullptr;
    void* calculate_internal_original_ = nullptr;
    void* calculate_damage_value_original_ = nullptr;
    void* element_source_target_original_ = nullptr;
    void* element_target_original_ = nullptr;
    void* element_value_only_original_ = nullptr;
    void* heal_source_target_original_ = nullptr;
    void* heal_target_original_ = nullptr;
    std::uint64_t calculate_hook_ = 0;
    std::uint64_t being_hook_ = 0;
    std::uint64_t entity_calculate_hook_ = 0;
    std::uint64_t buff_take_damage_hook_ = 0;
    std::uint64_t buff_output_damage_hook_ = 0;
    std::uint64_t penetrate_ratio_hook_ = 0;
    std::uint64_t penetrate_fixed_hook_ = 0;
    std::uint64_t calculate_damage_no_event_hook_ = 0;
    std::uint64_t calculate_damage_hook_ = 0;
    std::uint64_t calculate_internal_hook_ = 0;
    std::uint64_t calculate_damage_value_hook_ = 0;
    std::uint64_t element_source_target_hook_ = 0;
    std::uint64_t element_target_hook_ = 0;
    std::uint64_t element_value_only_hook_ = 0;
    std::uint64_t heal_source_target_hook_ = 0;
    std::uint64_t heal_target_hook_ = 0;

    mutable std::mutex mutex_;
    std::ofstream output_;
    std::string path_;
    std::atomic<bool> active_{false};
    std::atomic<std::uint32_t> frame_{0};
    std::atomic<std::uint64_t> records_{0};
    std::atomic<std::uint64_t> calls_{0};
    std::atomic<std::uint64_t> calculate_damage_calls_{0};
    std::atomic<std::uint64_t> calculate_damage_no_event_calls_{0};
    std::atomic<std::uint64_t> calculate_damage_internal_calls_{0};
    std::atomic<std::uint64_t> calculate_damage_value_calls_{0};
    std::atomic<std::uint64_t> element_source_target_calls_{0};
    std::atomic<std::uint64_t> element_target_calls_{0};
    std::atomic<std::uint64_t> element_value_only_calls_{0};
    std::atomic<std::uint64_t> heal_source_target_calls_{0};
    std::atomic<std::uint64_t> heal_target_calls_{0};
    std::atomic<bool> limit_reached_{false};
    std::unordered_map<std::string, EntitySnapshot> entities_;
    std::unordered_map<std::uint32_t, EntitySnapshot> entities_by_uid_;
    std::unordered_map<void*, std::string> entity_ids_;
    std::vector<PendingHit> pending_hits_;
    // Hits from a target-only context (value overloads, previews). They are
    // consumed only when no sourced hit matches the same event.
    std::vector<PendingHit> pending_fallback_hits_;
    std::vector<PendingEvent> pending_events_;
    std::vector<PendingModifier> pending_modifiers_;
    DamageAttributionSnapshot attribution_;
    std::unordered_map<std::string, std::size_t> attribution_index_;
    std::unordered_map<std::string, std::size_t> no_source_index_;
};

} // namespace ark::better_battle
