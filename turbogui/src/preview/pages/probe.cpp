// The host's own smoke page: plain Dear ImGui, no kit. `gui_preview --page ui5/probe` proves the standalone route (registry, full-screen
// window, scale, PNG) works before any 5.0 page exists, and tests/preview/test_gui_preview.sh renders it.
#include "imgui.h"
#include "preview/registry.h"

PREVIEW_REGISTER_PAGE("probe", [] {
    ImGui::SetCursorPos(ImVec2(24.0f * ImGui::GetStyle().FontScaleDpi, 24.0f * ImGui::GetStyle().FontScaleDpi));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("gui_preview standalone page probe");
    ImGui::Text("FontScaleDpi = %.3f", ImGui::GetStyle().FontScaleDpi);
    ImGui::Button("A button");
    ImGui::EndGroup();
});
