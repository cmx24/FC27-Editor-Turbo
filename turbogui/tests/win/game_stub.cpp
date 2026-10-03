// FC 27 LE Turbo GUI - Direct3D 12 stand-in "game" for the overlay test (Wine + vkd3d + software Vulkan, or Windows).
// It is NOT FC 27: a window, a D3D12 device, a flip-model swap chain and one clear colour per frame. Part-way it loads the
// stub FCLiveEditor.DLL and turbo\Turbo.dll the way Turbo's lua\autorun does while the game starts (mode "launch" in
// turbo_output\turbo_gui_load.json), writes Live Editor's log lines for this process (<LE>\Logs\live_editor_<date>.log:
// the session header with FCLiveEditor.DLL's base, later "Initial setup done"), presses the show/hide key with SendInput,
// resizes the swap chain, and keeps presenting.
//
//   game_stub.exe <LE folder> <seconds> <load_turbo_ms> <toggle_ms> <resize_ms> <status file> [pause_ms] [le_done_ms]
//
// pause_ms: after loading Turbo, stop presenting for this long (like a loading screen) while still pumping messages.
// le_done_ms: when Live Editor's log reports "Initial setup done" (default: load_turbo_ms + 500).
//
// The status file gets "frames=<n> resized=<0|1> errors=<n>" at exit. Exit code 0 = the game ran to the end.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <fstream>
#include <string>

static const UINT kBuffers = 2;
static HWND g_hwnd = nullptr;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Pressed from its own thread, like real input: the game keeps rendering while the key is held
static DWORD WINAPI press_thread(LPVOID p) {
    void press_key(WORD vk);
    press_key(static_cast<WORD>(reinterpret_cast<UINT_PTR>(p)));
    return 0;
}

