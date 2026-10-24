// FC 27 LE Turbo GUI - the Turbo 3.0 theme (ui_style.h).
#include "ui_style.h"

#include "ui_theme.h"

namespace turbo::theme {

void apply_turbo_theme(ImGuiStyle& st) {
    ImGui::StyleColorsDark(&st);

    // ---- metrics (unscaled px)
    st.WindowRounding = 8.0f;
    st.ChildRounding = 8.0f;
    st.FrameRounding = 6.0f;
    st.PopupRounding = 6.0f;
    st.TabRounding = 6.0f;
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.WindowPadding = ImVec2(12.0f, 12.0f);
    st.FramePadding = ImVec2(8.0f, 5.0f);
    st.ItemSpacing = ImVec2(8.0f, 6.0f);
    st.CellPadding = ImVec2(6.0f, 3.0f);
    st.IndentSpacing = 16.0f;
    // Thin bars with a small rounding (Dear ImGui's default rounding 9 turns a short grab into an oval)
    st.ScrollbarSize = 12.0f;
    st.ScrollbarRounding = 3.0f;
    st.GrabMinSize = 12.0f;
    st.GrabRounding = 3.0f;

    // ---- colours
    const ImVec4 acc = kAccent;
    const ImVec4 acc_hi = accent_hover();
    ImVec4* c = st.Colors;
    // text
    c[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_TextSelectedBg] = accent_alpha(0.40f);
    c[ImGuiCol_TextLink] = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
    // surfaces: window < card < frame
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.090f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.088f, 0.122f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.26f, 0.34f, 0.55f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.125f, 0.148f, 0.205f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.170f, 0.205f, 0.285f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.210f, 0.265f, 0.385f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.070f, 0.085f, 0.130f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.100f, 0.150f, 0.270f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.055f, 0.065f, 0.090f, 0.80f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.088f, 0.122f, 1.0f);
    // scrollbars
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.040f, 0.048f, 0.068f, 0.55f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.30f, 0.40f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.40f, 0.53f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = acc;
    // accent controls
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = acc_hi;
    // buttons: neutral grey-navy; the accent fill is for theme::primary_button and the selected rail entry
    c[ImGuiCol_Button] = ImVec4(0.185f, 0.205f, 0.265f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.245f, 0.275f, 0.360f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.140f, 0.160f, 0.215f, 1.0f);
    // selected rows / selectables / collapsing headers: a soft accent wash
    c[ImGuiCol_Header] = accent_alpha(0.30f);
    c[ImGuiCol_HeaderHovered] = accent_alpha(0.48f);
    c[ImGuiCol_HeaderActive] = accent_alpha(0.65f);
    // separators
    c[ImGuiCol_Separator] = kRule;
    c[ImGuiCol_SeparatorHovered] = accent_alpha(0.70f);
    c[ImGuiCol_SeparatorActive] = acc;
    c[ImGuiCol_ResizeGrip] = accent_alpha(0.20f);
    c[ImGuiCol_ResizeGripHovered] = accent_alpha(0.60f);
    c[ImGuiCol_ResizeGripActive] = accent_alpha(0.90f);
    // tabs: dark idle, accent on hover, a deep accent-blue selected tab with an accent overline
    c[ImGuiCol_Tab] = ImVec4(0.095f, 0.112f, 0.160f, 1.0f);
    c[ImGuiCol_TabHovered] = accent_alpha(0.55f);
    c[ImGuiCol_TabSelected] = ImVec4(0.170f, 0.340f, 0.660f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = acc;
    c[ImGuiCol_TabDimmed] = ImVec4(0.080f, 0.094f, 0.134f, 1.0f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.130f, 0.215f, 0.400f, 1.0f);
    c[ImGuiCol_TabDimmedSelectedOverline] = accent_alpha(0.60f);
    // tables
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.068f, 0.080f, 0.116f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.165f, 0.22f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
    // misc
    c[ImGuiCol_DragDropTarget] = kWarn;
    c[ImGuiCol_NavCursor] = acc_hi;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

}  // namespace turbo::theme
