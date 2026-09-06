#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unistd.h>
#include <vector>

#include <wayland-client.h>
#include <wayland.hpp>
#include <wlr-foreign-toplevel-management-unstable-v1.hpp>

#include <hyprutils/memory/Casts.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

using namespace Hyprutils::Memory;

// hyprland-toplevel-export-v1: no CC client wrapper exists in this tree.
// Reconstruct the wire interface from the protocol XML and use the C API directly.

static const struct wl_message s_frameMethods[] = {
    {"copy", "oi", nullptr},  // opcode 0
    {"destroy", "", nullptr}, // opcode 1
};
static const struct wl_message s_frameEvents[] = {
    {"buffer", "uuuu", nullptr},      // opcode 0
    {"damage", "uuuu", nullptr},      // opcode 1
    {"flags", "u", nullptr},          // opcode 2
    {"ready", "uuu", nullptr},        // opcode 3
    {"failed", "", nullptr},          // opcode 4
    {"linux_dmabuf", "uuu", nullptr}, // opcode 5
    {"buffer_done", "", nullptr},     // opcode 6
};
static const struct wl_interface s_exportFrameIface = {
    "hyprland_toplevel_export_frame_v1", 2, 2, s_frameMethods, 7, s_frameEvents,
};

static const struct wl_message s_mgrMethods[] = {
    {"capture_toplevel", "niu", nullptr},                           // opcode 0
    {"destroy", "", nullptr},                                       // opcode 1
    {"capture_toplevel_with_wlr_toplevel_handle", "2nio", nullptr}, // opcode 2
};
static const struct wl_interface s_exportMgrIface = {
    "hyprland_toplevel_export_manager_v1", 2, 3, s_mgrMethods, 0, nullptr,
};

// ── State ─────────────────────────────────────────────────────────────────────

struct SHandle {
    CSharedPointer<CCZwlrForeignToplevelHandleV1> resource;
    std::string                                   appID;
};

struct SState {
    wl_display*                                    display = nullptr;
    CSharedPointer<CCWlRegistry>                   registry;
    CSharedPointer<CCWlShm>                        shm;
    CSharedPointer<CCZwlrForeignToplevelManagerV1> toplevelMgr;
    wl_proxy*                                      exportMgr = nullptr;

    std::vector<CSharedPointer<SHandle>>           handles;
    CSharedPointer<CCZwlrForeignToplevelHandleV1>  target;
    std::string                                    targetAppID;

    wl_proxy*                                      frame = nullptr;
    CSharedPointer<CCWlShmPool>                    pool;
    CSharedPointer<CCWlBuffer>                     buffer;
    int                                            bufferFD = -1;

    uint32_t                                       bufferFormat = 0;
    uint32_t                                       bufferWidth  = 0;
    uint32_t                                       bufferHeight = 0;
    uint32_t                                       bufferStride = 0;

    bool                                           frameReceived = false;
    bool                                           shouldExit    = false;
};

static bool createCaptureBuffer(SState&);

// ── Frame event callbacks ─────────────────────────────────────────────────────

static void onFrameBuffer(void* d, wl_proxy*, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    auto& st        = *static_cast<SState*>(d);
    st.bufferFormat = fmt;
    st.bufferWidth  = w;
    st.bufferHeight = h;
    st.bufferStride = stride;
}
static void onFrameDamage(void*, wl_proxy*, uint32_t, uint32_t, uint32_t, uint32_t) {}
static void onFrameFlags(void*, wl_proxy*, uint32_t) {}
static void onFrameReady(void* d, wl_proxy*, uint32_t, uint32_t, uint32_t) {
    auto& st         = *static_cast<SState*>(d);
    st.frameReceived = true;
    st.shouldExit    = true;
}
static void onFrameFailed(void* d, wl_proxy*) {
    std::println(stderr, "capture failed (server sent 'failed')");
    static_cast<SState*>(d)->shouldExit = true;
}
static void onFrameLinuxDmabuf(void*, wl_proxy*, uint32_t, uint32_t, uint32_t) {}
static void onFrameBufferDone(void* d, wl_proxy*) {
    auto& st = *static_cast<SState*>(d);
    if (!createCaptureBuffer(st)) {
        std::println(stderr, "failed to allocate capture buffer");
        st.shouldExit = true;
        return;
    }
    wl_proxy_marshal_flags(st.frame, 0 /*copy*/, nullptr, wl_proxy_get_version(st.frame), 0, rc<wl_proxy*>(st.buffer->resource()), (int32_t)1 /*ignore_damage*/);
}

