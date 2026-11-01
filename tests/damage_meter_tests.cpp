// Unit tests for the header-only damage meter model: event history, statistics and stage names.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../plugins/damage_meter/event_history.h"
#include "../plugins/damage_meter/stage_names.h"
#include "../plugins/damage_meter/stats.h"

using ark::damage_meter::ActorRow;
using ark::damage_meter::BattleStats;
using ark::damage_meter::EventHistory;
using ark::damage_meter::StageNameIndex;

namespace {

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
    }
}

#define ARK_CHECK(condition) check((condition), #condition, __LINE__)

ArkEventV1 make_event(std::uint32_t kind) {
    ArkEventV1 event;
    event.kind = kind;
    return event;
}

void set_id(char (&target)[ARK_MAX_ID], const char* value) {
    std::snprintf(target, sizeof(target), "%s", value);
}

void consume_all(BattleStats& stats, const ArkEventV1* events, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        stats.consume(events[i]);
    }
}

// Covers replay, duplicate suppression, buildup isolation, bounded history and battle reset.
void test_history_replay_and_bounds() {
    EventHistory history(4);
    ArkEventV1 event = make_event(ARK_BATTLE_START);
    history.publish(event);
    event.kind = ARK_DAMAGE;
    event.damage_type = ARK_DAMAGE_PHYSICAL;
    event.calculated_damage = 300;
    event.actual_damage = 100;
    event.game_time = 2;
    set_id(event.actor_id, "A");
    history.publish(event);
    event.kind = ARK_CLOCK;
    event.game_time = 4;
    history.publish(event);

    ArkEventV1 out[8];
    ArkReadStatusV1 status;
    std::size_t count = history.read(0, out, 8, &status);
    ARK_CHECK(count == 3 && !status.partial);

    BattleStats model;
    consume_all(model, out, count);
    ARK_CHECK(model.total(true) == 100 && model.total(false) == 300 && model.duration() == 4);

    // Consuming the same events again is ignored.
    consume_all(model, out, count);
    ARK_CHECK(model.total(false) == 300);

    // Replaying into a newly loaded plugin recovers exactly the same totals.
    BattleStats reloaded;
    consume_all(reloaded, out, count);
    ARK_CHECK(reloaded.total(true) == model.total(true));

    // Elemental buildup never counts as damage.
    event.kind = ARK_ELEMENTAL;
    event.calculated_damage = 20;
    history.publish(event);
    count = history.read(model.sequence, out, 8, &status);
    consume_all(model, out, count);
    ARK_CHECK(model.total(false) == 300 && model.rows["A"].elemental_buildup == 20);

    // Exceeding the capacity drops the oldest events and reports the gap.
    event.kind = ARK_CLOCK;
    history.publish(event);
    history.publish(event);
    count = history.read(1, out, 8, &status);
    ARK_CHECK(status.partial && status.overrun && count == 4);

    // A new battle discards the previous one and clears the partial state.
    event = {};
    event.kind = ARK_BATTLE_START;
    history.publish(event);
    count = history.read(1, out, 8, &status);
    ARK_CHECK(count == 1 && !status.overrun && !status.partial);
}

