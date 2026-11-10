#include "replay_document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ark::better_battle {
namespace {

using Json = nlohmann::ordered_json;

const Json* find_value(const Json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() || it->is_null() ? nullptr : &*it;
}

const Json& require_value(const Json& object, const char* key, const char* label) {
    const Json* value = find_value(object, key);
    if (!value) {
        throw std::runtime_error(std::string(label) + " 缺失");
    }
    return *value;
}

std::string optional_string(const Json& object, const char* key,
                            std::string fallback = {}) {
    const Json* value = find_value(object, key);
    if (!value) {
        return fallback;
    }
    return value->get<std::string>();
}

template <class T>
void optional_number(const Json& object, const char* key, T& output) {
    const Json* value = find_value(object, key);
    if (!value) {
        return;
    }
    if (value->is_number_float()) {
        output = static_cast<T>(value->get<double>());
        return;
    }
    output = static_cast<T>(value->get<std::int64_t>());
}

void optional_bool(const Json& object, const char* key, bool& output) {
    const Json* value = find_value(object, key);
    if (!value) {
        return;
    }
    if (value->is_boolean()) {
        output = value->get<bool>();
        return;
    }
    if (value->is_number()) {
        output = value->get<double>() != 0.0;
        return;
    }
    if (value->is_string()) {
        const std::string text = value->get<std::string>();
        output = text == "true" || text == "1";
    }
}

std::uint32_t parse_frame(const Json& value, const char* label) {
    std::int64_t raw = 0;
    if (value.is_number_float()) {
        raw = static_cast<std::int64_t>(value.get<double>());
    } else {
        raw = value.get<std::int64_t>();
    }
    if (raw < 0) {
        throw std::runtime_error(std::string(label) + " 不能为负数");
    }
    if (raw > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(label) + " 超出范围");
    }
    return static_cast<std::uint32_t>(raw);
}

void parse_squad_slot(const Json& value, SquadSlot& slot) {
    if (!value.is_object()) {
        throw std::runtime_error("start.squad 条目必须是对象");
    }
    slot.id = optional_string(value, "id", slot.id);
    optional_number(value, "level", slot.level);
    optional_number(value, "evolve", slot.evolve);
    optional_number(value, "potential", slot.potential);
    optional_number(value, "trust", slot.trust);
    optional_number(value, "skill_index", slot.skill_index);
    optional_number(value, "skill_level", slot.skill_level);
    slot.module_id = optional_string(value, "module_id", slot.module_id);
    optional_number(value, "module_level", slot.module_level);
    if (slot.id.empty()) {
        throw std::runtime_error("start.squad.id 缺失");
    }
}

void parse_squad(const Json& value, std::vector<SquadSlot>& squad) {
    if (!value.is_array()) {
        throw std::runtime_error("start.squad 必须是数组");
    }
    for (const Json& item : value) {
        SquadSlot slot;
        parse_squad_slot(item, slot);
        squad.push_back(std::move(slot));
    }
}

void parse_string_array(const Json& value, std::vector<std::string>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("字符串数组字段必须是数组");
    }
    for (const Json& item : value) {
        output.push_back(item.get<std::string>());
    }
}

void parse_pair_array(const Json& value,
                      std::vector<std::pair<std::string, int>>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("二元组数组字段必须是数组");
    }
    for (const Json& item : value) {
        if (!item.is_array() || item.size() < 2) {
            throw std::runtime_error("二元组条目格式无效");
        }
        output.emplace_back(item[0].get<std::string>(), item[1].get<int>());
    }
}

void parse_int_triples(
    const Json& value,
    std::vector<std::tuple<std::string, std::string, int>>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("三元组数组字段必须是数组");
    }
    for (const Json& item : value) {
        if (!item.is_array() || item.size() < 3) {
            throw std::runtime_error("三元组条目格式无效");
        }
        output.emplace_back(item[0].get<std::string>(),
                            item[1].get<std::string>(), item[2].get<int>());
    }
}

void parse_outer_buffs(
    const Json& value,
    std::vector<std::tuple<std::string, std::string, double>>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("start.outer_buffs 必须是数组");
    }
    for (const Json& item : value) {
        if (!item.is_array() || item.size() < 3) {
            throw std::runtime_error("start.outer_buffs 条目格式无效");
        }
        output.emplace_back(item[0].get<std::string>(),
                            item[1].get<std::string>(), item[2].get<double>());
    }
}

