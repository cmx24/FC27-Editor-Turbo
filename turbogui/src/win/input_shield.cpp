// FC 27 LE Turbo GUI - input shield.
// While the Turbo window is under the mouse (or a Turbo text field has the keyboard), the game must not see that input:
// FC 27 reads the mouse and keyboard through DirectInput 8 and raw input, not only through window messages, so a click
// on a Turbo button also clicked the game's menu behind it (seen in game, 02-10-2026). The window procedure already keeps
// messages from the game; this hooks the polling APIs and hands the game "no input" while Turbo owns it:
//   IDirectInputDevice8W::GetDeviceState / GetDeviceData   (vtable taken from a throw-away system mouse device)
//   user32 GetRawInputBuffer / GetRawInputData
//   user32 GetAsyncKeyState / GetKeyState / GetKeyboardState / GetCursorPos (polled key and mouse state: FC 27 polls raw
//   input only about twice a second, yet a Turbo click moved the game's menu focus to the card under the mouse, seen in
//   game 02-10-2026)
// While Turbo has the mouse, the game gets mouse buttons up and the cursor where it was when Turbo took the mouse;
// while Turbo has the keyboard (a text field), every key reads as up. Turbo's own reads run inside TurboInputScope and
// get the real state.
// Everything is best effort: a hook that cannot be installed is logged and Turbo runs without it. Nothing is changed
// while Turbo does not want the input (the original call's result is returned untouched).
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <atomic>
#include <cstring>

#include "MinHook.h"
#include "core/hotkey.h"
#include "host.h"

