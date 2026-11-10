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
constexpr unsigned kBattleFrameCount =
    static_cast<unsigned>(kBattleDuration * 30.0);

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

ArkEventV2 make_damage_event(unsigned sequence) {
    const unsigned index = sequence % kActorCount;
    ArkEventV2 event;
    event.kind = ARK_DAMAGE;
    event.frame =
        (sequence + 1) * kBattleFrameCount / kReplayEventCount;
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
    const auto* service = static_cast<const ArkBattleTestApiV2*>(table);
    if (service->size < sizeof(*service) ||
        service->abi_version != ARK_BATTLE_ABI_VERSION_V2) {
        return;
    }
    auto& replay = *static_cast<ReplayState*>(user);

    if (!replay.seeded) {
        ArkEventV2 start;
        start.kind = ARK_BATTLE_START;
        start.frame = 0;
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
    const ArkEventV2 damage = make_damage_event(replay.count);
    if (!service->publish(&damage)) {
        replay.failed = true;
        return;
    }
    ++replay.count;

    if (replay.count == kReplayEventCount) {
        ArkEventV2 end;
        end.kind = ARK_BATTLE_END;
        end.frame = kBattleFrameCount;
        end.game_time = kBattleDuration;
        if (!service->publish(&end)) {
            replay.failed = true;
        }
    }
}

void inspect_history(const void* table, void* user) {
    constexpr size_t kCapacity = 64;
    const auto* service = static_cast<const ArkBattleEventsApiV2*>(table);
    if (service->size < sizeof(*service) ||
        service->abi_version != ARK_BATTLE_ABI_VERSION_V2) {
        return;
    }
    auto& history = *static_cast<HistorySnapshot*>(user);

    ArkEventV2 events[kCapacity];
    const size_t count = service->read(0, events, kCapacity, &history.status);
    unsigned previous_frame = 0;
    bool previous_frame_valid = false;
    unsigned previous_frame_event_index = 0;
    for (size_t i = 0; i < count; ++i) {
        const ArkEventV2& event = events[i];
        if (!previous_frame_valid || event.frame != previous_frame) {
            history.frame_valid = history.frame_valid && event.frame_event_index == 0;
            previous_frame = event.frame;
            previous_frame_event_index = 0;
            previous_frame_valid = true;
        } else {
            history.frame_valid =
                history.frame_valid &&
                event.frame_event_index == previous_frame_event_index + 1;
        }
        previous_frame_event_index = event.frame_event_index;
        history.last_frame_event_index = event.frame_event_index;
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
            history.valid = history.valid && event.frame == kBattleFrameCount &&
                            event.game_time == kBattleDuration;
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
    return history.valid && history.frame_valid && !status.partial && !status.overrun &&
           status.ended &&
           history.starts == 1 && history.damage == kReplayEventCount && history.ends == 1 &&
           std::abs(history.calculated - kExpected) < kTolerance &&
           std::abs(history.actual - kExpected * kActualRatio) < kTolerance;
}

} // namespace ark::test_host
