#pragma once

#include "battle_event.h"

#include <ark/plugin.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ark::il2cpp {
class Runtime;
}

namespace ark::better_battle {

// Captures native BattleLogger modifiers inside BetterBattle and exposes them as a
// lossless, sequence-ordered ArkBattleEvent stream. No other plugin is required.
class BattleEventCapture {
public:
    BattleEventCapture(il2cpp::Runtime& runtime, const ArkPluginApiV1& host);
    ~BattleEventCapture();

    BattleEventCapture(const BattleEventCapture&) = delete;
    BattleEventCapture& operator=(const BattleEventCapture&) = delete;

    bool initialize();
    bool shutdown();
    bool available() const;
    void seed_unit_health(const std::string& id, double hp, double effective_hp,
                          double minus_hp);

    std::size_t read(std::uint64_t after, ArkBattleEvent* out, std::size_t capacity,
                     ArkBattleEventReadStatus* status);

private:
    struct Bindings;
    class CallbackScope;

    void begin_battle(void* controller, bool partial);
    void end_battle();
    void update_battle(void* controller);
    void record_modifier(void* logger, void* modifier, bool elemental);
    void record_regen(void* entity, double amount);
    void flush_regen();
    void publish(ArkBattleEvent event);
    double game_clock();
    std::uint32_t current_frame();
    void publish_frame();

    static void on_log_modifier(void* self, void* modifier, void* method);
    static void on_log_ep_modifier(void* self, void* modifier, void* method);
    static void on_start_game(void* self, void* method);
    static void on_clear_runtime_data(void* self, void* method);
    static void on_destroy(void* self, void* method);
    static void on_update(void* self, void* method);
    static void on_update_hp_recovery(void* self, std::int64_t delta,
                                      void* method);

    il2cpp::Runtime& runtime_;
    const ArkPluginApiV1& host_;
    std::unique_ptr<Bindings> bindings_;

    std::recursive_mutex lifecycle_mutex_;
    std::mutex callback_mutex_;
    std::condition_variable callbacks_drained_;
    std::size_t callbacks_in_flight_ = 0;
    bool callbacks_enabled_ = false;
    std::atomic<bool> ready_{false};
    std::atomic<bool> host_active_{false};

    std::mutex events_mutex_;
    std::deque<ArkBattleEvent> events_;
    std::uint64_t sequence_ = 0;
    std::uint64_t battle_ = 0;
    std::uint32_t frame_event_source_ = 0;
    std::uint32_t frame_event_index_ = 0;
    bool partial_ = false;
    bool ended_ = false;
    bool frame_valid_ = false;
    std::uint32_t frame_ = 0;
    std::unordered_map<std::string, double> last_hp_by_id_;
    std::unordered_map<std::string, double> last_effective_hp_by_id_;
    std::unordered_map<std::string, double> last_minus_hp_by_id_;
    std::mutex regen_mutex_;
    struct PendingRegen {
        std::string id;
        std::uint32_t uid = 0;
        double amount = 0;
    };
    std::unordered_map<std::string, PendingRegen> regen_pending_;

    bool battle_active_ = false;
    bool battle_ended_ = false;
    double last_clock_ = -1;
    void* controller_ = nullptr;
    std::vector<std::uint64_t> hooks_;
};

} // namespace ark::better_battle
