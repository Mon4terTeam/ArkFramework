#include "battle_replay.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace ark::test_host {
namespace {

constexpr char kStageId[] = "Activities/ACT3D0/level_act3d0_01";
constexpr double kBattleDuration = 192.06648349761963;
constexpr double kEventInterval = .15;
constexpr double kDamageScale = 10;
constexpr double kActualRatio = .8;
constexpr double kTolerance = .000001;

// Captured OF-1 calculated damage per operator. Actual damage is a synthetic
// fraction of it, used only to exercise the UI.
constexpr const char* kActorIds[] = {"char_124_kroos", "char_208_melan",  "char_002_amiya",
                                     "char_145_prove", "char_166_skfire", "char_123_fang"};
constexpr double kCalculatedDamage[] = {33652, 10600.958984375, 7668, 5548.24267578125, 3272, 2490};
static_assert(std::size(kActorIds) == std::size(kCalculatedDamage));
constexpr unsigned kActorCount = std::size(kActorIds);

constexpr double total_calculated_damage() {
    double total = 0;
    for (double damage : kCalculatedDamage) {
        total += damage;
    }
    return total;
}

bool is_magical_actor(unsigned index) {
    return index == 2 || index == 4;
}

ArkEventV1 make_damage_event(unsigned sequence) {
    const unsigned index = sequence % kActorCount;
    ArkEventV1 event;
    event.kind = ARK_DAMAGE;
    event.game_time = (sequence + 1) * kBattleDuration / kReplayEventCount;
    snprintf(event.actor_id, sizeof(event.actor_id), "%s", kActorIds[index]);
    event.calculated_damage = kCalculatedDamage[index] / kDamageScale;
    event.actual_damage = event.calculated_damage * kActualRatio;
    event.damage_type = is_magical_actor(index) ? ARK_DAMAGE_MAGICAL : ARK_DAMAGE_PHYSICAL;
    return event;
}

} // namespace

void publish_replay(const void* table, void* user) {
    // Neither the table nor its function pointers escape this service lease.
    const auto* service = static_cast<const ArkBattleTestApiV1*>(table);
    if (service->size < sizeof(*service) || service->abi_version != ARK_BATTLE_ABI_VERSION) {
        return;
    }
    auto& replay = *static_cast<ReplayState*>(user);

    if (!replay.seeded) {
        ArkEventV1 start;
        start.kind = ARK_BATTLE_START;
        snprintf(start.stage_id, sizeof(start.stage_id), "%s", kStageId);
        if (!service->publish(&start)) {
            replay.failed = true;
            return;
        }
        replay.seeded = true;
        replay.elapsed = 0;
    }

    if (replay.count >= kReplayEventCount || replay.elapsed <= replay.count * kEventInterval) {
        return;
    }
    const ArkEventV1 damage = make_damage_event(replay.count);
    if (!service->publish(&damage)) {
        replay.failed = true;
        return;
    }
    ++replay.count;

    if (replay.count == kReplayEventCount) {
        ArkEventV1 end;
        end.kind = ARK_BATTLE_END;
        end.game_time = kBattleDuration;
        if (!service->publish(&end)) {
            replay.failed = true;
        }
    }
}

void inspect_history(const void* table, void* user) {
    constexpr size_t kCapacity = 64;
    const auto* service = static_cast<const ArkBattleEventsApiV1*>(table);
    auto& history = *static_cast<HistorySnapshot*>(user);

    ArkEventV1 events[kCapacity];
    const size_t count = service->read(0, events, kCapacity, &history.status);
    for (size_t i = 0; i < count; ++i) {
        const ArkEventV1& event = events[i];
        switch (event.kind) {
        case ARK_BATTLE_START:
            ++history.starts;
            history.valid = history.valid && std::strcmp(event.stage_id, kStageId) == 0;
            break;
        case ARK_DAMAGE:
            ++history.damage;
            history.calculated += event.calculated_damage;
            history.actual += event.actual_damage;
            break;
        case ARK_BATTLE_END:
            ++history.ends;
            history.valid = history.valid && event.game_time == kBattleDuration;
            break;
        default:
            history.valid = false;
            break;
        }
    }
}

bool replay_complete(const HistorySnapshot& history) {
    // Each actor appears kReplayEventCount / kActorCount times at 1/kDamageScale.
    constexpr double kExpected = total_calculated_damage();
    const ArkReadStatusV1& status = history.status;
    return history.valid && !status.partial && !status.overrun && status.ended &&
           history.starts == 1 && history.damage == kReplayEventCount && history.ends == 1 &&
           std::abs(history.calculated - kExpected) < kTolerance &&
           std::abs(history.actual - kExpected * kActualRatio) < kTolerance;
}

} // namespace ark::test_host
