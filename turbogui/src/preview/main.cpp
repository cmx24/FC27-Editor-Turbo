// FC 27 LE Turbo GUI preview host (tools/gui-preview).
//
// Draws a Turbo page with the REAL page code (src/ui, the same Dear ImGui, theme and font as the overlay) on a Direct3D 11 WARP (CPU)
// device into an offscreen target and writes a PNG. No game, no hooks, no GPU contention: the data layer is a fake world.
//
//   gui_preview --list
//   gui_preview --selftest
//   gui_preview --page <area>/<page> [--size 1920x1080[,2560x1440]] --out <file.png> [--world <dir>] [--data <dir>]
//   gui_preview --all [--size ...] --out <dir>
//
// The fake world (--world, default build/preview/world) is written by tests/native/gui_world.lua (the Turbo Lua simulator) with
// TURBO_PREVIEW_FIXTURE=tests/preview/preview_fixture.json: world.img (a TSIM page image of the game's database), LE/ (the
// turbo_output bridge files) and expected.json. The host reads world.img through preview::SimMemory (a turbo::Memory); nothing here
// reads or writes game memory, and nothing leaves a scratch folder: LOCALAPPDATA, USERPROFILE, HOME and OneDrive point there and
// the Live Editor root is a scratch copy of the world's LE folder.
//
// Exit codes: 0 ok, 2 usage or unknown page id (nothing is created), 3 setup failed (no world, no device), 4 a render failed.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/bridge.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "nlohmann/json.hpp"
#include "preview/page_ids.h"
#include "preview/registry.h"
#include "preview/sim_memory.h"
#include "ui/app.h"
#include "ui/ui_shell.h"
#include "ui/ui_x2_state.h"
#if __has_include("ui/ui5_theme.h")
#include "ui/ui5_theme.h"
#define PREVIEW_HAS_UI5 1
#endif

namespace fs = std::filesystem;
using nlohmann::json;
using turbo::shell::Area;
using turbo::shell::Page;
using turbo::shell::Place;

// IMGUI_USER_CONFIG (turbo_imconfig.h) routes IM_ASSERT here. The overlay logs it; the native tests throw it; the preview throws it too,
// and the page that raised it is reported as failed with the expression (a half-built frame is never drawn).
namespace turbo {
void imgui_assert_failed(const char* expr, const char* file, int line) {
    throw std::runtime_error(std::string("ImGui assertion failed: ") + expr + " (" + file + ":" + std::to_string(line) + ")");
}
}  // namespace turbo

namespace {

constexpr int kExitUsage = 2, kExitSetup = 3, kExitRender = 4;

struct Options {
    bool list = false, selftest = false, all = false;
    std::string page, out, world = "build/preview/world", data = "../turbo/package/turbo/data", scratch, theme;
    float scale = 0.0f;
    long long player = 1021;  // the player the Player page opens on (a fixture id; 0 = the page's own default)
    std::vector<std::pair<int, int>> sizes;
    bool keep_scratch = false;
};

void usage(FILE* f) {
    std::fprintf(f,
                 "usage: gui_preview --list | --selftest\n"
                 "       gui_preview --page <area>/<page> [--size WxH[,WxH]] --out <file.png> [--world <dir>] [--data <dir>]\n"
                 "       gui_preview --all [--size WxH[,WxH]] --out <dir>\n"
                 "  page ids come from --list (the rail and page-strip labels, lower case, e.g. home/home, players/player, club/squad)\n"
                 "  default size 1920x1080; --world is the folder gui_world.lua build wrote (world.img, LE/)\n");
}

bool parse_sizes(const std::string& s, std::vector<std::pair<int, int>>& out) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        int w = 0, h = 0;
        char x = 0;
        std::stringstream is(item);
        if (!(is >> w >> x >> h) || (x != 'x' && x != 'X') || w < 320 || h < 240 || w > 7680 || h > 4320) return false;
        out.emplace_back(w, h);
    }
    return !out.empty();
}

