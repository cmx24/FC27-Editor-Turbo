// FC 27 LE Turbo GUI - the mini pitch of the Tactics tab (see pitch_view.h).
#include "pitch_view.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "app.h"  // S()

namespace turbo {

namespace {

PitchViewResult g_last;

constexpr int kDashes = 8;      // dashes of one dashed line: a fixed number, so the cost does not depend on the zoom
constexpr int kRingArcs = 4;    // stippled ring = this many arcs
constexpr int kExposureHatch = 8;

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

bool shown(const PitchDrawOptions& o, const char* id) { return o.show_derived && o.hidden.find(id) == o.hidden.end(); }

// Canvas over the pitch rectangle. `along` runs from the own goal (0) to the opponent's (1), `across` from one touchline to the other.
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
    ImVec2 at(float along, float across) const { return ImVec2(o.x + along * sz.x, o.y + across * sz.y); }
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
    void triangle_fill(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col) {
        if (!room()) return;
        dl->AddTriangleFilled(a, b, c, col);
        ++used;
    }
    void arc(ImVec2 c, float r, float a0, float a1, ImU32 col, float th) {
        if (!room()) return;
        dl->PathArcTo(c, r, a0, a1, 6);
        dl->PathStroke(col, th);
        ++used;
    }
    void text(ImVec2 p, ImU32 c, const std::string& s) {
        if (!room()) return;
        dl->AddText(p, c, s.c_str());
        ++used;
    }
    void dashed(ImVec2 a, ImVec2 b, ImU32 c, float th) {
        for (int i = 0; i < kDashes; ++i) {
            const float t0 = float(i) / kDashes, t1 = (float(i) + 0.55f) / kDashes;
            line(ImVec2(lerp(a.x, b.x, t0), lerp(a.y, b.y, t0)), ImVec2(lerp(a.x, b.x, t1), lerp(a.y, b.y, t1)), c, th);
        }
    }
};

}  // namespace

const char* pitch_chip_text() { return kModelChip; }

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

int pitch_primitive_estimate(const PreviewModel& m, const PitchDrawOptions& o) {
    int n = 9;  // grass, outline, halfway line, centre circle and spot, four boxes
    for (const PreviewDot& d : m.dots) n += 3 + (d.selected ? 1 : 0);  // fill, outline, name
    for (const PreviewLine& l : m.lines)
        if (shown(o, l.id.c_str())) n += kDashes + 1;
    for (const PreviewBand& b : m.bands)
        if (shown(o, b.id.c_str())) n += 2 * kDashes + 1;
    if (shown(o, "arrows") && !m.arrows.empty()) n += int(m.arrows.size()) * 2 + 1;
    if (!m.rings.empty()) n += int(m.rings.size()) * kRingArcs + 1;
    if (!m.exposure_label.empty()) n += 2 + kExposureHatch;
    if (!m.heat.cell.empty()) n += 1;  // the heat grid's own label
    return n;
}

const PitchViewResult& pitch_view_last() { return g_last; }

