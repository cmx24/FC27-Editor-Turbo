// FC 27 LE Turbo GUI - overall rating formula (overall.h)
#include "core/overall.h"

#include <algorithm>
#include <cmath>

namespace turbo {
namespace overall {

const char* const kAttrNames[kAttrCount] = {
    "acceleration",
    "sprintspeed",
    "agility",
    "balance",
    "jumping",
    "stamina",
    "strength",
    "reactions",
    "aggression",
    "composure",
    "interceptions",
    "positioning",
    "vision",
    "ballcontrol",
    "crossing",
    "dribbling",
    "finishing",
    "freekickaccuracy",
    "headingaccuracy",
    "longpassing",
    "shortpassing",
    "shotpower",
    "longshots",
    "standingtackle",
    "slidingtackle",
    "volleys",
    "curve",
    "penalties",
    "gkdiving",
    "gkhandling",
    "gkkicking",
    "gkreflexes",
    "gkpositioning",
    "defensiveawareness"};

int attr_index(const std::string& n) {
    for (int i = 0; i < kAttrCount; ++i)
        if (n == kAttrNames[i]) return i;
    return -1;
}

namespace {

// One fitted table per position FC 27 stores players with: the position id it was fitted on, the constant bonus and one
// weight per attribute (order of Attr). The weights add up to about 1.
struct Table {
    int position;
    double bonus;
    double w[kAttrCount];
};

const Table kTables[] = {
    {0, 0.988, {0, 0, 0, 0, 0, 0, 0, 0.1069, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.2102, 0.2108, 0.0479, 0.2123, 0.2116, 0}},   // GK
    {3, 1.977, {0.0479, 0.0664, 0, 0, 0, 0.0778, 0, 0.089, 0, 0, 0.1138, 0, 0, 0.0698, 0.0906, 0.0016, 0, 0, 0.0363, 0, 0.0674, 0, 0, 0.1206, 0.1417, 0, 0, 0, 0, 0.0003, 0.0012, 0.0021, 0.0007, 0.077}},   // RB
    {5, -0.223, {0.0024, 0.0193, 0, 0, 0.0265, 0.0016, 0.0971, 0.0551, 0.0667, 0, 0.1276, 0, 0, 0.0428, 0, 0, 0, 0, 0.0993, 0, 0.0482, 0, 0, 0.1758, 0.0971, 0, 0, 0, 0, 0, 0.0007, 0.002, 0, 0.1448}},   // CB
    {7, 2.086, {0.0526, 0.0626, 0.0002, 0.0001, 0.0008, 0.0763, 0, 0.0814, 0, 0, 0.1128, 0, 0, 0.0715, 0.0915, 0, 0.0005, 0, 0.0364, 0, 0.0685, 0, 0, 0.1215, 0.1412, 0, 0, 0, 0, 0.0005, 0, 0.0038, 0.0008, 0.0803}},   // LB
    {10, 1.061, {0.0003, 0.0014, 0, 0, 0.0001, 0.056, 0.0372, 0.0784, 0.0451, 0, 0.1347, 0, 0.0399, 0.1012, 0, 0, 0, 0, 0, 0.0953, 0.148, 0, 0, 0.1227, 0.0479, 0, 0, 0, 0, 0, 0, 0, 0, 0.0909}},   // CDM
    {12, 1.283, {0.0654, 0.0578, 0, 0, 0, 0.047, 0.0004, 0.0739, 0.0002, 0, 0, 0.0855, 0.0669, 0.1345, 0.0936, 0.1461, 0.0602, 0, 0, 0.0472, 0.1153, 0, 0, 0.0004, 0, 0, 0, 0, 0.0017, 0, 0.001, 0.0034, 0.0026, 0.0006}},   // RM
    {14, -0.181, {0.0017, 0.0018, 0, 0, 0, 0.0553, 0, 0.0952, 0, 0, 0.0499, 0.058, 0.1232, 0.1461, 0, 0.0624, 0.0197, 0, 0, 0.134, 0.168, 0, 0.0382, 0.0515, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},   // CM
    {16, 1.008, {0.0689, 0.0544, 0, 0.0015, 0, 0.0459, 0.0002, 0.0723, 0, 0, 0, 0.094, 0.0588, 0.1367, 0.0991, 0.1541, 0.0537, 0, 0.0004, 0.0506, 0.1088, 0, 0, 0, 0, 0, 0, 0, 0.0019, 0, 0.0005, 0, 0, 0}},   // LM
    {18, -0.449, {0.0414, 0.0321, 0.0251, 0, 0, 0, 0.0007, 0.0803, 0, 0, 0, 0.0895, 0.1376, 0.1534, 0, 0.1275, 0.0705, 0, 0.0004, 0.0381, 0.1642, 0, 0.046, 0, 0, 0, 0, 0, 0, 0.0048, 0, 0, 0, 0}},   // CAM
    {23, 0.102, {0.0693, 0.0531, 0.0243, 0.0002, 0, 0.0008, 0, 0.0739, 0, 0, 0, 0.098, 0.0638, 0.1419, 0.0889, 0.1592, 0.1057, 0, 0, 0, 0.0855, 0, 0.0323, 0, 0, 0, 0, 0.0041, 0, 0, 0, 0.0013, 0, 0}},   // RW
    {25, -0.366, {0.0379, 0.0495, 0, 0, 0, 0, 0.0469, 0.0891, 0, 0, 0, 0.1305, 0.0007, 0.1077, 0, 0.0627, 0.1958, 0, 0.095, 0, 0.048, 0.0964, 0.0316, 0, 0, 0.0142, 0, 0, 0.0009, 0, 0, 0, 0, 0}},   // ST
    {27, -0.228, {0.0602, 0.0658, 0.0284, 0.0011, 0, 0, 0.0038, 0.076, 0, 0.0016, 0, 0.0911, 0.0719, 0.1385, 0.0927, 0.1509, 0.1107, 0, 0, 0, 0.0832, 0, 0.0268, 0.0017, 0, 0, 0, 0, 0.0009, 0.0027, 0, 0.0018, 0.0071, 0}},   // LW
};
// kTables indexes
enum { TGK, TRB, TCB, TLB, TCDM, TRM, TCM, TLM, TCAM, TRW, TST, TLW };

// Which fitted table a position id uses (the ids FC 27 has no table for share the neighbouring position's)
int table_of(int pos) {
    switch (pos) {
        case 0: return TGK;
        case 1: case 4: case 5: case 6: return TCB;               // SW, RCB, CB, LCB
        case 2: case 3: return TRB;                               // RWB, RB
        case 7: case 8: return TLB;                               // LB, LWB
        case 9: case 10: case 11: return TCDM;                    // RDM, CDM, LDM
        case 12: return TRM;
        case 13: case 14: case 15: return TCM;                    // RCM, CM, LCM
        case 16: return TLM;
        case 17: case 18: case 19: return TCAM;                   // RAM, CAM, LAM
        case 20: case 23: return TRW;                             // RF, RW
        case 21: case 24: case 25: case 26: return TST;           // CF, RS, ST, LS
        case 22: case 27: return TLW;                             // LF, LW
        default: return TCM;                                      // unknown: a central midfielder
    }
}

const Table& table_for(int pos) { return kTables[table_of(pos)]; }

}  // namespace

Group group_of_position(int p) {
    switch (table_of(p)) {
        case TGK: return GroupGK;
        case TCB: return GroupCB;
        case TRB: case TLB: return GroupFB;
        case TCDM: return GroupDM;
        case TCM: return GroupCM;
        case TCAM: return GroupAM;
        case TRM: case TLM: case TRW: case TLW: return GroupWide;
        default: return GroupST;
    }
}

const char* group_name(Group g) {
    static const char* n[kGroupCount] = {"Goalkeeper", "Centre-back", "Full-back / wing-back", "Defensive midfield",
                                         "Central midfield", "Attacking midfield", "Wide midfield / winger", "Striker"};
    return g >= 0 && g < kGroupCount ? n[g] : "?";
}

double raw_overall(int pos, const Attrs& a) {
    const Table& t = table_for(pos);
    double s = t.bonus;
    for (int i = 0; i < kAttrCount; ++i) s += t.w[i] * a[static_cast<size_t>(i)];
    return s;
}

int compute_overall(int pos, const Attrs& a) {
    const int v = static_cast<int>(std::floor(raw_overall(pos, a) + 0.5));
    return std::max(1, std::min(99, v));
}

double attr_weight(int pos, int attr) { return attr >= 0 && attr < kAttrCount ? table_for(pos).w[attr] : 0.0; }

bool adjust(int pos, Attrs& a, int delta, std::vector<Move>* moves) {
    if (delta != 1 && delta != -1) return false;
    const int target = compute_overall(pos, a) + delta;
    if (target < 1 || target > 99) return false;
    const Table& t = table_for(pos);
    Attrs w = a;
    int moved[kAttrCount] = {0};
    // Pass 0 uses the attributes that weigh at least 0.04 (the position's real drivers); pass 1 adds the light ones when
    // the drivers are all at their limit. One point of an attribute changes the raw value by at most its weight (< 0.25),
    // so the rounded overall steps by one and cannot skip the target.
    for (int pass = 0; pass < 2 && compute_overall(pos, w) != target; ++pass) {
        const double min_w = pass == 0 ? 0.04 : 0.0005;
        for (int guard = 0; guard < 4000 && compute_overall(pos, w) != target; ++guard) {
            int best = -1;
            double best_score = -1;
            for (int i = 0; i < kAttrCount; ++i) {
                if (t.w[i] < min_w) continue;
                const int nv = w[static_cast<size_t>(i)] + delta;
                if (nv < 1 || nv > 99) continue;
                const double score = t.w[i] / (1.0 + moved[i]);   // heavy attributes first, then round-robin
                if (score > best_score) {
                    best_score = score;
                    best = i;
                }
            }
            if (best < 0) break;
            w[static_cast<size_t>(best)] += delta;
            ++moved[best];
        }
    }
    if (compute_overall(pos, w) != target) return false;
    if (moves)
        for (int i = 0; i < kAttrCount; ++i)
            if (w[static_cast<size_t>(i)] != a[static_cast<size_t>(i)])
                moves->push_back({i, a[static_cast<size_t>(i)], w[static_cast<size_t>(i)]});
    a = w;
    return true;
}

}  // namespace overall
}  // namespace turbo
