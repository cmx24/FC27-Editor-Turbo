// FC 27 LE Turbo GUI - Direct3D 12 overlay.
// Hooks IDXGISwapChain::Present/Present1/ResizeBuffers, IDXGISwapChain3::ResizeBuffers1 and
// ID3D12CommandQueue::ExecuteCommandLists (addresses taken from a throw-away swap chain), draws
// Dear ImGui into the game's back buffer on the game's direct queue right before Present, and
// subclasses the game window for input. Live Editor hooks the same functions; MinHook chains onto
// whatever is already there, so both overlays draw.
//
// No reference to a back buffer is kept between frames (each frame takes and releases it), so the
// game can always resize or replace its swap chain. Font/texture uploads use a private queue, so
// ImGui never waits on the game's queue.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "nlohmann/json.hpp"
#include "host.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "ui/app.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace host {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
                                                     IUnknown* const*);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

static PresentFn o_present = nullptr;
static Present1Fn o_present1 = nullptr;
static ResizeBuffersFn o_resize = nullptr;
static ResizeBuffers1Fn o_resize1 = nullptr;
static ExecuteCommandListsFn o_execute = nullptr;
static void* t_present = nullptr;
static void* t_present1 = nullptr;
static void* t_resize = nullptr;
static void* t_resize1 = nullptr;
static void* t_execute = nullptr;

struct FrameCtx {
    ID3D12CommandAllocator* alloc = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fence_value = 0;
};

// Direct queue used to draw the overlay: the swap chain's own queue when DXGI hands it back,
// else the last direct queue of the swap chain's device that submitted work.
static ID3D12CommandQueue* g_queue = nullptr;
static ID3D12CommandQueue* g_upload_queue = nullptr;  // private: ImGui texture uploads
static std::atomic<ID3D12CommandQueue*> g_last_direct{nullptr};
static ID3D12Device* g_device = nullptr;
static ID3D12DescriptorHeap* g_rtv_heap = nullptr;
static ID3D12DescriptorHeap* g_srv_heap = nullptr;
static ID3D12GraphicsCommandList* g_cmd = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE g_fence_event = nullptr;
static UINT64 g_fence_counter = 0;
static std::vector<FrameCtx> g_frames;
static IDXGISwapChain3* g_swapchain = nullptr;  // identity only, not referenced
static DXGI_FORMAT g_format = DXGI_FORMAT_UNKNOWN;
static HWND g_hwnd = nullptr;
static WNDPROC g_orig_wndproc = nullptr;
static std::atomic<bool> g_ready{false};
static bool g_failed = false;
static std::atomic<bool> g_in_frame{false};
// Crash-guard bookkeeping (see host::guard_hold): the start phase is proven after kStartFrames frames, the first frames
// drawn on screen after kDrawFrames submitted frames.
constexpr int kStartFrames = 300;
constexpr int kDrawFrames = 120;
static int g_frames_run = 0;
static bool g_start_proven = false;
static int g_frames_drawn = 0;
static bool g_draw_held = false;
static bool g_draw_proven = false;
static thread_local bool g_in_resize = false;
// Guards ImGui and the App on the render thread (Present / resize). The game's window thread never takes it:
// WndProc only queues messages (g_msg_mutex, held for a push) and decides from the previous frame's flags.
static std::recursive_mutex g_ui_mutex;
struct WinMsg {
    UINT msg;
    WPARAM wp;
    LPARAM lp;
};
static std::mutex g_msg_mutex;
static std::vector<WinMsg> g_msgs;
static std::atomic<bool> g_visible{false};
static std::atomic<bool> g_want_mouse{false};
static std::atomic<bool> g_want_keyboard{false};
static std::atomic<int> g_toggle_vk{0x77};
static std::atomic<int> g_toggle_presses{0};  // first key-down messages of the show/hide key, consumed by poll_input

static ProcessMemory* g_mem = nullptr;
static turbo::App* g_app = nullptr;
static std::string g_ini_path;
static LARGE_INTEGER g_qpf{}, g_qpc0{};

// ---------------------------------------------------------------- SRV descriptors for ImGui textures
static UINT g_srv_inc = 0;
static std::vector<int> g_srv_free;
static const int kSrvCount = 512;  // font + the picture cache (ui/textures.cpp keeps at most 320)

