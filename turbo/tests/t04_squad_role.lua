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
    H.eq(ok, true, msg); H.has(msg, "PlayerStatusManager +0x18 (entry 8 bytes): 26 players")
    H.has(msg, "career_playercontract: not present")
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

H.case("finds a moved vector with 16-byte entries and skips loaned-in players", function()
    local ok, msg = H.turbo().run("squad_role", { role = 5 })
    H.eq(ok, true, msg)
    H.has(msg, "PlayerStatusManager +0x48 (entry 16 bytes): 25 players")
    H.has(msg, "career_playercontract: 25 rows")
    H.has(msg, "1 loaned-in players skipped")
    local r = roles(sim)
    H.eq(r[1002], 5, "set")
    H.eq(r[W.LOANED_IN], 3, "loaned-in untouched")
    local c = sim:find_row("career_playercontract", "playerid", 1002)
    H.eq(sim:value("career_playercontract", c, "playerrole"), 5, "contract role")
end)

H.case("include_loaned_in covers every squad player in memory", function()
    local ok, msg = H.turbo().run("squad_role", { role = 2, include_loaned_in = true })
    H.eq(ok, true, msg)
    H.eq(roles(sim)[W.LOANED_IN], 2, "loaned-in set")
end)

H.case("use_memory=false leaves memory alone and still updates the contract table", function()
    local before = roles(sim)[1002]
    local ok, msg = H.turbo().run("squad_role", { role = 4, use_memory = false })
    H.eq(ok, true, msg); H.has(msg, "memory pass disabled")
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
    H.eq(ok, true, msg); H.has(msg, "PlayerStatusManager +0x18 (entry 8 bytes): 26 players")
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
    H.has(msg, "25 players")
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

H.finish()
