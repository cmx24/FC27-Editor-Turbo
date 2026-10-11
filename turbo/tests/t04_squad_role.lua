package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t04 squad role")

local function roles(sim)
    local out = {}
    for pid, addr in pairs(W.role_entries) do out[pid] = sim:r32(addr) end
    return out
end

-- FC 26 layout, no contract table
local sim = H.setup({ in_cm = true })
W.build(sim, {})

H.case("role validated 1..5", function()
    local ok, msg = H.turbo().run("squad_role", { role = 6 })
    H.eq(ok, false); H.has(msg, "role must be 1")
end)

H.case("memory pass with the FC 26 layout", function()
    local ok, msg = H.turbo().run("squad_role", { role = 1 })
    H.eq(ok, true, msg); H.has(msg, "26 of 26 players have the role now in memory (PlayerStatusManager +0x18, entry 8 bytes)")
    H.ok(not msg:find("career_playercontract", 1, true), "FC 27 has no contract table: not mentioned")
    H.has(msg, "\n  updated (26): Player 1001 (1001) Crucial, ")
    H.has(msg, "open the Squad screen again")
    for pid, r in pairs(roles(sim)) do H.eq(r, 1, "role of " .. pid) end
    H.eq(sim:r32(sim.managers[87] + 0x08) ~= 0, true, "noise vector untouched (pointer)")
end)

H.case("noise vector of other players never written", function()
    local psm = sim.managers[87]
    local b = sim:r64(psm + 0x08)
    for i = 0, 29 do H.eq(sim:r32(b + i * 8 + 4), 1, "noise role") end
end)

H.case("dry run writes nothing", function()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = H.turbo().run("squad_role", { role = 4 })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]")
    for _, r in pairs(roles(sim)) do H.eq(r, 1, "unchanged") end
    H.write_config({ turbo = { dry_run = false } })
end)

-- Moved vector, wider entries, contract table + loans
sim = H.setup({ in_cm = true })
W.build(sim, { role_vec_off = 0x48, role_stride = 16, career_playercontract = true, playerloans = true })

H.case("finds a moved vector with 16-byte entries; include_loaned_in = false leaves loaned-in players alone", function()
    local ok, msg = H.turbo().run("squad_role", { role = 5, include_loaned_in = false })
    H.eq(ok, true, msg)
    H.has(msg, "25 of 26 players have the role now in memory (PlayerStatusManager +0x48, entry 16 bytes)")
    H.has(msg, "career_playercontract: 25 rows")
    H.has(msg, "; 1 skipped")
    H.has(msg, "skipped (1): Player 1026 (1026): loaned in (include_loaned_in is off)")
    local r = roles(sim)
    H.eq(r[1002], 5, "set")
    H.eq(r[W.LOANED_IN], 3, "loaned-in untouched")
    local c = sim:find_row("career_playercontract", "playerid", 1002)
    H.eq(sim:value("career_playercontract", c, "playerrole"), 5, "contract role")
end)

H.case("loaned-in players are included by default (the game keeps a role for them too)", function()
    local ok, msg = H.turbo().run("squad_role", { role = 2 })
    H.eq(ok, true, msg)
    H.eq(roles(sim)[W.LOANED_IN], 2, "loaned-in set")
end)