static void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    int idx = 0;
    if (!g_srv_free.empty()) {
        idx = g_srv_free.back();
        g_srv_free.pop_back();
    } else {
        log("ImGui asked for more than %d texture descriptors; reusing slot 0", kSrvCount);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE c = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE g = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
    c.ptr += static_cast<SIZE_T>(idx) * g_srv_inc;
    g.ptr += static_cast<UINT64>(idx) * g_srv_inc;
    *cpu = c;
    *gpu = g;
}

static void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    int idx = static_cast<int>((cpu.ptr - g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr) / g_srv_inc);
    if (idx >= 0 && idx < kSrvCount) g_srv_free.push_back(idx);
}

// ---------------------------------------------------------------- helpers
template <class T> static void safe_release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

static void wait_gpu_idle() {
    if (!g_queue || !g_fence) return;
    UINT64 v = ++g_fence_counter;
    if (FAILED(g_queue->Signal(g_fence, v))) return;
    if (g_fence->GetCompletedValue() < v) {
        g_fence->SetEventOnCompletion(v, g_fence_event);
        WaitForSingleObject(g_fence_event, 2000);
    }
}

// One command allocator + RTV slot per back buffer; rebuilt when the buffer count changes
static bool ensure_frames(IDXGISwapChain3* sc, DXGI_SWAP_CHAIN_DESC& desc) {
    if (FAILED(sc->GetDesc(&desc))) return false;
    UINT count = desc.BufferCount;
    if (count == 0 || count > 16) return false;
    if (g_frames.size() == count && g_rtv_heap) return true;
    wait_gpu_idle();
    for (auto& f : g_frames) safe_release(f.alloc);
    g_frames.assign(count, FrameCtx{});
    safe_release(g_rtv_heap);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = count;
    if (FAILED(g_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_rtv_heap)))) return false;
    UINT inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (auto& f : g_frames) {
        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc)))) return false;
        f.rtv = h;
        h.ptr += inc;
    }
    return true;
}

static double now_seconds() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart - g_qpc0.QuadPart) / static_cast<double>(g_qpf.QuadPart);
}

// ---------------------------------------------------------------- input
static bool is_toggle_key_msg(UINT msg, WPARAM wp) {
    return (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
           static_cast<int>(wp) == g_toggle_vk.load();
}

// FC 27 reads the mouse through raw input only (no legacy WM_LBUTTONDOWN / WM_MOUSEWHEEL reach the window), so a click
// shorter than one frame was missed by the per-frame poll. While no legacy mouse message has been seen, button and wheel
// changes are taken from WM_INPUT itself and queued as the equivalent window messages. Once a legacy mouse message
// arrives the game delivers them and this stops, so nothing is counted twice.
static std::atomic<bool> g_legacy_mouse_seen{false};

static void queue_raw_mouse(HWND hwnd, LPARAM lp, std::vector<WinMsg>& out) {
    if (g_legacy_mouse_seen.load()) return;
    host::TurboInputScope scope;  // Turbo's own read: the input shield passes the real data through
    RAWINPUT ri{};
    UINT size = sizeof(ri);
    UINT got = GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER));
    if (got == static_cast<UINT>(-1) || got < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) || ri.header.dwType != RIM_TYPEMOUSE) return;
    const USHORT f = ri.data.mouse.usButtonFlags;
    if (!f) return;
    POINT screen{}, client{};
    GetCursorPos(&screen);
    client = screen;
    ScreenToClient(hwnd, &client);
    const LPARAM at = MAKELPARAM(static_cast<short>(client.x), static_cast<short>(client.y));
    struct Map { USHORT flag; UINT msg; WPARAM wp; };
    static const Map maps[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN, WM_LBUTTONDOWN, MK_LBUTTON}, {RI_MOUSE_LEFT_BUTTON_UP, WM_LBUTTONUP, 0},
        {RI_MOUSE_RIGHT_BUTTON_DOWN, WM_RBUTTONDOWN, MK_RBUTTON}, {RI_MOUSE_RIGHT_BUTTON_UP, WM_RBUTTONUP, 0},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, WM_MBUTTONDOWN, MK_MBUTTON}, {RI_MOUSE_MIDDLE_BUTTON_UP, WM_MBUTTONUP, 0},
    };
    for (const auto& m : maps)
        if (f & m.flag) out.push_back({m.msg, m.wp, at});
    if (f & RI_MOUSE_WHEEL) {
        const short delta = static_cast<short>(ri.data.mouse.usButtonData);
        out.push_back({WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(static_cast<short>(screen.x), static_cast<short>(screen.y))});
    }
}