namespace host {

namespace {

// GUIDs used here (from dinput.h) without linking dxguid
const GUID kIID_IDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kGUID_SysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
using GetDeviceDataFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using GetRawInputBufferFn = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);

GetDeviceStateFn o_state = nullptr;
GetDeviceDataFn o_data = nullptr;
GetRawInputBufferFn o_rawbuf = nullptr;
GetRawInputDataFn o_rawdata = nullptr;
GetAsyncKeyStateFn o_async = nullptr;
GetKeyStateFn o_keystate = nullptr;
GetKeyboardStateFn o_kbstate = nullptr;
GetCursorPosFn o_cursor = nullptr;

std::atomic<long> n_state{0}, n_data{0}, n_rawbuf{0}, n_rawdata{0}, n_blocked{0};
std::atomic<long> n_async{0}, n_keystate{0}, n_kbstate{0}, n_cursor{0};

thread_local int t_turbo = 0;  // > 0 while Turbo itself reads input (TurboInputScope)

// Last cursor position the game saw while Turbo did not have the mouse
std::atomic<long> g_cur_x{0}, g_cur_y{0};
std::atomic<bool> g_cur_known{false};

bool is_mouse_vk(int vk) { return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }

// Does the shield hide this virtual key from the game right now?
bool block_vk(int vk) {
    if (t_turbo > 0) return false;
    if (input_block_vk(vk)) return true;  // the show/hide key (Status tab > Settings) is Turbo's
    return is_mouse_vk(vk) ? input_block_mouse() : input_block_keyboard();
}

// DirectInput keyboard offset (DIK_*) of a virtual key (extended keys: + 0x80), 0 when unknown
DWORD dik_of_vk(int vk) {
    UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
    if (!sc) return 0;
    return (sc & 0xFF) | (((sc & 0xFF00) == 0xE000) ? 0x80 : 0);
}

// Device type per DirectInput device (mouse / keyboard / other), cached: GetDeviceInfo is not called every poll
struct DevType {
    std::atomic<void*> dev{nullptr};
    std::atomic<int> type{0};
};
DevType g_types[16];

int device_type(IDirectInputDevice8W* dev) {
    for (auto& t : g_types)
        if (t.dev.load() == dev) return t.type.load();
    DIDEVICEINSTANCEW info{};
    info.dwSize = sizeof(info);
    int type = 0;
    if (SUCCEEDED(dev->GetDeviceInfo(&info))) type = static_cast<int>(GET_DIDEVICE_TYPE(info.dwDevType));
    for (auto& t : g_types) {
        void* empty = nullptr;
        if (t.dev.compare_exchange_strong(empty, dev)) {
            t.type = type;
            break;
        }
    }
    return type;
}

bool block_for_device(IDirectInputDevice8W* dev) {
    const bool mouse = input_block_mouse(), keys = input_block_keyboard();
    if (!mouse && !keys) return false;
    int t = device_type(dev);
    return (mouse && t == DI8DEVTYPE_MOUSE) || (keys && t == DI8DEVTYPE_KEYBOARD);
}

HRESULT STDMETHODCALLTYPE hk_state(IDirectInputDevice8W* dev, DWORD cb, LPVOID data) {
    ++n_state;
    HRESULT hr = o_state(dev, cb, data);
    if (SUCCEEDED(hr) && data && cb > 0 && block_for_device(dev)) {
        std::memset(data, 0, cb);  // no movement, no buttons, no keys down
        ++n_blocked;
    } else if (SUCCEEDED(hr) && data && cb == 256) {
        // the show/hide key alone (keyboard state: one byte per DIK offset)
        const int vk = input_hidden_vk();
        const DWORD dik = vk ? dik_of_vk(vk) : 0;
        if (dik && dik < 256 && device_type(dev) == DI8DEVTYPE_KEYBOARD && (static_cast<BYTE*>(data)[dik] & 0x80)) {
            static_cast<BYTE*>(data)[dik] = 0;
            ++n_blocked;
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_data(IDirectInputDevice8W* dev, DWORD cb, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags) {
    ++n_data;
    HRESULT hr = o_data(dev, cb, rg, inout, flags);
    if (SUCCEEDED(hr) && inout && block_for_device(dev)) {
        *inout = 0;  // the events were taken from the buffer; the game gets none of them
        ++n_blocked;
    } else if (SUCCEEDED(hr) && inout && rg && *inout > 0 && cb >= sizeof(DIDEVICEOBJECTDATA) && !(flags & DIGDD_PEEK)) {
        // the show/hide key's key-downs are dropped from the keyboard's buffered data (its key-ups always pass)
        const int vk = input_hidden_vk();
        const DWORD dik = vk ? dik_of_vk(vk) : 0;
        if (dik && device_type(dev) == DI8DEVTYPE_KEYBOARD) {
            auto* base = reinterpret_cast<unsigned char*>(rg);
            DWORD kept = 0;
            for (DWORD i = 0; i < *inout; ++i) {
                auto* e = reinterpret_cast<DIDEVICEOBJECTDATA*>(base + size_t(i) * cb);
                if (turbo::hotkey_hides_event(static_cast<int>(dik), static_cast<int>(e->dwOfs), !(e->dwData & 0x80))) continue;
                if (kept != i) std::memmove(base + size_t(kept) * cb, e, cb);
                ++kept;
            }
            if (kept != *inout) {
                n_blocked += static_cast<long>(*inout - kept);
                *inout = kept;
            }
        }
    }
    return hr;
}

bool block_raw_type(DWORD type) {
    return (type == RIM_TYPEMOUSE && input_block_mouse()) || (type == RIM_TYPEKEYBOARD && input_block_keyboard());
}

// One raw-input entry the game must not get: its device is blocked, or it is a key-down of the show/hide key
bool block_raw(const RAWINPUT* ri, size_t avail) {
    if (block_raw_type(ri->header.dwType)) return true;
    return ri->header.dwType == RIM_TYPEKEYBOARD && avail >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD) &&
           turbo::hotkey_hides_event(input_hidden_vk(), ri->data.keyboard.VKey, (ri->data.keyboard.Flags & RI_KEY_BREAK) != 0);
}

UINT WINAPI hk_rawbuf(PRAWINPUT data, PUINT size, UINT header) {
    ++n_rawbuf;
    UINT n = o_rawbuf(data, size, header);
    if (t_turbo > 0) return n;  // Turbo's own read (TurboInputScope): the real data
    if (!data || n == 0 || n == static_cast<UINT>(-1)) return n;
    // Entries are 8-byte aligned, as NEXTRAWINPUTBLOCK walks
    auto align = [](size_t v) { return (v + 7) & ~static_cast<size_t>(7); };
    unsigned char* base = reinterpret_cast<unsigned char*>(data);
    // Wheel notches the game drained from the buffer never reach the window: the overlay may take them (core/wheel.h)
    for (UINT i = 0, rd = 0; i < n; ++i) {
        auto* ri = reinterpret_cast<RAWINPUT*>(base + rd);
        if (ri->header.dwType == RIM_TYPEMOUSE && ri->header.dwSize >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) &&
            (ri->data.mouse.usButtonFlags & RI_MOUSE_WHEEL))
            input_raw_wheel(ri->data.mouse.usButtonData);
        rd += static_cast<UINT>(align(ri->header.dwSize));
    }
    if (!input_block_mouse() && !input_block_keyboard() && !input_hidden_vk()) return n;
    // Drop the blocked entries and pack the rest to the front
    size_t rd = 0, wr = 0;
    UINT kept = 0;
    for (UINT i = 0; i < n; ++i) {
        auto* ri = reinterpret_cast<RAWINPUT*>(base + rd);
        size_t len = align(ri->header.dwSize);
        if (!block_raw(ri, ri->header.dwSize)) {
            if (wr != rd) std::memmove(base + wr, base + rd, ri->header.dwSize);
            wr += len;
            ++kept;
        } else {
            ++n_blocked;
        }
        rd += len;
    }
    return kept;
}

UINT WINAPI hk_rawdata(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT header) {
    ++n_rawdata;
    UINT r = o_rawdata(h, cmd, data, size, header);
    if (t_turbo > 0) return r;  // Turbo's own read (TurboInputScope, overlay queue_raw_mouse): the real data
    if (cmd != RID_INPUT || !data || r == 0 || r == static_cast<UINT>(-1)) return r;
    auto* ri = static_cast<RAWINPUT*>(data);
    if (!block_raw(ri, r)) return r;
    ++n_blocked;
    if (ri->header.dwType == RIM_TYPEMOUSE && r >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
        std::memset(&ri->data.mouse, 0, sizeof(RAWMOUSE));
        ri->data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    } else if (ri->header.dwType == RIM_TYPEKEYBOARD && r >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
        ri->data.keyboard.VKey = 0xFF;  // "no key"
        ri->data.keyboard.MakeCode = 0;
        ri->data.keyboard.Message = WM_NULL;
    }
    return r;
}

SHORT WINAPI hk_async(int vk) {
    if (t_turbo > 0) return o_async(vk);
    ++n_async;
    SHORT r = o_async(vk);
    if (r != 0 && block_vk(vk)) {
        ++n_blocked;
        return 0;
    }
    return r;
}

SHORT WINAPI hk_keystate(int vk) {
    if (t_turbo > 0) return o_keystate(vk);
    ++n_keystate;
    SHORT r = o_keystate(vk);
    if ((r & 0x8000) && block_vk(vk)) {
        ++n_blocked;
        return static_cast<SHORT>(r & 0x0001);  // keep the toggle bit (Caps Lock and the like), report "up"
    }
    return r;
}

BOOL WINAPI hk_kbstate(PBYTE keys) {
    if (t_turbo > 0) return o_kbstate(keys);
    ++n_kbstate;
    BOOL ok = o_kbstate(keys);
    if (!ok || !keys) return ok;
    const bool mouse = input_block_mouse(), kb = input_block_keyboard();
    const int hidden = input_hidden_vk();  // the show/hide key (Status tab > Settings)
    if (hidden > 0 && hidden < 256 && (keys[hidden] & 0x80)) {
        keys[hidden] &= 0x01;
        ++n_blocked;
    }
    if (!mouse && !kb) return ok;
    bool any = false;
    for (int vk = 1; vk < 256; ++vk) {
        if ((keys[vk] & 0x80) && (is_mouse_vk(vk) ? mouse : kb)) {
            keys[vk] &= 0x01;
            any = true;
        }
    }
    if (any) ++n_blocked;
    return ok;
}

BOOL WINAPI hk_cursor(LPPOINT p) {
    if (t_turbo > 0) return o_cursor(p);
    ++n_cursor;
    BOOL ok = o_cursor(p);
    if (!ok || !p) return ok;
    if (input_block_mouse() && g_cur_known) {
        p->x = g_cur_x;  // the game keeps the cursor where it was before Turbo took the mouse
        p->y = g_cur_y;
        ++n_blocked;
    } else {
        g_cur_x = p->x;
        g_cur_y = p->y;
        g_cur_known = true;
    }
    return ok;
}

bool hook(void* target, void* detour, void** original, const char* name) {
    if (!target) {
        log("input shield: %s not found", name);
        return false;
    }
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        log("input shield: hooking %s failed: %s", name, MH_StatusToString(st));
        return false;
    }
    return true;
}

}  // namespace

