// FC 27 LE Turbo GUI - body shapes (5.0c E8-body): the plain name of every body type and the figure drawn for it.
// Platform independent, no ImGui, unit-tested (tests/native/test_body_picker.h).
//
// One shape model for both pickers (the Look card and the visual picker window), for players AND managers:
//   body_shape()  height and build of a code. The codes the game names (1-9 and 11: "Average height, lean" ...) take their size from the
//                 name itself (a nominal table, Source::Nominal) because the census of the few players or managers who carry them
//                 contradicts their own labels (code 3 "stocky" has a BMI of a normal build; manager code 8 "Short" averages taller than
//                 manager code 1 "Average"). The manager set 81-89 and every other code with census data take the midpoint of the census
//                 range (Source::Census): INDICATIVE only (an FC 26-era export, ranges are outlier-driven), so the tooltip says "estimated".
//   body_name()   the name the grid shows. "" when nothing names the code (the caller says "His own body": it is only ever offered
//                 when the record already holds it). The words "Manager body", "Style", "Specific body" and "Unique body" never appear.
//   silhouette()  the figure as convex polygons in a unit box, tile by tile on ONE common height scale so tiles differ visibly.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bodytype_catalog.h"

namespace turbo::bodyshape {

enum class Source { Nominal, Census, Estimated };  // Estimated = no data at all: an average, normal figure

struct BodyShape {
    int64_t code = 0;
    float height_cm = 176.0f;
    float build = 0.5f;  // 0 lean .. 1 stocky
    Source src = Source::Estimated;
    bool estimated() const { return src != Source::Nominal; }
};

struct Pt {
    float x = 0.0f, y = 0.0f;
};
using Polygon = std::vector<Pt>;

constexpr float kMinHeightCm = 150.0f, kMaxHeightCm = 210.0f;

// The codes 81-89: the common manager body set. A manager with a generic head carries them (the game itself pairs them), so they are
// never a "risky pairing" for the manager table.
bool manager_set(int64_t code);
// bodytype::risky_pairing, per subject: false for a manager holding the manager set
bool risky_for(bool manager, const bodytype::Catalog& cat, int64_t code, int head_class);

BodyShape body_shape(bool manager, int64_t code, const bodytype::Catalog* cat);
// "" when nothing names the code. Unique among the codes of one subject that the grid offers (the manager set gets "(n of m)" where
// several share a height and build).
std::string body_name(bool manager, int64_t code, const bodytype::Catalog* cat);
// "about 178 cm, 80 kg (estimated), used by 107 managers" - what the tile tooltip adds to the name; "" when nothing is known
std::string body_detail(bool manager, int64_t code, const bodytype::Catalog* cat);

std::vector<Polygon> silhouette(const BodyShape& s);

}  // namespace turbo::bodyshape