H.case("use_memory=false leaves memory alone and still updates the contract table", function()
    local before = roles(sim)[1002]
    local ok, msg = H.turbo().run("squad_role", { role = 4, use_memory = false })
    H.eq(ok, true, msg); H.has(msg, "PlayerStatusManager: not touched (use_memory = false)")
    H.eq(roles(sim)[1002], before, "memory untouched")
    local c = sim:find_row("career_playercontract", "playerid", 1002)
    H.eq(sim:value("career_playercontract", c, "playerrole"), 4, "contract role")
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

-- No vector, no contract table: must refuse
sim = H.setup({ in_cm = true })
W.build(sim, { role_vec_off = false })

H.case("refuses when neither storage exists", function()
    local ok, msg = H.turbo().run("squad_role", { role = 3 })
    H.eq(ok, false)
    H.has(msg, "no role list matching your squad")
end)

H.case("no unmapped memory reads (no vector)", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

-- 2.0 role fix: gap in the teamsheet, 52-entry list with empties and mixed role bytes ---------------------------------------
sim = H.setup({ in_cm = true })
W.build(sim, { role_vec_52 = true, sheet_gap = 8 })

H.case("user_squad: an empty sheet slot is skipped, never the end of the list; the club links complete the squad", function()
    local game = require 'imports/turbo/core/game'
    local squad, count, source, info = game.user_squad()
    H.eq(count, 26, "all club players")
    H.eq(source, "teamplayerlinks")
    H.eq(info.sheet, 25, "the sheet lists 25"); H.eq(info.links, 26); H.eq(info.gaps, 1)
    H.eq(squad[W.USER_PLAYERS[9]], true, "the player of the empty slot is in the squad")
end)

H.case("psm_members: every entry with a player, empty entries left out", function()
    local role = require 'imports/turbo/features/squad_role'
    local game = require 'imports/turbo/core/game'
    local squad, count = game.user_squad()
    local layout = role.locate(squad, count, nil)
    H.eq(layout ~= nil, true, "52-entry list with empties is found")
    H.eq(layout.count, 52)
    local set, n, list = role.psm_members(layout)
    H.eq(n, 26); H.eq(#list, 26); H.eq(set[W.USER_PLAYERS[1]], true); H.eq(set[0], nil)
end)

H.case("sheet gap + 52-entry list: every club player gets the role, empty entries stay empty", function()
    local before = {}
    for pid, addr in pairs(W.role_entries) do before[pid] = sim:rb(addr) end
    local ok, msg = H.turbo().run("squad_role", { role = 2 })
    H.eq(ok, true, msg); H.has(msg, "26 of 26 players have the role now in memory (PlayerStatusManager +0x18, entry 8 bytes)")
    for pid, addr in pairs(W.role_entries) do H.eq(sim:rb(addr), 2, "role of " .. pid) end
    local v = W.role_vec
    for idx = 0, 51 do
        local e = v.begin + idx * v.stride
        local pid = sim:r32(e)
        if pid <= 0 then H.eq(sim:rb(e + 4), 0xFF, "empty entry " .. idx .. " untouched") end
    end
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.case("a role list player outside the club is left alone and counted; a club player with no entry is counted", function()
    local v = W.role_vec
    sim:w32(v.begin, 9999); sim:wb(v.begin + 4, 4)                       -- entry 0: a stranger
    local gone = W.USER_PLAYERS[3]
    sim:w32(W.role_entries[gone] - 4, 0)                                  -- the club player's entry is emptied
    local ok, msg = H.turbo().run("squad_role", { role = 5 })
    H.eq(ok, true, msg)
    H.has(msg, "25 of 26 players have the role now in memory"); H.has(msg, "; 1 skipped")
    H.has(msg, string.format("skipped (1): Player %d (%d): no entry in the role list", gone, gone))
    H.has(msg, "1 role list players are not in your squad")
    H.has(msg, "1 squad players have no entry in the role list")
    H.eq(sim:rb(v.begin + 4), 4, "stranger untouched")
end)

-- one stray role byte (9) no longer hides the list; the other bytes are still written
sim = H.setup({ in_cm = true })
W.build(sim, { role_bad_byte = { [W.USER_PLAYERS[5]] = 9 } })

H.case("a role byte of 9 still locates the vector (warning), and the player is written like the rest", function()
    local ok, msg = H.turbo().run("squad_role", { role = 4 })
    H.eq(ok, true, msg); H.has(msg, "26 players")
    for pid, addr in pairs(W.role_entries) do H.eq(sim:rb(addr), 4, "role of " .. pid) end
    local lines = table.concat(require('imports/turbo/core/log').lines or {}, "\n")
    H.has(lines, "role byte outside 0..5"); H.has(lines, tostring(W.USER_PLAYERS[5]))
end)

H.case("more than a tenth of the squad with bad role bytes: the vector is rejected", function()
    sim = H.setup({ in_cm = true })
    local bad = {}
    for i = 1, 8 do bad[W.USER_PLAYERS[i]] = 9 end
    W.build(sim, { role_bad_byte = bad })
    local ok, msg = H.turbo().run("squad_role", { role = 4 })
    H.eq(ok, false); H.has(msg, "no role list matching your squad")
end)


-- FC 27 PlayerStatusManager: players without an entry (the match crash of 2026-10-05: Find(pid) returned null)
local function status_entries(s)
    local out, count = {}, s:r32(W.psm + 0x14)
    for i = 0, 51 do
        local e = W.status_begin + i * 8
        out[#out + 1] = { pid = s:r32(e), role = s:r8(e + 4), promised = s:r8(e + 5), dismissed = s:r8(e + 6) }
    end
    return out, count
end

sim = H.setup({ in_cm = true })
W.build(sim, { fc27_status = true, status_missing = { 1003, 1010, 1020 }, status_extra = { 7001 } })

H.case("crash guard: adds an entry for each club player without one, entry before count", function()
    local role = require 'imports/turbo/features/squad_role'
    local added, text, left = role.repair_entries(function(pid) return pid == 1010 and 5 or nil end, false)
    H.eq(added, 3, text); H.eq(left, 0, "none left")
    H.has(text, "1003 (role 3)"); H.has(text, "1010 (role 5)"); H.has(text, "1020 (role 3)")
    local e, count = status_entries(sim)
    H.eq(count, 27, "1 stale + 23 + 3 added")
    local seen = {}
    for i = 1, count do
        H.eq(seen[e[i].pid], nil, "no duplicate " .. tostring(e[i].pid))
        seen[e[i].pid] = true
    end
    for _, p in ipairs(W.USER_PLAYERS) do H.eq(seen[p], true, "entry for " .. p) end
    H.eq(e[25].pid, 1003); H.eq(e[25].role, 3); H.eq(e[25].promised, 0); H.eq(e[25].dismissed, 0)
    H.eq(e[26].role, 5, "role_of used")
    H.eq(e[28].pid, -1, "slots after the count stay empty"); H.eq(e[28].role, 0xFF)
    H.eq(e[1].pid, 7001, "stale entry left alone"); H.eq(e[1].promised, 1, "flags of existing entries untouched")
end)

H.case("crash guard: nothing to add the second time", function()
    local role = require 'imports/turbo/features/squad_role'
    local added, text = role.repair_entries(nil, false)
    H.eq(added, 0, text); H.has(text, "have a squad status entry")
end)

H.case("crash guard: the bridge repair runs on the career events and logs it", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1005 } })
    local bridge = require 'imports/turbo/bridge'
    local added, text = bridge.repair_squad_status()
    H.eq(added, 1, text); H.has(text, "1005")
    local _, count = status_entries(sim)
    H.eq(count, 26, "complete")
end)

H.case("crash guard: off while turbo_output\\squad_status_repair_off.txt exists (kill switch), on again once it is deleted", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1005 } })
    local f = assert(io.open(H.out("squad_status_repair_off.txt"), "wb"))
    f:write("off")
    f:close()
    local bridge = require 'imports/turbo/bridge'
    local events = require 'imports/turbo/core/events'
    local added, text = bridge.repair_squad_status()
    H.eq(added, nil, text); H.eq(text, "off")
    local _, count = status_entries(sim)
    H.eq(count, 25, "nothing added while the file exists")
    -- the career-event path goes through the same check
    sim:fire("post__CareerModeEvent", 0, events.resolve("DAY_PASSED"), 0)
    _, count = status_entries(sim)
    H.eq(count, 25, "nothing added on a career event either")
    os.remove(H.out("squad_status_repair_off.txt"))
    added, text = bridge.repair_squad_status()
    H.eq(added, 1, text); H.has(text, "1005")
    _, count = status_entries(sim)
    H.eq(count, 26, "complete once the file is gone")