// Runs on the game's window thread: never waits for the render thread.
static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (is_toggle_key_msg(msg, wp)) {  // the show/hide key is Turbo's; the render thread toggles (poll_input)
        if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30))) ++g_toggle_presses;
        return 0;
    }
    if ((msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONDBLCLK) || msg == WM_MOUSEWHEEL) g_legacy_mouse_seen = true;
    if (g_ready && g_visible) {
        {
            std::vector<WinMsg> raw;
            if (msg == WM_INPUT) queue_raw_mouse(hwnd, lp, raw);
            std::lock_guard<std::mutex> lock(g_msg_mutex);
            if (g_msgs.size() < 1024) g_msgs.push_back({msg, wp, lp});
            for (const auto& r : raw)
                if (g_msgs.size() < 1024) g_msgs.push_back(r);
        }
        bool mouse_msg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_INPUT;
        bool key_msg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if ((mouse_msg && g_want_mouse) || (key_msg && g_want_keyboard)) {
            // Keep the game from seeing input meant for Turbo. Raw input still gets its system cleanup.
            if (msg == WM_INPUT) return DefWindowProcW(hwnd, msg, wp, lp);
            return 0;
        }
        if (msg == WM_SETCURSOR && g_want_mouse) return TRUE;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

// Render thread, before each ImGui frame: show/hide key and mouse are polled (they work whether the game gets window
// messages or only raw input), then the queued window messages (text, wheel, keys) go to ImGui.
static void poll_input() {
    ImGuiIO& io = ImGui::GetIO();
    g_toggle_vk = g_app->toggle_vk;
    HWND root = GetAncestor(g_hwnd, GA_ROOT);
    const bool fg = GetForegroundWindow() == (root ? root : g_hwnd);
    // Two sources for the show/hide key: the window message (catches even a very short tap) and polling (games that
    // only read raw input send no key message). One press seen by both counts once.
    static bool was_down = false;
    static DWORD last_toggle = 0;
    auto toggle = [&] {
        DWORD now = GetTickCount();
        if (now - last_toggle >= 250) {
            g_app->visible = !g_app->visible;
            last_toggle = now;
        }
    };
    if (g_toggle_presses.exchange(0) > 0) toggle();
    const bool down = fg && (GetAsyncKeyState(g_app->toggle_vk) & 0x8000) != 0;
    if (down && !was_down) toggle();
    was_down = down;
    g_visible = g_app->visible;

    std::vector<WinMsg> msgs;
    {
        std::lock_guard<std::mutex> lock(g_msg_mutex);
        msgs.swap(g_msgs);
    }
    if (g_app->visible) {
        for (const auto& m : msgs) ImGui_ImplWin32_WndProcHandler(g_hwnd, m.msg, m.wp, m.lp);
        if (fg) {
            POINT p;
            if (GetCursorPos(&p) && ScreenToClient(g_hwnd, &p)) io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
            const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
            const int vks[3] = {swapped ? VK_RBUTTON : VK_LBUTTON, swapped ? VK_LBUTTON : VK_RBUTTON, VK_MBUTTON};
            for (int b = 0; b < 3; ++b) io.AddMouseButtonEvent(b, (GetAsyncKeyState(vks[b]) & 0x8000) != 0);
        }
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
    } else {
        // Hidden: leave the game's cursor and mouse alone
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    }
}

// ---------------------------------------------------------------- init / render
static bool same_device(ID3D12CommandQueue* q, ID3D12Device* dev) {
    ID3D12Device* qd = nullptr;
    if (FAILED(q->GetDevice(IID_PPV_ARGS(&qd))) || !qd) return false;
    bool same = qd == dev;
    qd->Release();
    return same;
}

// Picks the device and the direct queue for the overlay. false = not yet (try again next frame).
static bool pick_queue(IDXGISwapChain3* sc) {
    // A D3D12 swap chain is created on a command queue; DXGI hands it back through GetDevice
    ID3D12CommandQueue* q = nullptr;
    if (SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&q))) && q) {
        D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT && SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&g_device))) && g_device) {
            g_queue = q;
            log("overlay queue: the swap chain's own queue");
            return true;
        }
        q->Release();
    }
    if (!g_device && FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) g_device = nullptr;
    ID3D12CommandQueue* last = g_last_direct.load();
    if (!last) return false;  // the game has not submitted work on a direct queue yet
    if (!g_device) {
        if (FAILED(last->GetDevice(IID_PPV_ARGS(&g_device))) || !g_device) return false;
    } else if (!same_device(last, g_device)) {
        return false;  // a queue of another device; wait for the game's own
    }
    last->AddRef();
    g_queue = last;
    log("overlay queue: last direct queue that submitted work");
    return true;
}

