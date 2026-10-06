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

H.case("squad roles mass action reaches players that had no entry", function()
    sim = H.setup({ in_cm = true })
    W.build(sim, { fc27_status = true, status_missing = { 1002, 1004 } })
    local ok, msg = H.turbo().run("squad_role", { role = 2 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entries added for 2 of 2")
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

H.finish()