end)

H.case("squad roles mass action reaches players that had no entry", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1002, 1004 } })
    local ok, msg = H.turbo().run("squad_role", { role = 2 })
    H.eq(ok, true, msg)
    H.has(msg, "26 of 26 players have the role now in memory (24 updated, 2 entries created, 0 unchanged)")
    H.has(msg, "entry created (2): Player 1002 (1002) Important, Player 1004 (1004) Important")
    local e, count = status_entries(sim)
    H.eq(count, 26)
    for i = 1, count do H.eq(e[i].role, 2, "role of " .. e[i].pid) end
end)

H.case("crash guard: refuses a table that is not your club's or not the FC 27 layout, writes nothing", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1002 }, status_team = 77 })
    local role = require 'imports/turbo/features/squad_role'
    local added, text = role.repair_entries(nil, false)
    H.eq(added, nil); H.has(text, "not set up for your club")
    local _, count = status_entries(sim)
    H.eq(count, 25, "untouched")
    sim:w32(W.psm + 0x10, W.USER_TEAM)
    sim:w64(W.psm + 0x20, W.status_begin + 0x100)
    added, text = role.repair_entries(nil, false)
    H.eq(added, nil); H.has(text, "FC 27 layout")
end)

H.case("crash guard: a full table makes room from entries of players who left the club", function()
    sim = H.setup({ in_cm = true })
    local stale = {}
    for i = 1, 30 do stale[i] = 8000 + i end
    W.build(sim, { fc27_status = true, status_missing = { 1001, 1002, 1003, 1004, 1005 }, status_extra = stale })
    local role = require 'imports/turbo/features/squad_role'
    local added, text, left = role.repair_entries(nil, false)
    H.eq(added, 5, text); H.eq(left, 0); H.has(text, "4 entries of players no longer at your club removed")
    local e, count = status_entries(sim)
    H.eq(count, 52)
    local seen = {}
    for i = 1, count do
        H.eq(seen[e[i].pid], nil, "no duplicate " .. tostring(e[i].pid))
        seen[e[i].pid] = true
    end
    for _, p in ipairs(W.USER_PLAYERS) do H.eq(seen[p], true, "entry for " .. p) end
    H.eq(seen[8001], nil, "first stale entries went"); H.eq(seen[8005], true, "the rest stay")
    H.eq(e[1].pid, 8005); H.eq(e[1].promised, 1, "moved entries keep their flags")
end)