enum class Init { Ok, Retry, Fail };

static Init init_imgui(IDXGISwapChain3* sc) {
    if (!pick_queue(sc)) return Init::Retry;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!ensure_frames(sc, desc)) {
        log("cannot prepare per-frame resources");
        return Init::Fail;
    }
    g_hwnd = desc.OutputWindow;
    g_format = desc.BufferDesc.Format;

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_upload_queue)))) return Init::Fail;

    D3D12_DESCRIPTOR_HEAP_DESC sd{};
    sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sd.NumDescriptors = kSrvCount;
    sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&g_srv_heap)))) return Init::Fail;
    g_srv_inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srv_free.clear();
    for (int i = kSrvCount - 1; i >= 0; --i) g_srv_free.push_back(i);

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].alloc, nullptr, IID_PPV_ARGS(&g_cmd))))
        return Init::Fail;
    g_cmd->Close();
    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return Init::Fail;
    g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_fence_event) return Init::Fail;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // v2: window sizes follow the UI scale since 0.2.5; layouts saved by older versions (fixed small sizes) are not reused
    g_ini_path = (le_root() / "turbo_output" / "turbo_gui_layout_v2.ini").string();
    io.IniFilename = g_ini_path.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Colours and sizes: App::update_style (scaled to the window height and the user's UI size) on the first tick
    wchar_t windir[MAX_PATH];
    UINT wn = GetWindowsDirectoryW(windir, MAX_PATH);
    std::filesystem::path font = std::filesystem::path(std::wstring(windir, wn)) / "Fonts" / "segoeui.ttf";
    std::error_code ec;
    if (wn > 0 && std::filesystem::exists(font, ec)) io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f);

    if (!ImGui_ImplWin32_Init(g_hwnd)) return Init::Fail;
    ImGui_ImplDX12_InitInfo info;
    info.Device = g_device;
    info.CommandQueue = g_upload_queue;
    info.NumFramesInFlight = static_cast<int>(g_frames.size());
    info.RTVFormat = g_format;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = g_srv_heap;
    info.SrvDescriptorAllocFn = srv_alloc;
    info.SrvDescriptorFreeFn = srv_free;
    if (!ImGui_ImplDX12_Init(&info)) return Init::Fail;

    g_orig_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hk_wndproc)));
    if (!g_orig_wndproc) {
        log("cannot subclass the game window (error %lu)", GetLastError());
        return Init::Fail;
    }
    g_swapchain = sc;
    log("overlay ready: %u back buffers, format %d", static_cast<unsigned>(g_frames.size()), static_cast<int>(g_format));
    return Init::Ok;
}

// The game replaced its swap chain (e.g. display mode change): follow it when it renders to the same
// window in the same format.
static bool retarget(IDXGISwapChain3* sc) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d)) || d.OutputWindow != g_hwnd) return false;
    if (d.BufferDesc.Format != g_format) {
        static bool logged = false;
        if (!logged) log("new swap chain has another format (%d); overlay paused", static_cast<int>(d.BufferDesc.Format));
        logged = true;
        return false;
    }
    ID3D12CommandQueue* q = nullptr;
    if (SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&q))) && q) {
        if (q != g_queue && same_device(q, g_device)) {
            wait_gpu_idle();
            safe_release(g_queue);
            g_queue = q;
        } else {
            q->Release();
        }
    }
    g_swapchain = sc;
    log("overlay follows the game's new swap chain");
    return true;
}