bool parse_args(int argc, char** argv, Options& o, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&](std::string& dst) {
            if (i + 1 >= argc) {
                err = a + " needs a value";
                return false;
            }
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--list") o.list = true;
        else if (a == "--selftest") o.selftest = true;
        else if (a == "--all") o.all = true;
        else if (a == "--keep-scratch") o.keep_scratch = true;
        else if (a == "--page") { if (!val(o.page)) return false; }
        else if (a == "--out") { if (!val(o.out)) return false; }
        else if (a == "--world") { if (!val(o.world)) return false; }
        else if (a == "--data") { if (!val(o.data)) return false; }
        else if (a == "--scratch") { if (!val(o.scratch)) return false; }
        else if (a == "--theme") { if (!val(o.theme)) return false; }
        else if (a == "--scale") { if (!val(v)) return false; o.scale = static_cast<float>(std::atof(v.c_str())); }
        else if (a == "--player") { if (!val(v)) return false; o.player = std::atoll(v.c_str()); }
        else if (a == "--size") {
            if (!val(v)) return false;
            if (!parse_sizes(v, o.sizes)) {
                err = "bad --size '" + v + "' (WxH, 320x240 to 7680x4320, comma separated)";
                return false;
            }
        } else {
            err = "unknown option '" + a + "'";
            return false;
        }
    }
    if (o.sizes.empty()) o.sizes.emplace_back(1920, 1080);
    return true;
}

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// ---------------------------------------------------------------- Direct3D 11 on WARP, offscreen
struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D *rt = nullptr, *staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    int w = 0, h = 0;

    bool init_device() {
        D3D_FEATURE_LEVEL fl{};
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        // WARP: the CPU rasteriser, so a preview never competes with the game for the GPU
        return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 3, D3D11_SDK_VERSION, &dev, &fl, &ctx));
    }
    bool make_target(int width, int height) {
        release(rtv);
        release(rt);
        release(staging);
        w = width;
        h = height;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(w);
        d.Height = static_cast<UINT>(h);
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &rt)) || FAILED(dev->CreateRenderTargetView(rt, nullptr, &rtv))) return false;
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging));
    }
    void draw(ImDrawData* dd, const float clear[4]) {
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(dd);
    }
    // 24-bit BGR rows, top down
    bool read_back(std::vector<uint8_t>& bgr) {
        ctx->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return false;
        bgr.assign(static_cast<size_t>(w) * h * 3, 0);
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
            uint8_t* d = bgr.data() + static_cast<size_t>(y) * w * 3;
            for (int x = 0; x < w; ++x) {
                d[x * 3 + 0] = s[x * 4 + 0];
                d[x * 3 + 1] = s[x * 4 + 1];
                d[x * 3 + 2] = s[x * 4 + 2];
            }
        }
        ctx->Unmap(staging, 0);
        return true;
    }
    ~Gpu() {
        release(rtv);
        release(rt);
        release(staging);
        release(ctx);
        release(dev);
    }
};

// ---------------------------------------------------------------- PNG through WIC
bool write_png(const fs::path& path, int w, int h, const std::vector<uint8_t>& bgr) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    IWICImagingFactory* f = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    IPropertyBag2* props = nullptr;
    IWICStream* st = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&fr, &props)) && SUCCEEDED(fr->Initialize(props)) &&
        SUCCEEDED(fr->SetSize(static_cast<UINT>(w), static_cast<UINT>(h)))) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(fr->SetPixelFormat(&pf)) && pf == GUID_WICPixelFormat24bppBGR &&
            SUCCEEDED(fr->WritePixels(static_cast<UINT>(h), static_cast<UINT>(w * 3), static_cast<UINT>(bgr.size()),
                                      const_cast<BYTE*>(bgr.data()))) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit()))
            ok = true;
    }
    release(props);
    release(fr);
    release(enc);
    release(st);
    release(f);
    if (!ok) fs::remove(path, ec);
    return ok;
}