PitchViewResult draw_pitch_view(const char* id, const PreviewModel& m, const PitchDrawOptions& o, float width) {
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

        const ImVec2 o0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        Canvas c{ImGui::GetWindowDrawList(), o0, ImVec2(w, h)};
        const ImU32 grass = IM_COL32(28, 70, 40, 255), chalk = IM_COL32(215, 225, 215, 255);
        const bool grey = m.greyed;
        const ImU32 dcol = grey ? IM_COL32(150, 150, 150, 200) : IM_COL32(255, 205, 90, 230);   // derived: dashed
        const ImU32 mcol = grey ? IM_COL32(150, 150, 150, 90) : IM_COL32(120, 190, 255, 120);   // modelled: hatch
        const ImU32 dtext = grey ? IM_COL32(170, 170, 170, 255) : IM_COL32(255, 220, 130, 255);
        const float lh = ImGui::GetTextLineHeight();

        // ---- Exact: the pitch markings (solid)
        c.rect_fill(o0, ImVec2(o0.x + w, o0.y + h), grass);
        c.rect(c.at(0, 0), c.at(1, 1), chalk, 1.5f);
        c.line(c.at(0.5f, 0), c.at(0.5f, 1), chalk, 1.0f);
        c.circle(c.at(0.5f, 0.5f), h * 0.135f, chalk, 1.0f);
        c.circle_fill(c.at(0.5f, 0.5f), 2.0f, chalk);
        c.rect(c.at(0, 0.2f), c.at(0.157f, 0.8f), chalk, 1.0f);          // penalty areas
        c.rect(c.at(0.843f, 0.2f), c.at(1, 0.8f), chalk, 1.0f);
        c.rect(c.at(0, 0.37f), c.at(0.052f, 0.63f), chalk, 1.0f);        // six-yard boxes
        c.rect(c.at(0.948f, 0.37f), c.at(1, 0.63f), chalk, 1.0f);

        int legend_rows = 0;  // texts stacked up from the bottom edge (everything that is not a line)
        auto legend = [&](const std::string& t) {
            res.labels.push_back(t);
            c.text(ImVec2(o0.x + 6, o0.y + h - 2 - lh * float(++legend_rows)), dtext, t);
        };

        // ---- Modelled (M): hatched grid, one diagonal stroke per cell, monochrome ramp, no numbers; the model only has it when asked for
        if (!m.heat.cell.empty() && m.heat.cols > 0 && m.heat.rows > 0 &&
            m.heat.cell.size() >= static_cast<size_t>(m.heat.cols) * static_cast<size_t>(m.heat.rows)) {
            const int left = kPitchPrimitiveCap - pitch_primitive_estimate(m, o);
            int stride = 0;
            for (int s = 1; s <= 6 && !stride; ++s)
                if (((m.heat.cols + s - 1) / s) * ((m.heat.rows + s - 1) / s) <= left) stride = s;
            res.heat_stride = stride;
            for (int r0 = 0; stride && r0 < m.heat.rows; r0 += stride)
                for (int c0 = 0; c0 < m.heat.cols; c0 += stride) {
                    float sum = 0.0f;
                    int n = 0;
                    for (int r = r0; r < std::min(m.heat.rows, r0 + stride); ++r)
                        for (int q = c0; q < std::min(m.heat.cols, c0 + stride); ++q, ++n) sum += m.heat.at(q, r);
                    const float val = clamp01(n ? sum / float(n) : 0.0f);
                    if (val <= 0.05f) continue;
                    const float a0 = float(r0) / m.heat.rows, a1 = float(std::min(m.heat.rows, r0 + stride)) / m.heat.rows;
                    const float x0 = float(c0) / m.heat.cols, x1 = float(std::min(m.heat.cols, c0 + stride)) / m.heat.cols;
                    const ImU32 col = (mcol & 0x00FFFFFFu) | (ImU32(30.0f + val * 170.0f) << 24);
                    c.line(c.at(a0, x1), c.at(a1, x0), col, 1.0f);  // a hatch stroke, never a solid fill
                }
            if (stride) legend(m.heat_label);
        }

        // ---- Derived (D): dashed, thin, labelled "62/100 (schematic)"; only for sliders somebody switched on
        for (const PreviewBand& b : m.bands) {
            if (!shown(o, b.id.c_str())) continue;
            c.dashed(c.at(0.02f, b.x_lo), c.at(0.98f, b.x_lo), dcol, 1.0f);
            c.dashed(c.at(0.02f, b.x_hi), c.at(0.98f, b.x_hi), dcol, 1.0f);
            legend(b.label);
        }
        int line_rows = 0;
        for (const PreviewLine& l : m.lines) {
            if (!shown(o, l.id.c_str())) continue;
            const float th = 0.8f + 1.0f * clamp01(l.weight);
            c.dashed(c.at(clamp01(l.y), 0.02f), c.at(clamp01(l.y), 0.98f), dcol, th);
            res.labels.push_back(l.label);
            const float tw = ImGui::CalcTextSize(l.label.c_str()).x;
            const float tx = std::min(c.at(clamp01(l.y), 0).x + 3, o0.x + w - tw - 2);
            c.text(ImVec2(std::max(o0.x + 2, tx), o0.y + 2 + lh * float(line_rows++)), dtext, l.label);
        }
        if (shown(o, "arrows") && !m.arrows.empty()) {
            const float hl = std::max(4.0f, h * 0.03f), hs = hl * 0.55f;
            for (const PreviewArrow& a : m.arrows) {
                const ImVec2 p0 = c.at(clamp01(a.y0), clamp01(a.x0)), p1 = c.at(clamp01(a.y1), clamp01(a.x1));
                if (p1.x - p0.x < 2.0f) continue;
                c.line(p0, ImVec2(p1.x - hl, p1.y), dcol, 1.0f);
                c.triangle_fill(p1, ImVec2(p1.x - hl, p1.y - hs), ImVec2(p1.x - hl, p1.y + hs), dcol);
            }
            legend(m.arrows_label);
        }

        // ---- Modelled (M): roaming rings (stippled: a few short arcs) and the exposure hint (a hatched gauge)
        if (!m.rings.empty()) {
            for (const PreviewRing& rg : m.rings) {
                if (rg.slot < 0 || size_t(rg.slot) >= m.dots.size()) continue;
                const PreviewDot& d = m.dots[size_t(rg.slot)];
                const ImVec2 p = c.at(clamp01(d.y), clamp01(d.x));
                const float rr = std::max(3.0f, rg.radius * h);
                for (int k = 0; k < kRingArcs; ++k) {
                    const float a0 = 6.2831853f * (float(k) + 0.1f) / kRingArcs, a1 = 6.2831853f * (float(k) + 0.6f) / kRingArcs;
                    c.arc(p, rr, a0, a1, mcol, 1.0f);
                }
            }
            legend(m.ring_label);
        }
        if (!m.exposure_label.empty()) {
            const ImVec2 a = c.at(0.80f, 0.04f), b = c.at(0.98f, 0.12f);
            c.rect(a, b, mcol, 1.0f);
            const int n = std::clamp(int(std::lround(clamp01(m.exposure) * kExposureHatch)), 0, kExposureHatch);
            for (int k = 0; k < n; ++k) {
                const float x0 = lerp(a.x, b.x, float(k) / kExposureHatch), x1 = lerp(a.x, b.x, float(k + 1) / kExposureHatch);
                c.line(ImVec2(x0, b.y), ImVec2(x1, a.y), mcol, 1.0f);
            }
            legend(m.exposure_label);
        }

        // ---- Exact: formation dots from stored data (or the fallback table), solid
        const float r = std::max(3.0f, h * 0.035f);
        for (size_t i = 0; i < m.dots.size(); ++i) {
            const PreviewDot& d = m.dots[i];
            const ImVec2 p = c.at(clamp01(d.y), clamp01(d.x));
            c.circle_fill(p, r, IM_COL32(235, 235, 235, 255));
            c.circle(p, r, IM_COL32(20, 20, 20, 255), 1.0f);
            if (d.selected) c.circle(p, r + 3.0f, IM_COL32(255, 220, 90, 255), 2.0f);
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
        if (!o.dots_note.empty()) ImGui::TextDisabled("%s", o.dots_note.c_str());
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Pitch view error: %s", e.what());
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Pitch view error");
    }
    g_last = res;
    return res;
}

}  // namespace turbo
