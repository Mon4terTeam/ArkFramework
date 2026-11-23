#include "damage_attribution.h"
#include "damage_calculator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using ark::better_battle::DamageAttributionInput;
using ark::better_battle::DamageKind;
using ark::better_battle::AttributeContribution;
using ark::better_battle::AttributeStage;
using ark::better_battle::DamageFormulaInput;
using ark::better_battle::attribute_damage_formula;
using ark::better_battle::calculate_damage_formula;
using ark::better_battle::evaluate_attribute_stage;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void require_near(double actual, double expected, double tolerance,
                  const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        std::fprintf(stderr, "FAIL: %s (actual=%.12f expected=%.12f)\n",
                     message, actual, expected);
        std::exit(1);
    }
}

AttributeContribution atk(double multiplier) {
    AttributeContribution contribution;
    contribution.attribute = "ATK";
    contribution.key = "test_atk";
    contribution.label = "test_atk";
    contribution.multiplier = multiplier;
    return contribution;
}

} // namespace

int main() {
    {
        using ark::better_battle::fixed_from_float32;
        using ark::better_battle::fixed_mul;
        require(fixed_from_float32(0.05f) == 214748368,
                "float32 0.05 fixed conversion");
        require(fixed_from_float32(0.01f) == 42949672,
                "float32 0.01 fixed conversion");
#if defined(__SIZEOF_INT128__)
        const std::int64_t values[] = {
            ark::better_battle::kFpOne,
            ark::better_battle::kFpOne / 3,
            -(ark::better_battle::kFpOne / 3),
            42949672,
            -98765,
            1,
            -1,
            0,
        };
        for (std::int64_t left : values) {
            for (std::int64_t right : values) {
                const std::int64_t expected =
                    static_cast<std::int64_t>(
                        (static_cast<__int128>(left) * right) >> 32);
                require(fixed_mul(left, right) == expected,
                        "fixed_mul matches signed arithmetic shift");
            }
        }
#endif
    }
    {
        DamageFormulaInput input;
        input.kind = DamageKind::Magical;
        input.attack = 550;
        input.magic_resistance = 15;
        const auto output = calculate_damage_formula(input);
        require_near(output.formula_damage, 467.4999895505607, 1e-6,
                     "magic client percent");
        require(output.formula_damage_raw == 2007897166000,
                "magic client percent is exact Q32.32");
    }
    {
        DamageFormulaInput input;
        input.kind = DamageKind::Physical;
        input.attack = 1499.8799889534712;
        input.attack_scale = 3.799999952316284;
        input.defense = 220;
        const auto output = calculate_damage_formula(input);
        require_near(output.basic_damage, 5699.54395802319, 1e-3,
                     "physical basic");
        require_near(output.formula_damage, 5479.543886503205, 1e-2,
                     "physical result");
        require(output.formula_damage_raw == 23534461789528,
                "physical result is exact Q32.32");
    }
    {
        DamageFormulaInput input;
        input.kind = DamageKind::Healing;
        input.attack = 1010.239990234375;
        input.attack_scale = 0.1;
        const auto output = calculate_damage_formula(input);
        require_near(output.formula_damage, 101.02400052873418, 1e-5,
                     "healing result");
    }
    {
        AttributeStage stage;
        stage.raw = 100;
        stage.contributions.push_back(atk(0.30));
        stage.contributions.push_back(atk(0.20));
        auto final_scaler = atk(0);
        final_scaler.final_scaler = 2.0;
        stage.contributions.push_back(final_scaler);
        require_near(evaluate_attribute_stage(stage, {0, 1, 2}), 300.0, 1e-9,
                     "attribute stage");
    }
    {
        DamageAttributionInput input;
        input.kind = DamageKind::Physical;
        input.source_attack.raw = 100;
        input.source_attack.contributions.push_back(atk(1.0));
        input.attack_scale = 2.0;
        input.target_mitigation.raw = 0;
        input.expected_formula_damage = 400;
        const auto result = attribute_damage_formula(input);
        double allocated = result.base_damage;
        for (const auto& entry : result.allocations) {
            allocated += entry.shapley;
        }
        require_near(allocated, 400.0, 1e-6, "shapley conservation");
        require_near(result.unallocated, 0.0, 1e-6,
                     "attribution residual");
    }
    {
        DamageAttributionInput input;
        input.kind = DamageKind::True;
        input.source_attack.raw = 100;
        input.target_mitigation.raw = 0;
        input.expected_formula_damage = 100;
        input.has_expected_final_damage = true;
        input.expected_final_damage = 115;
        input.post_damage_factors.push_back(
            {"test.post", "test post delta", "", 1,
             ark::better_battle::fixed_from_double(15)});
        const auto result = attribute_damage_formula(input);
        double allocated = result.base_damage;
        for (const auto& entry : result.allocations) {
            allocated += entry.shapley;
        }
        require_near(result.modeled_damage, 115.0, 1e-5,
                     "post multiplier model");
        require_near(allocated, result.modeled_damage, 1e-9,
                     "post multiplier shapley conservation");
        require(result.unallocated == 0.0,
                "post multiplier residual is exactly zero");
    }
    std::puts("damage calculator tests passed");
    return 0;
}