void parse_char_buffs(const Json& value,
                      std::vector<BattleStart::CharBuffSpec>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("start.char_buffs 必须是数组");
    }
    for (const Json& item : value) {
        BattleStart::CharBuffSpec spec;
        spec.id = optional_string(item, "id", spec.id);
        optional_number(item, "layer", spec.layer);
        if (const Json* targets = find_value(item, "targets")) {
            parse_string_array(*targets, spec.targets);
        }
        if (spec.id.empty()) {
            throw std::runtime_error("start.char_buffs.id 缺失");
        }
        output.push_back(std::move(spec));
    }
}

void parse_masters(const Json& value, std::vector<BattleStart::MasterSpec>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("start.masters 必须是数组");
    }
    for (const Json& item : value) {
        BattleStart::MasterSpec spec;
        spec.char_id = optional_string(item, "char_id", spec.char_id);
        spec.master_id = optional_string(item, "master_id", spec.master_id);
        optional_number(item, "level", spec.level);
        output.push_back(std::move(spec));
    }
}

void parse_outer_buff_specs(const Json& value,
                            std::vector<BattleStart::OuterBuffSpec>& output) {
    if (!value.is_array()) {
        throw std::runtime_error("start.outer_buff_specs 必须是数组");
    }
    for (const Json& item : value) {
        BattleStart::OuterBuffSpec spec;
        spec.key = optional_string(item, "key", spec.key);
        if (const Json* fields = find_value(item, "fields")) {
            if (!fields->is_array()) {
                throw std::runtime_error("start.outer_buff_specs.fields 必须是数组");
            }
            for (const Json& field_value : *fields) {
                BattleStart::OuterBuffField field;
                field.key = optional_string(field_value, "key", field.key);
                field.value = optional_string(field_value, "value", field.value);
                spec.fields.push_back(std::move(field));
            }
        }
        output.push_back(std::move(spec));
    }
}

void parse_battle_start(const Json& value, BattleStart& start) {
    if (!value.is_object()) {
        throw std::runtime_error("meta.start 必须是对象");
    }
    const std::string kind = optional_string(value, "kind", "normal");
    start.rogue = kind == "rogue";
    start.stage = optional_string(value, "text", start.stage);
    start.path = optional_string(value, "path", start.path);
    if (const Json* squad = find_value(value, "squad")) {
        parse_squad(*squad, start.squad);
    }
    if (const Json* relics = find_value(value, "relics")) {
        parse_pair_array(*relics, start.relics);
    }
    if (const Json* buff_items = find_value(value, "buff_items")) {
        parse_pair_array(*buff_items, start.buff_items);
    }
    if (const Json* char_buffs = find_value(value, "char_buffs")) {
        parse_char_buffs(*char_buffs, start.char_buffs);
    }
    if (const Json* masters = find_value(value, "masters")) {
        parse_masters(*masters, start.masters);
    }
    if (const Json* extra_buffs = find_value(value, "extra_buffs")) {
        parse_int_triples(*extra_buffs, start.extra_buffs);
    }
    if (const Json* tech_buffs = find_value(value, "tech_buffs")) {
        parse_string_array(*tech_buffs, start.tech_buffs);
    }
    if (const Json* outer_buffs = find_value(value, "outer_buffs")) {
        parse_outer_buffs(*outer_buffs, start.outer_buffs);
    }
    if (const Json* specs = find_value(value, "outer_buff_specs")) {
        parse_outer_buff_specs(*specs, start.outer_buff_specs);
    }
    optional_number(value, "mode", start.mode);
    optional_number(value, "grade", start.grade);
    optional_number(value, "growth", start.growth);
    optional_number(value, "hp_bonus", start.hp_bonus);
    optional_number(value, "max_hp_bonus", start.max_hp_bonus);
    optional_number(value, "shield_bonus", start.shield_bonus);
    optional_number(value, "gold_bonus", start.gold_bonus);
    optional_number(value, "scrap_count", start.scrap_count);
    optional_number(value, "scrap_limit", start.scrap_limit);
    optional_number(value, "rogue_seed", start.rogue_seed);
    optional_bool(value, "official_extra", start.official_extra);
}