// ---------------------------------------------------------------- the fake game
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct World {
    fs::path dir;      // --world
    fs::path scratch;  // working copy root
    fs::path le;       // scratch/LE (the Live Editor root the App writes its bridge files under)
    json expected;
    bool prepare(const Options& o, std::string& err) {
        dir = fs::absolute(o.world);
        if (!fs::exists(dir / "world.img") || !fs::exists(dir / "LE")) {
            err = "no fake world in " + dir.string() + " (world.img and LE/ are written by: lua5.4 tests/native/gui_world.lua build <dir> with TURBO_PREVIEW_FIXTURE=tests/preview/preview_fixture.json)";
            return false;
        }
        std::error_code ec;
        scratch = o.scratch.empty() ? fs::temp_directory_path() / ("gui_preview_" + std::to_string(GetCurrentProcessId())) : fs::absolute(o.scratch);
        fs::remove_all(scratch, ec);
        fs::create_directories(scratch / "home", ec);
        fs::copy(dir / "LE", scratch / "LE", fs::copy_options::recursive, ec);
        if (ec) {
            err = "cannot copy the world's LE folder to " + scratch.string() + ": " + ec.message();
            return false;
        }
        le = scratch / "LE";
        fs::remove(le / "turbo_output" / "gui_settings.json", ec);
        // nothing may read the real user profile or the game's settings folder
        const std::string home = (scratch / "home").string();
        _putenv_s("LOCALAPPDATA", home.c_str());
        _putenv_s("APPDATA", home.c_str());
        _putenv_s("USERPROFILE", home.c_str());
        _putenv_s("HOME", home.c_str());
        _putenv_s("OneDrive", "");
        try {
            if (fs::exists(dir / "expected.json")) expected = json::parse(slurp(dir / "expected.json"));
        } catch (...) {
        }
        return true;
    }
};

struct PageResult {
    std::string id, file, note;
    bool ok = false;
    int frames = 0;
    double seconds = 0.0;
    std::string landed;  // the id the app reports after the frames
    int failed_reads = 0;
};

