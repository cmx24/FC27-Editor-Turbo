// FC 27 LE Turbo GUI - lazy imports.
//
// Turbo.dll is loaded by lua\autorun while the game is still starting (see dllmain.cpp). Loading it must not pull Direct3D 12,
// DXGI, the shader compiler or DWM into the game at that moment, so Turbo.dll imports none of them: the functions below stand
// in for the import libraries (build_win.sh does not link d3d12 / dxgi / d3dcompiler / dwmapi) and load the real system DLL
// the first time Turbo calls one, which is after Live Editor has finished setting up the game. A DLL the game has already
// loaded is used as it is.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <dwmapi.h>

namespace {

HMODULE system_dll(const wchar_t* name) {
    if (HMODULE m = GetModuleHandleW(name)) return m;
    if (HMODULE m = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) return m;
    return LoadLibraryW(name);
}

template <typename F>
F resolve(const wchar_t* dll, const char* name) {
    HMODULE m = system_dll(dll);
    return m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}

}  // namespace

extern "C" {

HRESULT __stdcall D3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void** device) {
    using Fn = HRESULT(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    static Fn fn = resolve<Fn>(L"d3d12.dll", "D3D12CreateDevice");
    return fn ? fn(adapter, minimum_feature_level, iid, device) : E_NOINTERFACE;
}

HRESULT __stdcall CreateDXGIFactory1(REFIID riid, void** factory) {
    using Fn = HRESULT(__stdcall*)(REFIID, void**);
    static Fn fn = resolve<Fn>(L"dxgi.dll", "CreateDXGIFactory1");
    return fn ? fn(riid, factory) : E_NOINTERFACE;
}

HRESULT WINAPI D3DCompile(const void* data, SIZE_T data_size, const char* filename, const D3D_SHADER_MACRO* defines,
                          ID3DInclude* include, const char* entrypoint, const char* target, UINT sflags, UINT eflags,
                          ID3DBlob** shader, ID3DBlob** error_messages) {
    static pD3DCompile fn = resolve<pD3DCompile>(L"d3dcompiler_47.dll", "D3DCompile");
    return fn ? fn(data, data_size, filename, defines, include, entrypoint, target, sflags, eflags, shader, error_messages)
              : E_NOINTERFACE;
}

HRESULT WINAPI DwmIsCompositionEnabled(WINBOOL* enabled) {
    using Fn = HRESULT(WINAPI*)(WINBOOL*);
    static Fn fn = resolve<Fn>(L"dwmapi.dll", "DwmIsCompositionEnabled");
    if (fn) return fn(enabled);
    if (enabled) *enabled = FALSE;
    return E_NOTIMPL;
}

HRESULT WINAPI DwmGetColorizationColor(DWORD* colorization, WINBOOL* opaque_blend) {
    using Fn = HRESULT(WINAPI*)(DWORD*, WINBOOL*);
    static Fn fn = resolve<Fn>(L"dwmapi.dll", "DwmGetColorizationColor");
    return fn ? fn(colorization, opaque_blend) : E_NOTIMPL;
}

HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hwnd, const DWM_BLURBEHIND* blur_behind) {
    using Fn = HRESULT(WINAPI*)(HWND, const DWM_BLURBEHIND*);
    static Fn fn = resolve<Fn>(L"dwmapi.dll", "DwmEnableBlurBehindWindow");
    return fn ? fn(hwnd, blur_behind) : E_NOTIMPL;
}

}  // extern "C"
