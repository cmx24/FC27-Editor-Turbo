// FC 27 LE Turbo GUI - Dear ImGui configuration (IMGUI_USER_CONFIG).
// A failed ImGui assertion must never abort() the game: it is routed to a handler that the DLL
// implements as a log line, and the native tests implement as a thrown exception.
#pragma once

#ifdef __cplusplus
namespace turbo {
void imgui_assert_failed(const char* expr, const char* file, int line);
}
#define IM_ASSERT(_EXPR) \
    do { \
        if (!(_EXPR)) ::turbo::imgui_assert_failed(#_EXPR, __FILE__, __LINE__); \
    } while (0)
#endif

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
// No ShellExecute (and no SHELL32 import): Turbo never opens links or files from ImGui
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
