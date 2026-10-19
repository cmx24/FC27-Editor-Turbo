// FC 27 LE Turbo GUI - the small play button of Players > Callname: plays the callname's wav from the FC 27 master's
// wav folder (core/callname_audio.h). The triangle / square are drawn, not text: the overlay font has no such glyphs.
#include "ui_callname_play.h"

#include <cstdio>
#include <string>

#include "app.h"
#include "imgui.h"

namespace turbo {

void callname_play_button(App& app, CallnameAudioKind kind, int64_t id) {
    const float h = ImGui::GetTextLineHeight();
    if (!ImGui::IsRectVisible(ImVec2(h, h))) {  // a row scrolled out of the list: keep the space, skip the file checks
        ImGui::Dummy(ImVec2(h, h));
        return;
    }
    CallnamePlayer& pl = app.callname_player;
    const MasterAudio& audio = app.callnames.masters.audio;
    const CallnamePlayer::Button b = pl.button(audio, kind, id, app.now);
    char label[48];
    std::snprintf(label, sizeof(label), "##play_%c%lld", kind == CallnameAudioKind::Own ? 'o' : 'g', static_cast<long long>(id));
    if (!b.enabled) ImGui::BeginDisabled();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    const bool pressed = ImGui::Button(label, ImVec2(h, h));
    ImGui::PopStyleVar();
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float pad = h * 0.25f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (b.playing)
        dl->AddRectFilled(ImVec2(lo.x + pad, lo.y + pad), ImVec2(hi.x - pad, hi.y - pad), col);
    else
        dl->AddTriangleFilled(ImVec2(lo.x + pad, lo.y + pad * 0.8f), ImVec2(lo.x + pad, hi.y - pad * 0.8f),
                              ImVec2(hi.x - pad * 0.8f, (lo.y + hi.y) * 0.5f), col);
    if (!b.enabled) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", b.tip.c_str());
    if (pressed && b.enabled) pl.click(audio, kind, id, app.now);
}

bool callname_preview_button(App& app, CallnameAudioKind kind, int64_t id) {
    CallnamePlayer& pl = app.callname_player;
    const MasterAudio& audio = app.callnames.masters.audio;
    const CallnamePlayer::Button b = pl.button(audio, kind, id, app.now);
    if (!b.enabled) ImGui::BeginDisabled();
    const std::string label = std::string(b.playing ? "Stop" : "Preview") + "###cnpreview";  // one id: the button keeps its place while it plays
    const bool pressed = ImGui::Button(label.c_str());
    if (!b.enabled) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", b.enabled ? "Hear this callname" : b.tip.c_str());
    if (pressed && b.enabled) pl.click(audio, kind, id, app.now);
    return b.enabled;
}

}  // namespace turbo