H.case("no unmapped memory reads (FC 27 status table)", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

-- ---------------------------------------------------------------- the game's own calls (Turbo.dll squad_role, op 14)
-- W.fake_squad_role (world.lua): a TurboSquadRole over the simulated table that behaves like turbogui/src/core/squad_role.cpp
local fake_native = W.fake_squad_role

local function with_native(f, fn)
    _G.TurboSquadRole = f
    local ok, err = pcall(fn)
    _G.TurboSquadRole = nil
    if not ok then error(err, 0) end
end

H.case("game call: the crash guard adds the missing entries through the game's AddPlayer, the game's role after the load event", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1003, 1010 } })
    local f, calls = fake_native(sim, { game_role = 4 })
    with_native(f, function()
        local bridge = require 'imports/turbo/bridge'
        local events = require 'imports/turbo/core/events'
        local added, text = bridge.repair_squad_status(events.resolve("DAY_PASSED"))
        H.eq(added, 2, text); H.has(text, "through the game's AddPlayer"); H.has(text, "1003 (role 4, the game's choice)")
        for _, c in ipairs(calls) do H.eq(c[1], 2, "code 2: the game picks the role") end
        local e, count = status_entries(sim)
        H.eq(count, 26)
        H.eq(e[25].pid, 1003); H.eq(e[25].role, 4); H.eq(e[25].promised, 0)
        -- at the load event the role is Turbo's (by age), code 1
        sim = H.setup({ in_cm = true })
        W.build(sim, { fc27_status = true, status_missing = { 1005 } })
        local f2, calls2 = fake_native(sim)
        _G.TurboSquadRole = f2
        added, text = bridge.repair_squad_status(events.resolve("POST_LOAD_PREPARE"))
        H.eq(added, 1, text); H.eq(calls2[1][1], 1, "code 1 at the load event"); H.eq(calls2[1][3], 3, "Rotation by age")
    end)
