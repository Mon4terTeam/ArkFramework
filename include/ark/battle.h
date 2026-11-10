// Battle event service provided by damage_meter.dll.
//
// ABI 1 is the legacy service (`ark.battle.events`) and remains byte-for-byte
// compatible. ABI 2 is published under `ark.battle.events.v2`; consumers choose
// the service name explicitly and no legacy consumer needs to change.
//
// Consumers declare `damage_meter.dll` as a dependency and acquire the table
// matching their service name through `require_service`.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ARK_BATTLE_ABI_VERSION 1u
#define ARK_BATTLE_SERVICE_NAME "ark.battle.events"
#define ARK_BATTLE_TEST_SERVICE_NAME "ark.battle.test"

#define ARK_BATTLE_ABI_VERSION_V2 2u
#define ARK_BATTLE_SERVICE_NAME_V2 "ark.battle.events.v2"
#define ARK_BATTLE_TEST_SERVICE_NAME_V2 "ark.battle.test.v2"
#define ARK_MAX_ID 128

enum ArkEventKind : uint32_t {
    ARK_BATTLE_START = 1,
    ARK_DAMAGE = 2,
    ARK_ELEMENTAL = 3, // Elemental buildup, not elemental HP damage (ARK_DAMAGE, type 5).
    ARK_BATTLE_END = 4,
    ARK_CLOCK = 5,
};

// Matches the game's native damage type numbering.
enum ArkDamageType : int32_t {
    ARK_DAMAGE_NONE = 0,
    ARK_DAMAGE_PHYSICAL = 1,
    ARK_DAMAGE_MAGICAL = 2,
    ARK_DAMAGE_TRUE = 3,
    ARK_DAMAGE_HEAL = 4,
    ARK_DAMAGE_ELEMENTAL = 5,
};

// Legacy ABI-1 event layout. Never change this structure: ABI 1 consumers
// allocate arrays of it and pass them to ArkBattleEventsApiV1.read.
struct ArkEventV1 {
    uint32_t size = sizeof(ArkEventV1);
    uint32_t kind = 0;
    uint64_t sequence = 0;
    uint64_t battle = 0;
    double game_time = 0;
    double calculated_damage = 0;
    double actual_damage = 0;
    int32_t damage_type = 0;
    uint32_t partial = 0;
    char actor_id[ARK_MAX_ID]{};
    char stage_id[ARK_MAX_ID]{};
};

// ABI 2 event layout, published only through `ark.battle.events.v2`.
//
// Frame is the authoritative timestamp. game_time is retained as a derived display
// value. frame_event_index orders events within one fixed frame; sequence remains
// the global history cursor used for lossless replay/deduplication.
struct ArkEventV2 {
    uint32_t size = sizeof(ArkEventV2);
    uint32_t kind = 0;
    uint32_t frame = 0;
    uint32_t frame_event_index = 0;
    uint64_t sequence = 0;
    uint64_t battle = 0;
    double game_time = 0;
    double calculated_damage = 0;
    double actual_damage = 0;
    int32_t damage_type = 0;
    uint32_t partial = 0;
    char actor_id[ARK_MAX_ID]{};
    // Set when actor_id is a token/summon and its host character can be resolved.
    char owner_id[ARK_MAX_ID]{};
    char target_id[ARK_MAX_ID]{};
    char original_source_id[ARK_MAX_ID]{};
    char stage_id[ARK_MAX_ID]{};
};

struct ArkReadStatusV1 {
    uint32_t size = sizeof(ArkReadStatusV1);
    uint32_t partial = 0;
    uint32_t overrun = 0;
    uint32_t ended = 0;
    uint64_t oldest_sequence = 0;
    uint64_t newest_sequence = 0;
};

struct ArkBattleFrameV1 {
    uint32_t size = sizeof(ArkBattleFrameV1);
    uint32_t frame = 0; // Native BattleController.fixedFrameCnt; zero is valid.
    uint64_t battle = 0;
};

// Legacy ABI-1 service layout. Keep this structure and its ABI version intact.
struct ArkBattleEventsApiV1 {
    uint32_t size;
    uint32_t abi_version;
    // Copies up to `capacity` (at most 4096) events newer than `after`.
    size_t (*read)(uint64_t after, ArkEventV1* out, size_t capacity, ArkReadStatusV1* status);
    // Cached on the game thread; safe to call from a rendering callback.
    bool (*frame)(ArkBattleFrameV1* out);
};

// ABI 2 service layout, published under ARK_BATTLE_SERVICE_NAME_V2.
struct ArkBattleEventsApiV2 {
    uint32_t size;
    uint32_t abi_version;
    // Copies up to `capacity` (at most 4096) events newer than `after`.
    size_t (*read)(uint64_t after, ArkEventV2* out, size_t capacity,
                   ArkReadStatusV1* status);
    // Cached on the game thread; safe to call from a rendering callback.
    bool (*frame)(ArkBattleFrameV1* out);
};

// Registered only inside the graphical test host.
// Legacy ABI-1 test-service layout. Keep it intact for old test hosts.
struct ArkBattleTestApiV1 {
    uint32_t size;
    uint32_t abi_version;
    bool (*publish)(const ArkEventV1* event);
};

// ABI 2 test-service layout, published under ARK_BATTLE_TEST_SERVICE_NAME_V2.
struct ArkBattleTestApiV2 {
    uint32_t size;
    uint32_t abi_version;
    bool (*publish)(const ArkEventV2* event);
};
