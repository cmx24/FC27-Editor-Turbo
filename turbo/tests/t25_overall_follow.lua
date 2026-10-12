package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t25 overall follow (2.1): the formula port, the follow rule, bulk edit moves the overall, kill switch, undo")

local sim = H.setup({ in_cm = true })
W.build(sim, { development = true })

local overall = require 'imports/turbo/core/overall'
local db = require 'imports/turbo/core/db'

local function attrs_of(fn)
    local a = {}
    for i, name in ipairs(overall.ATTRS) do a[name] = fn(i - 1) end
    return a
end

H.case("formula: the Lua port gives what the Turbo window's core/overall.cpp gives", function()
    -- printed by core/overall.cpp compute_overall() for attribute i = 40 + (i * 7 + seed * 13) % 55 (C++ order)
    local want = {
        [1] = { [0] = 68, [3] = 64, [5] = 60, [7] = 65, [10] = 69, [12] = 68, [14] = 73, [16] = 67, [18] = 70, [23] = 63, [25] = 65, [27] = 64, [1] = 60, [21] = 65, [22] = 64, [99] = 73 },
        [2] = { [0] = 57, [3] = 66, [5] = 68, [7] = 66, [10] = 63, [12] = 61, [14] = 59, [16] = 61, [18] = 58, [23] = 60, [25] = 65, [27] = 61, [1] = 68, [21] = 65, [22] = 61, [99] = 59 },
        [3] = { [0] = 70, [3] = 77, [5] = 74, [7] = 77, [10] = 71, [12] = 66, [14] = 61, [16] = 66, [18] = 64, [23] = 68, [25] = 65, [27] = 69, [1] = 74, [21] = 65, [22] = 69, [99] = 61 },
    }
    H.eq(#overall.ATTRS, 34, "34 attributes")
    for seed, byp in pairs(want) do
        local a = attrs_of(function(i) return 40 + (i * 7 + seed * 13) % 55 end)
        for pos, v in pairs(byp) do H.eq(overall.compute(pos, a), v, string.format("seed %d position %d", seed, pos)) end
    end
    H.eq(overall.compute(25, attrs_of(function() return 99 end)), 99, "capped at 99")
    H.eq(overall.compute(25, attrs_of(function() return 1 end)), 1, "at least 1")
end)

H.case("follow rule: unchanged formula keeps the stored overall, an offset is kept, a broken overall takes the formula", function()
    local a = attrs_of(function(i) return 40 + (i * 7 + 13) % 55 end)
    local b = {}
    for k, v in pairs(a) do b[k] = v end
    b.finishing, b.positioning, b.shotpower = 95, 95, 95
    local was, now = overall.compute(25, a), overall.compute(25, b)
    H.ok(now > was, "a striker's finishing / positioning / shot power move his formula")
    H.eq(overall.follow(25, a, 25, a, 70), 70, "nothing moved: stored stays, even off the formula")
    local g = {}
    for k, v in pairs(a) do g[k] = v end
    g.gkdiving = 99
    H.eq(overall.follow(25, a, 25, g, was + 1), was + 1, "an attribute the striker formula ignores: unchanged")
    H.eq(overall.follow(25, a, 25, b, was), now, "on the formula: follows it")
    H.eq(overall.follow(25, a, 25, b, was + 2), now + 2, "within the tolerance: keeps the offset")
    H.eq(overall.follow(25, a, 25, b, was - 2), now - 2, "within the tolerance below")
    H.eq(overall.follow(25, a, 25, b, 1), now, "OVR-1 style stored overall: becomes the formula's value")
    H.eq(overall.follow(25, a, 25, b, was + 9), now + 9, "far above the formula (typed in Live Editor): the offset stays")
    -- review fix: a far-off striker whose +1 finishing flips the rounding up moves up by one, never down to the formula
    local c, up = nil, was
    for k = 1, 30 do
        c = {}
        for key, v in pairs(a) do c[key] = v end
        c.finishing = a.finishing + k
        up = overall.compute(25, c)
        if up ~= was then break end
    end
    H.eq(up, was + 1, "a finishing raise moves the striker formula by one")
    H.eq(overall.follow(25, a, 25, c, was + 5), was + 6, "far above the formula: the raise moves the overall up")
    local cm = overall.follow(25, a, 14, a, was + 5)
    H.eq(overall.follow(14, a, 25, a, cm), was + 5, "position round trip ST -> CM -> ST ends where it started")
    H.eq(overall.follow(25, a, 0, a, was), overall.compute(0, a), "position change: the new position's formula")
    H.eq(overall.follow(25, a, 25, attrs_of(function() return 99 end), 98), 99, "1..99")
    H.ok(overall.follows_field("finishing") and overall.follows_field("preferredposition1") and overall.follows_field("defensiveawareness"),
        "attributes and position follow")
    H.ok(not overall.follows_field("potential") and not overall.follows_field("overallrating") and not overall.follows_field("preferredposition2"),
        "other fields do not")
end)

-- 1003: position 4 (RCB, centre-back formula), 1004: 5 (CB), 1005: 6 (LCB); all attributes 40 + pid % 30
local players = db.get_table("players")
local function rec_of(pid) return db.find(players, "playerid", pid) end
local function ovr(pid) return db.get_int(players, rec_of(pid), "overallrating", -1) end
local function set(pid, f, v) assert(db.set(players, rec_of(pid), f, v)) end
local function cur_attrs(pid) return (overall.read(db, players, rec_of(pid))) end
local function pos_of(pid) local _, p = overall.read(db, players, rec_of(pid)); return p end

H.case("bulk_edit: attributes in \"set\" move each player's overall; undo file; undo puts it back", function()
    H.eq(pos_of(1003), 4, "1003 RCB"); H.eq(pos_of(1004), 5, "1004 CB"); H.eq(pos_of(1005), 6, "1005 LCB")
    -- 1003 on the formula, 1004 one above it, 1005 far off it (stored 65 for attributes of 55)
    set(1003, "overallrating", overall.compute(4, cur_attrs(1003)))
    set(1004, "overallrating", overall.compute(5, cur_attrs(1004)) + 1)
    local o3, o4, o5 = ovr(1003), ovr(1004), ovr(1005)
    local f5 = overall.compute(6, cur_attrs(1005))
    H.ok(math.abs(o5 - f5) > overall.TOLERANCE, "1005 is far off the formula")
    local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003, 1004, 1005 } }, filters = {},
        set = { standingtackle = 85, defensiveawareness = 85, interceptions = 85 }, actions = {} })
    H.eq(ok, true, msg)
    H.has(msg, "overall followed the attributes for 3 players")
    H.has(msg, "overall_follow_undo.json")
    H.eq(ovr(1003), overall.compute(4, cur_attrs(1003)), "1003 follows the formula")
    H.eq(ovr(1004), overall.compute(5, cur_attrs(1004)) + 1, "1004 keeps his +1")
    H.eq(ovr(1005), o5 + overall.compute(6, cur_attrs(1005)) - f5, "1005 (far off, not broken) keeps his offset")
    H.ok(ovr(1003) > o3 and ovr(1004) > o4 and ovr(1005) > o5, "they went up")
    local index = H.read(H.out("overall_follow_undo.json"))
    H.has(index, "turbo_overall_follow_undo/2"); H.has(index, tostring(W.USER_TEAM), "the run names the user's club")
    local data = H.read(H.out("overall_follow_undo_1.txt"))
    H.has(data, "#turbo-overall-undo 1 3"); H.has(data, "1004 " .. o4 .. " " .. ovr(1004))
    -- 1005 changed after the bulk edit: left as it is by the undo
    set(1005, "overallrating", 50)
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.eq(ok, true, msg)
    H.has(msg, "2 overalls put back"); H.has(msg, "1 changed since")
    H.eq(ovr(1003), o3, "1003 back"); H.eq(ovr(1004), o4, "1004 back"); H.eq(ovr(1005), 50, "1005 untouched")
    H.eq(H.read(H.out("overall_follow_undo_1.txt")), nil, "every overall settled: the run and its file are gone")
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.eq(ok, true, msg); H.has(msg, "no overall follow to undo")
    set(1005, "overallrating", o5)