void test_damage_types() {
    EventHistory history(64);
    const ArkEventV1 start = make_event(ARK_BATTLE_START);
    history.publish(start);
    const auto emit = [&](const char* actor, std::int32_t type, double calculated, double actual,
                          std::uint32_t kind = ARK_DAMAGE) {
        ArkEventV1 event = make_event(kind);
        event.damage_type = type;
        event.calculated_damage = calculated;
        event.actual_damage = actual;
        event.game_time = 2;
        set_id(event.actor_id, actor);
        history.publish(event);
    };
    emit("A", ARK_DAMAGE_PHYSICAL, 120, 100);
    emit("A", ARK_DAMAGE_MAGICAL, 80, 60);
    emit("A", ARK_DAMAGE_TRUE, 50, 40);
    emit("A", ARK_DAMAGE_ELEMENTAL, 30, 20);
    emit("A", ARK_DAMAGE_PHYSICAL, 12, 10);
    emit("B", ARK_DAMAGE_MAGICAL, 400, 150);
    emit("A", ARK_DAMAGE_ELEMENTAL, 999, 777, ARK_ELEMENTAL);
    // Untracked damage types are dropped.
    emit("A", ARK_DAMAGE_HEAL, 1000, 1000);
    emit("A", ARK_DAMAGE_NONE, 1000, 1000);
    emit("A", 99, 1000, 1000);

    ArkEventV1 events[64];
    ArkReadStatusV1 status;
    std::size_t count = history.read(0, events, 64, &status);
    BattleStats model;
    consume_all(model, events, count);
    ARK_CHECK(model.rows.size() == 2 && model.total(true) == 380 && model.total(false) == 692);

    const ActorRow& a = model.rows.at("A");
    ARK_CHECK(a.actual == 230 && a.calculated == 292 && a.elemental_buildup == 999);
    ARK_CHECK(a.types[0].actual == 110 && a.types[0].calculated == 132);
    ARK_CHECK(a.types[1].actual == 60 && a.types[1].calculated == 80);
    ARK_CHECK(a.types[2].actual == 40 && a.types[2].calculated == 50);
    ARK_CHECK(a.types[3].actual == 20 && a.types[3].calculated == 30);
    double actual = 0;
    double calculated = 0;
    for (const auto& type : a.types) {
        actual += type.actual;
        calculated += type.calculated;
    }
    ARK_CHECK(actual == a.actual && calculated == a.calculated);
    ARK_CHECK(model.rows.at("B").types[1].actual == 150 && model.rows.at("B").types[3].actual == 0);

    consume_all(model, events, count);
    ARK_CHECK(model.rows.at("A").types[3].actual == 20 &&
              model.rows.at("A").elemental_buildup == 999);

    BattleStats replay;
    consume_all(replay, events, count);
    ARK_CHECK(replay.rows.at("A").types[3].calculated == 30 && replay.total(true) == 380);

    std::vector<const ActorRow*> order;
    model.sort_rows(order, true);
    ARK_CHECK(order.front()->id == "A");
    model.sort_rows(order, false);
    ARK_CHECK(order.front()->id == "B");

    history.publish(start);
    emit("A", ARK_DAMAGE_ELEMENTAL, 5, 4);
    count = history.read(model.sequence, events, 64, &status);
    consume_all(model, events, count);
    ARK_CHECK(model.rows.size() == 1 && model.total(true) == 4 && model.total(false) == 5);
    ARK_CHECK(model.rows.at("A").types[0].actual == 0 && model.rows.at("A").types[3].actual == 4 &&
              model.rows.at("A").elemental_buildup == 0);
}

void test_battle_frame_snapshot() {
    EventHistory history(2);
    ArkBattleFrameV1 frame;

    // No frame before the first battle.
    history.set_frame(7);
    ARK_CHECK(!history.frame(frame));

    ArkEventV1 event = make_event(ARK_BATTLE_START);
    history.publish(event);
    history.set_frame(0);
    ARK_CHECK(history.frame(frame) && frame.frame == 0 && frame.battle == 1);

    // The snapshot survives history eviction.
    history.set_frame(90);
    event.kind = ARK_CLOCK;
    for (int i = 0; i < 3; ++i) {
        history.publish(event);
    }
    ARK_CHECK(history.frame(frame) && frame.frame == 90 && frame.battle == 1);

    // The final frame is frozen once the battle ends.
    event.kind = ARK_BATTLE_END;
    history.publish(event);
    history.set_frame(91);
    ARK_CHECK(history.frame(frame) && frame.frame == 90);

    event.kind = ARK_BATTLE_START;
    history.publish(event);
    ARK_CHECK(!history.frame(frame));
    history.set_frame(0);
    ARK_CHECK(history.frame(frame) && frame.frame == 0 && frame.battle == 2);
}

