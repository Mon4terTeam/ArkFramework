#pragma once

#include <cstdint>
#include <mutex>

#include <ark/plugin.h>

namespace ark {

// Routes Unity's legacy Input polling (UnityEngine.Input) through native hooks so that
// mouse and keyboard input captured by plugin UI never reaches the game.
//
// A press that starts while input is captured stays owned by the UI until it is
// physically released and Unity has polled a neutral frame; a press the game already
// observed receives exactly one synthetic release. shutdown() keeps the hooks installed
// until every owned press has drained.
class UnityInput {
public:
    explicit UnityInput(const ArkPluginApiV1& host);
    ~UnityInput();
    UnityInput(const UnityInput&) = delete;
    UnityInput& operator=(const UnityInput&) = delete;

    // Installs and enables the input hooks. Returns false while the IL2CPP runtime or the
    // Unity input assemblies are not ready yet; throws on incompatible game builds.
    bool initialize();
    void set_capture(bool mouse, bool keyboard);
    // Feeds a window message to the capture state machine. Returns true when the game
    // must not see the message.
    bool route_message(uint32_t message, uintptr_t wparam, intptr_t lparam);
    bool protected_input() const;
    // False while a captured press is still physically held.
    bool can_unload() const;
    // Drains captured presses and removes the hooks. On false the hooks are retained and
    // the call may be retried.
    bool shutdown();

private:
    struct Impl;

    const ArkPluginApiV1& host_;
    unsigned capture_ = 0;
    // Owned, but intentionally leaked when shutdown cannot remove the hooks: a still
    // installed detour may call into it at any time.
    Impl* impl_ = nullptr;
    mutable std::recursive_mutex lifecycle_;
    std::recursive_mutex operation_;
};

} // namespace ark
