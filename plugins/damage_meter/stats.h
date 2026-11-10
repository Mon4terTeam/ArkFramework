#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <ark/battle.h>

namespace ark::damage_meter {

inline constexpr std::size_t kDamageTypeCount = 4;
inline constexpr std::array<const char*, kDamageTypeCount> kDamageTypeNames = {"物理", "法术",
                                                                               "真实", "元素"};

// Maps a native damage type to its column in ActorRow::types, or -1 when it is not tracked.
inline int damage_type_index(std::int32_t type) {
    switch (type) {
    case ARK_DAMAGE_PHYSICAL:
        return 0;
    case ARK_DAMAGE_MAGICAL:
        return 1;
    case ARK_DAMAGE_TRUE:
        return 2;
    case ARK_DAMAGE_ELEMENTAL:
        return 3;
    default:
        return -1;
    }
}

struct DamageTotals {
    double calculated = 0;
    double actual = 0;
};

struct ActorRow {
    std::string id;
    double calculated = 0;
    double actual = 0;
    double elemental_buildup = 0;
    std::array<DamageTotals, kDamageTypeCount> types{};
};

// Per-actor totals of one battle, rebuilt by replaying the event history.
class BattleStats {
public:
    // Applies one event. Events at or below the last consumed sequence are ignored, so
    // replaying the same history is idempotent.
    void consume(const ArkEventV2& event) {
        if (event.sequence && event.sequence <= sequence) {
            return;
        }
        sequence = event.sequence;
        if (!frame_valid || event.frame > frame) {
            frame = event.frame;
            frame_valid = true;
        }
        if (event.kind == ARK_BATTLE_START) {
            start_battle(event);
            return;
        }
        game_time = std::max(game_time, event.game_time);
        if (event.kind == ARK_BATTLE_END) {
            ended = true;
            return;
        }
        if (event.kind == ARK_DAMAGE || event.kind == ARK_ELEMENTAL) {
            add_damage(event);
        }
    }

    double duration() const {
        if (frame_valid && frame >= start_frame) {
            return static_cast<double>(frame - start_frame) / 30.0;
        }
        return std::max(0.0, game_time - start_time);
    }

    double total(bool actual) const {
        double sum = 0;
        for (const auto& [id, row] : rows) {
            sum += actual ? row.actual : row.calculated;
        }
        return sum;
    }

    // Orders rows by descending actual or calculated damage, ties broken by actor ID.
    void sort_rows(std::vector<const ActorRow*>& out, bool by_actual) const {
        out.clear();
        out.reserve(rows.size());
        for (const auto& [id, row] : rows) {
            out.push_back(&row);
        }
        std::sort(out.begin(), out.end(), [by_actual](const ActorRow* a, const ActorRow* b) {
            const double left = by_actual ? a->actual : a->calculated;
            const double right = by_actual ? b->actual : b->calculated;
            return left == right ? a->id < b->id : left > right;
        });
    }

    std::map<std::string, ActorRow> rows;
    std::string stage;
    std::uint64_t sequence = 0;
    std::uint64_t battle = 0;
    std::uint32_t frame = 0;
    std::uint32_t start_frame = 0;
    bool frame_valid = false;
    double game_time = 0;
    // Partial battles (joined mid-way) measure duration from the first observed event.
    double start_time = 0;
    bool partial = false;
    bool ended = false;

private:
    void start_battle(const ArkEventV2& event) {
        rows.clear();
        stage = event.stage_id;
        battle = event.battle;
        partial = event.partial != 0;
        start_time = partial ? event.game_time : 0;
        start_frame = partial ? event.frame : 0;
        frame = event.frame;
        frame_valid = true;
        game_time = event.game_time;
        ended = false;
    }

    void add_damage(const ArkEventV2& event) {
        if (!event.actor_id[0] || !std::isfinite(event.calculated_damage) ||
            !std::isfinite(event.actual_damage)) {
            return;
        }
        const int type = damage_type_index(event.damage_type);
        if (event.kind == ARK_DAMAGE && type < 0) {
            return;
        }
        ActorRow& row = rows[event.actor_id];
        row.id = event.actor_id;
        // Elemental buildup is tracked separately and never counts as HP damage.
        if (event.kind == ARK_ELEMENTAL) {
            row.elemental_buildup += std::abs(event.calculated_damage);
            return;
        }
        const double calculated = std::abs(event.calculated_damage);
        const double actual = std::abs(event.actual_damage);
        row.calculated += calculated;
        row.actual += actual;
        DamageTotals& totals = row.types[static_cast<std::size_t>(type)];
        totals.calculated += calculated;
        totals.actual += actual;
    }
};

} // namespace ark::damage_meter
