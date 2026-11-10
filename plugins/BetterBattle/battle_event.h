#pragma once

#include <cstdint>
#include <string>

namespace ark {

// Event model owned by BetterBattle. It carries one native combat modifier or one
// battle lifecycle transition; multiple events may share the same logical frame.
enum class ArkBattleEventKind : std::uint8_t {
    BattleStart,
    BattleEnd,
    Clock,
    Damage,
    Healing,
    Regen,
    ElementalBuildup,
};

// Mirrors the native Modifier damage type numbering.
enum class ArkDamageType : std::int32_t {
    None = 0,
    Physical = 1,
    Magical = 2,
    True = 3,
    Healing = 4,
    Elemental = 5,
};

struct ArkBattleEvent {
    ArkBattleEventKind kind = ArkBattleEventKind::Clock;
    std::uint64_t sequence = 0;
    std::uint64_t battle = 0;
    std::uint32_t frame = 0;
    std::uint32_t frame_event_index = 0;
    double game_time = 0;
    double calculated_delta = 0;
    double applied_delta = 0;
    // Observed HP change on the target at this event boundary. NaN means the target
    // was not seeded before its first modifier.
    double hp_delta = 0;
    double hp_after = 0;
    // TODO: barrier gain/damage and negative-HP change statistics are not
    // implemented; minusHp is exposed for battle-page display only.
    double minus_hp_after = 0;
    double minus_hp_delta = 0;
    double max_minus_hp_after = 0;
    double hp_effective_after = 0;
    double hp_effective_delta = 0;
    std::int32_t damage_type = 0;
    bool partial = false;

    std::string source_id;
    std::string source_owner_id;
    std::string target_id;
    std::string original_source_id;
    std::string stage_id;
};

struct ArkBattleEventReadStatus {
    bool partial = false;
    bool overrun = false;
    bool ended = false;
    std::uint64_t oldest_sequence = 0;
    std::uint64_t newest_sequence = 0;
};

} // namespace ark