void test_stage_names() {
    StageNameIndex names;
    names.add("a001_03", "Activities/A001/level_a001_03", "GT-3", "意外之旅");
    names.add("act3d0_01", "Activities/ACT3D0/level_act3d0_01", "OF-1", "火山制造");
    ARK_CHECK(names.resolve("level_a001_03") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("Activities/A001/level_a001_03") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("a001_03") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("ACTIVITIES\\A001\\LEVEL_A001_03.JSON") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("LEVEL_A001_03.Json") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("A001_03.json") == "GT-3 意外之旅");
    ARK_CHECK(names.resolve("Activities/ACT3D0/level_act3d0_01") == "OF-1 火山制造");

    const auto rogue = names.add("ro3_n_01", "Roguelike/RO3/level_ro3_n_01", "ISW-NO", "与虫为伴");
    names.add_alias(rogue, "Roguelike/RO3/level_ro3_n_01_replace");
    names.add_alias(rogue, "Roguelike/RO3/level_ro3_n_01_hard.json");
    ARK_CHECK(names.resolve("ro3_n_01") == "ISW-NO 与虫为伴");
    ARK_CHECK(names.resolve("level_ro3_n_01") == "ISW-NO 与虫为伴");
    ARK_CHECK(names.resolve("ROGUELIKE\\RO3\\LEVEL_RO3_N_01_REPLACE.JSON") == "ISW-NO 与虫为伴");
    ARK_CHECK(names.resolve("level_ro3_n_01_replace") == "ISW-NO 与虫为伴");
    ARK_CHECK(names.resolve("level_ro3_n_01_hard") == "ISW-NO 与虫为伴");
    ARK_CHECK(names.resolve("Roguelike/RO3/level_ro3_n_01_hard") == "ISW-NO 与虫为伴");

    ARK_CHECK(names.resolve("level_unknown").empty());
    ARK_CHECK(names.resolve("unrecognized/path/level_unknown.json").empty());
    ARK_CHECK(names.resolve("").empty());
    ARK_CHECK(names.resolve("level_a001_03.json.bak").empty());

    names.add("code_only", "levels/code_only", "S-9", "");
    names.add("name_only", "levels/name_only", "", "无名之地");
    names.add("unnamed", "levels/unnamed", "", "");
    ARK_CHECK(names.resolve("code_only") == "S-9");
    ARK_CHECK(names.resolve("name_only") == "无名之地");
    ARK_CHECK(names.resolve("unnamed").empty());

    // An earlier entry handle still identifies its native name after more rows arrive.
    names.add_alias(rogue, "Roguelike/RO3/level_ro3_late_replace");
    ARK_CHECK(names.resolve("level_ro3_late_replace") == "ISW-NO 与虫为伴");

    // The panel shows the resolved label, falling back to the raw stage ID.
    BattleStats model;
    ArkEventV1 start = make_event(ARK_BATTLE_START);
    for (const char* stage :
         {"level_a001_03", "ro3_n_01", "Unrecognized/level_unknown.json", "a001_03"}) {
        set_id(start.stage_id, stage);
        model.consume(start);
        const std::string_view native = names.resolve(model.stage);
        const std::string visible(native.empty() ? std::string_view(model.stage) : native);
        if (model.stage == "Unrecognized/level_unknown.json") {
            ARK_CHECK(visible == "Unrecognized/level_unknown.json");
        } else if (model.stage == "ro3_n_01") {
            ARK_CHECK(visible == "ISW-NO 与虫为伴");
        } else {
            ARK_CHECK(visible == "GT-3 意外之旅");
        }
    }
    ARK_CHECK(names.resolve("act3d0_01") == "OF-1 火山制造");
}

