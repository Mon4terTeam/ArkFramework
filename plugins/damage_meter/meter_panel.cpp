#include <algorithm>

#include <imgui.h>

#include "../common/window_layout.h"
#include "meter.h"

namespace ark::damage_meter {
namespace {

constexpr ImVec2 kWindowSize{790, 430};
// Default window position, chosen by display width.
constexpr float kWideDisplayWidth = 1300;
constexpr ImVec2 kWideWindowPosition{500, 18};
constexpr ImVec2 kNarrowWindowPosition{40, 210};
// Height reserved below the table for the export button and path.
constexpr float kFooterHeight = 62;

constexpr ImVec4 kWarningColor{1.f, .7f, .2f, 1.f};
constexpr ImVec4 kTestHostColor{1.f, .72f, .25f, 1.f};

constexpr int kColumnCount = 6;

bool has_typed_damage(const ActorRow& row) {
    return std::any_of(row.types.begin(), row.types.end(), [](const DamageTotals& totals) {
        return totals.actual != 0 || totals.calculated != 0;
    });
}

// Fills actual/calculated damage, optional actual DPS and actual share for the current row.
void draw_amount_columns(double actual, double calculated, double total, double inverse_duration) {
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", actual);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", calculated);
    if (ImGui::TableNextColumn()) {
        ImGui::Text("%.2f", actual * inverse_duration);
    }
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", total ? actual / total * 100 : 0);
}

} // namespace

void Meter::draw(const ArkFrameV1& frame, Backend* backend) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(frame.imgui_context));
    std::lock_guard lock(mutex_);
    if (!session_.window_open) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ui::fitting_window_layout(kWindowSize, display.x >= kWideDisplayWidth ? kWideWindowPosition
                                                                          : kNarrowWindowPosition);
    if (!ImGui::Begin("干员伤害统计##ArkDamageMeter", &session_.window_open)) {
        ImGui::End();
        return;
    }
    draw_status(backend);
    ImGui::Checkbox("显示 DPS", &show_dps_);
    const double actual_total = session_.stats.total(true);
    const double calculated_total = session_.stats.total(false);
    ImGui::Text("实际扣血：%.2f    计算伤害：%.2f", actual_total, calculated_total);
    ImGui::Separator();
    draw_table(actual_total);
    draw_footer();
    ImGui::End();
}

const char* Meter::stage_display_name() const {
    const auto label = session_.stage_labels.find(session_.stats.stage);
    if (label == session_.stage_labels.end() || label->second.label.empty()) {
        return session_.stats.stage.c_str();
    }
    return label->second.label.c_str();
}

void Meter::draw_status(Backend* backend) {
    const BattleStats& stats = session_.stats;
    if (host_->test_host) {
        ImGui::TextColored(kTestHostColor, "测试宿主 / 数据回放，不是游戏实测画面");
    }
    ImGui::Text("关卡：%s   游戏时间：%.2f s   %s", stage_display_name(), stats.game_time,
                stats.ended ? "已结束" : "统计中");
    ImGui::SameLine();
    ArkBattleFrameV1 battle_frame;
    if (backend && backend->current_frame(battle_frame) && battle_frame.battle == stats.battle) {
        ImGui::Text("逻辑帧：%u", static_cast<unsigned>(battle_frame.frame));
    } else {
        ImGui::TextUnformatted("逻辑帧：--");
    }
    if (stats.partial) {
        ImGui::TextColored(kWarningColor, "仅包含加载后记录到的数据");
    }
    if (!session_.capture_error.empty()) {
        ImGui::TextColored(kWarningColor, "原生采集连接失败：%s", session_.capture_error.c_str());
        if (ImGui::Button("重新连接采集器") && backend) {
            backend->retry_requested = true;
        }
    }
    if (session_.actor_lookup_failed) {
        ImGui::TextColored(kWarningColor, "原生名称查询失败，暂显示 ID：%s",
                           session_.actor_lookup_error.c_str());
    } else if (session_.name_lookup_pending) {
        ImGui::TextDisabled("正在读取原生名称…");
    }
}

void Meter::draw_table(double actual_total) {
    constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("damage", kColumnCount, kTableFlags, {0, -kFooterHeight})) {
        return;
    }
    for (const char* title : {"干员", "伤害类型", "实际扣血", "计算伤害"}) {
        ImGui::TableSetupColumn(title);
    }
    ImGui::TableSetupColumn("DPS", show_dps_ ? ImGuiTableColumnFlags_None
                                           : ImGuiTableColumnFlags_Disabled);
    ImGui::TableSetupColumn("占比");
    ImGui::TableHeadersRow();
    const double duration = show_dps_ ? session_.stats.duration() : 0;
    const double inverse_duration = duration > 0 ? 1.0 / duration : 0;

    for (const ActorRow* row : sorted_rows()) {
        const bool has_types = has_typed_damage(*row);
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (!has_types) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const char* label = display_name(session_.actor_names, row->id);
        const bool expanded = ImGui::TreeNodeEx(row->id.c_str(), flags, "%s", label);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("合计");
        draw_amount_columns(row->actual, row->calculated, actual_total, inverse_duration);
        if (!expanded || !has_types) {
            continue;
        }
        for (std::size_t type = 0; type < row->types.size(); ++type) {
            const DamageTotals& amount = row->types[type];
            if (amount.actual == 0 && amount.calculated == 0) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("  -");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kDamageTypeNames[type]);
            draw_amount_columns(amount.actual, amount.calculated, actual_total, inverse_duration);
        }
        ImGui::TreePop();
    }
    ImGui::EndTable();
}

void Meter::draw_footer() {
    if (ImGui::Button("导出 CSV")) {
        export_csv();
    }
    if (!session_.exported_path.empty()) {
        ImGui::TextDisabled("已保存：%s", session_.exported_path.c_str());
    }
}

} // namespace ark::damage_meter
