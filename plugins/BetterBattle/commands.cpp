#include "battle_helpers.h"

#include <cctype>
#include <string_view>

namespace ark::better_battle {
namespace {

std::string percent_decode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int high = hex(text[i + 1]);
            const int low = hex(text[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::vector<std::string> split_text(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        const std::size_t length =
            end == std::string::npos ? text.size() - start : end - start;
        if (length) {
            parts.push_back(text.substr(start, length));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::vector<std::string> split_keep_empty(const std::string& text,
                                          char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

bool parse_plan_text(
    const std::string& text,
    std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>>*
        plan,
    std::string* error) {
    if (!plan || !error) {
        return false;
    }
    plan->clear();
    std::map<std::uint32_t, std::vector<RecordedOperation>> merged;
    for (const std::string& entry : split_text(text, ';')) {
        const std::vector<std::string> fields = split_text(entry, ':');
        if (fields.size() < 3) {
            *error = "操作格式应为 frame:kind:unit";
            return false;
        }
        std::uint32_t frame = 0;
        try {
            frame = static_cast<std::uint32_t>(std::stoul(fields[0]));
        } catch (...) {
            *error = "操作帧号无效: " + fields[0];
            return false;
        }
        RecordedOperation operation;
        if (fields[1] == "deploy") {
            if (fields.size() != 6) {
                *error = "部署格式应为 frame:deploy:unit:dir:row:col";
                return false;
            }
            operation.kind = RecordedOperation::Kind::Deploy;
            operation.unit = fields[2];
            try {
                operation.direction = std::stoi(fields[3]);
                operation.row = std::stoi(fields[4]);
                operation.col = std::stoi(fields[5]);
            } catch (...) {
                *error = "部署参数无效: " + entry;
                return false;
            }
        } else if (fields[1] == "skill") {
            if (fields.size() != 3) {
                *error = "技能格式应为 frame:skill:unit";
                return false;
            }
            operation.kind = RecordedOperation::Kind::Skill;
            operation.unit = fields[2];
        } else if (fields[1] == "withdraw") {
            if (fields.size() != 3) {
                *error = "撤退格式应为 frame:withdraw:unit";
                return false;
            }
            operation.kind = RecordedOperation::Kind::Withdraw;
            operation.unit = fields[2];
        } else {
            *error = "未知操作类型: " + fields[1];
            return false;
        }
        merged[frame].push_back(std::move(operation));
    }
    plan->reserve(merged.size());
    for (auto& [frame, operations] : merged) {
        plan->emplace_back(frame, std::move(operations));
    }
    return true;
}

int parse_layer_suffix(const std::string& text, std::string* id) {
    const std::size_t separator = text.rfind('@');
    if (separator == std::string::npos) {
        *id = text;
        return 1;
    }
    *id = text.substr(0, separator);
    try {
        return std::clamp(std::stoi(text.substr(separator + 1)), 0, 9);
    } catch (...) {
        return 1;
    }
}

std::vector<SquadSlot> parse_roster(const std::string& roster) {
    std::vector<SquadSlot> squad;
    std::size_t begin = 0;
    while (begin <= roster.size() && !roster.empty()) {
        const std::size_t end = roster.find(',', begin);
        const std::string item = roster.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!item.empty()) {
            SquadSlot slot;
            std::istringstream fields(item);
            std::string field;
            std::vector<std::string> values;
            while (std::getline(fields, field, ':')) {
                values.push_back(field);
            }
            if (values.empty() || values[0].empty()) {
                throw std::runtime_error("编队项缺少干员 ID");
            }
            slot.id = values[0];
            if (values.size() > 1) slot.level = std::stoi(values[1]);
            if (values.size() > 2) slot.evolve = std::stoi(values[2]);
            if (values.size() > 3) slot.potential = std::stoi(values[3]);
            if (values.size() > 4) slot.skill_index = std::stoi(values[4]) - 1;
            if (values.size() > 5) slot.skill_level = std::stoi(values[5]);
            if (values.size() > 6) slot.module_id = values[6];
            if (values.size() > 7 && !values[7].empty()) {
                slot.module_level = std::stoi(values[7]);
            }
            if (values.size() > 8 && !values[8].empty()) {
                slot.trust = std::stoi(values[8]);
            }
            slot.level = std::clamp(slot.level, 1, 90);
            slot.evolve = std::clamp(slot.evolve, 0, 2);
            slot.potential = std::clamp(slot.potential, 0, 6);
            slot.trust = std::clamp(slot.trust, 0, 200);
            slot.skill_index = std::clamp(slot.skill_index, 0, 2);
            slot.skill_level = std::clamp(slot.skill_level, 1, 10);
            slot.module_level = std::clamp(slot.module_level, 1, 3);
            squad.push_back(std::move(slot));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return squad;
}

std::string strip_quotes(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

struct CommandSpec {
    std::string_view name;
    void (LocalBattle::*handler)(Args&);
};



} // namespace

bool Args::empty() const {
    return position_ >= text_.size();
}

std::string Args::next() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
    }
    if (position_ >= text_.size()) {
        return {};
    }
    const std::size_t start = position_;
    while (position_ < text_.size() &&
           !std::isspace(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
    }
    return text_.substr(start, position_ - start);
}

std::string Args::rest() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
    }
    std::string value = text_.substr(position_);
    position_ = text_.size();
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return last == std::string::npos ? std::string{} : value.substr(0, last + 1);
}

int Args::integer() {
    return std::stoi(next());
}

std::uint32_t Args::uid() {
    return static_cast<std::uint32_t>(std::stoul(next()));
}

void LocalBattle::run_command(const Command& command) {
    static constexpr CommandSpec kCommands[] = {
    {"start", &LocalBattle::cmd_start},
    {"deploy", &LocalBattle::cmd_deploy},
    {"tiles", &LocalBattle::cmd_tiles},
    {"skill", &LocalBattle::cmd_skill},
    {"skill-state", &LocalBattle::cmd_skill_state},
    {"skill-methods", &LocalBattle::cmd_skill_methods},
    {"type-inspect", &LocalBattle::cmd_type_inspect},
    {"type-search", &LocalBattle::cmd_type_search},
    {"object-scan", &LocalBattle::cmd_object_scan},
    {"static-scan", &LocalBattle::cmd_static_scan},
    {"ref-scan", &LocalBattle::cmd_ref_scan},
    {"root-bfs", &LocalBattle::cmd_root_bfs},
    {"method-at", &LocalBattle::cmd_method_at},
    {"singleton-scan", &LocalBattle::cmd_singleton_scan},
    {"rogue-probe-extra", &LocalBattle::cmd_rogue_probe_extra},
    {"rogue-synth", &LocalBattle::cmd_rogue_synth},
    {"rogue-data", &LocalBattle::cmd_rogue_data},
    {"rogue-outbuffs", &LocalBattle::cmd_rogue_out_buffs},
    {"rogue-player-dump", &LocalBattle::cmd_rogue_player_dump},
    {"rogue-player-json", &LocalBattle::cmd_rogue_player_json},
    {"rogue-pending-detail", &LocalBattle::cmd_rogue_pending_detail},
    {"rogue-net-capture", &LocalBattle::cmd_rogue_net_capture},
    {"rogue-capture", &LocalBattle::cmd_rogue_capture},
    {"rogue-dump-battle", &LocalBattle::cmd_rogue_dump_battle},
    {"rogue-global-buffs", &LocalBattle::cmd_rogue_dump_global_buffs},
    {"rogue-global-capture", &LocalBattle::cmd_rogue_global_capture},
    {"debug-buff", &LocalBattle::cmd_debug_buff},
    {"field-type", &LocalBattle::cmd_field_type},
    {"rogue-catalog", &LocalBattle::cmd_rogue_catalog},
    {"rogue-start", &LocalBattle::cmd_rogue_start},
    {"game-mode", &LocalBattle::cmd_game_mode},
    {"buff-fields", &LocalBattle::cmd_buff_fields},
    {"buffs", &LocalBattle::cmd_buffs},
    {"buff-postborn", &LocalBattle::cmd_buff_post_born},
    {"record", &LocalBattle::cmd_record},
    {"timeline", &LocalBattle::cmd_timeline},
    {"replay", &LocalBattle::cmd_replay},
    {"trace", &LocalBattle::cmd_trace},
    {"probe", &LocalBattle::cmd_probe},
    {"withdraw", &LocalBattle::cmd_withdraw},
    {"pause", &LocalBattle::cmd_pause},
    {"resume", &LocalBattle::cmd_resume},
    {"leave", &LocalBattle::cmd_leave},
    {"enemy-focus", &LocalBattle::cmd_enemy_focus},
    {"panel", &LocalBattle::cmd_panel},
    {"enemy-outline-test", &LocalBattle::cmd_enemy_outline_test},
    {"enemy-profile", &LocalBattle::cmd_enemy_profile},
    {"enemy-waves", &LocalBattle::cmd_enemy_waves},
    {"tas-bench", &LocalBattle::cmd_tas_bench},
    {"refresh", &LocalBattle::cmd_refresh},
    {"interval", &LocalBattle::cmd_interval},
    {"catalog", &LocalBattle::cmd_catalog},
    {"asset", &LocalBattle::cmd_asset},
    {"withdraw-signature", &LocalBattle::cmd_withdraw_signature},
    {"export-web", &LocalBattle::cmd_export_web},
};
    for (const CommandSpec& spec : kCommands) {
        if (spec.name != command.name) {
            continue;
        }
        Args args(command.args);
        (this->*spec.handler)(args);
        return;
    }
    throw std::runtime_error("未知命令: " + command.name);
}

void LocalBattle::cmd_start(Args& args) {
    BattleStart start;
    start.stage = args.next();
    start.squad = parse_roster(args.next());
    {
        std::lock_guard lock(mutex_);
        scene_.stage = StageInfo{};
        scene_.stage.id = start.stage;
        scene_.stage_request = start.stage;
        squad_ = start.squad;
    }
    const int seed = tas_mode_.load(std::memory_order_acquire)
                         ? tas_seed_.load(std::memory_order_acquire)
                         : -1;
    queue_timeline_start(PendingTimelineMode::NewRoot, start, start.stage, seed,
                         {});
    start_battle(start);
}

void LocalBattle::cmd_deploy(Args& args) {
    const std::uint32_t uid = args.uid();
    const int row = args.integer();
    const int col = args.integer();
    const std::string direction = args.next();
    deploy(uid, row, col, direction.empty() ? 1 : std::stoi(direction));
}

void LocalBattle::cmd_tiles(Args& args) {
    list_tiles(args.uid());
}

void LocalBattle::cmd_skill(Args& args) {
    trigger_skill(args.uid());
}

void LocalBattle::cmd_skill_state(Args& args) {
    inspect_skill(args.uid());
}

void LocalBattle::cmd_skill_methods(Args& args) {
    list_skill_methods(args.uid());
}

void LocalBattle::cmd_type_inspect(Args& args) {
    const std::string text = args.next();
    const std::string path = args.next();
    inspect_type(text, path);
}

void LocalBattle::cmd_type_search(Args& args) {
    search_types(args.rest());
}

void LocalBattle::cmd_object_scan(Args& args) {
    scan_objects(args.next(), args.next());
}

void LocalBattle::cmd_static_scan(Args& args) {
    static_scan(args.next(), args.next());
}

void LocalBattle::cmd_ref_scan(Args& args) {
    ref_scan(args.next(), args.next());
}

void LocalBattle::cmd_root_bfs(Args& args) {
    const std::string text = args.next();
    const std::string path = args.next();
    const std::string depth = args.next();
    int value = depth.empty() ? 0 : std::stoi(depth);
    if (value <= 0) {
        value = 2;
    }
    root_bfs(text, path, value);
}

void LocalBattle::cmd_method_at(Args& args) {
    method_at(args.rest());
}

void LocalBattle::cmd_singleton_scan(Args& args) {
    singleton_scan(args.next(), args.next());
}

void LocalBattle::cmd_rogue_probe_extra(Args& args) {
    probe_rogue_extra(args.next());
}

void LocalBattle::cmd_rogue_synth(Args& args) {
    const std::string text = args.next();
    std::vector<std::tuple<std::string, std::string, int>> buffs;
    for (const std::string& item : split_text(args.next(), ',')) {
        const std::vector<std::string> fields = split_text(item, ':');
        if (fields.size() < 2) {
            continue;
        }
        buffs.emplace_back(fields[0], fields[1], 1);
    }
    rogue_synth(text, buffs);
}

void LocalBattle::cmd_rogue_data(Args& args) {
    probe_rogue_data(args.next());
}

void LocalBattle::cmd_rogue_out_buffs(Args& args) {
    rogue_out_buffs(args.rest());
}

void LocalBattle::cmd_rogue_player_dump(Args&) {
    rogue_player_dump();
}

void LocalBattle::cmd_rogue_player_json(Args&) {
    rogue_player_json();
}

void LocalBattle::cmd_rogue_pending_detail(Args&) {
    rogue_pending_detail();
}

void LocalBattle::cmd_rogue_net_capture(Args& args) {
    rogue_net_capture(args.next());
}

void LocalBattle::cmd_rogue_capture(Args& args) {
    rogue_capture(args.rest());
}

void LocalBattle::cmd_rogue_dump_battle(Args&) {
    rogue_dump_battle();
}

void LocalBattle::cmd_rogue_dump_global_buffs(Args&) {
    rogue_dump_global_buffs();
}

void LocalBattle::cmd_rogue_global_capture(Args&) {
    capture_queued_global_buffs();
}

void LocalBattle::cmd_debug_buff(Args& args) {
    debug_buff(args.next(), args.integer());
}

void LocalBattle::cmd_field_type(Args& args) {
    inspect_field_type(args.next(), args.next(), args.next());
}

void LocalBattle::cmd_rogue_catalog(Args& args) {
    rogue_catalog(args.rest());
}

void LocalBattle::cmd_rogue_start(Args& args) {
    BattleStart start;
    start.rogue = true;
    start.stage = args.next();
    start.path = args.next();
    start.squad = parse_roster(args.next());
    start.mode = 2;
    int numeric_option = 0;
    for (const std::string& option : split_text(args.rest(), ' ')) {
        if (option.rfind("--relics=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(9), ',')) {
                std::string id;
                const int layer = parse_layer_suffix(item, &id);
                if (!id.empty()) {
                    start.relics.emplace_back(std::move(id), layer);
                }
            }
            continue;
        }
        if (option.rfind("--buffitem=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(11), ',')) {
                std::string id;
                const int layer = parse_layer_suffix(item, &id);
                if (!id.empty()) {
                    start.buff_items.emplace_back(std::move(id), layer);
                }
            }
            continue;
        }
        if (option.rfind("--charbuff=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(11), ',')) {
                const std::size_t colon = item.find(':');
                const std::string head =
                    colon == std::string::npos ? item : item.substr(0, colon);
                std::string id;
                const int layer = parse_layer_suffix(head, &id);
                if (id.empty()) {
                    continue;
                }
                BattleStart::CharBuffSpec spec;
                spec.id = std::move(id);
                spec.layer = layer;
                if (colon != std::string::npos) {
                    for (const std::string& target :
                         split_text(item.substr(colon + 1), '|')) {
                        if (!target.empty()) {
                            spec.targets.push_back(target);
                        }
                    }
                }
                start.char_buffs.push_back(std::move(spec));
            }
            continue;
        }
        if (option.rfind("--master=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(9), ',')) {
                const std::size_t colon = item.find(':');
                if (colon == std::string::npos || colon == 0) {
                    continue;
                }
                std::string master_id;
                const int level =
                    parse_layer_suffix(item.substr(colon + 1), &master_id);
                if (master_id.empty()) {
                    continue;
                }
                BattleStart::MasterSpec spec;
                spec.char_id = item.substr(0, colon);
                spec.master_id = std::move(master_id);
                spec.level = std::clamp(level, 1, 3);
                start.masters.push_back(std::move(spec));
            }
            continue;
        }
        if (option.rfind("--extra=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(8), ',')) {
                const std::vector<std::string> fields = split_text(item, ':');
                if (fields.size() < 2) {
                    continue;
                }
                int layer = 1;
                if (fields.size() >= 3) {
                    try {
                        layer = std::clamp(std::stoi(fields[2]), 0, 9);
                    } catch (...) {
                    }
                }
                start.extra_buffs.emplace_back(fields[0], fields[1], layer);
            }
            continue;
        }
        if (option.rfind("--tech=", 0) == 0) {
            for (const std::string& id : split_text(option.substr(7), ',')) {
                if (!id.empty()) {
                    start.tech_buffs.push_back(id);
                }
            }
            continue;
        }
        if (option.rfind("--outer=", 0) == 0) {
            for (const std::string& item : split_text(option.substr(8), ',')) {
                const std::vector<std::string> fields = split_text(item, ':');
                if (fields.size() < 3) {
                    continue;
                }
                try {
                    start.outer_buffs.emplace_back(fields[0], fields[1],
                                                   std::stod(fields[2]));
                } catch (...) {
                }
            }
            continue;
        }
        if (option.rfind("--outerbuff=", 0) == 0) {
            const std::vector<std::string> sections =
                split_keep_empty(option.substr(12), '|');
            if (sections.size() < 2 || sections[0].empty()) {
                continue;
            }
            BattleStart::OuterBuffSpec spec;
            spec.key = sections[0];
            if (!sections[1].empty()) {
                spec.fields.push_back({"key", percent_decode(sections[1])});
            }
            if (sections.size() > 2) {
                for (const std::string& field : split_text(sections[2], ';')) {
                    const std::size_t equals = field.find('=');
                    if (equals == std::string::npos || equals == 0) {
                        continue;
                    }
                    spec.fields.push_back(
                        {field.substr(0, equals),
                         percent_decode(field.substr(equals + 1))});
                }
            }
            if (!spec.fields.empty()) {
                start.outer_buff_specs.push_back(std::move(spec));
            }
            continue;
        }
        if (option == "--official-extra") {
            start.official_extra = true;
            continue;
        }
        if (option.rfind("--seed=", 0) == 0) {
            try {
                start.rogue_seed = std::stoi(option.substr(7));
            } catch (...) {
            }
            continue;
        }
        if (option.rfind("--zone=", 0) == 0) {
            try {
                start.zone =
                    std::clamp(std::stoi(option.substr(7)), 1, 99);
            } catch (...) {
            }
            continue;
        }
        if (option.rfind("--hp=", 0) == 0 || option.rfind("--maxhp=", 0) == 0 ||
            option.rfind("--shield=", 0) == 0 ||
            option.rfind("--gold=", 0) == 0 ||
            option.rfind("--scrap=", 0) == 0 ||
            option.rfind("--scraplimit=", 0) == 0) {
            const std::size_t equals = option.find('=');
            try {
                const int value =
                    std::clamp(std::stoi(option.substr(equals + 1)), 0, 9999);
                if (option.rfind("--hp=", 0) == 0) {
                    start.hp_bonus = value;
                } else if (option.rfind("--maxhp=", 0) == 0) {
                    start.max_hp_bonus = value;
                } else if (option.rfind("--shield=", 0) == 0) {
                    start.shield_bonus = value;
                } else if (option.rfind("--scrap=", 0) == 0) {
                    start.scrap_count = value;
                } else if (option.rfind("--scraplimit=", 0) == 0) {
                    start.scrap_limit = value;
                } else {
                    start.gold_bonus = value;
                }
            } catch (...) {
            }
            continue;
        }
        try {
            const int value = std::stoi(option);
            if (numeric_option == 0) {
                start.mode = value;
            } else if (numeric_option == 1) {
                start.grade = value;
            } else if (numeric_option == 2) {
                start.growth = value;
            }
            ++numeric_option;
        } catch (...) {
        }
    }
    const int seed = tas_mode_.load(std::memory_order_acquire)
                         ? tas_seed_.load(std::memory_order_acquire)
                         : -1;
    queue_timeline_start(PendingTimelineMode::NewRoot, start, start.path, seed,
                         {});
    rogue_start(start);
}

void LocalBattle::cmd_game_mode(Args&) {
    game_mode_info();
}

void LocalBattle::cmd_buff_fields(Args& args) {
    inspect_buff_fields(args.uid());
}

void LocalBattle::cmd_buffs(Args& args) {
    dump_buffs(args.uid());
}

void LocalBattle::cmd_buff_post_born(Args& args) {
    const std::uint32_t uid = args.uid();
    dispatch_buff_post_born(uid, args.next());
}

void LocalBattle::cmd_record(Args& args) {
    const std::string action = args.next();
    if (action == "start") {
        start_recording();
    } else if (action == "stop") {
        stop_recording();
    } else if (action == "write") {
        write_recording();
        publish_timeline();
        publish_result("操作录制已写入 battle_operations.json 与 battle_timeline.json");
    } else {
        throw std::runtime_error("record 需要 start、stop 或 write");
    }
}

void LocalBattle::cmd_timeline(Args& args) {
    const std::string action = args.next();
    std::string name;
    std::string label;
    bool auto_play = false;
    int value = 0;
    if (action == "jump") {
        value = args.integer();
        std::string argument = args.next();
        if (argument == "fast" || argument == "normal" ||
            argument == "play") {
            if (argument == "fast") {
                label = "fast";
            } else if (argument == "play") {
                auto_play = true;
            }
        } else if (!argument.empty()) {
            name = argument;
            argument = args.next();
        }
        if (argument == "fast") {
            label = "fast";
        } else if (argument == "normal") {
            label.clear();
        } else if (argument == "play") {
            auto_play = true;
        } else if (!argument.empty()) {
            throw std::runtime_error("未知 timeline jump 参数: " + argument);
        }
        for (std::string extra = args.next(); !extra.empty();
             extra = args.next()) {
            if (extra == "play") {
                auto_play = true;
            } else {
                throw std::runtime_error("未知 timeline jump 参数: " + extra);
            }
        }
    } else if (action == "advance") {
        value = args.integer();
    } else if (action == "branch") {
        name = args.next();
    } else if (action == "rename") {
        name = args.next();
        label = args.rest();
    } else if (action == "delete") {
        name = args.next();
    } else if (action != "status") {
        throw std::runtime_error(
            "timeline 需要 jump <frame> [node] [fast]、advance <frames>、"
            "branch <node>、status、"
            "rename <node> <name> 或 delete <node>");
    }
    timeline_action(action, name, label, value, auto_play);
}

void LocalBattle::cmd_replay(Args& args) {
    const std::string action = args.next();
    if (action == "start") {
        start_replay(strip_quotes(args.rest()));
        return;
    }
    if (action == "seek") {
        const int frame = args.integer();
        std::string node_id;
        std::string mode = args.next();
        if (!mode.empty() && mode != "fast") {
            node_id = mode;
            mode = args.next();
        }
        replay_seek(static_cast<std::uint32_t>(std::max(frame, 0)),
                    mode == "fast", node_id);
        return;
    }
    if (action == "plan") {
        std::string first = args.next();
        std::string origin;
        std::string text = first;
        if (!first.empty() && first.front() == '@') {
            origin = first.substr(1);
            text = args.next();
        }
        std::uint32_t stop = 0;
        bool fast = false;
        for (std::string extra = args.next(); !extra.empty();
             extra = args.next()) {
            if (extra == "fast") {
                fast = true;
                continue;
            }
            if (extra.rfind("stop=", 0) == 0) {
                stop = static_cast<std::uint32_t>(
                    std::stoul(extra.substr(5)));
                continue;
            }
            throw std::runtime_error("未知 replay plan 参数: " + extra);
        }
        if (text.empty()) {
            throw std::runtime_error(
                "replay plan 需要 frame:ops 操作串");
        }
        std::vector<std::pair<std::uint32_t,
                              std::vector<RecordedOperation>>>
            plan;
        std::string error;
        if (!parse_plan_text(text, &plan, &error)) {
            throw std::runtime_error(error);
        }
        ReplayMeta meta;
        {
            std::lock_guard lock(record_mutex_);
            meta = replay_meta_locked();
            if (origin.empty()) {
                origin = playback_path_.empty()
                             ? timeline_current_id_
                             : playback_path_.back();
            }
            if (!origin.empty() &&
                timeline_nodes_.find(origin) == timeline_nodes_.end()) {
                throw std::runtime_error("时间轴里找不到分支节点: " +
                                         origin);
            }
        }
        start_replay_plan(meta,
                          origin.empty() ? std::string("plan://inline")
                                         : "plan://" + origin,
                          {}, std::move(plan), stop, fast, origin, {}, false);
        return;
    }
    if (action == "simulate") {
        const int frame = args.integer();
        const std::string kind = args.next();
        RecordedOperation operation;
        if (kind == "deploy") {
            operation.kind = RecordedOperation::Kind::Deploy;
            operation.unit = args.next();
            operation.direction = args.integer();
            operation.row = args.integer();
            operation.col = args.integer();
        } else if (kind == "skill") {
            operation.kind = RecordedOperation::Kind::Skill;
            operation.unit = args.next();
        } else if (kind == "withdraw") {
            operation.kind = RecordedOperation::Kind::Withdraw;
            operation.unit = args.next();
        } else {
            throw std::runtime_error(
                "replay simulate 需要 deploy、skill 或 withdraw");
        }
        if (operation.unit.empty()) {
            throw std::runtime_error("replay simulate 缺少干员 ID");
        }
        record_operation(operation,
                         static_cast<std::uint32_t>(std::max(frame, 0)));
        publish_result("已模拟 " + std::to_string(std::max(frame, 0)) +
                       " 帧操作：" + kind + " " + operation.unit);
        return;
    }
    if (action == "save") {
        std::string path = args.rest();
        bool include_events = false;
        const std::string suffix = " events";
        if (path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            path.erase(path.size() - suffix.size());
            const std::size_t trim = path.find_last_not_of(" \t\r\n");
            path = trim == std::string::npos ? std::string{} : path.substr(0, trim + 1);
            include_events = true;
        }
        save_replay_file(strip_quotes(std::move(path)), include_events);
        return;
    }
    if (action == "stop") {
        stop_replay();
        return;
    }
    throw std::runtime_error(
        "replay 需要 start、plan、seek、simulate、save 或 stop");
}

void LocalBattle::cmd_trace(Args& args) {
    const std::string action = args.next();
    if (action == "start") {
        start_trace(args.next());
    } else if (action == "stop") {
        stop_trace();
    } else {
        throw std::runtime_error("trace 需要 start 或 stop");
    }
}

void LocalBattle::cmd_probe(Args& args) {
    const std::string action = args.next();
    if (action == "start") {
        const std::string interval = args.next();
        start_probe(interval.empty() ? 10 : std::stoi(interval));
        return;
    }
    if (action == "stop") {
        stop_probe();
        return;
    }
    if (action == "snapshot") {
        std::vector<void*> controllers =
            find_controllers(runtime_, classes_.battle_controller);
        if (controllers.empty()) {
            publish_result("当前不在战斗场景", false);
        } else {
            probe_snapshot(controllers.front(), true);
            publish_result("探针快照已写入 battle_probe.jsonl");
        }
        return;
    }
    if (action == "status") {
        publish_result(std::string("探针 ") +
                       (probe_active_.load() ? "运行中" : "未运行") +
                       "，实体=" + std::to_string(probe_entities_.size()) +
                       "，地块=" + std::to_string(probe_tiles_.size()) +
                       "，间隔=" +
                       std::to_string(probe_interval_frames_.load()) + " 帧");
        return;
    }
    throw std::runtime_error("probe 需要 start [帧间隔]、stop、snapshot 或 status");
}

void LocalBattle::cmd_withdraw(Args& args) {
    withdraw(args.uid());
}

void LocalBattle::cmd_pause(Args&) {
    cancel_timeline_fast_forward();
    set_paused(true);
}

void LocalBattle::cmd_resume(Args&) {
    set_paused(false);
}

void LocalBattle::cmd_leave(Args&) {
    leave_battle();
}

void LocalBattle::cmd_enemy_focus(Args& args) {
    const std::uint32_t uid = args.uid();
    const std::string duration = args.next();
    focus_enemy(uid, duration.empty() ? 0 : std::stoi(duration));
}

void LocalBattle::cmd_enemy_outline_test(Args& args) {
    test_enemy_outline(args.uid());
}

void LocalBattle::cmd_enemy_profile(Args& args) {
    const std::string id = args.next();
    if (id.empty()) {
        throw std::runtime_error("enemy-profile 需要 <enemyId>");
    }
    enemy_profile_probe(id);
}

void LocalBattle::cmd_enemy_waves(Args&) {
    enemy_wave_probe();
}

void LocalBattle::cmd_tas_bench(Args& args) {
    const std::string text = args.next();
    int value = -1;
    std::string path;
    if (text == "seek") {
        value = args.integer();
        path = strip_quotes(args.rest());
    } else {
        value = args.integer();
    }
    tas_bench(text, path, value);
}

void LocalBattle::cmd_refresh(Args&) {
    refresh_battle();
}

void LocalBattle::cmd_interval(Args& args) {
    const int milliseconds = args.integer();
    const std::string frames_text = args.next();
    const int frames = frames_text.empty() ? 3 : std::stoi(frames_text);
    refresh_interval_ms_.store(static_cast<std::uint64_t>(milliseconds),
                               std::memory_order_relaxed);
    refresh_frame_interval_.store(static_cast<std::uint32_t>(frames),
                                  std::memory_order_relaxed);
    publish_result("动态快照间隔已设置为 " + std::to_string(milliseconds) +
                   " ms 或 " + std::to_string(frames) + " 个逻辑帧（取先到）");
}

void LocalBattle::cmd_catalog(Args&) {
    load_catalog();
}

void LocalBattle::cmd_asset(Args& args) {
    const std::string kind = args.next();
    const std::string id = args.next();
    export_asset(kind, id);
}

void LocalBattle::cmd_withdraw_signature(Args&) {
    describe_withdraw();
}

void LocalBattle::cmd_export_web(Args& args) {
    export_web(args.rest());
}

} // namespace ark::better_battle
