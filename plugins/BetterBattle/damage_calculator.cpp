#include "damage_calculator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ark::better_battle {
namespace {

const FixedPoint kFivePercent = fixed_from_float32(0.05f);
const FixedPoint kClientPercent = fixed_from_float32(0.01f);

} // namespace

FixedPoint fixed_from_double(double value) {
    if (!std::isfinite(value)) {
        return value < 0 ? std::numeric_limits<FixedPoint>::min()
                         : std::numeric_limits<FixedPoint>::max();
    }
    const long double scaled =
        static_cast<long double>(value) * 4294967296.0L;
    if (scaled >=
        static_cast<long double>(std::numeric_limits<FixedPoint>::max())) {
        return std::numeric_limits<FixedPoint>::max();
    }
    if (scaled <=
        static_cast<long double>(std::numeric_limits<FixedPoint>::min())) {
        return std::numeric_limits<FixedPoint>::min();
    }
    return static_cast<FixedPoint>(std::llround(scaled));
}

FixedPoint fixed_from_float32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    const bool negative = (bits >> 31) != 0;
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    const std::uint32_t fraction = bits & 0x7fffffu;
    if (exponent == 0xffu) {
        return std::numeric_limits<FixedPoint>::min();
    }
    if (exponent == 0) {
        return 0;
    }
    const std::uint64_t mantissa = 0x800000u | fraction;
    const int shift = static_cast<int>(exponent) - 118;
    const std::uint64_t magnitude =
        shift >= 0 ? mantissa << shift : mantissa >> -shift;
    if (negative) {
        return -static_cast<FixedPoint>(magnitude);
    }
    return static_cast<FixedPoint>(magnitude);
}

double fixed_to_double(FixedPoint value) {
    return static_cast<double>(value) / 4294967296.0;
}

FixedPoint fixed_add(FixedPoint left, FixedPoint right) {
    return left + right;
}

FixedPoint fixed_sub(FixedPoint left, FixedPoint right) {
    return left - right;
}

FixedPoint fixed_max(FixedPoint left, FixedPoint right) {
    return std::max(left, right);
}

