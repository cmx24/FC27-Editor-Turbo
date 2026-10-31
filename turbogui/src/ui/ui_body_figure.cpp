// FC 27 LE Turbo GUI - the body-type figure (5.0c E8-body). See ui_body_figure.h.
#include "ui/ui_body_figure.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace turbo::bodyfig {
namespace {

float channel(int c8) {
    const float c = static_cast<float>(c8) / 255.0f;
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float luminance(ImU32 c) {
    const int r = static_cast<int>((c >> IM_COL32_R_SHIFT) & 0xFF), g = static_cast<int>((c >> IM_COL32_G_SHIFT) & 0xFF), b = static_cast<int>((c >> IM_COL32_B_SHIFT) & 0xFF);
    return 0.2126f * channel(r) + 0.7152f * channel(g) + 0.0722f * channel(b);
}

}  // namespace

float contrast_ratio(ImU32 a, ImU32 b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

ImU32 tile_ground(bool selected, bool hovered) {
    if (selected) return IM_COL32(28, 52, 98, 255);  // the Turbo blue, dark
    if (hovered) return IM_COL32(46, 52, 68, 255);
    return IM_COL32(30, 35, 48, 255);
}
ImU32 figure_fill(bool selected) { return selected ? IM_COL32(232, 240, 255, 255) : IM_COL32(176, 188, 210, 255); }
ImU32 selected_ring() { return IM_COL32(96, 160, 255, 255); }

void draw_body_figure(ImDrawList* dl, ImVec2 p0, ImVec2 size, const bodyshape::BodyShape& shape, bool selected, bool hovered) {
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->AddRectFilled(p0, p1, tile_ground(selected, hovered), 4.0f);
    if (selected) dl->AddRect(p0, p1, selected_ring(), 4.0f, 2.0f, ImDrawFlags_None);
    const ImU32 fill = figure_fill(selected);
    for (const bodyshape::Polygon& poly : bodyshape::silhouette(shape)) {
        std::vector<ImVec2> pts;
        pts.reserve(poly.size());
        for (const bodyshape::Pt& p : poly) pts.push_back(ImVec2(p0.x + p.x * size.x, p0.y + p.y * size.y));
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fill);
    }
    // the ground line under the feet: the common baseline every figure stands on
    const float gy = p0.y + 0.985f * size.y;
    dl->AddLine(ImVec2(p0.x + 0.12f * size.x, gy), ImVec2(p1.x - 0.12f * size.x, gy), IM_COL32(110, 120, 140, 255), 1.0f);
}

}  // namespace turbo::bodyfig
