#include "preview/registry.h"

#include <algorithm>
#include <cctype>

namespace preview {

namespace {
std::map<std::string, StandaloneFn>& table() {
    static std::map<std::string, StandaloneFn> t;  // function-local: static registrars in other files run before main's statics
    return t;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

bool register_page(const std::string& name, StandaloneFn fn) {
    if (name.empty() || !fn || name.find_first_of("/ \t\\") != std::string::npos) return false;
    return table().emplace(lower(name), std::move(fn)).second;
}

const std::map<std::string, StandaloneFn>& standalone_pages() { return table(); }

std::string standalone_id(const std::string& name) { return "ui5/" + lower(name); }

const StandaloneFn* find_standalone(const std::string& id, std::string* name) {
    const std::string l = lower(id);
    if (l.rfind("ui5/", 0) != 0) return nullptr;
    const auto it = table().find(l.substr(4));
    if (it == table().end()) return nullptr;
    if (name) *name = it->first;
    return &it->second;
}

}  // namespace preview