namespace {

struct WideProduct {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

WideProduct multiply_u64(std::uint64_t left, std::uint64_t right) {
    const std::uint64_t left_low = left & 0xffffffffu;
    const std::uint64_t left_high = left >> 32;
    const std::uint64_t right_low = right & 0xffffffffu;
    const std::uint64_t right_high = right >> 32;
    const std::uint64_t low_low = left_low * right_low;
    const std::uint64_t low_high = left_low * right_high;
    const std::uint64_t high_low = left_high * right_low;
    const std::uint64_t high_high = left_high * right_high;
    const std::uint64_t middle =
        (low_low >> 32) + (low_high & 0xffffffffu) +
        (high_low & 0xffffffffu);
    WideProduct product;
    product.low = (low_low & 0xffffffffu) | (middle << 32);
    product.high = high_high + (low_high >> 32) + (high_low >> 32) +
                   (middle >> 32);
    return product;
}

std::uint64_t unsigned_magnitude(FixedPoint value) {
    return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                     : static_cast<std::uint64_t>(value);
}

} // namespace

FixedPoint fixed_mul(FixedPoint left, FixedPoint right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    const bool negative = (left < 0) != (right < 0);
    const WideProduct product =
        multiply_u64(unsigned_magnitude(left), unsigned_magnitude(right));
    if ((product.high >> 32) != 0) {
        return negative ? std::numeric_limits<FixedPoint>::min()
                        : std::numeric_limits<FixedPoint>::max();
    }
    const std::uint64_t magnitude = (product.high << 32) | (product.low >> 32);
    const std::uint64_t fraction = product.low & 0xffffffffu;
    if (!negative) {
        if (magnitude > static_cast<std::uint64_t>(
                            std::numeric_limits<FixedPoint>::max())) {
            return std::numeric_limits<FixedPoint>::max();
        }
        return static_cast<FixedPoint>(magnitude);
    }
    // BigInt >> 32 is floor, so a negative product rounds away from zero
    // whenever any low fraction bit is set.
    const std::uint64_t rounded = magnitude + (fraction != 0 ? 1 : 0);
    if (rounded >
        static_cast<std::uint64_t>(
            std::numeric_limits<FixedPoint>::max()) +
            1) {
        return std::numeric_limits<FixedPoint>::min();
    }
    if (rounded ==
        static_cast<std::uint64_t>(
            std::numeric_limits<FixedPoint>::max()) +
            1) {
        return std::numeric_limits<FixedPoint>::min();
    }
    return -static_cast<FixedPoint>(rounded);
}

FixedPoint evaluate_attribute_stage_raw(
    const AttributeStage& stage,
    const std::vector<std::size_t>& selected) {
    FixedPoint addition = 0;
    FixedPoint multiplier = 0;
    FixedPoint final_addition = 0;
    FixedPoint final_scaler = kFpOne;
    for (std::size_t index : selected) {
        if (index >= stage.contributions.size()) {
            continue;
        }
        const AttributeContribution& contribution =
            stage.contributions[index];
        addition = fixed_add(addition, fixed_from_double(contribution.addition));
        multiplier =
            fixed_add(multiplier, fixed_from_double(contribution.multiplier));
        final_addition = fixed_add(
            final_addition, fixed_from_double(contribution.final_addition));
        final_scaler =
            fixed_mul(final_scaler, fixed_from_double(contribution.final_scaler));
    }
    const FixedPoint base = fixed_add(fixed_from_double(stage.raw), addition);
    const FixedPoint scaled = fixed_mul(
        base, fixed_max(0, fixed_add(kFpOne, multiplier)));
    return fixed_mul(final_scaler, fixed_add(scaled, final_addition));
}

double evaluate_attribute_stage(
    const AttributeStage& stage,
    const std::vector<std::size_t>& selected) {
    return fixed_to_double(evaluate_attribute_stage_raw(stage, selected));
}

DamageFormulaOutput calculate_damage_formula(
    const DamageFormulaInput& input) {
    DamageFormulaOutput output;
    const FixedPoint attack_scale = fixed_from_double(input.attack_scale);
    const FixedPoint attack_addition = fixed_from_double(input.attack_addition);
    output.attack_power_multiplier = input.attack_scale;
    output.basic_damage_raw = fixed_max(
        0, fixed_add(
               fixed_mul(fixed_from_double(input.attack), attack_scale),
               attack_addition));
    output.basic_damage = fixed_to_double(output.basic_damage_raw);
    const FixedPoint basic = output.basic_damage_raw;
    FixedPoint damage = basic;
    if (basic > 0 && input.kind == DamageKind::Physical) {
        output.effective_mitigation_raw = fixed_mul(
            fixed_sub(kFpOne,
                      fixed_from_double(input.physical_penetration_ratio)),
            fixed_max(0, fixed_sub(fixed_from_double(input.defense),
                                   fixed_from_double(
                                       input.physical_penetration_fixed))));
        output.minimum_damage_raw = fixed_mul(basic, kFivePercent);
        damage = fixed_max(output.minimum_damage_raw,
                           fixed_sub(basic, output.effective_mitigation_raw));
    } else if (basic > 0 && input.kind == DamageKind::Magical) {
        output.effective_mitigation_raw = fixed_mul(
            fixed_sub(kFpOne,
                      fixed_from_double(input.magical_penetration_ratio)),
            fixed_max(0, fixed_sub(fixed_from_double(input.magic_resistance),
                                   fixed_from_double(
                                       input.magical_penetration_fixed))));
        output.minimum_damage_raw = fixed_mul(basic, kFivePercent);
        const FixedPoint resistance_factor = fixed_max(
            0, fixed_sub(FixedPoint{100} << 32,
                         output.effective_mitigation_raw));
        const FixedPoint resisted = fixed_mul(
            fixed_mul(basic, resistance_factor), kClientPercent);
        damage = fixed_max(output.minimum_damage_raw, resisted);
    } else if (basic > 0 && input.kind == DamageKind::Elemental) {
        output.effective_mitigation_raw =
            fixed_max(0, fixed_from_double(input.elemental_resistance));
        output.minimum_damage_raw = fixed_mul(basic, kFivePercent);
        const FixedPoint resistance_factor = fixed_max(
            0, fixed_sub(FixedPoint{100} << 32,
                         output.effective_mitigation_raw));
        const FixedPoint resisted = fixed_mul(
            fixed_mul(basic, resistance_factor), kClientPercent);
        damage = fixed_max(output.minimum_damage_raw, resisted);
    }

    output.before_post_multipliers_raw = damage;
    FixedPoint result = damage;
    for (FixedPoint delta : input.post_damage_deltas) {
        result = fixed_add(result, delta);
    }
    output.formula_damage_raw = fixed_max(0, result);
    output.formula_damage = fixed_to_double(output.formula_damage_raw);
    output.effective_mitigation =
        fixed_to_double(output.effective_mitigation_raw);
    output.minimum_damage = fixed_to_double(output.minimum_damage_raw);
    output.before_post_multipliers =
        fixed_to_double(output.before_post_multipliers_raw);
    return output;
}

const char* damage_kind_name(DamageKind kind) {
    switch (kind) {
    case DamageKind::Physical:
        return "physical";
    case DamageKind::Magical:
        return "magical";
    case DamageKind::True:
        return "true";
    case DamageKind::Healing:
        return "healing";
    case DamageKind::Elemental:
        return "elemental";
    default:
        return "none";
    }
}

} // namespace ark::better_battle