void test_ambiguous_basenames() {
    for (bool reverse : {false, true}) {
        StageNameIndex names;
        const auto add_first = [&] {
            return names.add("stage_first", "Episodes/First/level_shared", "A-1", "甲");
        };
        const auto add_second = [&] {
            return names.add("stage_second", "Episodes/Second/level_shared", "B-2", "乙");
        };
        std::size_t first_entry;
        if (reverse) {
            add_second();
            first_entry = add_first();
        } else {
            first_entry = add_first();
            add_second();
        }
        ARK_CHECK(names.resolve("level_shared").empty());
        ARK_CHECK(names.resolve("LEVEL_SHARED.JSON").empty());
        ARK_CHECK(names.resolve("Unknown/level_shared").empty());
        ARK_CHECK(names.resolve("Episodes/First/level_shared") == "A-1 甲");
        ARK_CHECK(names.resolve("EPISODES\\SECOND\\LEVEL_SHARED.JSON") == "B-2 乙");
        ARK_CHECK(names.resolve("stage_first") == "A-1 甲");
        ARK_CHECK(names.resolve("stage_second") == "B-2 乙");

        // Re-adding an alias does not lift the ambiguity.
        names.add_alias(first_entry, "Episodes/First/level_shared.json");
        ARK_CHECK(names.resolve("level_shared").empty());

        // An exact stage ID wins over an ambiguous basename.
        names.add("level_shared", "Episodes/Third/level_unique", "C-3", "丙");
        ARK_CHECK(names.resolve("level_shared") == "C-3 丙");
        ARK_CHECK(names.resolve("Episodes/First/level_shared") == "A-1 甲");
        ARK_CHECK(names.resolve("Episodes/Second/level_shared") == "B-2 乙");
    }
}

void test_duplicate_labels_stay_unambiguous() {
    StageNameIndex names;
    const auto original = names.add("same_first", "First/level_same", "D-4", "同名");
    names.add_alias(original, "FIRST\\LEVEL_SAME.JSON");
    names.add("same_second", "Second/level_same", "D-4", "同名");
    ARK_CHECK(names.resolve("level_same") == "D-4 同名");
    ARK_CHECK(names.resolve("FIRST/level_same") == "D-4 同名");
    ARK_CHECK(names.resolve("Second/LEVEL_SAME.json") == "D-4 同名");
    ARK_CHECK(names.resolve("same_first") == "D-4 同名");
    ARK_CHECK(names.resolve("same_second") == "D-4 同名");

    const auto replacement = names.add("different", "Third/level_other", "E-5", "异名");
    names.add_alias(replacement, "Third/level_same");
    ARK_CHECK(names.resolve("level_same").empty());
    ARK_CHECK(names.resolve("First/level_same") == "D-4 同名");
    ARK_CHECK(names.resolve("Third/level_same") == "E-5 异名");
}

void test_unqualified_level_collision() {
    StageNameIndex names;
    names.add("qualified", "Episodes/First/level_collision", "A-1", "甲");
    names.add("unqualified", "level_collision", "B-2", "乙");
    ARK_CHECK(names.resolve("level_collision").empty());
    ARK_CHECK(names.resolve("Episodes/First/level_collision") == "A-1 甲");
    ARK_CHECK(names.resolve("unqualified") == "B-2 乙");
}

struct TestCase {
    const char* name;
    void (*run)();
};

constexpr TestCase kTests[] = {
    {"history replay and bounds", test_history_replay_and_bounds},
    {"damage types", test_damage_types},
    {"battle frame snapshot", test_battle_frame_snapshot},
    {"stage names", test_stage_names},
    {"ambiguous basenames", test_ambiguous_basenames},
    {"duplicate labels stay unambiguous", test_duplicate_labels_stay_unambiguous},
    {"unqualified level collision", test_unqualified_level_collision},
};

} // namespace

int wmain() {
    for (const TestCase& test : kTests) {
        try {
            test.run();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL %s: %s\n", test.name, error.what());
            return 1;
        }
        std::printf("PASS %s\n", test.name);
    }
    return 0;
}