void parse_game_mode(const Json& value, GameModeConfig& game_mode) {
    if (!value.is_object()) {
        throw std::runtime_error("meta.game_mode 必须是对象");
    }
    optional_number(value, "mode_type", game_mode.mode_type);
    optional_bool(value, "is_deterministic", game_mode.is_deterministic);
    optional_bool(value, "allow_manual_tick", game_mode.allow_manual_tick);
    optional_number(value, "player_side", game_mode.player_side);
    game_mode.valid = true;
}

void parse_meta(const Json& value, ReplayMeta& meta) {
    if (!value.is_object()) {
        throw std::runtime_error("meta 必须是对象");
    }
    meta.schema = 1;
    meta.source = optional_string(value, "source", meta.source);
    meta.stage_id = optional_string(value, "stage_id", meta.stage_id);
    meta.stage_code = optional_string(value, "stage_code", meta.stage_code);
    meta.stage_name = optional_string(value, "stage_name", meta.stage_name);
    meta.level_id = optional_string(value, "level_id", meta.level_id);
    optional_number(value, "seed", meta.seed);
    optional_number(value, "seed_observed", meta.seed_observed);
    optional_number(value, "seed_applied", meta.seed_applied);
    if (const Json* game_mode = find_value(value, "game_mode")) {
        parse_game_mode(*game_mode, meta.game_mode);
    }
    optional_number(value, "created_unix_ms", meta.created_unix_ms);
    if (const Json* start = find_value(value, "start")) {
        parse_battle_start(*start, meta.start);
    }
    meta.rogue = meta.start.rogue;
    meta.start.game_mode = meta.game_mode;
}

RecordedOperation parse_operation(const Json& value) {
    if (!value.is_array() || value.size() < 2) {
        throw std::runtime_error("操作条目格式无效");
    }
    const std::string kind = value[0].get<std::string>();
    RecordedOperation operation;
    operation.unit = value[1].get<std::string>();
    if (operation.unit.empty()) {
        throw std::runtime_error("操作缺少单位 ID");
    }
    if (kind == "deploy") {
        if (value.size() < 4 || !value[3].is_array() || value[3].size() < 2) {
            throw std::runtime_error("部署操作格式无效");
        }
        operation.kind = RecordedOperation::Kind::Deploy;
        operation.direction = value[2].get<int>();
        operation.row = value[3][0].get<int>();
        operation.col = value[3][1].get<int>();
    } else if (kind == "skill") {
        operation.kind = RecordedOperation::Kind::Skill;
    } else if (kind == "withdraw") {
        operation.kind = RecordedOperation::Kind::Withdraw;
    } else {
        throw std::runtime_error("未知操作类型: " + kind);
    }
    return operation;
}

ReplayFrameOperations parse_operations_map(const Json& value) {
    ReplayFrameOperations operations;
    if (!value.is_object()) {
        throw std::runtime_error("operations 必须是对象");
    }
    for (const auto& [frame_text, list] : value.items()) {
        std::uint32_t frame = 0;
        try {
            frame = static_cast<std::uint32_t>(std::stoul(frame_text));
        } catch (...) {
            throw std::runtime_error("帧键不是非负整数: " + frame_text);
        }
        if (!list.is_array()) {
            throw std::runtime_error("第 " + frame_text + " 帧的操作数组无效");
        }
        std::vector<RecordedOperation> frame_operations;
        frame_operations.reserve(list.size());
        for (const Json& item : list) {
            frame_operations.push_back(parse_operation(item));
        }
        operations[frame] = std::move(frame_operations);
    }
    return operations;
}

std::map<std::uint32_t, std::vector<ReplayEvent>> parse_events(const Json& value) {
    std::map<std::uint32_t, std::vector<ReplayEvent>> events;
    if (!value.is_array()) {
        throw std::runtime_error("events 必须是数组");
    }
    for (const Json& item : value) {
        ReplayEvent event;
        event.frame = parse_frame(require_value(item, "frame", "events.frame"),
                                  "events.frame");
        event.actor = optional_string(item, "actor", event.actor);
        event.target = optional_string(item, "target", event.target);
        optional_number(item, "actor_uid", event.actor_uid);
        optional_number(item, "target_uid", event.target_uid);
        const int type = require_value(item, "type", "events.type").get<int>();
        if (type < 0 || type > 255) {
            throw std::runtime_error("events.type 越界");
        }
        event.type = static_cast<std::uint8_t>(type);
        optional_number(item, "actual", event.actual);
        events[event.frame].push_back(std::move(event));
    }
    return events;
}

