#include "battle_helpers.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace ark::better_battle {
namespace {

struct PanelAttributeSpec {
    const char* name;
};

constexpr PanelAttributeSpec kPanelAttributes[] = {
    {"MAX_HP"},
    {"ATK"},
    {"DEF"},
    {"MAGIC_RESISTANCE"},
    {"ATTACK_SPEED"},
    {"MOVE_SPEED"},
    {"BLOCK_CNT"},
};

constexpr std::int64_t kFpOne = std::int64_t{1} << 32;

double fp_to_double(std::int64_t raw) {
    return static_cast<double>(raw) / kFixedPointScale;
}

bool is_identity_modifier(double addition, double multiplier,
                          double final_addition, double final_scaler) {
    return std::abs(addition) < 1e-9 && std::abs(multiplier) < 1e-9 &&
           std::abs(final_addition) < 1e-9 &&
           std::abs(final_scaler - 1.0) < 1e-9;
}

void read_modifier_identity(il2cpp::Runtime& runtime, void* modifier,
                            void* modifier_class, void* buff_class,
                            void* entity_class, AttributeModifierInfo& info) {
    info.class_name = runtime.klass_name(modifier_class);
    info.kind = "other";
    if (!buff_class ||
        !runtime.class_assignable(buff_class, modifier_class)) {
        return;
    }
    info.kind = "buff";
    info.key = read_string_field(runtime, modifier, modifier_class,
                                 "<key>k__BackingField");
    read_field_deep(runtime, modifier, modifier_class,
                    "<instanceUid>k__BackingField", &info.uid);
    if (void* get_source =
            find_method_optional(runtime, modifier_class, "get_source", 0)) {
        void* source = nullptr;
        try {
            source = runtime.invoke(get_source, modifier);
        } catch (...) {
        }
        if (source) {
            il2cpp::Runtime::Root source_root(runtime, source);
            info.source =
                method_string(runtime, source, entity_class, "get_id");
        }
    }
}

void collect_attribute_modifiers(il2cpp::Runtime& runtime, void* modifier_set,
                                 int attribute, void* buff_class,
                                 void* entity_class,
                                 AttributeBreakdown& row) {
    if (!modifier_set) {
        return;
    }
    il2cpp::Runtime::Root set_root(runtime, modifier_set);
    void* set_class = runtime.object_class(modifier_set);
    void* slots = nullptr;
    int last_index = -1;
    if (!read_field_deep(runtime, modifier_set, set_class, "_slots", &slots) ||
        !slots ||
        !read_field_deep(runtime, modifier_set, set_class, "_lastIndex",
                         &last_index) ||
        last_index < 0) {
        return;
    }
    il2cpp::Runtime::Root slots_root(runtime, slots);
    int slot_count = 0;
    try {
        slot_count = runtime.count(slots);
    } catch (...) {
        return;
    }
    std::unordered_set<void*> seen;
    for (int index = 0; index <= last_index && index < slot_count; ++index) {
        void* slot = nullptr;
        try {
            slot = runtime.at(slots, index);
        } catch (...) {
            continue;
        }
        if (!slot) {
            continue;
        }
        il2cpp::Runtime::Root slot_root(runtime, slot);
        void* slot_class = runtime.object_class(slot);
        void* value_field = find_field_deep(runtime, slot_class, "value");
        if (!value_field) {
            value_field = find_field_deep(runtime, slot_class, "Value");
        }
        if (!value_field) {
            continue;
        }
        void* modifier = nullptr;
        try {
            modifier = runtime.field_get_object(value_field, slot);
        } catch (...) {
            continue;
        }
        if (!modifier || !seen.insert(modifier).second) {
            continue;
        }
        il2cpp::Runtime::Root modifier_root(runtime, modifier);
        void* modifier_class = runtime.object_class(modifier);
        void* get_value =
            find_method_optional(runtime, modifier_class, "GetValue", 5);
        if (!get_value) {
            continue;
        }

        std::int64_t addition_raw = 0;
        std::int64_t multiplier_raw = 0;
        std::int64_t final_addition_raw = 0;
        std::int64_t final_scaler_raw = kFpOne;
        void* active_box = nullptr;
        try {
            active_box = runtime.invoke(
                get_value, modifier,
                {&attribute, &addition_raw, &multiplier_raw,
                 &final_addition_raw, &final_scaler_raw});
        } catch (...) {
            continue;
        }
        if (!active_box || !runtime.unbox<bool>(active_box)) {
            continue;
        }

        const double addition = fp_to_double(addition_raw);
        const double multiplier = fp_to_double(multiplier_raw);
        const double final_addition = fp_to_double(final_addition_raw);
        const double final_scaler = fp_to_double(final_scaler_raw);
        row.addition += addition;
        row.multiplier += multiplier;
        row.final_addition += final_addition;
        row.final_scaler *= final_scaler;

        if (is_identity_modifier(addition, multiplier, final_addition,
                                 final_scaler)) {
            continue;
        }
        AttributeModifierInfo info;
        info.addition = addition;
        info.multiplier = multiplier;
        info.final_addition = final_addition;
        info.final_scaler = final_scaler;
        read_modifier_identity(runtime, modifier, modifier_class, buff_class,
                               entity_class, info);
        row.modifiers.push_back(std::move(info));
    }
}

} // namespace

