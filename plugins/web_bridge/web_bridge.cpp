#include <ark/external_bridge.h>
#include <ark/plugin.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

namespace ark::web_bridge {
namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ArkFramework.WebView";
constexpr DWORD kPipeBufferSize = 8u * 1024u * 1024u;
constexpr std::size_t kMaxTopicBytes = 128;
constexpr std::size_t kMaxPayloadBytes = 8u * 1024u * 1024u;
const ArkPluginApiV1* g_host = nullptr;

void log_message(std::uint32_t level, const std::string& message) {
    if (g_host && g_host->log) {
        g_host->log(g_host->owner, level, message.c_str());
    }
}

class BridgeServer {
public:
    ~BridgeServer() {
        stop();
    }

    struct ThreadStart {
        BridgeServer* server = nullptr;
        std::uint64_t generation = 0;
    };

    static DWORD WINAPI thread_entry(LPVOID parameter) {
        auto* start = static_cast<ThreadStart*>(parameter);
        start->server->loop(start->generation);
        delete start;
        return 0;
    }

    void start() {
        std::lock_guard lock(start_mutex_);
        if (worker_thread_) {
            return;
        }
        spawn_locked();
    }

    void stop() {
        std::lock_guard lock(start_mutex_);
        stopping_.store(true);
        ++generation_;
        ready_.notify_all();
        HANDLE pipe = active_pipe_.exchange(nullptr);
        if (pipe && pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe, nullptr);
            DisconnectNamedPipe(pipe);
        }
        wake_connector();
        if (worker_thread_) {
            WaitForSingleObject(worker_thread_, 5000);
            CloseHandle(worker_thread_);
            worker_thread_ = nullptr;
        }
    }

    void ensure_healthy() {
        std::lock_guard lock(start_mutex_);
        if (!worker_thread_) {
            spawn_locked();
            return;
        }
        if (loop_entered_.load()) {
            return;
        }
        const std::uint64_t started = started_tick_.load();
        if (started && GetTickCount64() - started < 3000) {
            return;
        }
        log_message(2, "web_bridge: pipe server did not enter loop; restarting");
        restart_locked();
    }

    bool publish(const char* topic, const char* json, std::size_t size) {
        if (!topic || !*topic || !json || size == 0 || size > kMaxPayloadBytes ||
            std::strlen(topic) > kMaxTopicBytes || stopping_.load()) {
            return false;
        }
        {
            std::lock_guard lock(mutex_);
            latest_[topic] = std::string(json, size);
            dirty_.insert(topic);
            if (++sequence_ == 0) {
                sequence_ = 1;
            }
            sequence_by_topic_[topic] = sequence_;
        }
        ready_.notify_one();
        return true;
    }

private:
    bool stale(std::uint64_t generation) const {
        return stopping_.load() || generation != generation_.load();
    }

    void spawn_locked() {
        stopping_.store(false);
        const std::uint64_t generation = ++generation_;
        loop_entered_.store(false);
        started_tick_.store(GetTickCount64());
        auto* start = new ThreadStart{this, generation};
        worker_thread_ = CreateThread(nullptr, 0, &BridgeServer::thread_entry,
                                      start, 0, nullptr);
        if (!worker_thread_) {
            delete start;
            log_message(2, "web_bridge: CreateThread failed: " +
                               std::to_string(GetLastError()));
            return;
        }
        log_message(1, "web_bridge: pipe server thread started");
    }