static void render_frame_impl(IDXGISwapChain3* sc) {
    std::lock_guard<std::recursive_mutex> lock(g_ui_mutex);
    if (g_failed) return;
    if (!g_ready) {
        Init r = init_imgui(sc);
        if (r == Init::Retry) return;
        if (r == Init::Fail) {
            g_failed = true;
            log("overlay initialisation failed; Turbo GUI disabled for this session");
            return;
        }
        g_ready = true;
    }
    if (sc != g_swapchain && !retarget(sc)) return;

    try {
        TurboInputScope own_input;  // Turbo's own key / mouse reads see the real state (the input shield is for the game)
        g_app->tick(now_seconds());
        poll_input();
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = g_app->visible;
        g_app->draw();
        ImGui::Render();
        g_want_mouse = g_app->visible && ImGui::GetIO().WantCaptureMouse;
        g_want_keyboard = g_app->visible && ImGui::GetIO().WantCaptureKeyboard;
        if (!g_start_proven && ++g_frames_run >= kStartFrames) {
            g_start_proven = true;
            log("overlay proven: %d frames ran without problems", kStartFrames);
            guard_release();
        }
        // Which input APIs the game polls (once after about a minute, again after about ten)
        static long frames_total = 0;
        if (++frames_total == 3600 || frames_total == 36000) input_shield_report();
    } catch (const std::exception& e) {
        log("frame error: %s", e.what());
        if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->WithinFrameScope) ImGui::EndFrame();
        return;
    }
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdLists.Size == 0) return;  // nothing visible: no GPU work at all
    if (!g_draw_held && !g_draw_proven) {
        g_draw_held = true;
        log("first frame drawn on screen");
        guard_hold("first frames drawn on screen");
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (!ensure_frames(sc, desc)) return;
    UINT idx = sc->GetCurrentBackBufferIndex();
    if (idx >= g_frames.size()) return;
    ID3D12Resource* rt = nullptr;
    if (FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&rt))) || !rt) return;
    FrameCtx& fr = g_frames[idx];
    if (g_fence->GetCompletedValue() < fr.fence_value) {
        g_fence->SetEventOnCompletion(fr.fence_value, g_fence_event);
        if (WaitForSingleObject(g_fence_event, 1000) != WAIT_OBJECT_0) {
            rt->Release();
            return;  // GPU busy: skip the overlay this frame rather than reuse a running allocator
        }
    }
    g_device->CreateRenderTargetView(rt, nullptr, fr.rtv);
    fr.alloc->Reset();
    g_cmd->Reset(fr.alloc, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = rt;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_cmd->ResourceBarrier(1, &b);
    g_cmd->OMSetRenderTargets(1, &fr.rtv, FALSE, nullptr);
    g_cmd->SetDescriptorHeaps(1, &g_srv_heap);
    ImGui_ImplDX12_RenderDrawData(dd, g_cmd);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmd->ResourceBarrier(1, &b);
    g_cmd->Close();
    ID3D12CommandList* lists[] = {g_cmd};
    o_execute(g_queue, 1, lists);
    fr.fence_value = ++g_fence_counter;
    g_queue->Signal(g_fence, fr.fence_value);
    rt->Release();
    if (g_draw_held && !g_draw_proven && ++g_frames_drawn >= kDrawFrames) {
        g_draw_proven = true;
        g_draw_held = false;
        log("drawing proven: %d frames submitted without problems", kDrawFrames);
        guard_release();
    }
}

// Nothing may unwind into the game: any exception switches the overlay off for this session.
static void render_frame(IDXGISwapChain3* sc) {
    try {
        render_frame_impl(sc);
    } catch (const std::exception& e) {
        g_failed = true;
        g_visible = false;
        log("overlay error: %s; Turbo GUI disabled for this session (the game keeps running)", e.what());
    } catch (...) {
        g_failed = true;
        g_visible = false;
        log("unknown overlay error; Turbo GUI disabled for this session (the game keeps running)");
    }
}