std::map<std::uint32_t, std::vector<ReplayWorldEvent>> parse_world_events(
    const Json& value) {
    std::map<std::uint32_t, std::vector<ReplayWorldEvent>> events;
    if (!value.is_array()) {
        throw std::runtime_error("world_events 必须是数组");
    }
    for (const Json& item : value) {
        ReplayWorldEvent event;
        event.frame =
            parse_frame(require_value(item, "frame", "world_events.frame"),
                        "world_events.frame");
        const int kind = require_value(item, "kind", "world_events.kind").get<int>();
        if (kind < 0 || kind > 255) {
            throw std::runtime_error("world_events.kind 越界");
        }
        event.kind = static_cast<std::uint8_t>(kind);
        optional_number(item, "value", event.value);
        optional_number(item, "total", event.total);
        events[event.frame].push_back(std::move(event));
    }
    return events;
}

ReplayTreeNode parse_tree_node(const Json& value) {
    if (!value.is_object()) {
        throw std::runtime_error("tree.nodes 条目必须是对象");
    }
    ReplayTreeNode node;
    node.id = optional_string(value, "id", node.id);
    node.parent_id = optional_string(value, "parent_id", node.parent_id);
    node.name = optional_string(value, "name", node.name);
    optional_number(value, "fork_frame", node.fork_frame);
    optional_number(value, "end_frame", node.end_frame);
    optional_number(value, "branch", node.branch_no);
    if (const Json* operations = find_value(value, "operations")) {
        node.operations = parse_operations_map(*operations);
    }
    if (const Json* events = find_value(value, "events")) {
        node.observed_events = parse_events(*events);
    }
    if (const Json* world_events = find_value(value, "world_events")) {
        node.observed_world_events = parse_world_events(*world_events);
    }
    if (node.id.empty()) {
        throw std::runtime_error("tree.nodes.id 缺失");
    }
    return node;
}

ReplayTree parse_tree(const Json& value) {
    if (!value.is_object()) {
        throw std::runtime_error("tree 必须是对象");
    }
    ReplayTree tree;
    tree.root_id = optional_string(value, "root_id", tree.root_id);
    tree.current_id = optional_string(value, "current_id", tree.current_id);
    if (const Json* path = find_value(value, "active_path")) {
        parse_string_array(*path, tree.active_path);
    }
    const Json& nodes = require_value(value, "nodes", "tree.nodes");
    if (!nodes.is_array()) {
        throw std::runtime_error("tree.nodes 必须是数组");
    }
    tree.nodes.reserve(nodes.size());
    for (const Json& item : nodes) {
        tree.nodes.push_back(parse_tree_node(item));
    }
    return tree;
}

void validate_tree(const ReplayTree& tree) {
    if (tree.nodes.empty()) {
        throw std::runtime_error("time tree 没有节点");
    }
    std::map<std::string, const ReplayTreeNode*> nodes;
    for (const ReplayTreeNode& node : tree.nodes) {
        if (!nodes.emplace(node.id, &node).second) {
            throw std::runtime_error("时间树节点 ID 重复: " + node.id);
        }
    }
    const auto root = nodes.find(tree.root_id);
    if (root == nodes.end()) {
        throw std::runtime_error("时间树根节点不存在: " + tree.root_id);
    }
    if (!root->second->parent_id.empty()) {
        throw std::runtime_error("时间树根节点不能有父节点");
    }
    for (const auto& [id, node] : nodes) {
        if (id != tree.root_id && node->parent_id.empty()) {
            throw std::runtime_error("时间树存在多个根节点: " + id);
        }
        if (!node->parent_id.empty() &&
            nodes.find(node->parent_id) == nodes.end()) {
            throw std::runtime_error("时间树节点父级不存在: " + id + " -> " +
                                     node->parent_id);
        }
        if (node->end_frame < node->fork_frame) {
            throw std::runtime_error("时间树节点结束帧早于起始帧: " + id);
        }
        std::unordered_set<std::string> visited;
        const ReplayTreeNode* cursor = node;
        while (cursor && cursor->id != tree.root_id) {
            if (!visited.insert(cursor->id).second) {
                throw std::runtime_error("时间树存在循环: " + id);
            }
            const auto parent = nodes.find(cursor->parent_id);
            cursor = parent == nodes.end() ? nullptr : parent->second;
        }
        if (!cursor) {
            throw std::runtime_error("时间树节点未连接到根: " + id);
        }
    }
    if (nodes.find(tree.current_id) == nodes.end()) {
        throw std::runtime_error("当前节点不存在: " + tree.current_id);
    }
    if (tree.active_path.empty() || tree.active_path.front() != tree.root_id ||
        tree.active_path.back() != tree.current_id) {
        throw std::runtime_error("active_path 不是当前节点的完整根路径");
    }
    for (std::size_t index = 1; index < tree.active_path.size(); ++index) {
        const auto node = nodes.find(tree.active_path[index]);
        if (node == nodes.end() ||
            node->second->parent_id != tree.active_path[index - 1]) {
            throw std::runtime_error("active_path 节点链不连续");
        }
    }
}

