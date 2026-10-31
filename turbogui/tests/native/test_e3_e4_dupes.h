// 5.0c E3 + E4 (brief briefs\5.0c\E3-E4-dupes.md, Verifier V1-V10).
//
// STATEMENTS, not functions: test_main.cpp includes this once, inside test_ui(), after test_integration_50b.h, so app / ui / le /
// g_lua are in scope. No #pragma once on purpose.
//
// E4 (D-4, reviews\5.0b-design-review.md): a filled accent button drew its text at 3.27:1 (rest) and 2.65:1 (hover). The fill now
// comes from theme::accent_fill*() (text >= 4.5:1 in all three states); kAccent itself (text colour on the dark window, check mark,
// slider grab, outlines) is unchanged. Two checks fail on the base tree:
//   E4a  the WCAG ratio of the three fill states against the button text colours (the style's Text, and white for the rail),
//        and the non-text edge against the surfaces the fill sits on (>= 3:1, WCAG 1.4.11);
//   E4b  a source scan of src/ui: no button-fill push may use accent() / accent_hover() / accent_active() (16 sites on the base).
// E3 (D-1, reviews\5.0b-review.md:167-170): the manager Appearance tab had three editors for the hair attribute. See the E3 cases below.
{
    namespace e34fs = std::filesystem;
    const e34fs::path e34_root = e34fs::path(g_lua).parent_path().parent_path().parent_path();  // g_lua = <root>/tests/native/gui_world.lua
    const e34fs::path e34_ui = e34_root / "src" / "ui";

    // WCAG 2.x relative luminance and contrast ratio of two opaque colours
    auto e34_lin = [](float c) { return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    auto e34_lum = [&](const ImVec4& c) { return 0.2126f * e34_lin(c.x) + 0.7152f * e34_lin(c.y) + 0.0722f * e34_lin(c.z); };
    auto e34_ratio = [&](const ImVec4& a, const ImVec4& b) {
        const float la = e34_lum(a), lb = e34_lum(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    };
    auto e34_slurp = [](const e34fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    run_case("5.0c E4a: filled accent buttons keep text >= 4.5:1 in rest, hover and active (and a >= 3:1 edge)", [&] {
        ImGuiStyle st;
        turbo::theme::apply_turbo_theme(st);
        const ImVec4 text = st.Colors[ImGuiCol_Text];
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);  // the selected rail entry pushes a white text
        struct St { const char* name; ImVec4 fill; };
        const St fills[] = {{"rest", turbo::theme::accent_fill()}, {"hover", turbo::theme::accent_fill_hover()}, {"active", turbo::theme::accent_fill_active()}};
        const ImVec4 surfaces[] = {st.Colors[ImGuiCol_WindowBg], st.Colors[ImGuiCol_PopupBg], turbo::theme::card_bg()};
        const char* surface_names[] = {"WindowBg", "PopupBg", "card"};
        for (const St& f : fills) {
            const float rt = e34_ratio(text, f.fill), rw = e34_ratio(white, f.fill);
            std::printf("  [e34] %-6s fill (%.3f %.3f %.3f)  text %.2f:1  white %.2f:1\n", f.name, f.fill.x, f.fill.y, f.fill.z, rt, rw);
            CHECK(rt >= 4.5f, "filled accent button text (style Text) >= 4.5:1");
            CHECK(rw >= 4.5f, "selected rail entry text (white) >= 4.5:1");
            for (int i = 0; i < 3; ++i) {
                const float re = e34_ratio(f.fill, surfaces[i]);
                std::printf("  [e34] %-6s fill vs %-8s edge %.2f:1\n", f.name, surface_names[i], re);
                CHECK(re >= 3.0f, "the fill stays distinguishable from the surface it sits on (WCAG 1.4.11)");
            }
        }
        // the three states stay three states: hover and active differ from rest by a visible step, and hover is the lightest
        CHECK(e34_lum(fills[1].fill) > e34_lum(fills[0].fill), "hover is lighter than rest");
        CHECK(e34_lum(fills[2].fill) < e34_lum(fills[0].fill), "active is darker than rest");
        // kAccent is NOT touched: it is the text / glyph colour on the dark window, where it reads at >= 4.5:1
        CHECK(turbo::theme::kAccent.x == 0.26f && turbo::theme::kAccent.y == 0.52f && turbo::theme::kAccent.z == 0.96f, "kAccent unchanged");
        CHECK(e34_ratio(turbo::theme::kAccent, st.Colors[ImGuiCol_WindowBg]) >= 4.5f, "kAccent as text on the window >= 4.5:1");
        // text-bearing style slots composited over the window (selected tab, hovered tab, header washes) stay readable
        auto over = [](const ImVec4& top, const ImVec4& bg) {
            return ImVec4(top.x * top.w + bg.x * (1.0f - top.w), top.y * top.w + bg.y * (1.0f - top.w), top.z * top.w + bg.z * (1.0f - top.w), 1.0f);
        };
        const ImGuiCol slots[] = {ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmedSelected};
        for (ImGuiCol s : slots)
            CHECK(e34_ratio(text, over(st.Colors[s], st.Colors[ImGuiCol_WindowBg])) >= 4.5f, "text on the Header / Tab slots >= 4.5:1");
    });

    run_case("5.0c E4b: no button fill in src/ui is pushed from accent() / accent_hover() / accent_active()", [&] {
        const std::regex acc_use("\\baccent(_hover|_active)?\\(\\)");
        int hits = 0, files = 0;
        for (const auto& de : e34fs::directory_iterator(e34_ui)) {
            const std::string ext = de.path().extension().string();
            if (ext != ".cpp" && ext != ".h") continue;
            ++files;
            std::istringstream in(e34_slurp(de.path()));
            std::string line;
            int n = 0;
            while (std::getline(in, line)) {
                ++n;
                if (line.find("accent_fill") != std::string::npos) continue;
                const bool fill_line = line.find("ImGuiCol_Button") != std::string::npos || line.find("fill =") != std::string::npos;
                if (fill_line && std::regex_search(line, acc_use)) {
                    ++hits;
                    std::printf("  [e34] accent used as a button fill: %s:%d\n", de.path().filename().string().c_str(), n);
                }
            }
        }
        CHECK(files > 20, "the scan saw the ui sources");
        CHECK(hits == 0, "a filled button takes theme::accent_fill*(), never kAccent (3.27:1 text)");
    });
}