// ---------------------------------------------------------------- hooks
static HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain3* sc, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST) && !g_in_frame.exchange(true)) {
        render_frame(sc);
        g_in_frame = false;
    }
    return o_present(sc, sync, flags);
}

static HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain3* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    if (!(flags & DXGI_PRESENT_TEST) && !g_in_frame.exchange(true)) {
        render_frame(sc);
        g_in_frame = false;
    }
    return o_present1(sc, sync, flags, p);
}

// Our last command list may still use the old back buffers: let it finish before a resize.
// The lock is not held across the real call (DXGI may talk to the window thread while resizing).
template <class Fn> static HRESULT around_resize(IDXGISwapChain3* sc, Fn call) {
    if (!g_in_resize && g_ready) {
        std::lock_guard<std::recursive_mutex> lock(g_ui_mutex);
        if (sc == g_swapchain) wait_gpu_idle();
    }
    bool outer = !g_in_resize;
    g_in_resize = true;
    HRESULT hr = call();
    if (outer) g_in_resize = false;
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain3* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    return around_resize(sc, [&] { return o_resize(sc, count, w, h, fmt, flags); });
}

static HRESULT STDMETHODCALLTYPE hk_resize1(IDXGISwapChain3* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags,
                                            const UINT* masks, IUnknown* const* queues) {
    return around_resize(sc, [&] { return o_resize1(sc, count, w, h, fmt, flags, masks, queues); });
}

// Remembers the last direct queue that submitted work (fallback for picking the overlay queue).
// Queue types are cached so GetDesc is not called on every submission.
static void STDMETHODCALLTYPE hk_execute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (!g_ready && q) {
        static std::atomic<ID3D12CommandQueue*> seen[8];
        static std::atomic<int> seen_direct[8];
        int slot = -1;
        for (int i = 0; i < 8; ++i)
            if (seen[i].load() == q) slot = i;
        bool direct;
        if (slot >= 0) {
            direct = seen_direct[slot].load() == 1;
        } else {
            D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
            direct = d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
            for (int i = 0; i < 8; ++i) {
                ID3D12CommandQueue* empty = nullptr;
                if (seen[i].compare_exchange_strong(empty, q)) {
                    seen_direct[i] = direct ? 1 : 0;
                    break;
                }
            }
        }
        if (direct) g_last_direct = q;
    }
    o_execute(q, n, lists);
}

// Throw-away swap chain to read the vtables
static bool find_targets() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TurboGuiProbe";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        log("probe window failed");
        return false;
    }
    log("probe: window created");
    bool ok = false;
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* sc = nullptr;
    do {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) break;
        log("probe: DXGI factory");
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) break;
        log("probe: D3D12 device");
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) break;
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = 64;
        d.Height = 64;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        log("probe: command queue");
        if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &d, nullptr, nullptr, &sc))) break;
        log("probe: swap chain");
        IDXGISwapChain3* sc3 = nullptr;
        if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) break;
        void** sc_vt = *reinterpret_cast<void***>(sc3);
        void** q_vt = *reinterpret_cast<void***>(queue);
        t_present = sc_vt[8];    // IDXGISwapChain::Present
        t_resize = sc_vt[13];    // IDXGISwapChain::ResizeBuffers
        t_present1 = sc_vt[22];  // IDXGISwapChain1::Present1
        t_resize1 = sc_vt[39];   // IDXGISwapChain3::ResizeBuffers1
        t_execute = q_vt[10];    // ID3D12CommandQueue::ExecuteCommandLists
        sc3->Release();
        ok = true;
    } while (false);
    if (sc) sc->Release();
    if (queue) queue->Release();
    if (dev) dev->Release();
    if (factory) factory->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (!ok) log("could not create the probe swap chain (D3D12 unavailable?)");
    return ok;
}

