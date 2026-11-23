#include "damage_attribution.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>

namespace ark::better_battle {
namespace {

constexpr std::size_t kMaxExactZones = 8;
constexpr double kEpsilon = 1e-9;

struct AttributionState {
    FixedPoint source_attack = 0;
    FixedPoint target_mitigation = 0;
    FixedPoint attack_scale = kFpOne;
    FixedPoint attack_addition = 0;
    std::vector<FixedPoint> post_damage_deltas;
};

struct ZoneSource {
    DamageAttributionSource source;
    double weight = 0;
    FixedPoint weight_raw = 0;
};

struct Zone {
    std::string key;
    std::vector<ZoneSource> sources;
    std::function<void(AttributionState&)> apply;
};

FixedPoint evaluate_state(const DamageAttributionInput& input,
                          const AttributionState& state) {
    DamageFormulaInput formula;
    formula.kind = input.kind;
    formula.attack = fixed_to_double(state.source_attack);
    formula.attack_scale = fixed_to_double(state.attack_scale);
    formula.attack_addition = fixed_to_double(state.attack_addition);
    formula.physical_penetration_ratio =
        input.physical_penetration_ratio;
    formula.physical_penetration_fixed =
        input.physical_penetration_fixed;
    formula.magical_penetration_ratio =
        input.magical_penetration_ratio;
    formula.magical_penetration_fixed =
        input.magical_penetration_fixed;
    formula.post_damage_deltas = state.post_damage_deltas;
    if (input.kind == DamageKind::Physical) {
        formula.defense = fixed_to_double(state.target_mitigation);
    } else if (input.kind == DamageKind::Magical) {
        formula.magic_resistance = fixed_to_double(state.target_mitigation);
    } else if (input.kind == DamageKind::Elemental) {
        formula.elemental_resistance =
            fixed_to_double(state.target_mitigation);
    }
    return calculate_damage_formula(formula).formula_damage_raw;
}

std::vector<ZoneSource> attribute_zone_sources(
    const AttributeStage& stage, const char* kind) {
    std::vector<ZoneSource> sources;
    const FixedPoint baseline = evaluate_attribute_stage_raw(stage, {});
    for (std::size_t index = 0; index < stage.contributions.size(); ++index) {
        const AttributeContribution& contribution =
            stage.contributions[index];
        const FixedPoint isolated =
            evaluate_attribute_stage_raw(stage, {index}) - baseline;
        DamageAttributionSource source;
        source.kind = kind;
        source.key = contribution.key;
        source.label = contribution.label.empty() ? contribution.key
                                                   : contribution.label;
        source.source = contribution.source;
        source.buff_uid = contribution.uid;
        sources.push_back(
            {std::move(source), fixed_to_double(isolated), isolated});
    }
    return sources;
}

std::vector<ZoneSource> factor_zone_sources(
    const std::vector<DamageFactor>& factors) {
    std::map<std::string, ZoneSource> grouped;
    for (const DamageFactor& factor : factors) {
        const std::string identity =
            factor.key + "#" + std::to_string(factor.uid);
        auto iterator = grouped.find(identity);
        if (iterator == grouped.end()) {
            DamageAttributionSource source;
            source.kind = "damage-factor";
            source.key = factor.key;
            source.label = factor.label.empty() ? factor.key : factor.label;
            source.source = factor.source;
            source.buff_uid = factor.uid;
            iterator = grouped
                           .emplace(identity,
                                    ZoneSource{std::move(source), 0, 0})
                           .first;
        }
        iterator->second.weight_raw =
            fixed_add(iterator->second.weight_raw, factor.delta);
    }
    std::vector<ZoneSource> sources;
    for (auto& [identity, entry] : grouped) {
        (void)identity;
        entry.weight = fixed_to_double(entry.weight_raw);
        sources.push_back(std::move(entry));
    }
    return sources;
}

std::vector<double> proportional_parts(
    double total, const std::vector<ZoneSource>& sources) {
    std::vector<double> parts(sources.size(), 0.0);
    if (sources.empty()) {
        return parts;
    }
    if (sources.size() == 1) {
        parts[0] = total;
        return parts;
    }
    double weight_sum = 0;
    for (const ZoneSource& source : sources) {
        weight_sum += source.weight;
    }
    if (std::abs(weight_sum) <= kEpsilon) {
        for (double& part : parts) {
            part = total / static_cast<double>(sources.size());
        }
    } else {
        for (std::size_t index = 0; index < sources.size(); ++index) {
            parts[index] =
                total * sources[index].weight / weight_sum;
        }
    }
    double allocated = std::accumulate(parts.begin(), parts.end(), 0.0);
    std::size_t recipient = 0;
    for (std::size_t index = 1; index < sources.size(); ++index) {
        if (std::abs(sources[index].weight) >
            std::abs(sources[recipient].weight)) {
            recipient = index;
        }
    }
    parts[recipient] += total - allocated;
    return parts;
}

double factorial(std::size_t value) {
    double result = 1;
    for (std::size_t index = 2; index <= value; ++index) {
        result *= static_cast<double>(index);
    }
    return result;
}

std::vector<double> exact_shapley(
    const std::vector<double>& coalition_values) {
    const std::size_t zone_count =
        static_cast<std::size_t>(
            std::llround(std::log2(static_cast<double>(
                coalition_values.size()))));
    const double denominator = factorial(zone_count);
    std::vector<double> result(zone_count, 0);
    const std::size_t full_mask = coalition_values.size() - 1;
    for (std::size_t zone = 0; zone < zone_count; ++zone) {
        for (std::size_t mask = 0; mask <= full_mask; ++mask) {
            if ((mask & (std::size_t{1} << zone)) != 0) {
                continue;
            }
            const std::size_t members =
                static_cast<std::size_t>(std::popcount(mask));
            const double weight =
                factorial(members) *
                factorial(zone_count - members - 1);
            result[zone] +=
                (coalition_values[mask | (std::size_t{1} << zone)] -
                 coalition_values[mask]) *
                weight;
        }
    }
    for (double& value : result) {
        value /= denominator;
    }
    const double expected =
        coalition_values.back() - coalition_values.front();
    double actual =
        std::accumulate(result.begin(), result.end(), 0.0);
    if (std::abs(expected - actual) > kEpsilon && !result.empty()) {
        std::size_t recipient = 0;
        for (std::size_t index = 1; index < result.size(); ++index) {
            if (std::abs(result[index]) > std::abs(result[recipient])) {
                recipient = index;
            }
        }
        result[recipient] += expected - actual;
    }
    return result;
}

} // namespace

DamageAttributionResult attribute_damage_formula(
    const DamageAttributionInput& input) {
    DamageAttributionResult result;
    std::vector<Zone> zones;

    zones.push_back({
        "source-attribute.attack",
        attribute_zone_sources(input.source_attack, "attribute"),
        [&](AttributionState& state) {
            std::vector<std::size_t> all(
                input.source_attack.contributions.size());
            std::iota(all.begin(), all.end(), 0);
            state.source_attack =
                evaluate_attribute_stage_raw(input.source_attack, all);
        },
    });

    if (input.kind == DamageKind::Physical ||
        input.kind == DamageKind::Magical ||
        input.kind == DamageKind::Elemental) {
        zones.push_back({
            "target-attribute.mitigation",
            attribute_zone_sources(input.target_mitigation,
                                   "target-attribute"),
            [&](AttributionState& state) {
                std::vector<std::size_t> all(
                    input.target_mitigation.contributions.size());
                std::iota(all.begin(), all.end(), 0);
                state.target_mitigation =
                    evaluate_attribute_stage_raw(input.target_mitigation, all);
            },
        });
    }

    if (!input.post_damage_factors.empty()) {
        zones.push_back({
            "post-defense",
            factor_zone_sources(input.post_damage_factors),
            [&](AttributionState& state) {
                for (const DamageFactor& factor :
                     input.post_damage_factors) {
                    state.post_damage_deltas.push_back(factor.delta);
                }
            },
        });
    }

    AttributionState base_state;
    base_state.source_attack = fixed_from_double(input.source_attack.raw);
    base_state.target_mitigation =
        fixed_from_double(input.target_mitigation.raw);
    // AttackInfo.atkScale/atkAddition describe the skill's own damage
    // transform, not a buff. Removing them from the counterfactual baseline
    // creates artificial interactions (and negative "attack multiplier"
    // contributions) when a skill scales a buffed ATK down before conversion.
    base_state.attack_scale = fixed_from_double(input.attack_scale);
    base_state.attack_addition = fixed_from_double(input.attack_addition);
    const FixedPoint base_raw = evaluate_state(input, base_state);
    result.base_damage = fixed_to_double(base_raw);

    AttributionState full_state = base_state;
    for (const Zone& zone : zones) {
        zone.apply(full_state);
    }
    const FixedPoint modeled_raw = evaluate_state(input, full_state);
    result.modeled_damage = fixed_to_double(modeled_raw);
    const double expected_damage = input.has_expected_final_damage
                                       ? input.expected_final_damage
                                       : input.expected_formula_damage;
    result.unallocated = fixed_to_double(
        fixed_from_double(expected_damage) - modeled_raw);

    if (zones.empty()) {
        return result;
    }
    result.exact_zones = zones.size() <= kMaxExactZones;
    if (!result.exact_zones) {
        // Keep the result explicit instead of silently inventing an
        // approximation. The caller can still display unallocated damage.
        return result;
    }

    const std::size_t coalition_count =
        std::size_t{1} << zones.size();
    std::vector<FixedPoint> raw_values(coalition_count, 0);
    for (std::size_t mask = 0; mask < coalition_count; ++mask) {
        AttributionState state = base_state;
        for (std::size_t index = 0; index < zones.size(); ++index) {
            if ((mask & (std::size_t{1} << index)) != 0) {
                zones[index].apply(state);
            }
        }
        raw_values[mask] = evaluate_state(input, state);
    }
    std::vector<double> values(raw_values.size(), 0);
    for (std::size_t mask = 0; mask != raw_values.size(); ++mask) {
        values[mask] = fixed_to_double(raw_values[mask]);
    }
    const std::vector<double> shapley = exact_shapley(values);
    const std::size_t full_mask = coalition_count - 1;
    for (std::size_t index = 0; index < zones.size(); ++index) {
        const Zone& zone = zones[index];
        const double counterfactual =
            fixed_to_double(
                raw_values[full_mask] -
                raw_values[full_mask ^ (std::size_t{1} << index)]);
        const std::vector<double> shapley_parts =
            proportional_parts(shapley[index], zone.sources);
        const std::vector<double> counterfactual_parts =
            proportional_parts(counterfactual, zone.sources);
        for (std::size_t source_index = 0;
             source_index < zone.sources.size(); ++source_index) {
            result.allocations.push_back({
                zone.key,
                zone.sources[source_index].source,
                shapley_parts[source_index],
                counterfactual_parts[source_index],
            });
        }
    }
    return result;
}

} // namespace ark::better_battle
