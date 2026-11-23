#pragma once

#include "damage_calculator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ark::better_battle {

struct DamageFactor {
    std::string key;
    std::string label;
    std::string source;
    std::uint32_t uid = 0;
    // Exact post-defense delta in Q32.32.
    FixedPoint delta = 0;
};

struct DamageAttributionSource {
    std::string kind;
    std::string key;
    std::string label;
    std::string source;
    std::uint32_t buff_uid = 0;
};

struct DamageAttributionAllocation {
    std::string zone;
    DamageAttributionSource source;
    double shapley = 0;
    double counterfactual = 0;
};

struct DamageAttributionInput {
    DamageKind kind = DamageKind::None;
    AttributeStage source_attack;
    AttributeStage target_mitigation;
    double attack_scale = 1;
    double attack_addition = 0;
    double physical_penetration_ratio = 0;
    double physical_penetration_fixed = 0;
    double magical_penetration_ratio = 0;
    double magical_penetration_fixed = 0;
    std::vector<DamageFactor> post_damage_factors;
    double expected_formula_damage = 0;
    bool has_expected_final_damage = false;
    double expected_final_damage = 0;
};

struct DamageAttributionResult {
    // Damage from the raw attribute baseline, before any attributed source.
    double base_damage = 0;
    // Damage reproduced by the local calculator using every modeled zone.
    double modeled_damage = 0;
    // Final client damage (or the formula value when the final settlement is
    // unavailable) minus modeled_damage. Positive means the local model
    // under-reproduced the client result.
    double unallocated = 0;
    bool exact_zones = true;
    std::vector<DamageAttributionAllocation> allocations;
};

DamageAttributionResult attribute_damage_formula(
    const DamageAttributionInput& input);

} // namespace ark::better_battle
