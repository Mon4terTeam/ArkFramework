#pragma once

#include "BetterBattle.h"

#include <nlohmann/json.hpp>

#include <map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ark::better_battle {

using ReplayFrameOperations =
    std::map<std::uint32_t, std::vector<RecordedOperation>>;

struct ReplayDocument {
    ReplayMeta meta;
    ReplayTree tree;
    std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> plan;
};

// 解析整份录像文档。error 保留底层解析信息，由调用方补中文前缀。
bool parse_replay_document(const std::string& text, ReplayDocument& document,
                           std::string* error);

// 以下序列化函数与解析共用同一份字段定义。
nlohmann::ordered_json operations_to_json(const ReplayFrameOperations& operations,
                                          std::uint32_t maximum_frame =
                                              std::numeric_limits<std::uint32_t>::max());
nlohmann::ordered_json meta_to_json(const ReplayMeta& meta);
nlohmann::ordered_json string_array_to_json(const std::vector<std::string>& values);
nlohmann::ordered_json nodes_to_json(const std::vector<ReplayTreeNode>& nodes,
                                     bool include_events);
nlohmann::ordered_json replay_document_to_json(const ReplayMeta& meta,
                                               const ReplayTree& tree,
                                               bool include_events);

std::string replay_event_key(const ReplayEvent& event);
std::string replay_world_event_key(const ReplayWorldEvent& event);

std::map<std::uint32_t, std::vector<ReplayEvent>> merged_node_events(
    const ReplayTreeNode& node);
std::map<std::uint32_t, std::vector<ReplayWorldEvent>> merged_node_world_events(
    const ReplayTreeNode& node);

} // namespace ark::better_battle