std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> build_plan(
    const ReplayTree& tree) {
    std::map<std::string, const ReplayTreeNode*> nodes;
    for (const ReplayTreeNode& node : tree.nodes) {
        nodes.emplace(node.id, &node);
    }
    std::map<std::uint32_t, std::vector<RecordedOperation>> merged;
    for (const std::string& id : tree.active_path) {
        const auto node = nodes.find(id);
        if (node == nodes.end()) {
            continue;
        }
        for (const auto& [frame, operations] : node->second->operations) {
            auto& target = merged[frame];
            target.insert(target.end(), operations.begin(), operations.end());
        }
    }
    std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> plan;
    plan.reserve(merged.size());
    for (auto& [frame, operations] : merged) {
        plan.emplace_back(frame, std::move(operations));
    }
    return plan;
}

void append_replay_events(std::vector<ReplayEvent>& target,
                          const std::vector<ReplayEvent>& source) {
    std::unordered_map<std::string, std::vector<std::size_t>> slots;
    for (std::size_t index = 0; index < target.size(); ++index) {
        slots[replay_event_key(target[index])].push_back(index);
    }
    std::unordered_map<std::string, int> seen;
    for (const ReplayEvent& event : source) {
        const std::string key = replay_event_key(event);
        const int ordinal = seen[key]++;
        const auto slot = slots.find(key);
        if (slot != slots.end() &&
            static_cast<std::size_t>(ordinal) < slot->second.size()) {
            // Older recordings do not carry runtime UIDs. When the live replay
            // reproduces the same event, fill the identity into the
            // already-present row so the timeline can show the exact serial.
            ReplayEvent& existing = target[slot->second[ordinal]];
            if (!existing.actor_uid) {
                existing.actor_uid = event.actor_uid;
            }
            if (!existing.target_uid) {
                existing.target_uid = event.target_uid;
            }
            continue;
        }
        target.push_back(event);
        slots[key].push_back(target.size() - 1);
    }
}

void append_replay_world_events(std::vector<ReplayWorldEvent>& target,
                                const std::vector<ReplayWorldEvent>& source) {
    std::unordered_map<std::string, int> counts;
    for (const ReplayWorldEvent& event : target) {
        ++counts[replay_world_event_key(event)];
    }
    std::unordered_map<std::string, int> seen;
    for (const ReplayWorldEvent& event : source) {
        const std::string key = replay_world_event_key(event);
        if (++seen[key] > counts[key]) {
            target.push_back(event);
            ++counts[key];
        }
    }
}

Json pair_array_to_json(const std::vector<std::pair<std::string, int>>& values) {
    Json out = Json::array();
    for (const auto& [key, value] : values) {
        out.push_back(Json::array({key, value}));
    }
    return out;
}

Json squad_to_json(const std::vector<SquadSlot>& squad) {
    Json out = Json::array();
    for (const SquadSlot& slot : squad) {
        out.push_back({
            {"id", slot.id},
            {"level", slot.level},
            {"evolve", slot.evolve},
            {"potential", slot.potential},
            {"trust", slot.trust},
            {"skill_index", slot.skill_index},
            {"skill_level", slot.skill_level},
            {"module_id", slot.module_id},
            {"module_level", slot.module_level},
        });
    }
    return out;
}