// ---------------------------------------------------------------- hook targets from TurboProbe.exe
// TurboProbe.exe creates the throw-away window, device and swap chain in its own process and reports module, image size,
// time stamp, offset and code bytes of each function. An address is used only if this process has loaded the very same
// module (path, image size, time stamp) and the code bytes from offset 16 match (an inline hook placed by another tool
// at the start does not matter). Nothing is created inside the game.
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static void* resolve_target(const nlohmann::json& j, const char* name) {
    if (!j.contains(name) || !j[name].is_object()) return nullptr;
    const auto& t = j[name];
    std::wstring module = widen(t.value("module", std::string()));
    HMODULE m = module.empty() ? nullptr : GetModuleHandleW(module.c_str());
    if (!m) {
        log("probe: %s: %s is not loaded in the game", name, t.value("module", std::string()).c_str());
        return nullptr;
    }
    auto* base = reinterpret_cast<const unsigned char*>(m);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage != t.value("image_size", 0u) || nt->FileHeader.TimeDateStamp != t.value("timestamp", 0u)) {
        log("probe: %s: the game's %s is another build than TurboProbe's", name, t.value("module", std::string()).c_str());
        return nullptr;
    }
    uint64_t rva = t.value("rva", uint64_t(0));
    if (rva == 0 || rva + 48 > nt->OptionalHeader.SizeOfImage) return nullptr;
    const unsigned char* fn = base + rva;
    MEMORY_BASIC_INFORMATION mbi{};
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!VirtualQuery(fn, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || !(mbi.Protect & exec) ||
        fn + 48 > static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize) {
        log("probe: %s: not executable code at the reported offset", name);
        return nullptr;
    }
    std::string want = t.value("code16", std::string());
    char have[65] = {0};
    for (int i = 0; i < 32; ++i) std::snprintf(have + i * 2, 3, "%02x", fn[16 + i]);
    if (want != have) {
        log("probe: %s: code differs from TurboProbe's", name);
        return nullptr;
    }
    return const_cast<unsigned char*>(fn);
}

static bool find_targets_external() {
    std::wstring exe = (le_root() / "turbo" / "TurboProbe.exe").wstring();
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        log("TurboProbe.exe not found next to Turbo.dll");
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    // Only the pipe's write end is handed to TurboProbe.exe: no other inheritable handle of the game reaches the child
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = wr;
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<unsigned char> attr_buf(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    HANDLE inherit[1] = {wr};
    bool have_attrs = attr_size > 0 && InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
                      UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr,
                                                nullptr);
    if (!have_attrs) {
        log("cannot limit the handles TurboProbe.exe inherits (error %lu); not running it", GetLastError());
        CloseHandle(wr);
        CloseHandle(rd);
        return false;
    }
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    log("running TurboProbe.exe (a separate process) to find the hook targets");
    BOOL started = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                  nullptr, nullptr, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(wr);
    if (!started) {
        CloseHandle(rd);
        log("cannot start TurboProbe.exe (error %lu)", GetLastError());
        return false;
    }
    std::string text;
    const DWORD t0 = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char buf[4096];
            DWORD got = 0;
            if (!ReadFile(rd, buf, avail < sizeof(buf) ? avail : sizeof(buf), &got, nullptr) || got == 0) break;
            text.append(buf, got);
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            DWORD left = 0;
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &left, nullptr) && left > 0) {
                char buf[4096];
                DWORD got = 0;
                if (!ReadFile(rd, buf, left < sizeof(buf) ? left : sizeof(buf), &got, nullptr) || got == 0) break;
                text.append(buf, got);
            }
            break;
        }
        if (GetTickCount() - t0 > 20000 || text.size() > 65536) {
            TerminateProcess(pi.hProcess, 1);
            log("TurboProbe.exe did not finish in 20 s");
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (code != 0 || j.is_discarded() || !j.is_object()) {
        log("TurboProbe.exe failed (exit %lu): %.200s", static_cast<unsigned long>(code), text.c_str());
        return false;
    }
    void* p = resolve_target(j, "present");
    void* p1 = resolve_target(j, "present1");
    void* r = resolve_target(j, "resize");
    void* r1 = resolve_target(j, "resize1");
    void* e = resolve_target(j, "execute");
    if (!p || !p1 || !r || !r1 || !e) return false;
    t_present = p;
    t_present1 = p1;
    t_resize = r;
    t_resize1 = r1;
    t_execute = e;
    log("hook targets found by TurboProbe.exe and verified in the game (nothing created inside the game)");
    return true;
}

