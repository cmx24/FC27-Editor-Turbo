-- FC 27 LE Turbo - the overall rating as a function of the attributes (a port of the Turbo window's core/overall.cpp:
-- one fitted table per position, weights . attributes + bonus, rounded, 1..99) and the rule that makes the stored
-- overall follow an attribute or position edit (2.1, used by bulk edit; the window does the same in App::edit).
-- FC 27 shows the stored overallrating until its own growth works it out again, so an attribute-only write left the
-- overall the game shows unchanged. Kill switch: turbo_output\overall_follow_off.txt (attributes alone, as in 2.0).
-- Keep the tables identical to core/overall.cpp (t25_overall_follow.lua checks values the C++ side computes).

local M = {}

M.KILL_FILE = "overall_follow_off.txt"
M.TOLERANCE = 2   -- core/overall.h kFollowTolerance
M.BROKEN = 5      -- core/overall.h kFollowBroken

-- The 34 attributes in the C++ order (Attr enum)
M.ATTRS = { "acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength", "reactions",
    "aggression", "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling",
    "finishing", "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "shotpower", "longshots",
    "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling", "gkkicking",
    "gkreflexes", "gkpositioning", "defensiveawareness" }
local IS_ATTR = {}
for _, a in ipairs(M.ATTRS) do IS_ATTR[a] = true end