void install_input_shield() {
    wchar_t buf[8];
    if (GetEnvironmentVariableW(L"TURBO_GUI_NO_INPUT_SHIELD", buf, 8) > 0 && buf[0] == L'1') {
        log("input shield off (TURBO_GUI_NO_INPUT_SHIELD=1)");
        return;
    }
    int ok = 0;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputBuffer")), reinterpret_cast<void*>(&hk_rawbuf),
                   reinterpret_cast<void**>(&o_rawbuf), "GetRawInputBuffer");
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputData")), reinterpret_cast<void*>(&hk_rawdata),
                   reinterpret_cast<void**>(&o_rawdata), "GetRawInputData");
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetAsyncKeyState")), reinterpret_cast<void*>(&hk_async),
                   reinterpret_cast<void**>(&o_async), "GetAsyncKeyState");
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyState")), reinterpret_cast<void*>(&hk_keystate),
                   reinterpret_cast<void**>(&o_keystate), "GetKeyState");
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyboardState")), reinterpret_cast<void*>(&hk_kbstate),
                   reinterpret_cast<void**>(&o_kbstate), "GetKeyboardState");
        ok += hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetCursorPos")), reinterpret_cast<void*>(&hk_cursor),
                   reinterpret_cast<void**>(&o_cursor), "GetCursorPos");
    }
    // DirectInput 8: only when the game itself has loaded it (Turbo never loads dinput8.dll into a game that does not use it)
    HMODULE di = GetModuleHandleW(L"dinput8.dll");
    if (di) {
        using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
        auto create = reinterpret_cast<CreateFn>(GetProcAddress(di, "DirectInput8Create"));
        IDirectInput8W* dinput = nullptr;
        IDirectInputDevice8W* mouse = nullptr;
        if (create && SUCCEEDED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIID_IDirectInput8W,
                                       reinterpret_cast<void**>(&dinput), nullptr)) && dinput &&
            SUCCEEDED(dinput->CreateDevice(kGUID_SysMouse, &mouse, nullptr)) && mouse) {
            void** vt = *reinterpret_cast<void***>(mouse);
            ok += hook(vt[9], reinterpret_cast<void*>(&hk_state), reinterpret_cast<void**>(&o_state),
                       "IDirectInputDevice8::GetDeviceState");
            ok += hook(vt[10], reinterpret_cast<void*>(&hk_data), reinterpret_cast<void**>(&o_data),
                       "IDirectInputDevice8::GetDeviceData");
        } else {
            log("input shield: could not open a DirectInput 8 mouse to find its functions");
        }
        if (mouse) mouse->Release();
        if (dinput) dinput->Release();
    } else {
        log("input shield: the game does not use DirectInput 8");
    }
    log("input shield: %d input hooks installed (the game gets no mouse input while the mouse is over the Turbo window)", ok);
}

void input_shield_report() {
    log("input shield: game polled DirectInput state %ld / data %ld, raw input buffer %ld / data %ld, GetAsyncKeyState %ld, "
        "GetKeyState %ld, GetKeyboardState %ld, GetCursorPos %ld; %ld blocked while Turbo had the input",
        n_state.load(), n_data.load(), n_rawbuf.load(), n_rawdata.load(), n_async.load(), n_keystate.load(),
        n_kbstate.load(), n_cursor.load(), n_blocked.load());
}

TurboInputScope::TurboInputScope() { ++t_turbo; }
TurboInputScope::~TurboInputScope() { --t_turbo; }

}  // namespace host
