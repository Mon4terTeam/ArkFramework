#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ark::better_battle {

// Torappu.FP is a signed Q32.32 fixed-point value.
using FixedPoint = std::int64_t;
inline constexpr FixedPoint kFpOne = FixedPoint{1} << 32;

FixedPoint fixed_from_double(double value);
FixedPoint fixed_from_float32(float value);
double fixed_to_double(FixedPoint value);
FixedPoint fixed_add(FixedPoint left, FixedPoint right);
FixedPoint fixed_sub(FixedPoint left, FixedPoint right);
// Arithmetic-shift product, matching the client's Q32.32 multiply.
FixedPoint fixed_mul(FixedPoint left, FixedPoint right);
FixedPoint fixed_max(FixedPoint left, FixedPoint right);

// Native Modifier.damageType numbering. This is intentionally not the
// DamageTypeMask numbering used by Buff filters.
enum class DamageKind : std::int32_t {
    None = 0,
    Physical = 1,
    Magical = 2,
    True = 3,
    Healing = 4,
    Elemental = 5,
};

struct AttributeContribution {
    std::string attribute;
    std::string key;
    std::string label;
    std::string source;
    std::string class_name;
    std::uint32_t uid = 0;
    double addition = 0;
    double multiplier = 0;
    double final_addition = 0;
    double final_scaler = 1;
};

struct AttributeStage {
    double raw = 0;
    std::vector<AttributeContribution> contributions;
};

// Mirrors Attributes._CalculateAttributeValue. Contributions are applied in
// the supplied order; sums and the final-scaler product are order-independent
// apart from floating-point rounding.
double evaluate_attribute_stage(
    const AttributeStage& stage,
    const std::vector<std::size_t>& selected);
FixedPoint evaluate_attribute_stage_raw(
    const AttributeStage& stage,
    const std::vector<std::size_t>& selected);

struct DamageFormulaInput {
    DamageKind kind = DamageKind::None;
    double attack = 0;
    double attack_scale = 1;
    double attack_addition = 0;
    double defense = 0;
    double magic_resistance = 0;
    double elemental_resistance = 0;
    double physical_penetration_ratio = 0;
    double physical_penetration_fixed = 0;
    double magical_penetration_ratio = 0;
    double magical_penetration_fixed = 0;
    // Additive post-defense deltas in Q32.32. The client's Modifier chain is
    // observed as exact before/after values, so summing its deltas closes the
    // final value without deriving a lossy ratio.
    std::vector<FixedPoint> post_damage_deltas;
};

struct DamageFormulaOutput {
    FixedPoint basic_damage_raw = 0;
    FixedPoint effective_mitigation_raw = 0;
    FixedPoint minimum_damage_raw = 0;
    FixedPoint before_post_multipliers_raw = 0;
    FixedPoint formula_damage_raw = 0;
    double basic_damage = 0;
    double effective_mitigation = 0;
    double minimum_damage = 0;
    double before_post_multipliers = 0;
    double formula_damage = 0;
    double attack_power_multiplier = 1;
};

DamageFormulaOutput calculate_damage_formula(const DamageFormulaInput& input);

const char* damage_kind_name(DamageKind kind);

} // namespace ark::better_battle
