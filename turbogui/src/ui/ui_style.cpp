// FC 27 LE Turbo GUI - the Turbo X theme (ui_style.h): the 3.0 structure with the X palette (ui_theme.h).
#include "ui_style.h"

#include "ui_theme.h"

namespace turbo::theme {

void apply_turbo_theme(ImGuiStyle& st) {
    ImGui::StyleColorsDark(&st);

    // ---- metrics (unscaled px). Turbo X (2026-10-31): a little more air, softer corners, no frame borders
    st.WindowRounding = 10.0f;
    st.ChildRounding = 10.0f;
    st.FrameRounding = 7.0f;
    st.PopupRounding = 8.0f;
    st.TabRounding = 7.0f;
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.WindowPadding = ImVec2(14.0f, 12.0f);
    st.FramePadding = ImVec2(10.0f, 6.0f);
    st.ItemSpacing = ImVec2(9.0f, 7.0f);
    st.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
    st.CellPadding = ImVec2(7.0f, 3.0f);
    st.IndentSpacing = 16.0f;
    st.ScrollbarSize = 12.0f;
    st.ScrollbarRounding = 6.0f;
    st.GrabMinSize = 14.0f;
    st.GrabRounding = 6.0f;
    st.TabBarBorderSize = 1.0f;
    st.TabBarOverlineSize = 2.0f;
    st.SeparatorTextBorderSize = 1.0f;
    st.SeparatorTextPadding = ImVec2(16.0f, 4.0f);

    // ---- colours: slate surfaces (rail < window < card < frame), mint-teal accent
    const ImVec4 acc = kAccent;
    const ImVec4 acc_hi = accent_hover();
    ImVec4* c = st.Colors;
    // text
    c[ImGuiCol_Text] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_TextSelectedBg] = accent_alpha(0.35f);
    c[ImGuiCol_TextLink] = acc;
    // surfaces
    c[ImGuiCol_WindowBg] = kWindowBg;
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.062f, 0.078f, 0.108f, 0.985f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.25f, 0.32f, 0.60f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.135f, 0.185f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.185f, 0.250f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.175f, 0.230f, 0.310f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.045f, 0.058f, 0.082f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.060f, 0.090f, 0.115f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.039f, 0.051f, 0.071f, 0.80f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.062f, 0.078f, 0.108f, 1.0f);
    // scrollbars
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.29f, 0.37f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.38f, 0.48f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = acc;
    // accent controls
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = acc_hi;
    // buttons: neutral slate; the accent fill is for theme::primary_button and the selected rail entry
    c[ImGuiCol_Button] = ImVec4(0.150f, 0.185f, 0.245f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.205f, 0.250f, 0.325f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.120f, 0.150f, 0.200f, 1.0f);
    // selected rows / selectables / collapsing headers: a soft accent wash
    c[ImGuiCol_Header] = accent_alpha(0.22f);
    c[ImGuiCol_HeaderHovered] = accent_alpha(0.34f);
    c[ImGuiCol_HeaderActive] = accent_alpha(0.48f);
    // separators
    c[ImGuiCol_Separator] = kRule;
    c[ImGuiCol_SeparatorHovered] = accent_alpha(0.70f);
    c[ImGuiCol_SeparatorActive] = acc;
    c[ImGuiCol_ResizeGrip] = accent_alpha(0.15f);
    c[ImGuiCol_ResizeGripHovered] = accent_alpha(0.55f);
    c[ImGuiCol_ResizeGripActive] = accent_alpha(0.85f);
    // tabs: quiet idle, an accent-tinted selected tab with an accent overline
    c[ImGuiCol_Tab] = ImVec4(0.080f, 0.100f, 0.140f, 1.0f);
    c[ImGuiCol_TabHovered] = accent_alpha(0.40f);
    c[ImGuiCol_TabSelected] = ImVec4(0.090f, 0.260f, 0.235f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = acc;
    c[ImGuiCol_TabDimmed] = ImVec4(0.070f, 0.088f, 0.122f, 1.0f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.080f, 0.190f, 0.175f, 1.0f);
    c[ImGuiCol_TabDimmedSelectedOverline] = accent_alpha(0.60f);
    // tables
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.058f, 0.074f, 0.104f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = kTableBorderStrong;
    c[ImGuiCol_TableBorderLight] = kTableBorderLight;
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.035f);
    // misc
    c[ImGuiCol_DragDropTarget] = kWarn;
    c[ImGuiCol_NavCursor] = acc_hi;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
}

}  // namespace turbo::theme