end)

H.case("review fixes: the undo only in the database the run wrote; a run that put nothing back is kept", function()
    local base = ovr(1003)
    local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { standingtackle = 88 }, actions = {} })
    H.eq(ok, true, msg); H.has(msg, "overall followed the attributes for 1 players")
    local after = ovr(1003)
    H.ok(after ~= base, "1003 moved")
    -- the main-menu squads: refused, nothing written, the run kept
    H.sim.in_cm = false
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.eq(ok, false, msg); H.has(msg, "load that career first"); H.has(msg, "the run is kept")
    H.eq(ovr(1003), after, "squads: untouched")
    H.sim.in_cm = true
    -- another career (another club, same player ids): refused
    local game = require 'imports/turbo/core/game'
    local team_of = game.user_team_id
    game.user_team_id = function() return W.USER_TEAM + 1 end
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    game.user_team_id = team_of
    H.eq(ok, false, msg); H.has(msg, "belongs to another career"); H.has(msg, "the run is kept")
    H.eq(ovr(1003), after, "another career: untouched")
    -- another load of this career (a restart, a reload): only with any_load
    local B = type(TURBO_STATE) == "table" and TURBO_STATE.bridge or nil
    H.ok(B and type(B.session) == "string", "the bridge state is there (session + load_gen recorded)")
    local gen = B.load_gen
    B.load_gen = (gen or 0) + 1
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.eq(ok, false, msg); H.has(msg, "any_load"); H.eq(ovr(1003), after, "another load: untouched without any_load")
    -- every overall changed since: nothing put back, the run kept (the old code dropped it here)
    set(1003, "overallrating", after - 1)
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true, any_load = true })
    H.eq(ok, true, msg); H.has(msg, "0 overalls put back: nothing matched"); H.has(msg, "the run is kept")
    H.ok(H.read(H.out("overall_follow_undo_2.txt")) ~= nil, "the run's file is still there")
    set(1003, "overallrating", after)
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true, any_load = true })
    H.eq(ok, true, msg); H.has(msg, "1 overalls put back"); H.eq(ovr(1003), base, "put back with any_load")
    B.load_gen = gen
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.has(msg, "no overall follow to undo")
end)