void LocalBattle::cmd_panel(Args& args) {
    const std::string value = args.next();
    if (value.empty() || value == "off" || value == "0") {
        panel_target_uid_ = 0;
        publish_result("面板计算过程已关闭");
        return;
    }
    try {
        panel_target_uid_ = static_cast<std::uint32_t>(std::stoul(value));
    } catch (...) {
        throw std::runtime_error("panel 需要单位 UID 或 off");
    }
    publish_result(panel_target_uid_ ? "面板计算过程目标已更新"
                                     : "面板计算过程已关闭");
}

void LocalBattle::read_attribute_panel(void* entity, EnemyInfo& info) {
    if (!panel_target_uid_ || info.uid != panel_target_uid_ || !entity ||
        !classes_.attributes || !classes_.attribute_type) {
        return;
    }
    info.panel.clear();
    try {
        void* attributes = nullptr;
        if (void* get_attributes =
                find_method_optional(runtime_, classes_.entity,
                                     "get_attributes", 0)) {
            attributes = runtime_.invoke(get_attributes, entity);
        }
        if (!attributes) {
            read_field_deep(runtime_, entity, classes_.entity, "m_attributes",
                            &attributes);
        }
        if (!attributes) {
            return;
        }
        il2cpp::Runtime::Root attributes_root(runtime_, attributes);
        void* attributes_class = runtime_.object_class(attributes);
        void* modifier_array = nullptr;
        read_field_deep(runtime_, attributes, attributes_class,
                        "m_attributeModifiers", &modifier_array);
        void* get_raw = find_method_optional(
            runtime_, attributes_class, "GetRawValue", 1);
        void* get_value =
            find_method_optional(runtime_, attributes_class, "GetValue", 1);
        void* get_min = find_method_optional(
            runtime_, attributes_class, "GetMinValue", 1);
        void* get_max = find_method_optional(
            runtime_, attributes_class, "GetMaxValue", 1);
        if (!get_raw || !get_value) {
            return;
        }

        for (const PanelAttributeSpec& spec : kPanelAttributes) {
            int attribute = 0;
            try {
                attribute =
                    runtime_.static_value<int>(classes_.attribute_type,
                                               spec.name);
            } catch (...) {
                continue;
            }
            AttributeBreakdown row;
            row.attribute = spec.name;
            try {
                row.raw =
                    unbox_fp(runtime_, runtime_.invoke(get_raw, attributes,
                                                      {&attribute}));
                row.actual =
                    unbox_fp(runtime_, runtime_.invoke(get_value, attributes,
                                                      {&attribute}));
            } catch (...) {
                continue;
            }
            const auto read_limit = [&](void* method, double fallback) {
                if (!method) {
                    return fallback;
                }
                try {
                    return unbox_fp(
                        runtime_,
                        runtime_.invoke(method, attributes, {&attribute}));
                } catch (...) {
                    return fallback;
                }
            };
            row.min_value = read_limit(get_min, 0.0);
            row.max_value =
                read_limit(get_max, std::numeric_limits<double>::infinity());
            row.has_max = std::isfinite(row.max_value) &&
                          row.max_value < 1e9;

            if (modifier_array) {
                void* modifier_set = nullptr;
                try {
                    modifier_set = runtime_.at(modifier_array, attribute);
                } catch (...) {
                }
                collect_attribute_modifiers(runtime_, modifier_set, attribute,
                                            classes_.buff, classes_.entity,
                                            row);
            }
            row.predicted =
                row.final_scaler *
                ((row.raw + row.addition) *
                     std::max(0.0, 1.0 + row.multiplier) +
                 row.final_addition);
            info.panel.push_back(std::move(row));
        }
    } catch (...) {
        info.panel.clear();
    }
}

} // namespace ark::better_battle