// One page, one size: a fresh ImGui context and App (a page never inherits another page's state), one shared GPU device.
PageResult render_page(Gpu& gpu, World& world, const Options& o, const Place& want, int w, int h, const fs::path& png) {
    PageResult r;
    r.id = preview::id_of(want);
    r.file = png.string();
    const auto t0 = std::chrono::steady_clock::now();
    if (!gpu.make_target(w, h)) {
        r.note = "cannot create the render target";
        return r;
    }

    preview::SimMemory mem;
    if (!mem.load(world.dir / "world.img")) {
        r.note = "world.img does not load";
        return r;
    }
    const uint64_t kMb = 0x31000000;
    mem.map(kMb, turbo::kMailboxSize);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    // the overlay's font (src/win/overlay_dx12.cpp): Segoe UI 17 px from the Windows folder
    {
        wchar_t windir[MAX_PATH];
        const UINT wn = GetWindowsDirectoryW(windir, MAX_PATH);
        const fs::path font = fs::path(std::wstring(windir, wn)) / "Fonts" / "segoeui.ttf";
        std::error_code ec;
        if (wn > 0 && fs::exists(font, ec)) io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f);
        else r.note += "segoeui.ttf missing (default font); ";
    }
    bool backend_ok = ImGui_ImplDX11_Init(gpu.dev, gpu.ctx);
    if (!backend_ok) {
        r.note = "ImGui DX11 backend did not start";
        ImGui::DestroyContext();
        return r;
    }

    {
        turbo::App app(mem, world.le, kMb, "preview");
        // the shipped player type list where the screens look for it (the native tests do the same)
        {
            std::error_code ec;
            fs::create_directories(app.bridge.root() / "turbo" / "data", ec);
            fs::copy_file(fs::absolute(o.data) / "archetypes.json", app.bridge.root() / "turbo" / "data" / "archetypes.json",
                          fs::copy_options::overwrite_existing, ec);
            if (ec) r.note += "archetypes.json not found in --data " + o.data + " (the player type screens show nothing); ";
        }
        app.set_keep_players(false);
        app.visible = true;
        // Pictures come from the game's files through Turbo's Lua side, which asks for them and waits: with no game the queue never
        // drains ("Loading pictures: 0 of 36" for ever). The documented off switch (gui.preload_images) is used, so every picture
        // shows the placeholder a missing one has in the game; the RESULTS file says so.
        app.prefs.preload_images = false;
        if (!o.theme.empty()) turbo::x2::set_theme_mode(turbo::x2::theme_mode_from_name(o.theme));
        if (o.scale > 0.0f) app.ui_scale_user = o.scale;
        const float clear[4] = {0.047f, 0.055f, 0.075f, 1.0f};
        double t = 0.0;
        int settle = 0;
        bool navigated = false;
        const int kMaxFrames = 900, kMinFrames = 50;
        try {
        for (int f = 0; f < kMaxFrames; ++f) {
            ImGui_ImplDX11_NewFrame();
            io.DeltaTime = 1.0f / 30.0f;
            ImGui::NewFrame();
            app.tick(t);
            if (!navigated && app.connected()) {
                if (o.player) app.sel_player = o.player;
                app.go(want.area, want.page);
                navigated = true;
            }
            app.draw();
            ImGui::Render();
            gpu.draw(ImGui::GetDrawData(), clear);
            t += 1.0 / 30.0;
            ++r.frames;
            if (navigated) {
                const bool loading = !app.preload_line().empty() || app.textures.preload_queued() > 0;
                if (!loading) ++settle;
                else settle = 0;
                if (r.frames >= kMinFrames && settle >= 25) break;
            }
        }
        } catch (const std::exception& e) {
            r.note += std::string(e.what()) + "; ";
            navigated = false;
        }
        std::vector<uint8_t> bgr;
        if (!navigated) {
            r.note += "the app never connected to the fake world" + (app.db_error.empty() ? std::string() : (": " + app.db_error)) + "; ";
        } else if (!gpu.read_back(bgr)) {
            r.note += "readback failed; ";
        } else if (!write_png(png, w, h, bgr)) {
            r.note += "PNG write failed; ";
        } else {
            r.ok = true;
        }
        r.landed = preview::id_of(app.place());
        if (r.landed != r.id) {
            r.ok = false;
            r.note += "the app opened " + r.landed + " instead (the shell refused the page); ";
            std::error_code ec;
            fs::remove(png, ec);
        }
        if (!turbo::shell::registered_page(want.page) && !turbo::shell::registered_new_page(want.page)) r.note += "drawn by the hub, not by a shell page function; ";
        r.failed_reads = mem.failed_reads;
        // App is destroyed before the backend: its picture cache hands textures back to the renderer
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

// ---------------------------------------------------------------- a standalone page (registry.h): no App, no world, no game memory
PageResult render_standalone(Gpu& gpu, const std::string& name, const preview::StandaloneFn& fn, int w, int h, const fs::path& png) {
    PageResult r;
    r.id = preview::standalone_id(name);
    r.file = png.string();
    const auto t0 = std::chrono::steady_clock::now();
    if (!gpu.make_target(w, h)) {
        r.note = "cannot create the render target";
        return r;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    {
        wchar_t windir[MAX_PATH];
        const UINT wn = GetWindowsDirectoryW(windir, MAX_PATH);
        const fs::path font = fs::path(std::wstring(windir, wn)) / "Fonts" / "segoeui.ttf";
        std::error_code ec;
        if (wn > 0 && fs::exists(font, ec)) io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f);
        else r.note += "segoeui.ttf missing (default font); ";
    }
    if (!ImGui_ImplDX11_Init(gpu.dev, gpu.ctx)) {
        r.note = "ImGui DX11 backend did not start";
        ImGui::DestroyContext();
        return r;
    }
    // The kit's dp() is px * style.FontScaleDpi; the app sets that to the screen scale (height / 1080, 1 to 3: App::update_style)
    ImGui::GetStyle().FontScaleDpi = std::max(1.0f, std::min(3.0f, static_cast<float>(h) / 1080.0f));
    const float clear[4] = {0.043f, 0.059f, 0.090f, 1.0f};  // 5.0 bg0 #0B0F17
    bool drawn = false;
    try {
        for (int f = 0; f < 40; ++f) {  // a few frames: the glyph atlas fills, springs settle
            ImGui_ImplDX11_NewFrame();
            io.DeltaTime = 1.0f / 30.0f;
            ImGui::NewFrame();
#ifdef PREVIEW_HAS_UI5
            turbo::ui5::StyleScope style5;
#endif
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(clear[0], clear[1], clear[2], 1.0f));
            ImGui::Begin("##ui5_preview", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            fn();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::Render();
            gpu.draw(ImGui::GetDrawData(), clear);
            ++r.frames;
        }
        drawn = true;
    } catch (const std::exception& e) {
        r.note += std::string(e.what()) + "; ";
    }
    std::vector<uint8_t> bgr;
    if (!drawn) {
    } else if (!gpu.read_back(bgr)) {
        r.note += "readback failed; ";
    } else if (!write_png(png, w, h, bgr)) {
        r.note += "PNG write failed; ";
    } else {
        r.ok = true;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

// ---------------------------------------------------------------- self test (no device, no world)
int selftest() {
    int bad = 0;
    auto check = [&](bool c, const std::string& what) {
        std::printf("  %s %s\n", c ? "ok  " : "FAIL", what.c_str());
        if (!c) ++bad;
    };
    const auto places = preview::all_places();
    check(!places.empty(), "the shell lists pages");
    std::set<std::string> ids;
    bool roundtrip = true;
    for (const Place& p : places) {
        const std::string id = preview::id_of(p);
        if (!ids.insert(id).second) {
            check(false, "duplicate page id " + id);
        }
        Place back;
        std::string err;
        if (!preview::resolve_page_id(id, back, err) || !(back == p)) {
            roundtrip = false;
            std::printf("    does not round-trip: %s %s\n", id.c_str(), err.c_str());
        }
    }
    check(roundtrip, "every page id resolves back to its page");
    check(ids.size() == places.size(), "page ids are unique");
    int covered = 0;
    for (int p = static_cast<int>(Page::Home); p < static_cast<int>(Page::Count); ++p)
        for (const Place& pl : places)
            if (static_cast<int>(pl.page) == p) {
                ++covered;
                break;
            }
    check(covered == static_cast<int>(Page::Count) - 1, "every Page enumerator has an id (" + std::to_string(covered) + ")");
    Place out;
    std::string err;
    check(!preview::resolve_page_id("no/such-page", out, err) && err.find("valid ids") != std::string::npos, "an unknown id is refused and the message lists the valid ids");
    check(!preview::resolve_page_id("", out, err), "an empty id is refused");
    check(!preview::resolve_page_id("players/", out, err), "an area without a page is refused");
    check(preview::resolve_page_id("Players/Player", out, err) && preview::id_of(out) == "players/player", "ids ignore case");
    std::printf("selftest: %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::string err;
    if (!parse_args(argc, argv, o, err)) {
        std::fprintf(stderr, "gui_preview: %s\n", err.c_str());
        usage(stderr);
        return kExitUsage;
    }
    if (o.list) {
        for (const Place& p : preview::all_places()) std::printf("%s\n", preview::id_of(p).c_str());
        for (const auto& kv : preview::standalone_pages()) std::printf("%s\n", preview::standalone_id(kv.first).c_str());
        return 0;
    }
    if (o.selftest) return selftest();
    if (o.all == !o.page.empty()) {
        std::fprintf(stderr, "gui_preview: give exactly one of --page <id> or --all\n");
        usage(stderr);
        return kExitUsage;
    }
    if (o.out.empty()) {
        std::fprintf(stderr, "gui_preview: --out is required\n");
        usage(stderr);
        return kExitUsage;
    }

    // the page id is checked before anything is created: an unknown id leaves no device, no scratch folder and no file behind
    struct Todo {
        bool standalone = false;
        Place place;
        std::string name;  // the registry name of a standalone page
    };
    std::vector<Todo> todo;
    if (o.all) {
        for (const Place& p : preview::all_places()) todo.push_back({false, p, ""});
        for (const auto& kv : preview::standalone_pages()) todo.push_back({true, Place{}, kv.first});
    } else {
        std::string sname;
        if (preview::find_standalone(o.page, &sname)) {
            todo.push_back({true, Place{}, sname});
        } else {
            Place p;
            if (!preview::resolve_page_id(o.page, p, err)) {
                std::string more;
                for (const auto& kv : preview::standalone_pages()) more += (more.empty() ? "" : ", ") + preview::standalone_id(kv.first);
                std::fprintf(stderr, "gui_preview: %s%s\n", err.c_str(), more.empty() ? "" : ("; standalone pages: " + more).c_str());
                return kExitUsage;
            }
            todo.push_back({false, p, ""});
        }
    }
    bool need_world = false;
    for (const Todo& t : todo) need_world = need_world || !t.standalone;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    World world;
    if (need_world && !world.prepare(o, err)) {
        std::fprintf(stderr, "gui_preview: %s\n", err.c_str());
        return kExitSetup;
    }
    int rc = 0;
    {
        Gpu gpu;
        if (!gpu.init_device()) {
            std::fprintf(stderr, "gui_preview: cannot create a Direct3D 11 WARP device\n");
            rc = kExitSetup;
        } else {
            json index = json::array();
            for (const auto& sz : o.sizes) {
                for (const Todo& t : todo) {
                    const std::string stem = (t.standalone ? "ui5__" + t.name : preview::file_stem(t.place)) + "_" + std::to_string(sz.first) + "x" + std::to_string(sz.second);
                    const fs::path png = o.all ? fs::absolute(o.out) / (stem + ".png") : fs::absolute(o.out);
                    PageResult r = t.standalone ? render_standalone(gpu, t.name, preview::standalone_pages().at(t.name), sz.first, sz.second, png)
                                                : render_page(gpu, world, o, t.place, sz.first, sz.second, png);
                    std::printf("%s  %-28s %dx%d  %3d frames  %.1fs  failed_reads=%d  %s%s\n", r.ok ? "OK  " : "FAIL", r.id.c_str(), sz.first, sz.second, r.frames, r.seconds,
                                r.failed_reads, r.ok ? stem.c_str() : "", r.note.c_str());
                    std::fflush(stdout);
                    if (!r.ok) rc = kExitRender;
                    index.push_back({{"id", r.id}, {"size", std::to_string(sz.first) + "x" + std::to_string(sz.second)}, {"file", r.ok ? fs::path(r.file).filename().string() : ""},
                                     {"ok", r.ok}, {"frames", r.frames}, {"seconds", r.seconds}, {"failed_reads", r.failed_reads}, {"note", r.note}});
                }
            }
            if (o.all) {
                std::error_code ec;
                fs::create_directories(fs::absolute(o.out), ec);
                std::ofstream(fs::absolute(o.out) / "index.json") << index.dump(2);
            }
        }
    }
    if (need_world && !o.keep_scratch) {
        std::error_code ec;
        fs::remove_all(world.scratch, ec);
    }
    CoUninitialize();
    return rc;
}