H.case("review fixes: a refused write mid-run still records the overalls already followed", function()
    local o3, o4 = ovr(1003), ovr(1004)
    local tbl = db.get_table("players")
    local orig = db.set
    local fail_pid = nil
    db.set = function(t, rec, field, v, dry)
        if field == "interceptions" and db.get_int(t, rec, "playerid", -1) == fail_pid then return false, "refused (test)" end
        return orig(t, rec, field, v, dry)
    end
    -- the last of the three in record order is refused
    local order = {}
    for rec in db.records(tbl) do
        local pid = db.get_int(tbl, rec, "playerid", -1)
        if pid == 1003 or pid == 1004 or pid == 1005 then order[#order + 1] = pid end
    end
    fail_pid = order[3]
    local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003, 1004, 1005 } }, filters = {},
        set = { interceptions = 90, standingtackle = 90 }, actions = {} })
    db.set = orig
    H.eq(ok, false, msg); H.has(msg, "the 2 overalls followed before it are in overall_follow_undo.json")
    ok, msg = H.turbo().run("bulk_edit", { undo_overall = true })
    H.eq(ok, true, msg); H.has(msg, "2 overalls put back")
    H.eq(ovr(1003), o3, "1003 back"); H.eq(ovr(1004), o4, "1004 back")
end)

H.case("review fixes: the undo keeps at most UNDO_RUNS runs and UNDO_MAX_ENTRIES overalls", function()
    local be = require 'imports/turbo/features/bulk_edit'
    local runs, max = be.UNDO_RUNS, be.UNDO_MAX_ENTRIES
    be.UNDO_RUNS, be.UNDO_MAX_ENTRIES = 3, 2
    for _, v in ipairs({ 30, 90, 30, 90 }) do
        local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { standingtackle = v }, actions = {} })
        H.eq(ok, true, msg)
    end
    local json = require 'imports/external/json'
    local index = json.decode(H.read(H.out("overall_follow_undo.json")))
    H.eq(#index.runs, 2, "2 overalls at most: the two newest runs")
    H.eq(H.read(H.out("overall_follow_undo_" .. (index.runs[1].id - 1) .. ".txt")), nil, "a dropped run's file is removed")
    H.ok(H.read(H.out("overall_follow_undo_" .. index.runs[2].id .. ".txt")) ~= nil, "the newest run's file is there")
    be.UNDO_RUNS, be.UNDO_MAX_ENTRIES = runs, max
    for _ = 1, 2 do H.turbo().run("bulk_edit", { undo_overall = true }) end
end)

H.case("bulk_edit: an ignored attribute, overallrating in \"set\", overall_follow false, the kill switch", function()
    local o3 = ovr(1003)
    local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { gkdiving = 90 }, actions = {} })
    H.eq(ok, true, msg); H.eq(ovr(1003), o3, "GK diving does not count for a centre-back")
    ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { standingtackle = 30, overallrating = 77 }, actions = {} })
    H.eq(ok, true, msg); H.eq(ovr(1003), 77, "an overall in set wins")
    ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { standingtackle = 90 }, actions = {}, overall_follow = false })
    H.eq(ok, true, msg); H.eq(ovr(1003), 77, "overall_follow false: unchanged")
    local f = io.open(H.out("overall_follow_off.txt"), "w"); f:write("off"); f:close()
    ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { standingtackle = 20 }, actions = {} })
    H.eq(ok, true, msg); H.eq(ovr(1003), 77, "kill switch: unchanged")
    H.has(msg, "overall not followed (turbo_output\\overall_follow_off.txt)")
    os.remove(H.out("overall_follow_off.txt"))
    -- the position: a striker's formula
    set(1003, "overallrating", overall.compute(4, cur_attrs(1003)))
    ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { preferredposition1 = 25 }, actions = {} })
    H.eq(ok, true, msg); H.eq(ovr(1003), overall.compute(25, cur_attrs(1003)), "position change: the striker formula")
end)

H.case("bulk_edit without the 34 attributes (squads world): nothing extra written", function()
    local sim2 = H.setup({ in_cm = true })
    W.build(sim2, {})
    local p2 = require('imports/turbo/core/db').get_table("players")
    local r = require('imports/turbo/core/db').find(p2, "playerid", 1003)
    local before = require('imports/turbo/core/db').get_int(p2, r, "overallrating", -1)
    local ok, msg = H.turbo().run("bulk_edit", { scope = { playerids = { 1003 } }, filters = {}, set = { preferredposition1 = 25 }, actions = {} })
    H.eq(ok, true, msg)
    H.eq(require('imports/turbo/core/db').get_int(p2, r, "overallrating", -1), before, "no attributes: the overall stays")
end)

H.finish()
