#pragma once

#include "BetterBattle.h"
#include "battle_helpers.h"
#include "../common/il2cpp_exports.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ark::better_battle::probe_support {

inline void* read_object_field(il2cpp::Runtime& runtime, void* object,
                               void* klass, const char* name) {
    if (!object || !klass) {
        return nullptr;
    }
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return nullptr;
    }
    auto offset = field_offset_cache().find(field);
    if (offset == field_offset_cache().end()) {
        offset = field_offset_cache().emplace(field, runtime.field_offset(field)).first;
    }
    void* field_class = nullptr;
    try {
        field_class = runtime.class_from_type(runtime.field_type(field));
    } catch (...) {
    }
    if (field_class && runtime.class_is_value(field_class)) {
        if (!runtime.klass_name(field_class).starts_with("Torappu.ObjectPtr")) {
            return nullptr;
        }
    }
    void* value = nullptr;
    if (!read_at(static_cast<const unsigned char*>(object) + offset->second,
                 &value)) {
        return nullptr;
    }
    return value;
}

inline std::string probe_blackboard(il2cpp::Runtime& runtime, void* board) {
    std::string text;
    if (!board) {
        return text;
    }
    void* pairs = nullptr;
    try {
        read_field_deep(runtime, board, runtime.object_class(board), "_items",
                        &pairs);
    } catch (...) {
        return text;
    }
    if (!pairs) {
        return text;
    }
    const int count = runtime.count(pairs);
    for (int index = 0; index < count && index < 64; ++index) {
        void* pair = nullptr;
        try {
            pair = runtime.at(pairs, index);
        } catch (...) {
            continue;
        }
        if (!pair) {
            continue;
        }
        void* pair_class = runtime.object_class(pair);
        std::string key;
        std::string value_str;
        float value = 0.0f;
        try {
            key = read_string_field(runtime, pair, pair_class, "key");
            read_field_deep(runtime, pair, pair_class, "value", &value);
            value_str = read_string_field(runtime, pair, pair_class, "valueStr");
        } catch (...) {
        }
        if (!text.empty()) {
            text += ';';
        }
        text += key + "=" + std::to_string(value);
        if (!value_str.empty()) {
            text += "(" + value_str + ")";
        }
    }
    return text;
}

inline std::vector<void*> collect_buff_objects(il2cpp::Runtime& runtime,
                                               void* entity,
                                               void* entity_class,
                                               void* buff_class) {
    std::vector<void*> output;
    void* container = nullptr;
    if (!read_field_deep(runtime, entity, entity_class,
                         "<buffContainer>k__BackingField", &container) ||
        !container) {
        return output;
    }
    Root container_root(runtime, container);
    void* container_class = runtime.object_class(container);
    std::unordered_set<void*> seen;
    void* buffs = nullptr;
    if (read_field_deep(runtime, container, container_class, "m_buffs", &buffs) &&
        buffs) {
        Root buffs_root(runtime, buffs);
        void* list = nullptr;
        if (read_field_deep(runtime, buffs, runtime.object_class(buffs),
                            "m_internalList", &list) &&
            list) {
            Root list_root(runtime, list);
            void* items = nullptr;
            int size = 0;
            if (read_field_deep(runtime, list, runtime.object_class(list),
                                "_items", &items) &&
                items &&
                read_field_deep(runtime, list, runtime.object_class(list),
                                "_size", &size)) {
                Root items_root(runtime, items);
                const int array_count = runtime.count(items);
                for (int index = 0;
                     index < size && index < array_count && index < 512;
                     ++index) {
                    void* item = nullptr;
                    try {
                        item = runtime.at(items, index);
                    } catch (...) {
                        continue;
                    }
                    if (!item) {
                        continue;
                    }
                    void* item_class = runtime.object_class(item);
                    void* target = item;
                    if (runtime.klass_name(item_class).starts_with(
                            "Torappu.ObjectPtr")) {
                        target = nullptr;
                        if (runtime.class_is_value(item_class)) {
                            void* storage = runtime.object_unbox(item);
                            if (storage) {
                                std::memcpy(&target, storage, sizeof(target));
                            }
                        } else {
                            target =
                                read_object_field(runtime, item, item_class, "obj");
                        }
                    }
                    if (target && runtime.object_class(target) == buff_class &&
                        seen.insert(target).second) {
                        output.push_back(target);
                    }
                }
            }
        }
    }
    void* uid_map = nullptr;
    if (read_field_deep(runtime, container, container_class, "m_buffUidMap",
                        &uid_map) &&
        uid_map) {
        Root map_root(runtime, uid_map);
        void* entries = nullptr;
        if (read_field_deep(runtime, uid_map, runtime.object_class(uid_map),
                            "_entries", &entries) &&
            entries) {
            Root entries_root(runtime, entries);
            const int count = runtime.count(entries);
            for (int index = 0; index < count && index < 512; ++index) {
                void* entry = nullptr;
                try {
                    entry = runtime.at(entries, index);
                } catch (...) {
                    continue;
                }
                if (!entry) {
                    continue;
                }
                Root entry_root(runtime, entry);
                void* entry_class = runtime.object_class(entry);
                void* value_field = nullptr;
                try {
                    value_field = runtime.field(entry_class, "value");
                } catch (...) {
                }
                if (!value_field) {
                    continue;
                }
                void* buff =
                    read_object_field(runtime, entry, entry_class, "value");
                if (buff && runtime.object_class(buff) == buff_class &&
                    seen.insert(buff).second) {
                    output.push_back(buff);
                }
            }
        }
    }
    return output;
}