    void restart_locked() {
        ++generation_;
        stopping_.store(true);
        ready_.notify_all();
        HANDLE pipe = active_pipe_.exchange(nullptr);
        if (pipe && pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe, nullptr);
            DisconnectNamedPipe(pipe);
        }
        wake_connector();
        if (worker_thread_) {
            CloseHandle(worker_thread_);
            worker_thread_ = nullptr;
        }
        spawn_locked();
    }

    void wake_connector() {
        HANDLE client = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                                    nullptr);
        if (client != INVALID_HANDLE_VALUE) {
            CloseHandle(client);
        }
    }

    bool write_frame(HANDLE pipe, const std::string& frame) {
        const std::uint32_t size = static_cast<std::uint32_t>(frame.size());
        DWORD written = 0;
        if (!WriteFile(pipe, &size, sizeof(size), &written, nullptr) ||
            written != sizeof(size)) {
            return false;
        }
        return WriteFile(pipe, frame.data(), size, &written, nullptr) &&
               written == size;
    }

    void loop(std::uint64_t generation) {
        if (stale(generation)) {
            return;
        }
        loop_entered_.store(true);
        log_message(1, "web_bridge: pipe server loop entered");
        while (!stale(generation)) {
            HANDLE pipe = CreateNamedPipeW(
                kPipeName, PIPE_ACCESS_OUTBOUND,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                    PIPE_REJECT_REMOTE_CLIENTS,
                1, kPipeBufferSize, kPipeBufferSize, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                log_message(2, "web_bridge: CreateNamedPipe failed: " +
                                   std::to_string(GetLastError()));
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }
            active_pipe_.store(pipe);
            log_message(1, "web_bridge: pipe created, waiting for client");
            const BOOL connected =
                ConnectNamedPipe(pipe, nullptr) ||
                GetLastError() == ERROR_PIPE_CONNECTED;
            if (!connected || stale(generation)) {
                log_message(2, "web_bridge: ConnectNamedPipe failed: " +
                                   std::to_string(GetLastError()));
                active_pipe_.store(nullptr);
                CloseHandle(pipe);
                continue;
            }
            log_message(1, "web_bridge: WebView client connected");

            {
                std::lock_guard lock(mutex_);
                for (const auto& [topic, unused] : latest_) {
                    (void)unused;
                    dirty_.insert(topic);
                }
            }

            while (!stale(generation)) {
                std::vector<std::pair<std::string, std::string>> batch;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait_for(lock, std::chrono::milliseconds(250), [&] {
                        return stale(generation) || !dirty_.empty();
                    });
                    if (stale(generation)) {
                        break;
                    }
                    for (const std::string& topic : dirty_) {
                        const auto found = latest_.find(topic);
                        if (found != latest_.end()) {
                            batch.emplace_back(topic, found->second);
                        }
                    }
                    dirty_.clear();
                }
                if (batch.empty()) {
                    continue;
                }
                bool write_ok = true;
                for (const auto& [topic, payload] : batch) {
                    std::uint64_t sequence = 0;
                    {
                        std::lock_guard lock(mutex_);
                        sequence = sequence_by_topic_[topic];
                    }
                    nlohmann::ordered_json frame;
                    frame["topic"] = topic;
                    frame["seq"] = sequence;
                    try {
                        frame["payload"] =
                            nlohmann::ordered_json::parse(payload);
                    } catch (...) {
                        frame["payload"] = payload;
                    }
                    const std::string frame_text = frame.dump();
                    if (!write_frame(pipe, frame_text)) {
                        write_ok = false;
                        break;
                    }
                }
                if (!write_ok) {
                    break;
                }
            }

            active_pipe_.store(nullptr);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        if (generation == generation_.load()) {
            loop_entered_.store(false);
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::map<std::string, std::string> latest_;
    std::map<std::string, std::uint64_t> sequence_by_topic_;
    std::set<std::string> dirty_;
    std::uint64_t sequence_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<HANDLE> active_pipe_{nullptr};
    std::mutex start_mutex_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> loop_entered_{false};
    std::atomic<std::uint64_t> started_tick_{0};
    HANDLE worker_thread_ = nullptr;
};

BridgeServer& server() {
    static BridgeServer instance;
    return instance;
}

std::uint64_t g_frame_count = 0;
std::uint64_t g_frame_callback = 0;

void frame_callback(void*, const ArkFrameV1*) {
    ++g_frame_count;
    if (g_frame_count < 120) {
        return;
    }
    if ((g_frame_count % 30) != 0) {
        return;
    }
    server().ensure_healthy();
}

bool host_is_compatible(const ArkPluginApiV1* host) {
    return host && host->size >= sizeof(ArkPluginApiV1) &&
           host->abi_version == ARK_ABI_VERSION &&
           host->provide_service && host->log;
}

bool publish(const char* topic, const char* json, size_t size, uint32_t) {
    try {
        return server().publish(topic, json, size);
    } catch (...) {
        return false;
    }
}

} // namespace
} // namespace ark::web_bridge

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const ArkPluginInfoV1 info{
        .size = sizeof(ArkPluginInfoV1),
        .abi_version = ARK_ABI_VERSION,
        .imgui_version = IMGUI_VERSION_NUM,
        .name = "外部桥接",
        .version = "1.0.0",
        .author = "ArkFramework",
        .flags = ARK_PLUGIN_DEFAULT,
    };
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* host) {
    if (!ark::web_bridge::host_is_compatible(host)) {
        return false;
    }
    ark::web_bridge::g_host = host;
    if (host->on_frame) {
        ark::web_bridge::g_frame_callback =
            host->on_frame(host->owner, &ark::web_bridge::frame_callback,
                           nullptr);
    }
    if (!ark::web_bridge::g_frame_callback) {
        ark::web_bridge::server().start();
    }
    static const ArkExternalBridgeApiV1 table{
        .size = sizeof(ArkExternalBridgeApiV1),
        .abi_version = ARK_EXTERNAL_BRIDGE_ABI_VERSION,
        .publish = &ark::web_bridge::publish,
    };
    if (!host->provide_service(host->owner, ARK_EXTERNAL_BRIDGE_SERVICE_NAME,
                              ARK_EXTERNAL_BRIDGE_ABI_VERSION, &table,
                              sizeof(table))) {
        ark::web_bridge::server().stop();
        return false;
    }
    return true;
}

ARK_EXPORT bool ark_plugin_shutdown() {
    if (ark::web_bridge::g_frame_callback && ark::web_bridge::g_host &&
        ark::web_bridge::g_host->remove_callback) {
        ark::web_bridge::g_host->remove_callback(
            ark::web_bridge::g_host->owner,
            ark::web_bridge::g_frame_callback);
        ark::web_bridge::g_frame_callback = 0;
    }
    ark::web_bridge::server().stop();
    return true;
}
