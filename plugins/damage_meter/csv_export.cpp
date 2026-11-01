#include "csv_export.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace ark::damage_meter {
namespace {

constexpr char kUtf8Bom[] = "\xEF\xBB\xBF";
constexpr char kHeader[] =
    "干员,干员ID,实际扣血,计算伤害,实际DPS,计算DPS,实际占比,计算占比,元素损伤,游戏时间秒\n";
constexpr int kDecimalPlaces = 4;

double ratio(double value, double total) {
    return total ? value / total : 0;
}

std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace

std::optional<std::string> write_csv_report(const char* root_directory, const BattleStats& stats,
                                            const std::vector<const ActorRow*>& rows,
                                            const ActorNameMap& actor_names) {
    const std::filesystem::path directory = utf8_path(root_directory) / "exports";
    std::filesystem::create_directories(directory);
    const std::filesystem::path file =
        directory / ("damage-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(stats.battle) + ".csv");
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return std::nullopt;
    }

    stream << kUtf8Bom << kHeader;
    const double duration = stats.duration();
    const double actual_total = stats.total(true);
    const double calculated_total = stats.total(false);
    stream << std::fixed << std::setprecision(kDecimalPlaces);
    for (const ActorRow* row : rows) {
        stream << display_name(actor_names, row->id) << ',' << row->id << ',' << row->actual << ','
               << row->calculated << ',' << ratio(row->actual, duration) << ','
               << ratio(row->calculated, duration) << ',' << ratio(row->actual, actual_total) << ','
               << ratio(row->calculated, calculated_total) << ',' << row->elemental_buildup << ','
               << stats.game_time << '\n';
    }
    return path_to_utf8(file);
}

} // namespace ark::damage_meter