Json battle_start_to_json(const BattleStart& start) {
    Json out;
    out["kind"] = start.rogue ? "rogue" : "normal";
    out["text"] = start.stage;
    out["path"] = start.path;
    out["squad"] = squad_to_json(start.squad);
    out["relics"] = pair_array_to_json(start.relics);
    out["buff_items"] = pair_array_to_json(start.buff_items);
    Json char_buffs = Json::array();
    for (const BattleStart::CharBuffSpec& spec : start.char_buffs) {
        char_buffs.push_back({
            {"id", spec.id},
            {"layer", spec.layer},
            {"targets", string_array_to_json(spec.targets)},
        });
    }
    out["char_buffs"] = std::move(char_buffs);
    Json masters = Json::array();
    for (const BattleStart::MasterSpec& spec : start.masters) {
        masters.push_back({
            {"char_id", spec.char_id},
            {"master_id", spec.master_id},
            {"level", spec.level},
        });
    }
    out["masters"] = std::move(masters);
    Json extra_buffs = Json::array();
    for (const auto& [kind, id, layer] : start.extra_buffs) {
        extra_buffs.push_back(Json::array({kind, id, layer}));
    }
    out["extra_buffs"] = std::move(extra_buffs);
    out["tech_buffs"] = string_array_to_json(start.tech_buffs);
    Json outer_buffs = Json::array();
    for (const auto& [key, field, value] : start.outer_buffs) {
        outer_buffs.push_back(Json::array({key, field, value}));
    }
    out["outer_buffs"] = std::move(outer_buffs);
    Json outer_buff_specs = Json::array();
    for (const BattleStart::OuterBuffSpec& spec : start.outer_buff_specs) {
        Json fields = Json::array();
        for (const BattleStart::OuterBuffField& field : spec.fields) {
            fields.push_back({
                {"key", field.key},
                {"value", field.value},
            });
        }
        outer_buff_specs.push_back({
            {"key", spec.key},
            {"fields", std::move(fields)},
        });
    }
    out["outer_buff_specs"] = std::move(outer_buff_specs);
    out["mode"] = start.mode;
    out["grade"] = start.grade;
    out["growth"] = start.growth;
    out["hp_bonus"] = start.hp_bonus;
    out["max_hp_bonus"] = start.max_hp_bonus;
    out["shield_bonus"] = start.shield_bonus;
    out["gold_bonus"] = start.gold_bonus;
    out["scrap_count"] = start.scrap_count;
    out["scrap_limit"] = start.scrap_limit;
    out["rogue_seed"] = start.rogue_seed;
    out["official_extra"] = start.official_extra;
    return out;
}

} // namespace

std::string replay_event_key(const ReplayEvent& event) {
    std::ostringstream out;
    out << event.actor << '\x1f' << event.target << '\x1f'
        << static_cast<unsigned>(event.type) << '\x1f'
        << std::llround(event.actual * 10.0);
    return out.str();
}

std::string replay_world_event_key(const ReplayWorldEvent& event) {
    return std::to_string(static_cast<unsigned>(event.kind)) + ':' +
           std::to_string(event.value) + ':' + std::to_string(event.total);
}

bool parse_replay_document(const std::string& text, ReplayDocument& document,
                           std::string* error) {
    try {
        const Json root = Json::parse(text);
        if (!root.is_object()) {
            throw std::runtime_error("根节点必须是对象");
        }
        const Json& schema = require_value(root, "schema", "schema");
        if (!schema.is_number_integer() || schema.get<int>() != 1) {
            throw std::runtime_error("只支持 schema=1 的录像格式");
        }
        const Json& meta = require_value(root, "meta", "meta");
        const Json& tree = require_value(root, "tree", "tree");

        ReplayDocument parsed;
        parse_meta(meta, parsed.meta);
        parsed.tree = parse_tree(tree);
        validate_tree(parsed.tree);
        parsed.plan = build_plan(parsed.tree);
        document = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = exception.what();
        }
        return false;
    }
}

Json operations_to_json(const ReplayFrameOperations& operations,
                        std::uint32_t maximum_frame) {
    Json out = Json::object();
    for (const auto& [frame, frame_operations] : operations) {
        if (frame > maximum_frame) {
            continue;
        }
        Json list = Json::array();
        for (const RecordedOperation& operation : frame_operations) {
            Json item = Json::array();
            switch (operation.kind) {
                case RecordedOperation::Kind::Deploy:
                    item = Json::array(
                        {"deploy", operation.unit, operation.direction,
                         Json::array({operation.row, operation.col})});
                    break;
                case RecordedOperation::Kind::Skill:
                    item = Json::array({"skill", operation.unit});
                    break;
                case RecordedOperation::Kind::Withdraw:
                    item = Json::array({"withdraw", operation.unit});
                    break;
            }
            list.push_back(std::move(item));
        }
        out[std::to_string(frame)] = std::move(list);
    }
    return out;
}