end)

H.case("game call: a full table makes room through the game's Remove (never a player lent out), then the entries are added", function()
    sim = H.setup({ in_cm = true })
    local stale = {}
    for i = 1, 30 do stale[i] = 8000 + i end
    W.build(sim, { fc27_status = true, status_missing = { 1001, 1002, 1003, 1004, 1005 }, status_extra = stale, playerloans = true })
    -- 8001 is out on loan from your club: his entry stays
    H.ok(_G.InsertDBTableRow("playerloans", { playerid = 8001, teamidloanedfrom = W.USER_TEAM, loandateend = 0 }), "loan row added")
    local f, calls = fake_native(sim)
    with_native(f, function()
        local role = require 'imports/turbo/features/squad_role'
        local added, text, left = role.repair_entries(function() return 2 end, false)
        H.eq(added, 5, text); H.eq(left, 0); H.has(text, "4 entries of players no longer at your club removed to make room, the game's Remove")
        local removes = {}
        for _, c in ipairs(calls) do if c[1] == 4 then removes[#removes + 1] = c[2] end end
        H.eq(#removes, 4, "only the excess: 30 + 21 + 5 = 56")
        H.eq(table.concat(removes, ","), "8002,8003,8004,8005", "in table order, by pid (the entries move down after each Remove)")
        for _, p in ipairs(removes) do H.ok(p ~= 8001, "the player lent out keeps his entry") end
        local e, count = status_entries(sim)
        H.eq(count, 52)
        local seen = {}
        for i = 1, count do
            H.eq(seen[e[i].pid], nil, "no duplicate " .. tostring(e[i].pid))
            seen[e[i].pid] = true
        end
        for _, p in ipairs(W.USER_PLAYERS) do H.eq(seen[p], true, "entry for " .. p) end
        H.eq(seen[8001], true, "lent out: kept")
    end)
end)

H.case("game call off: the memory path takes over, the text says why", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1004 } })
    local f, calls = fake_native(sim, { off = true })
    with_native(f, function()
        local ok, msg = H.turbo().run("squad_role", { role = 2 })
        H.eq(ok, true, msg)
        H.has(msg, "the game call is off, in memory instead: squad_role: off (kill switch")
        H.has(msg, "26 of 26 players have the role now in memory")
        local e, count = status_entries(sim)
        H.eq(count, 26)
        for i = 1, count do H.eq(e[i].role, 2, "role of " .. e[i].pid) end
        H.ok(#calls >= 1, "the call was tried first")
    end)
end)

H.case("game call: one player (Players editor), his role read back; another club's player is refused", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1007 } })
    local f, calls = fake_native(sim)
    with_native(f, function()
        local ok, msg = H.turbo().run("squad_role", { playerid = 1012, role = 1 })
        H.eq(ok, true, msg); H.has(msg, "1 of 1 players have the role now through the game"); H.has(msg, "updated (1): Player 1012 (1012) Crucial")
        local set = 0
        for _, c in ipairs(calls) do
            if c[1] == 1 then
                set = set + 1
                H.eq(c[2], 1012, "only him")
            end
        end
        H.eq(set, 1)
        H.eq(sim:r8(W.role_entries[1012]), 1)
        ok, msg = H.turbo().run("squad_role", { playerid = 1012, mode = "check" })
        H.eq(ok, true, msg); H.has(msg, "Player 1012 (1012): squad role Crucial (read from the game)")
        ok, msg = H.turbo().run("squad_role", { playerid = 2001, role = 1 })
        H.eq(ok, false, msg); H.has(msg, "skipped (1): Player 2001 (2001): not in your club")
        -- the missing entry of 1007 was added on the way (the crash guard's rule: every club player gets one)
        local _, count = status_entries(sim)
        H.eq(count, 26, "1007 got his entry")
    end)
end)

