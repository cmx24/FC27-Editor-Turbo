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

H.finish()
