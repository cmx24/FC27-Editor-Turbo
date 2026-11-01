// FC 27 LE Turbo GUI preview host (tools/gui-preview): the registry of standalone pages (5.0 pages that need no App, no game memory).
//
// A 5.0 page is a pure draw function over a view-model plus a fixture function (ui5_page_<name>_draw(const <Name>VM&), <name>_fixture()).
// To get it in the preview a builder adds ONE file, src/preview/pages/<name>.cpp, and touches no host file:
//
//     #include "preview/registry.h"
//     #include "ui/ui5_page_squad.h"
//     PREVIEW_REGISTER_PAGE("squad", [] { static const SquadVM vm = squad_fixture(); ui5_page_squad_draw(vm); });
//
// The host (main.cpp) calls the function once per frame inside a full-screen window, with the 5.0 style pushed (ui5::StyleScope) and the
// base font at style.FontScaleDpi = height / 1080 (the kit's dp()). The page id is "ui5/<name>": `gui_preview --page ui5/squad`.
// build_preview.sh compiles every src/preview/pages/*.cpp and every src/ui/ui5_*.cpp it finds, so no list needs editing.
#pragma once
#include <functional>
#include <map>
#include <string>

namespace preview {

using StandaloneFn = std::function<void()>;

// false (and nothing registered) when the name is empty, has a '/' or space, or is taken
bool register_page(const std::string& name, StandaloneFn fn);
const std::map<std::string, StandaloneFn>& standalone_pages();  // sorted by name
std::string standalone_id(const std::string& name);              // "ui5/<name>"
// The registered page an id names ("ui5/squad", case-insensitive); nullptr when it is not a standalone id
const StandaloneFn* find_standalone(const std::string& id, std::string* name = nullptr);

struct Registrar {
    Registrar(const std::string& name, StandaloneFn fn) { register_page(name, std::move(fn)); }
};

}  // namespace preview

#define PREVIEW_CAT2(a, b) a##b
#define PREVIEW_CAT(a, b) PREVIEW_CAT2(a, b)
#define PREVIEW_REGISTER_PAGE(name, fn) static ::preview::Registrar PREVIEW_CAT(preview_registrar_, __LINE__)(name, fn)