H.case("game call: dry run calls nothing", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1008 } })
    H.write_config({ turbo = { dry_run = true } })
    local f, calls = fake_native(sim)
    with_native(f, function()
        local ok, msg = H.turbo().run("squad_role", { role = 5 })
        H.eq(ok, true, msg); H.has(msg, "[DRY RUN]"); H.has(msg, "26 of 26 players would get the role (1 of them need an entry first)")
        H.eq(#calls, 0, "no game call in a dry run")
        local _, count = status_entries(sim)
        H.eq(count, 25, "nothing added")
    end)
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("auto (opt-in): the roles go back after SEASON_RESET only; switching it on changes nothing", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true })
    local role = require 'imports/turbo/features/squad_role'
    local events = require 'imports/turbo/core/events'
    local f, calls = fake_native(sim)
    with_native(f, function()
        local ctx = { cfg = { enabled = true, rule = "role", rule_role = 4, include_loaned_in = true, use_memory = true }, dry = false, out_dir = H.out("") }
        role.auto(ctx, -1)
        H.eq(#calls, 0, "nothing when switched on")
        role.auto(ctx, events.resolve("SEASON_RESET"))
        local e, count = status_entries(sim)
        for i = 1, count do H.eq(e[i].role, 4, "rule_role after the reset: " .. e[i].pid) end
        ctx.cfg.rule = "age"
        role.auto(ctx, events.resolve("SEASON_RESET"))
        e, count = status_entries(sim)
        for i = 1, count do H.ok(e[i].role == 3 or e[i].role == 5, "by age after the reset: " .. e[i].pid) end
    end)
    local cfg = require('imports/turbo/core/config').DEFAULTS
    H.eq(cfg.auto.squad_role.enabled, false, "opt-in")
    H.eq(cfg.auto.squad_role.events[1], "SEASON_RESET")
    H.eq(H.turbo().MODULES.squad_role.kind, "both", "an auto feature")
end)

H.case("bridge.squad_role: argument checks, the words, off for a Turbo.dll without op 14", function()
    local bridge = require 'imports/turbo/bridge'
    local ok, text, status = bridge.squad_role(5, 1, 3)
    H.eq(ok, false); H.has(text, "unknown squad_role code 5")
    ok, text = bridge.squad_role(1, 0, 3)
    H.eq(ok, false); H.has(text, "positive")
    ok, text = bridge.squad_role(1, 5, 6)
    H.eq(ok, false); H.has(text, "out of range")
    local seen
    local real = bridge.game_call
    bridge.game_call = function(op, args) seen = { op, args }; return "ok", "player 5: squad role entry created with Rotation (he had none)", 3, 1 end
    local out0, out1
    ok, text, status, out0, out1 = bridge.squad_role(1, 5, 3)
    H.eq(ok, true); H.eq(status, "ok"); H.eq(out0, 3); H.eq(out1, 1)
    H.eq(seen[1], 14); H.eq(seen[2][2], 1); H.eq(seen[2][3], 5); H.eq(seen[2][4], 3)
    bridge.squad_role(9, 5, 4)
    H.eq(seen[2][4], 0, "the role goes only with code 1")
    bridge.game_call = function() return "failed", "unknown game call op 14" end
    ok, text, status = bridge.squad_role(1, 5, 3)
    H.eq(ok, false); H.eq(status, "off", "an older Turbo.dll: off, the memory path is used")
    bridge.game_call = function() return "unavailable", "no mailbox" end
    ok, text, status = bridge.squad_role(2, 5, 0)
    H.eq(status, "off"); H.has(text, "squad_role: off")
    bridge.game_call = real
end)

H.case("no unmapped memory reads (game call cases)", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
