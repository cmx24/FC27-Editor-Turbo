// FC 27 LE Turbo GUI - the mini pitch of the Tactics tab (see pitch_view.h).
#include "pitch_view.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"  // S()

namespace turbo {

namespace {

PitchViewResult g_last;

// The pitch is landscape (105 long, 68 across): x runs along it. Layer positions are a schematic ramp, never metres.
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float unit(int v) { return std::clamp(v, 0, 100) / 100.0f; }

struct Canvas {
    ImDrawList* dl;
    ImVec2 o, sz;  // top-left and size of the pitch rectangle
    int used = 0;
    bool capped = false;
    bool room(int n = 1) {
        if (used + n > kPitchPrimitiveCap) {
            capped = true;
            return false;
        }
        return true;
    }
    ImVec2 at(float x, float y) const { return ImVec2(o.x + x * sz.x, o.y + y * sz.y); }
    void line(ImVec2 a, ImVec2 b, ImU32 c, float th) {
        if (!room()) return;
        dl->AddLine(a, b, c, th);
        ++used;
    }
    void rect(ImVec2 a, ImVec2 b, ImU32 c, float th) {
        if (!room()) return;
        dl->AddRect(a, b, c, 0.0f, th);
        ++used;
    }
    void rect_fill(ImVec2 a, ImVec2 b, ImU32 c) {
        if (!room()) return;
        dl->AddRectFilled(a, b, c);
        ++used;
    }
    void circle(ImVec2 c, float r, ImU32 col, float th) {
        if (!room()) return;
        dl->AddCircle(c, r, col, 24, th);
        ++used;
    }
    void circle_fill(ImVec2 c, float r, ImU32 col) {
        if (!room()) return;
        dl->AddCircleFilled(c, r, col, 16);
        ++used;
    }
    void text(ImVec2 p, ImU32 c, const std::string& s) {
        if (!room()) return;
        dl->AddText(p, c, s.c_str());
        ++used;
    }
    // dashed line: a fixed number of dashes, so the cost does not depend on the zoom
    void dashed(ImVec2 a, ImVec2 b, ImU32 c, float th, int dashes = 10) {
        for (int i = 0; i < dashes; ++i) {
            const float t0 = float(i) / dashes, t1 = (float(i) + 0.55f) / dashes;
            line(ImVec2(lerp(a.x, b.x, t0), lerp(a.y, b.y, t0)), ImVec2(lerp(a.x, b.x, t1), lerp(a.y, b.y, t1)), c, th);
        }
    }
};

}  // namespace

const char* pitch_chip_text() { return "Model view: Turbo's estimate, not game output"; }

std::string pitch_derived_label(const PitchLayer& l) {
    char b[48];
    std::snprintf(b, sizeof(b), "%d/100 (schematic)", std::clamp(l.value, 0, 100));
    return (l.label.empty() ? std::string() : l.label + " ") + b;
}

bool pitch_label_has_unit(const std::string& s) {
    // a number followed by a distance unit word, or a bare unit word
    std::string low;
    for (char c : s) low += char(std::tolower(static_cast<unsigned char>(c)));
    static const char* const units[] = {"metre", "meter", "yard", "km", "kilomet", "feet", "foot", "mile"};
    for (const char* u : units)
        if (low.find(u) != std::string::npos) return true;
    for (size_t i = 0; i < low.size(); ++i) {  // "19 m", "48m", "20 yd"
        if (!std::isdigit(static_cast<unsigned char>(low[i]))) continue;
        size_t j = i;
        while (j < low.size() && std::isdigit(static_cast<unsigned char>(low[j]))) ++j;
        while (j < low.size() && low[j] == ' ') ++j;
        auto word_at = [&](const char* w) {
            const size_t n = std::char_traits<char>::length(w);
            return low.compare(j, n, w) == 0 && (j + n >= low.size() || !std::isalpha(static_cast<unsigned char>(low[j + n])));
        };
        if (j < low.size() && (word_at("m") || word_at("yd") || word_at("ft"))) return true;
        i = j;
    }
    return false;
}

int pitch_primitive_estimate(const PitchView& v) {
    int n = 12;  // outline, halfway line, circle, spot, boxes
    n += int(v.dots.size()) * 2;
    if (v.show_derived) {
        if (v.def_line.value >= 0) n += 11;
        if (v.press_line.value >= 0) n += 11;
        if (v.width_band.value >= 0) n += 21;
    }
    if (v.show_heat && !v.heat.empty()) n += kHeatCols * kHeatRows * 3 + 1;
    return n;
}

const PitchViewResult& pitch_view_last() { return g_last; }

PitchViewResult draw_pitch_view(const char* id, const PitchView& v, float width) {
    PitchViewResult res;
    res.chip = pitch_chip_text();
    try {
        const float avail = ImGui::GetContentRegionAvail().x;
        float w = width > 0.0f ? width : avail;
        w = std::max(w, S(120.0f));
        const float h = w * 68.0f / 105.0f;
        res.width = w;
        res.height = h;

        // the persistent, non-dismissible honesty chip: drawn text, no interaction
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 ts = ImGui::CalcTextSize(res.chip.c_str());
            const float pad = S(5.0f);
            dl->AddRectFilled(p, ImVec2(p.x + ts.x + 2 * pad, p.y + ts.y + pad), IM_COL32(60, 52, 20, 255), S(8.0f));
            dl->AddRect(p, ImVec2(p.x + ts.x + 2 * pad, p.y + ts.y + pad), IM_COL32(200, 170, 70, 255), S(8.0f));
            dl->AddText(ImVec2(p.x + pad, p.y + pad * 0.5f), IM_COL32(240, 215, 130, 255), res.chip.c_str());
            ImGui::Dummy(ImVec2(ts.x + 2 * pad, ts.y + pad));
        }

        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        Canvas c{ImGui::GetWindowDrawList(), o, ImVec2(w, h)};
        const ImU32 grass = IM_COL32(28, 70, 40, 255), chalk = IM_COL32(215, 225, 215, 255);
        const bool grey = v.greyed;
        const ImU32 dcol = grey ? IM_COL32(150, 150, 150, 200) : IM_COL32(255, 205, 90, 230);   // derived: dashed
        const ImU32 mcol = grey ? IM_COL32(150, 150, 150, 90) : IM_COL32(120, 190, 255, 120);   // modelled: hatch
        const ImU32 dtext = grey ? IM_COL32(170, 170, 170, 255) : IM_COL32(255, 220, 130, 255);

        c.rect_fill(o, ImVec2(o.x + w, o.y + h), grass);
        // ---- Exact: the pitch markings (solid)
        c.rect(c.at(0, 0), c.at(1, 1), chalk, 1.5f);
        c.line(c.at(0.5f, 0), c.at(0.5f, 1), chalk, 1.0f);
        c.circle(c.at(0.5f, 0.5f), h * 0.135f, chalk, 1.0f);
        c.circle_fill(c.at(0.5f, 0.5f), 2.0f, chalk);
        c.rect(c.at(0, 0.2f), c.at(0.157f, 0.8f), chalk, 1.0f);          // penalty areas
        c.rect(c.at(0.843f, 0.2f), c.at(1, 0.8f), chalk, 1.0f);
        c.rect(c.at(0, 0.37f), c.at(0.052f, 0.63f), chalk, 1.0f);        // six-yard boxes
        c.rect(c.at(0.948f, 0.37f), c.at(1, 0.63f), chalk, 1.0f);

        // ---- Modelled (M): hatched grid, monochrome ramp, no numbers; off by default
        if (v.show_heat && v.heat.size() >= size_t(kHeatCols * kHeatRows)) {
            for (int r = 0; r < kHeatRows; ++r)
                for (int q = 0; q < kHeatCols; ++q) {
                    const float val = std::clamp(v.heat[size_t(r * kHeatCols + q)], 0.0f, 1.0f);
                    if (val <= 0.02f) continue;
                    const ImVec2 a = c.at(float(q) / kHeatCols, float(r) / kHeatRows), b = c.at(float(q + 1) / kHeatCols, float(r + 1) / kHeatRows);
                    ImU32 col = (mcol & 0x00FFFFFFu) | (ImU32(std::clamp(val, 0.0f, 1.0f) * 150.0f) << 24);
                    c.rect(a, b, col, 1.0f);
                    c.line(a, b, col, 1.0f);                                // hatch, never a solid fill
                    c.line(ImVec2(a.x, b.y), ImVec2(b.x, a.y), col, 1.0f);
                }
            res.labels.push_back("relative intensity (arbitrary)");
            c.text(ImVec2(o.x + 6, o.y + h - ImGui::GetTextLineHeight() - 2), dtext, "relative intensity (arbitrary)");
        }

        // ---- Derived (D): dashed, thin, labelled "62/100 (schematic)"
        if (v.show_derived) {
            const float line_h = ImGui::GetTextLineHeight();
            if (v.def_line.value >= 0) {
                const float x = lerp(0.14f, 0.55f, unit(v.def_line.value));
                c.dashed(c.at(x, 0.02f), c.at(x, 0.98f), dcol, 1.0f);
                const std::string t = pitch_derived_label(v.def_line);
                res.labels.push_back(t);
                c.text(ImVec2(c.at(x, 0).x + 3, o.y + 2), dtext, t);
            }
            if (v.press_line.value >= 0) {
                const float x = lerp(0.30f, 0.92f, unit(v.press_line.value));
                c.dashed(c.at(x, 0.02f), c.at(x, 0.98f), dcol, 1.0f);
                const std::string t = pitch_derived_label(v.press_line);
                res.labels.push_back(t);
                c.text(ImVec2(c.at(x, 0).x + 3, o.y + 2 + line_h), dtext, t);
            }
            if (v.width_band.value >= 0) {
                const float half = lerp(0.18f, 0.48f, unit(v.width_band.value));
                c.dashed(c.at(0.02f, 0.5f - half), c.at(0.98f, 0.5f - half), dcol, 1.0f, 10);
                c.dashed(c.at(0.02f, 0.5f + half), c.at(0.98f, 0.5f + half), dcol, 1.0f, 10);
                const std::string t = pitch_derived_label(v.width_band);
                res.labels.push_back(t);
                c.text(ImVec2(o.x + 6, c.at(0, 0.5f + half).y - line_h - 2), dtext, t);
            }
        }

        // ---- Exact: formation dots from stored data (or the fallback table), solid
        const float r = std::max(3.0f, h * 0.035f);
        for (size_t i = 0; i < v.dots.size(); ++i) {
            const PitchDot& d = v.dots[i];
            const ImVec2 p = c.at(std::clamp(d.x, 0.0f, 1.0f), std::clamp(d.y, 0.0f, 1.0f));
            c.circle_fill(p, r, IM_COL32(235, 235, 235, 255));
            c.circle(p, r, IM_COL32(20, 20, 20, 255), 1.0f);
            if (!d.label.empty()) {
                res.exact_labels.push_back(d.label);
                const ImVec2 ts = ImGui::CalcTextSize(d.label.c_str());
                c.text(ImVec2(p.x - ts.x * 0.5f, p.y + r + 1), IM_COL32(235, 235, 235, 255), d.label);
            }
            const float dx = mouse.x - p.x, dy = mouse.y - p.y;
            if (hovered && dx * dx + dy * dy <= (r + 3) * (r + 3)) {
                res.hovered_dot = int(i);
                if (clicked) res.clicked_dot = int(i);
            }
        }
        res.primitives = c.used;
        res.capped = c.capped;
        if (!v.dots_note.empty()) ImGui::TextDisabled("%s", v.dots_note.c_str());
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Pitch view error: %s", e.what());
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Pitch view error");
    }
    g_last = res;
    return res;
}

}  // namespace turbo