inline std::map<std::string, ProbeBuff> collect_probe_buffs(
    il2cpp::Runtime& runtime, void* entity, void* entity_class,
    void* buff_class) {
    std::map<std::string, ProbeBuff> result;
    for (void* buff : collect_buff_objects(runtime, entity, entity_class,
                                           buff_class)) {
        Root buff_root(runtime, buff);
        void* buff_class_runtime = runtime.object_class(buff);
        ProbeBuff info;
        info.key =
            read_string_field(runtime, buff, buff_class_runtime,
                              "<key>k__BackingField");
        info.override_key =
            read_string_field(runtime, buff, buff_class_runtime,
                              "<overrideKey>k__BackingField");
        info.effect_key =
            read_string_field(runtime, buff, buff_class_runtime,
                              "<effectKey>k__BackingField");
        read_field_deep(runtime, buff, buff_class_runtime,
                        "<instanceUid>k__BackingField", &info.uid);
        std::int64_t remaining_raw = 0;
        read_field_deep(runtime, buff, buff_class_runtime, "m_remainingTime",
                        &remaining_raw);
        info.remaining_time =
            static_cast<double>(remaining_raw) / kFixedPointScale;
        read_field_deep(runtime, buff, buff_class_runtime, "m_stackCnt",
                        &info.stack_count);
        info.trigger_count =
            method_int(runtime, buff, buff_class_runtime, "get_triggerCnt", 0);
        info.enabled =
            method_bool(runtime, buff, buff_class_runtime, "get_isEnabled", true);
        info.finished =
            method_bool(runtime, buff, buff_class_runtime, "get_isFinished", false);
        void* data = nullptr;
        if (read_field_deep(runtime, buff, buff_class_runtime, "m_data", &data) &&
            data) {
            info.template_key = read_string_field(
                runtime, data, runtime.object_class(data), "templateKey");
        }
        if (info.template_key.empty()) {
            info.template_key = info.key;
        }
        void* blackboard = nullptr;
        if (!read_field_deep(runtime, buff, buff_class_runtime, "m_blackboard",
                             &blackboard)) {
            read_field_deep(runtime, buff, buff_class_runtime, "blackboard",
                            &blackboard);
        }
        info.blackboard = probe_blackboard(runtime, blackboard);
        void* get_source =
            find_method_optional(runtime, buff_class_runtime, "get_source", 0);
        if (get_source) {
            void* source = nullptr;
            try {
                source = runtime.invoke(get_source, buff);
            } catch (...) {
            }
            if (source && entity_class) {
                Root source_root(runtime, source);
                info.source_id =
                    method_string(runtime, source, entity_class, "get_id");
            }
        }
        const std::string map_key =
            info.uid ? std::to_string(info.uid)
                     : ("k:" + info.key + ":" + std::to_string(result.size()));
        result.emplace(map_key, std::move(info));
    }
    return result;
}

}  // namespace ark::better_battle::probe_support