// FC 27 ships its own Direct3D 12 runtime (x64\D3D12Core.dll, Agility SDK). TurboProbe.exe loads the system one, so its
// ExecuteCommandLists can never match the game's: running it only costs a process start, so it is skipped.
static bool game_ships_own_d3d12core() {
    HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
    if (!core) return false;
    wchar_t path[MAX_PATH * 2] = {0}, sys[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(core, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    UINT sn = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || sn == 0) return false;
    std::wstring p(path, n), d(sys, sn);
    auto lower = [](std::wstring x) {
        for (auto& c : x) c = static_cast<wchar_t>(towlower(c));
        return x;
    };
    if (lower(p).rfind(lower(d) + L"\\", 0) == 0) return false;
    log("the game uses its own Direct3D 12 runtime (%ls): TurboProbe.exe cannot match it, skipped", path);
    return true;
}

bool start_overlay(HMODULE) {
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qpc0);

    // Mailbox for commands to Turbo's Lua side
    void* mailbox = VirtualAlloc(nullptr, turbo::kMailboxSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mailbox) log("cannot allocate the command mailbox; Turbo Tools disabled");
    char session[32];
    std::snprintf(session, sizeof(session), "%08lX%08lX", static_cast<unsigned long>(GetCurrentProcessId()),
                  static_cast<unsigned long>(GetTickCount()));
    g_mem = new ProcessMemory();
    g_app = new turbo::App(*g_mem, le_root(), reinterpret_cast<uint64_t>(mailbox), session);
    g_app->bridge.set_min_file_time(load_time() - std::chrono::minutes(2));
    g_app->log_hook = [](const std::string& s) { log("%s", s.c_str()); };
    if (g_app->mailbox) start_memmap(reinterpret_cast<uint64_t>(mailbox));
    g_toggle_vk = g_app->toggle_vk;

    if (!game_ships_own_d3d12core() && find_targets_external()) {
        guard_hold("hooks being installed / first frames");
    } else {
        log("falling back to probing Direct3D 12 inside the game for the hook targets");
        guard_hold("probing Direct3D 12 in the game / hooks being installed / first frames");
        if (!find_targets()) return false;
    }
    log("hook targets found; initialising MinHook");
    if (MH_Initialize() != MH_OK) {
        log("MinHook init failed");
        return false;
    }
    struct Hook {
        void* target;
        void* detour;
        void** original;
        const char* name;
    } hooks[] = {
        {t_present, reinterpret_cast<void*>(&hk_present), reinterpret_cast<void**>(&o_present), "Present"},
        {t_present1, reinterpret_cast<void*>(&hk_present1), reinterpret_cast<void**>(&o_present1), "Present1"},
        {t_resize, reinterpret_cast<void*>(&hk_resize), reinterpret_cast<void**>(&o_resize), "ResizeBuffers"},
        {t_resize1, reinterpret_cast<void*>(&hk_resize1), reinterpret_cast<void**>(&o_resize1), "ResizeBuffers1"},
        {t_execute, reinterpret_cast<void*>(&hk_execute), reinterpret_cast<void**>(&o_execute), "ExecuteCommandLists"},
    };
    for (const auto& h : hooks) {
        MH_STATUS st = MH_CreateHook(h.target, h.detour, h.original);
        if (st != MH_OK) {
            log("hooking %s failed: %s", h.name, MH_StatusToString(st));
            MH_Uninitialize();
            return false;
        }
    }
    log("hooks created; enabling them");
    MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK) {
        log("enabling hooks failed: %s", MH_StatusToString(st));
        MH_Uninitialize();
        return false;
    }
    log("hooks installed; press %s in game to show Turbo", turbo::key_name(g_app->toggle_vk));
    install_input_shield();
    start_devtools();
    return true;
}

HWND game_window() { return g_hwnd; }

bool input_block_mouse() { return g_ready && g_visible && g_want_mouse; }
bool input_block_keyboard() { return g_ready && g_visible && g_want_keyboard; }

void stop_overlay() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

}  // namespace host