struct SFrameListener {
    void (*buffer)(void*, wl_proxy*, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*damage)(void*, wl_proxy*, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*flags)(void*, wl_proxy*, uint32_t);
    void (*ready)(void*, wl_proxy*, uint32_t, uint32_t, uint32_t);
    void (*failed)(void*, wl_proxy*);
    void (*linux_dmabuf)(void*, wl_proxy*, uint32_t, uint32_t, uint32_t);
    void (*buffer_done)(void*, wl_proxy*);
};
static SFrameListener s_frameListener = {
    onFrameBuffer, onFrameDamage, onFrameFlags, onFrameReady, onFrameFailed, onFrameLinuxDmabuf, onFrameBufferDone,
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool bindGlobals(SState& state) {
    state.registry = makeShared<CCWlRegistry>(rc<wl_proxy*>(wl_display_get_registry(state.display)));

    state.registry->setGlobal([&state](CCWlRegistry* r, uint32_t name, const char* iface, uint32_t ver) {
        const std::string_view IFACE = iface;

        if (IFACE == "wl_shm") {
            state.shm = makeShared<CCWlShm>(rc<wl_proxy*>(wl_registry_bind(rc<wl_registry*>(r->resource()), name, &wl_shm_interface, 1)));

        } else if (IFACE == "zwlr_foreign_toplevel_manager_v1") {
            state.toplevelMgr = makeShared<CCZwlrForeignToplevelManagerV1>(
                rc<wl_proxy*>(wl_registry_bind(rc<wl_registry*>(r->resource()), name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(ver, 3U))));

            state.toplevelMgr->setToplevel([&state](CCZwlrForeignToplevelManagerV1*, wl_proxy* res) {
                const auto H = makeShared<SHandle>();
                H->resource  = makeShared<CCZwlrForeignToplevelHandleV1>(res);
                auto* h      = H.get();
                H->resource->setAppId([h](CCZwlrForeignToplevelHandleV1*, const char* id) { h->appID = id; });
                H->resource->setDone([&state, h](CCZwlrForeignToplevelHandleV1*) {
                    if (h->appID == state.targetAppID)
                        state.target = h->resource;
                });
                H->resource->setClosed([&state, h](CCZwlrForeignToplevelHandleV1*) {
                    if (state.target == h->resource)
                        state.target.reset();
                });
                state.handles.emplace_back(H);
            });
            state.toplevelMgr->setFinished([](CCZwlrForeignToplevelManagerV1*) {});

        } else if (IFACE == "hyprland_toplevel_export_manager_v1") {
            state.exportMgr = rc<wl_proxy*>(wl_registry_bind(rc<wl_registry*>(r->resource()), name, &s_exportMgrIface, std::min(ver, 2U)));
        }
    });
    state.registry->setGlobalRemove([](CCWlRegistry*, uint32_t) {});

    if (wl_display_roundtrip(state.display) < 0)
        return false;

    return state.shm && state.toplevelMgr && state.exportMgr;
}

static bool findTarget(SState& state) {
    for (size_t i = 0; i < 3 && !state.target; ++i) {
        if (wl_display_roundtrip(state.display) < 0)
            return false;
    }
    return !!state.target;
}

static bool createCaptureBuffer(SState& state) {
    const size_t SIZE = sc<size_t>(state.bufferStride) * state.bufferHeight;
    if (SIZE == 0)
        return false;

    const std::string NAME = std::format("/wl-shm-toplevel-capture-{}", getpid());
    const int         FD   = shm_open(NAME.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (FD < 0)
        return false;
    if (shm_unlink(NAME.c_str()) < 0) {
        close(FD);
        return false;
    }
    if (ftruncate(FD, sc<off_t>(SIZE)) < 0) {
        close(FD);
        return false;
    }

    state.bufferFD = FD;
    state.pool     = makeShared<CCWlShmPool>(state.shm->sendCreatePool(FD, sc<int32_t>(SIZE)));
    if (!state.pool || !state.pool->resource())
        return false;

    state.buffer = makeShared<CCWlBuffer>(
        state.pool->sendCreateBuffer(0, sc<int32_t>(state.bufferWidth), sc<int32_t>(state.bufferHeight), sc<int32_t>(state.bufferStride), state.bufferFormat));
    return state.buffer && state.buffer->resource();
}

static bool requestCapture(SState& state) {
    // "2nio": version-gated, new_id (frame), int32 (overlay_cursor), object (wlr handle)
    state.frame = wl_proxy_marshal_flags(state.exportMgr, 2 /*capture_toplevel_with_wlr_toplevel_handle*/, &s_exportFrameIface, wl_proxy_get_version(state.exportMgr), 0,
                                         nullptr,                                  // new_id placeholder
                                         (int32_t)0,                               // overlay_cursor
                                         rc<wl_proxy*>(state.target->resource())); // wlr handle

    if (!state.frame)
        return false;

    if (wl_proxy_add_listener(state.frame, reinterpret_cast<void (**)(void)>(&s_frameListener), &state) < 0) {
        wl_proxy_destroy(state.frame);
        state.frame = nullptr;
        return false;
    }
    return true;
}

static bool dispatchUntilDone(SState& state, std::chrono::steady_clock::time_point deadline) {
    while (!state.shouldExit) {
        while (wl_display_prepare_read(state.display) != 0) {
            if (wl_display_dispatch_pending(state.display) < 0)
                return false;
            if (state.shouldExit)
                return true;
        }

        if (wl_display_flush(state.display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(state.display);
            return false;
        }

        const auto NOW = std::chrono::steady_clock::now();
        if (NOW >= deadline) {
            wl_display_cancel_read(state.display);
            return false;
        }

        const auto REMAINING_MS = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - NOW).count();
        pollfd     pfd          = {.fd = wl_display_get_fd(state.display), .events = POLLIN, .revents = 0};
        const int  POLL_RET     = poll(&pfd, 1, sc<int>(REMAINING_MS));

        if (POLL_RET <= 0) {
            wl_display_cancel_read(state.display);
            if (POLL_RET < 0 && errno == EINTR)
                continue;
            return false;
        }

        if (wl_display_read_events(state.display) < 0)
            return false;
        if (wl_display_dispatch_pending(state.display) < 0)
            return false;
    }
    return true;
}

static void disconnect(SState& state) {
    const auto DISPLAY = state.display;
    if (!DISPLAY)
        return;

    if (state.frame) {
        wl_proxy_marshal_flags(state.frame, 1 /*destroy*/, nullptr, wl_proxy_get_version(state.frame), WL_MARSHAL_FLAG_DESTROY);
        state.frame = nullptr;
    }
    state.buffer.reset();
    state.pool.reset();
    state.target.reset();
    state.handles.clear();
    state.toplevelMgr.reset();
    if (state.exportMgr) {
        wl_proxy_marshal_flags(state.exportMgr, 1 /*destroy*/, nullptr, wl_proxy_get_version(state.exportMgr), WL_MARSHAL_FLAG_DESTROY);
        state.exportMgr = nullptr;
    }
    state.shm.reset();
    state.registry.reset();

    if (state.bufferFD >= 0)
        close(state.bufferFD);

    state.display = nullptr;
    wl_display_flush(DISPLAY);
    wl_display_disconnect(DISPLAY);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println(stderr, "usage: toplevel-capture <app-id>");
        return 1;
    }

    SState state;
    state.targetAppID = argv[1];
    state.display     = wl_display_connect(nullptr);
    if (!state.display) {
        std::println(stderr, "failed to connect to Wayland display");
        return 1;
    }

    if (!bindGlobals(state)) {
        std::println(stderr, "wl_shm, zwlr_foreign_toplevel_manager_v1, or hyprland_toplevel_export_manager_v1 unavailable");
        disconnect(state);
        return 1;
    }

    if (!findTarget(state)) {
        std::println(stderr, "no foreign toplevel found for app ID '{}'", state.targetAppID);
        disconnect(state);
        return 1;
    }

    if (!requestCapture(state)) {
        std::println(stderr, "failed to request toplevel capture");
        disconnect(state);
        return 1;
    }

    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    dispatchUntilDone(state, DEADLINE);

    if (!state.frameReceived)
        std::println(stderr, "did not receive a captured frame");

    const bool RESULT = state.frameReceived;
    disconnect(state);
    return RESULT ? 0 : 1;
}