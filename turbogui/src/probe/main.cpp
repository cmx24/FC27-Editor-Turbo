// FC 27 LE Turbo - TurboProbe.exe: finds where the functions Turbo.dll hooks live, in a separate process.
//
// Turbo.dll needs the addresses of IDXGISwapChain::Present/Present1/ResizeBuffers, IDXGISwapChain3::ResizeBuffers1 and
// ID3D12CommandQueue::ExecuteCommandLists. Reading them from a throw-away swap chain means creating a DXGI factory,
// a D3D12 device and a swap chain; doing that inside the running game, while it renders, can stall it. This program does
// it in its own process and prints, for each function: the module file, its image size and time stamp, the offset of the
// function in it, and 32 code bytes from offset 16 (past any inline hook another tool may have placed at the start).
// Turbo.dll uses an address only if the game has loaded the very same module and the bytes match.
//
// Output (stdout): one JSON object. Exit code 0 = all five found.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <string>

static std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

static bool describe(const char* name, void* fn, std::string& out) {
    HMODULE m = nullptr;
    if (!fn || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(fn), &m) || !m)
        return false;
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return false;
    auto* base = reinterpret_cast<const unsigned char*>(m);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    char code[65] = {0};
    auto* f = static_cast<const unsigned char*>(fn);
    for (int i = 0; i < 32; ++i) std::snprintf(code + i * 2, 3, "%02x", f[16 + i]);
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
                  "\"%s\":{\"module\":\"%s\",\"rva\":%llu,\"image_size\":%lu,\"timestamp\":%lu,\"code16\":\"%s\"}", name,
                  json_escape(utf8(path)).c_str(), static_cast<unsigned long long>(f - base),
                  static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage),
                  static_cast<unsigned long>(nt->FileHeader.TimeDateStamp), code);
    if (!out.empty()) out += ",";
    out += buf;
    return true;
}

int wmain() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TurboProbeWindow";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::printf("{\"error\":\"window\"}\n");
        return 1;
    }
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    IDXGISwapChain3* sc = nullptr;
    const char* err = nullptr;
    do {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { err = "factory"; break; }
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) { err = "device"; break; }
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) { err = "queue"; break; }
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = 64;
        d.Height = 64;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &d, nullptr, nullptr, &sc1))) { err = "swapchain"; break; }
        if (FAILED(sc1->QueryInterface(IID_PPV_ARGS(&sc))) || !sc) { err = "swapchain3"; break; }
    } while (false);
    if (err) {
        std::printf("{\"error\":\"%s\"}\n", err);
        return 1;
    }
    void** sc_vt = *reinterpret_cast<void***>(sc);
    void** q_vt = *reinterpret_cast<void***>(queue);
    std::string out;
    bool ok = describe("present", sc_vt[8], out) && describe("present1", sc_vt[22], out) && describe("resize", sc_vt[13], out) &&
              describe("resize1", sc_vt[39], out) && describe("execute", q_vt[10], out);
    std::printf("{%s%s\"pid\":%lu}\n", out.c_str(), out.empty() ? "" : ",", static_cast<unsigned long>(GetCurrentProcessId()));
    std::fflush(stdout);
    ExitProcess(ok ? 0 : 1);  // no cleanup needed: the process ends here
}
