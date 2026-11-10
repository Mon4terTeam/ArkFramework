#include "self_test.h"

namespace ark::test_host {
namespace {

constexpr char kDamageMeter[] = "damage_meter.dll";

} // namespace

bool publishes_replay(SelfTestState state) {
    return state == SelfTestState::Initial || state == SelfTestState::ReloadReplay;
}

void advance_self_test(SelfTestState& state, FrameworkControl control,
                       ArkFrameworkWithService with_service, ReplayState& replay) {
    HistorySnapshot history;
    const bool present = with_service(ARK_BATTLE_SERVICE_NAME_V2, ARK_BATTLE_ABI_VERSION_V2,
                                      sizeof(ArkBattleEventsApiV2), inspect_history, &history);

    switch (state) {
    case SelfTestState::Initial:
        if (present && replay_complete(history)) {
            control("reload", kDamageMeter);
            state = SelfTestState::ReloadingDamage;
        }
        break;
    case SelfTestState::ReloadingDamage:
        if (present && !history.status.newest_sequence) {
            replay = {};
            state = SelfTestState::ReloadReplay;
        }
        break;
    case SelfTestState::ReloadReplay:
        if (present && replay.count == kReplayEventCount && replay_complete(history)) {
            state = SelfTestState::Complete;
        }
        break;
    case SelfTestState::Complete:
        break;
    }
}

} // namespace ark::test_host