void press_key(WORD vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(120);  // held for several frames, like a real key press
    std::printf("GAME: while held: GetAsyncKeyState=0x%04X foreground_is_game=%d\n",
                static_cast<unsigned>(GetAsyncKeyState(vk)) & 0xFFFF, GetForegroundWindow() == g_hwnd);
    SendInput(1, &in[1], sizeof(INPUT));
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 7) {
        std::printf("usage: game_stub <LE folder> <seconds> <load_turbo_ms> <toggle_ms> <resize_ms> <status file> [pause_ms]\n");
        return 2;
    }
    const std::wstring le = argv[1];
    const DWORD run_ms = static_cast<DWORD>(_wtoi(argv[2]) * 1000);
    const DWORD load_ms = static_cast<DWORD>(_wtoi(argv[3]));
    const DWORD toggle_ms = static_cast<DWORD>(_wtoi(argv[4]));
    const DWORD resize_ms = static_cast<DWORD>(_wtoi(argv[5]));
    const std::wstring status_file = argv[6];
    const DWORD pause_ms = argc > 7 ? static_cast<DWORD>(_wtoi(argv[7])) : 0;
    const DWORD le_done_ms = argc > 8 ? static_cast<DWORD>(_wtoi(argv[8])) : load_ms + 500;
    const std::wstring le_log = le + L"\\Logs\\live_editor_02-10-2026.log";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = L"FC27GameStub";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"D3D12 game stub", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 1024, 768, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 3;
    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    IDXGIFactory4* factory = nullptr;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 4;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        std::printf("GAME: no Direct3D 12 device\n");
        return 4;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return 4;
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 1024;
    sd.Height = 768;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc1 = nullptr;
    IDXGISwapChain3* sc = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1))) return 5;
    if (FAILED(sc1->QueryInterface(IID_PPV_ARGS(&sc)))) return 5;

    ID3D12DescriptorHeap* rtv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kBuffers;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap)))) return 6;
    const UINT rtv_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12CommandAllocator* alloc[kBuffers] = {};
    for (UINT i = 0; i < kBuffers; ++i)
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc[i])))) return 6;
    ID3D12GraphicsCommandList* cl = nullptr;
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0], nullptr, IID_PPV_ARGS(&cl)))) return 6;
    cl->Close();
    ID3D12Fence* fence = nullptr;
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fence_value = 0, frame_fence[kBuffers] = {};

    auto wait_idle = [&] {
        queue->Signal(fence, ++fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, ev);
            WaitForSingleObject(ev, 5000);
        }
    };

    const DWORD t0 = GetTickCount();
    bool loaded = false, toggled = false, resized = false, le_done = false;
    int frames = 0, errors = 0;
    // Live Editor reports its setup on its own clock, also while the game shows a loading screen
    auto maybe_le_done = [&] {
        if (loaded && !le_done && GetTickCount() - t0 >= le_done_ms) {
            le_done = true;
            std::ofstream(le_log.c_str(), std::ios::binary | std::ios::app) << "20:24:42.331781\tINFO\tInitial setup done\n";
            std::printf("GAME: frame %d, Live Editor log: Initial setup done\n", frames);
            std::fflush(stdout);
        }
    };
    MSG msg;
    while (GetTickCount() - t0 < run_ms) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const DWORD now = GetTickCount() - t0;
        if (!loaded && now >= load_ms) {
            loaded = true;
            HMODULE stub = LoadLibraryW((le + L"\\FCLiveEditor.DLL").c_str());
            // Live Editor's session header (written when it is injected, before its Lua autorun scripts run)
            CreateDirectoryW((le + L"\\Logs").c_str(), nullptr);
            char header[160];
            std::snprintf(header, sizeof(header), "20:24:09.779458\tINFO\tModule <FCLiveEditor.DLL> 0x%llX-0x%llX\n",
                          reinterpret_cast<unsigned long long>(stub), reinterpret_cast<unsigned long long>(stub) + 0xA6D000);
            std::ofstream(le_log.c_str(), std::ios::binary | std::ios::app)
                << "20:24:09.779039\tINFO\tFC 27 Live Editor - v27.1.2\n" << header
                << "20:24:09.993802\tINFO\t[LUA] Execute: lua\\autorun\\turbo_boot.lua\n";
            // what Turbo's autorun writes right before package.loadlib
            std::ofstream((le + L"\\turbo_output\\turbo_gui_load.json").c_str()) << "{\"mode\":\"launch\",\"time\":1}";
            HMODULE turbo = LoadLibraryW((le + L"\\turbo\\Turbo.dll").c_str());
            std::printf("GAME: frame %d, loaded stub=%d Turbo=%d\n", frames, stub != nullptr, turbo != nullptr);
            std::fflush(stdout);
            const DWORD p0 = GetTickCount();
            while (GetTickCount() - p0 < pause_ms) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                maybe_le_done();
                Sleep(10);
            }
            if (pause_ms) std::printf("GAME: resumed after a %lu ms pause\n", static_cast<unsigned long>(pause_ms));
        }
        maybe_le_done();
        if (!toggled && now >= toggle_ms) {
            toggled = true;
            SetForegroundWindow(hwnd);
            HANDLE th = CreateThread(nullptr, 0, press_thread, reinterpret_cast<LPVOID>(static_cast<UINT_PTR>(VK_F8)), 0, nullptr);
            if (th) CloseHandle(th);
            std::printf("GAME: frame %d, pressing F8\n", frames);
            std::fflush(stdout);
        }
        if (!resized && resize_ms > 0 && now >= resize_ms) {
            resized = true;
            wait_idle();
            HRESULT hr = sc->ResizeBuffers(0, 1280, 720, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) ++errors;
            std::printf("GAME: frame %d, ResizeBuffers 1280x720 hr=0x%08lX\n", frames, static_cast<unsigned long>(hr));
            std::fflush(stdout);
        }

        UINT idx = sc->GetCurrentBackBufferIndex();
        if (fence->GetCompletedValue() < frame_fence[idx]) {
            fence->SetEventOnCompletion(frame_fence[idx], ev);
            WaitForSingleObject(ev, 5000);
        }
        ID3D12Resource* bb = nullptr;
        if (FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&bb)))) {
            ++errors;
            break;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(idx) * rtv_inc;
        dev->CreateRenderTargetView(bb, nullptr, rtv);
        alloc[idx]->Reset();
        cl->Reset(alloc[idx], nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = bb;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &b);
        const float colour[4] = {0.10f, 0.30f, 0.20f, 1.0f};  // the game's own picture: plain green
        cl->ClearRenderTargetView(rtv, colour, 0, nullptr);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cl->ResourceBarrier(1, &b);
        cl->Close();
        ID3D12CommandList* lists[] = {cl};
        queue->ExecuteCommandLists(1, lists);
        bb->Release();
        if (FAILED(sc->Present(1, 0))) ++errors;
        frame_fence[idx] = ++fence_value;
        queue->Signal(fence, frame_fence[idx]);
        ++frames;
    }
    wait_idle();
    if (FILE* f = _wfopen(status_file.c_str(), L"w")) {
        std::fprintf(f, "frames=%d resized=%d errors=%d\n", frames, resized ? 1 : 0, errors);
        std::fclose(f);
    }
    std::printf("GAME: ran %d frames, errors %d\n", frames, errors);
    std::fflush(stdout);
    ExitProcess(errors == 0 ? 0 : 1);
}
