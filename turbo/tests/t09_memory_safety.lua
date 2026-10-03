-- luacheck: globals package
package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t09 memory safety: Turbo reads game memory only where the Turbo GUI's readable-memory map allows it")

-- In FC 27 a read of an unreadable address through Live Editor's natives kills the game (02-10-2026: 0x3600000028).
-- The simulator counts such reads (it returns 0 instead of crashing); every case here must end with zero.

H.case("pointers into unmapped memory (like 0x3600000028) are never followed: probe, squad role, exports", function()
    local sim = H.setup({ in_cm = true })
    W.build(sim, { career_playercontract = true, role_vec_off = false })  -- no real role list: the search scans everything
    -- poison the managers Turbo searches: plausible-looking pointers into memory that is not mapped
    local psm = require('imports/turbo/core/mem').manager(87)
    H.ok(psm, "PlayerStatusManager found through the map")
    -- {begin, end} pairs that look like vectors of 26 entries (a squad's worth) at unmapped addresses
    for off = 0x40, 0x1F0, 16 do
        sim:w64(psm + off, 0x3600000000 + off * 0x100)
        sim:w64(psm + off + 8, 0x3600000000 + off * 0x100 + 26 * 8)
    end
    local tm = require('imports/turbo/core/mem').manager(127)
    if tm then for off = 0x10, 0x200, 8 do if sim:r64(tm + off) == 0 then sim:w64(tm + off, 0x3600000028) end end end
    sim.unmapped_reads = 0
    for _, m in ipairs({ "probe", "squad_role", "export_fixtures", "export_transfer_history" }) do
        local ok, msg = pcall(H.turbo().run, m, m == "squad_role" and { role = 3 } or nil, { silent = true, dry = true })
        H.ok(ok, m .. " raised: " .. tostring(msg))
    end
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.case("without the Turbo GUI (no map) memory-based tools stop with a clear reason and read nothing", function()
    local sim = H.setup({ in_cm = true, no_gui = true })
    W.build(sim, { career_playercontract = true })
    sim.unmapped_reads = 0
    local reads = 0
    local real = ReadPointer
    ReadPointer = function(...) reads = reads + 1; return real(...) end
    local ok, msg = H.turbo().run("export_fixtures", nil, { silent = true })
    H.eq(ok, false, "fixtures refused")
    H.has(msg, "Turbo GUI is not running")
    ok, msg = H.turbo().run("export_transfer_history", nil, { silent = true })
    H.eq(ok, false); H.has(msg, "Turbo GUI is not running")
    ok, msg = H.turbo().run("squad_role", { role = 3, use_memory = true }, { silent = true, dry = true })
    H.has(tostring(msg), "Turbo GUI is not running")
    ReadPointer = real
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
    -- the database tools need no map (Live Editor's own table walk)
    ok, msg = H.turbo().run("team_jersey_numbers", { teamid = 1 }, { silent = true })
    H.eq(ok, true, msg)
    -- the user's club still comes from Live Editor's documented GetUserTeamID
    H.eq(require('imports/turbo/core/game').user_team_id(), W.USER_TEAM, "user team without the map")
end)

H.case("the map reader: region edges, a map being rewritten, a stale cache", function()
    local sim = H.setup({ in_cm = false })
    local mem = require 'imports/turbo/core/mem'
    local a = sim:alloc(0x3000, 0x1000)
    H.ok(mem.readable(a, 8), "inside")
    H.ok(mem.readable(a + 0x2FF8, 8), "last bytes of the block's pages")
    H.ok(not mem.readable(0x3600000028, 4), "far outside")
    H.ok(not mem.readable(0x8, 4) and not mem.readable(-5, 4), "null page, negative")
    local map = sim.gui.map
    local seq = sim:r32(map + 8)
    sim:w32(map + 8, seq + 1)   -- the DLL is rewriting it right now
    H.ok(not mem.readable(a + 0x100, 4), "odd sequence: refused, not guessed")
    sim:w32(map + 8, seq + 2)
    H.ok(mem.readable(a + 0x100, 4), "complete again")
    -- memory released and the map republished: the cached region must not be trusted any more
    local page = a >> 12
    sim.pages[page] = nil
    sim:publish_map()
    H.ok(not mem.readable(a, 4), "released page refused after republish")
end)

H.case("in-game date from the CalendarManager through the map (FC 26 offset, a moved one, none, no map)", function()
    local sim = H.setup({ in_cm = true })
    W.build(sim, {})
    local game = require 'imports/turbo/core/game'
    local mem = require 'imports/turbo/core/mem'
    local d = game._calendar_date()
    H.ok(d and d.day == 15 and d.month == 1 and d.year == 2027 and d.offset == 0x34, "FC 26 layout")
    local cal = mem.manager(24)
    for _, o in ipairs({ 0x34, 0x38, 0x3C }) do sim:w32(cal + o, 0) end
    sim:w32(cal + 0x50, 16); sim:w32(cal + 0x54, 1); sim:w32(cal + 0x58, 2027)
    d = game._calendar_date()
    H.ok(d and d.day == 16 and d.offset == 0x50, "moved: found by its shape, offset reported")
    sim:w32(cal + 0x58, 1999)
    H.eq(game._calendar_date(), nil, "no plausible date: nil, not garbage")
    local sim2 = H.setup({ in_cm = true, no_gui = true })
    W.build(sim2, {})
    sim2.unmapped_reads = 0
    H.eq(require('imports/turbo/core/game')._calendar_date(), nil, "no map: nothing read")
    H.eq(sim2.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
