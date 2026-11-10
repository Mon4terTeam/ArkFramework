#pragma once

#include <ark/battle.h>

namespace ark::test_host {

// Number of synthetic damage events published per replay.
constexpr unsigned kReplayEventCount = 60;

// Progress of the synthetic battle published through ARK_BATTLE_TEST_SERVICE_NAME_V2.
// Value-initialize to restart the replay from scratch.
struct ReplayState {
    bool seeded = false;
    bool failed = false;
    double elapsed = 0;
    unsigned count = 0;
};

// What the damage meter's event history currently contains.
struct HistorySnapshot {
    ArkReadStatusV1 status;
    unsigned starts = 0;
    unsigned damage = 0;
    unsigned ends = 0;
    double calculated = 0;
    double actual = 0;
    unsigned last_frame_event_index = 0;
    bool frame_valid = true;
    bool valid = true;
};

// ArkServiceCallback for ARK_BATTLE_TEST_SERVICE_NAME_V2; `user` is a ReplayState.
// Publishes the battle start and then paces damage events by elapsed time.
void publish_replay(const void* table, void* user);

// ArkServiceCallback for ARK_BATTLE_SERVICE_NAME_V2; `user` is a HistorySnapshot.
void inspect_history(const void* table, void* user);

// True when the history holds exactly one complete, intact replay.
bool replay_complete(const HistorySnapshot& history);

} // namespace ark::test_host