-- { bonus, weights in M.ATTRS order }
local T = {
    GK = { 0.988, { 0, 0, 0, 0, 0, 0, 0, 0.1069, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.2102, 0.2108, 0.0479, 0.2123, 0.2116, 0 } },
    RB = { 1.977, { 0.0479, 0.0664, 0, 0, 0, 0.0778, 0, 0.089, 0, 0, 0.1138, 0, 0, 0.0698, 0.0906, 0.0016, 0, 0, 0.0363, 0, 0.0674, 0, 0, 0.1206, 0.1417, 0, 0, 0, 0, 0.0003, 0.0012, 0.0021, 0.0007, 0.077 } },
    CB = { -0.223, { 0.0024, 0.0193, 0, 0, 0.0265, 0.0016, 0.0971, 0.0551, 0.0667, 0, 0.1276, 0, 0, 0.0428, 0, 0, 0, 0, 0.0993, 0, 0.0482, 0, 0, 0.1758, 0.0971, 0, 0, 0, 0, 0, 0.0007, 0.002, 0, 0.1448 } },
    LB = { 2.086, { 0.0526, 0.0626, 0.0002, 0.0001, 0.0008, 0.0763, 0, 0.0814, 0, 0, 0.1128, 0, 0, 0.0715, 0.0915, 0, 0.0005, 0, 0.0364, 0, 0.0685, 0, 0, 0.1215, 0.1412, 0, 0, 0, 0, 0.0005, 0, 0.0038, 0.0008, 0.0803 } },
    CDM = { 1.061, { 0.0003, 0.0014, 0, 0, 0.0001, 0.056, 0.0372, 0.0784, 0.0451, 0, 0.1347, 0, 0.0399, 0.1012, 0, 0, 0, 0, 0, 0.0953, 0.148, 0, 0, 0.1227, 0.0479, 0, 0, 0, 0, 0, 0, 0, 0, 0.0909 } },
    RM = { 1.283, { 0.0654, 0.0578, 0, 0, 0, 0.047, 0.0004, 0.0739, 0.0002, 0, 0, 0.0855, 0.0669, 0.1345, 0.0936, 0.1461, 0.0602, 0, 0, 0.0472, 0.1153, 0, 0, 0.0004, 0, 0, 0, 0, 0.0017, 0, 0.001, 0.0034, 0.0026, 0.0006 } },
    CM = { -0.181, { 0.0017, 0.0018, 0, 0, 0, 0.0553, 0, 0.0952, 0, 0, 0.0499, 0.058, 0.1232, 0.1461, 0, 0.0624, 0.0197, 0, 0, 0.134, 0.168, 0, 0.0382, 0.0515, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    LM = { 1.008, { 0.0689, 0.0544, 0, 0.0015, 0, 0.0459, 0.0002, 0.0723, 0, 0, 0, 0.094, 0.0588, 0.1367, 0.0991, 0.1541, 0.0537, 0, 0.0004, 0.0506, 0.1088, 0, 0, 0, 0, 0, 0, 0, 0.0019, 0, 0.0005, 0, 0, 0 } },
    CAM = { -0.449, { 0.0414, 0.0321, 0.0251, 0, 0, 0, 0.0007, 0.0803, 0, 0, 0, 0.0895, 0.1376, 0.1534, 0, 0.1275, 0.0705, 0, 0.0004, 0.0381, 0.1642, 0, 0.046, 0, 0, 0, 0, 0, 0, 0.0048, 0, 0, 0, 0 } },
    RW = { 0.102, { 0.0693, 0.0531, 0.0243, 0.0002, 0, 0.0008, 0, 0.0739, 0, 0, 0, 0.098, 0.0638, 0.1419, 0.0889, 0.1592, 0.1057, 0, 0, 0, 0.0855, 0, 0.0323, 0, 0, 0, 0, 0.0041, 0, 0, 0, 0.0013, 0, 0 } },
    ST = { -0.366, { 0.0379, 0.0495, 0, 0, 0, 0, 0.0469, 0.0891, 0, 0, 0, 0.1305, 0.0007, 0.1077, 0, 0.0627, 0.1958, 0, 0.095, 0, 0.048, 0.0964, 0.0316, 0, 0, 0.0142, 0, 0, 0.0009, 0, 0, 0, 0, 0 } },
    LW = { -0.228, { 0.0602, 0.0658, 0.0284, 0.0011, 0, 0, 0.0038, 0.076, 0, 0.0016, 0, 0.0911, 0.0719, 0.1385, 0.0927, 0.1509, 0.1107, 0, 0, 0, 0.0832, 0, 0.0268, 0.0017, 0, 0, 0, 0, 0.0009, 0.0027, 0, 0.0018, 0.0071, 0 } },
}
-- preferredposition1 id -> table (core/overall.cpp table_of; unknown ids use the central midfielder's)
local BY_POS = { [0] = T.GK, [1] = T.CB, [2] = T.RB, [3] = T.RB, [4] = T.CB, [5] = T.CB, [6] = T.CB, [7] = T.LB, [8] = T.LB,
    [9] = T.CDM, [10] = T.CDM, [11] = T.CDM, [12] = T.RM, [13] = T.CM, [14] = T.CM, [15] = T.CM, [16] = T.LM,
    [17] = T.CAM, [18] = T.CAM, [19] = T.CAM, [20] = T.RW, [21] = T.ST, [22] = T.LW, [23] = T.RW, [24] = T.ST,
    [25] = T.ST, [26] = T.ST, [27] = T.LW }

-- The players fields whose edit makes the overall follow
function M.follows_field(name)
    return name == "preferredposition1" or IS_ATTR[name] == true
end

-- attrs: { fieldname = value } (missing = 0)
function M.raw(pos, attrs)
    local t = BY_POS[pos] or T.CM
    local s, w = t[1], t[2]
    for i, name in ipairs(M.ATTRS) do s = s + w[i] * (attrs[name] or 0) end
    return s
end

function M.compute(pos, attrs)
    local v = math.floor(M.raw(pos, attrs) + 0.5)
    if v < 1 then v = 1 elseif v > 99 then v = 99 end
    return math.tointeger(v) or v
end

-- core/overall.h follow(): the formula does not move -> stored; otherwise stored + the formula's move (an offset from the
-- formula is kept: never against the edit, a position round trip ends where it started); only a broken overall (BROKEN or
-- less and more than TOLERANCE below the formula: OVR-1) takes the formula's value. 1..99.
function M.follow(pos_before, before, pos_after, after, stored)
    local was, now = M.compute(pos_before, before), M.compute(pos_after, after)
    if was == now then return stored end
    local broken = stored <= M.BROKEN and was - stored > M.TOLERANCE
    local v = broken and now or stored + (now - was)
    if v < 1 then v = 1 elseif v > 99 then v = 99 end
    return v
end

-- The player's attributes and preferredposition1 from the players table (db = core/db); nil when one is missing
function M.read(db, tbl, rec)
    local a = {}
    for _, name in ipairs(M.ATTRS) do
        if not db.has_field(tbl, name) then return nil end
        a[name] = db.get_int(tbl, rec, name, 0)
    end
    return a, db.get_int(tbl, rec, "preferredposition1", 14)
end

-- The kill switch: turbo_output\overall_follow_off.txt in the Live Editor folder (out_dir given = that folder)
function M.off(out_dir)
    local util = require 'imports/turbo/core/util'
    if not out_dir then
        local root = require('imports/turbo/core/env').le_root()
        if not root then return false end
        out_dir = util.join(root, "turbo_output")
    end
    return util.file_exists(util.join(out_dir, M.KILL_FILE)) == true
end

return M
