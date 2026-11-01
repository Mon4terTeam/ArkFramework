#pragma once

#include <ark/plugin.h>

#include "battle_replay.h"

namespace ark::test_host {

using FrameworkControl = void (*)(const char* operation, const char* filename);

// Reload of damage_meter.dll between two replays.
enum class SelfTestState {
    Initial,
    ReloadingDamage,
    ReloadReplay,
    Complete,
};

// Whether the host should publish replay events while in `state`.
bool publishes_replay(SelfTestState state);

// Advances the self-test by at most one step based on the current service state.
void advance_self_test(SelfTestState& state, FrameworkControl control,
                       ArkFrameworkWithService with_service, ReplayState& replay);

} // namespace ark::test_host