Json meta_to_json(const ReplayMeta& meta) {
    Json out;
    out["source"] = meta.source;
    out["stage_id"] = meta.stage_id;
    out["stage_code"] = meta.stage_code;
    out["stage_name"] = meta.stage_name;
    out["level_id"] = meta.level_id;
    out["seed"] = meta.seed;
    out["seed_observed"] = meta.seed_observed;
    out["seed_applied"] = meta.seed_applied;
    out["rogue"] = meta.rogue;
    out["game_mode"] = {
        {"mode_type", meta.game_mode.mode_type},
        {"is_deterministic", meta.game_mode.is_deterministic},
        {"allow_manual_tick", meta.game_mode.allow_manual_tick},
        {"player_side", meta.game_mode.player_side},
    };
    out["created_unix_ms"] = meta.created_unix_ms;
    out["start"] = battle_start_to_json(meta.start);
    return out;
}

Json string_array_to_json(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const std::string& value : values) {
        out.push_back(value);
    }
    return out;
}

Json nodes_to_json(const std::vector<ReplayTreeNode>& nodes, bool include_events) {
    Json out = Json::array();
    for (const ReplayTreeNode& node : nodes) {
        Json item;
        item["id"] = node.id;
        item["parent_id"] = node.parent_id;
        item["name"] = node.name;
        item["fork_frame"] = node.fork_frame;
        item["end_frame"] = node.end_frame;
        item["branch"] = node.branch_no;
        item["operations"] = operations_to_json(node.operations);
        if (include_events) {
            Json events = Json::array();
            for (const auto& [frame, values] : merged_node_events(node)) {
                for (const ReplayEvent& event : values) {
                    events.push_back({
                        {"frame", frame},
                        {"actor", event.actor},
                        {"target", event.target},
                        {"actor_uid", event.actor_uid},
                        {"target_uid", event.target_uid},
                        {"type", static_cast<unsigned>(event.type)},
                        {"actual", event.actual},
                    });
                }
            }
            item["events"] = std::move(events);
            Json world_events = Json::array();
            for (const auto& [frame, values] : merged_node_world_events(node)) {
                for (const ReplayWorldEvent& event : values) {
                    world_events.push_back({
                        {"frame", frame},
                        {"kind", static_cast<unsigned>(event.kind)},
                        {"value", event.value},
                        {"total", event.total},
                    });
                }
            }
            item["world_events"] = std::move(world_events);
        }
        out.push_back(std::move(item));
    }
    return out;
}

Json replay_document_to_json(const ReplayMeta& meta, const ReplayTree& tree,
                             bool include_events) {
    Json document;
    document["schema"] = 1;
    document["meta"] = meta_to_json(meta);
    document["tree"] = {
        {"root_id", tree.root_id},
        {"current_id", tree.current_id},
        {"active_path", string_array_to_json(tree.active_path)},
        {"nodes", nodes_to_json(tree.nodes, include_events)},
    };
    return document;
}

std::map<std::uint32_t, std::vector<ReplayEvent>> merged_node_events(
    const ReplayTreeNode& node) {
    std::map<std::uint32_t, std::vector<ReplayEvent>> merged;
    for (const auto& [frame, values] : node.expected_events) {
        append_replay_events(merged[frame], values);
    }
    for (const auto& [frame, values] : node.observed_events) {
        append_replay_events(merged[frame], values);
    }
    return merged;
}

std::map<std::uint32_t, std::vector<ReplayWorldEvent>> merged_node_world_events(
    const ReplayTreeNode& node) {
    std::map<std::uint32_t, std::vector<ReplayWorldEvent>> merged;
    for (const auto& [frame, values] : node.expected_world_events) {
        append_replay_world_events(merged[frame], values);
    }
    for (const auto& [frame, values] : node.observed_world_events) {
        append_replay_world_events(merged[frame], values);
    }
    return merged;
}

} // namespace ark::better_battle
