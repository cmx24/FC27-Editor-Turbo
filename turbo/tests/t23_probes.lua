package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t23 phase 0 probes: probe_tactics (tables, cm_teamsheets row, role list with ages) and bodytypes are read-only and write their JSON")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, sheet_gap = 8, role_vec_52 = true,
    player_fields = { { name = "bodytypecode", depth = 4 }, { name = "gender", depth = 1 } } })
local json = require 'imports/external/json'
local util = require 'imports/turbo/core/util'

local function out_file(name) return require('imports/turbo/core/env').output_dir() .. "/" .. name end
local function read_json(name)
    local f = io.open(out_file(name), "rb")
    if not f then return nil end
    local t = f:read("a")
    f:close()
    return json.decode(t)
end
local function pset(pid, field, v)
    local rec = sim:find_row("players", "playerid", pid)
    sim:set_field(rec, sim.tables.players[field], v)
end

H.case("probe_tactics: finds the tactic-like tables, the sheet gap and the role list with ages, and writes nothing else", function()
    pset(W.USER_PLAYERS[3], "birthdate", util.gregorian_days_from_date(1990, 3, 10))
    local snapshot = {}
    for k, v in pairs(sim.mem) do snapshot[k] = v end
    local ok, msg = H.turbo().run("probe_tactics")
    H.eq(ok, true, msg); H.has(msg, "cm_teamsheets: 25 filled slots, 1 empty before the last")
    H.has(msg, "role list: 26 players")
    local changed = 0
    for k, v in pairs(sim.mem) do if snapshot[k] ~= v then changed = changed + 1 end end
    H.eq(changed, 0, "no memory byte changed")
    local rep = read_json("probe_tactics.json")
    H.eq(rep ~= nil, true, "json written")
    local names = {}
    for _, t in ipairs(rep.tables.tables) do names[t.name] = t end
    H.eq(names.cm_teamsheets ~= nil, true, "teamsheet table"); H.eq(names.formations ~= nil, true, "formation table")
    H.eq(names.players, nil, "unrelated table not listed")
    H.eq(rep.sheet.gap_slots[1], 8); H.eq(rep.sheet.filled, 25)
    H.eq(rep.roles.members, 26); H.eq(#rep.roles.entries, 26)
    H.eq(#rep.roles.links_without_entry, 0)
    local e3
    for _, e in ipairs(rep.roles.entries) do if e.playerid == W.USER_PLAYERS[3] then e3 = e end end
    H.eq(e3.age, 36, "age from the birthdate on the simulated date"); H.eq(e3.on_links, true)
    local gap_player
    for _, e in ipairs(rep.roles.entries) do if e.playerid == W.USER_PLAYERS[9] then gap_player = e end end
    H.eq(gap_player.on_sheet_slot, nil, "the player of the empty slot is on no sheet slot but is in the list")
    H.eq(util.file_exists(out_file("probe_tactics.txt")), true, "text copy")
    H.eq(sim.unmapped_reads, 0)
end)

H.case("probe_tactics: custom patterns; outside a career only the tables", function()
    local ok, msg = H.turbo().run("probe_tactics", { patterns = { "playerloans" } })
    H.eq(ok, true, msg)
    local rep = read_json("probe_tactics.json")
    H.eq(#rep.tables.tables, 1); H.eq(rep.tables.tables[1].name, "playerloans")
    sim.in_cm = false
    ok, msg = H.turbo().run("probe_tactics")
    H.eq(ok, true, msg); H.has(msg, "no career save loaded")
    sim.in_cm = true
end)

H.case("bodytypes: one entry per code in use with counts, ranges, examples, and codes outside the named set", function()
    local n = 0
    for _, row in ipairs(sim:rows("players")) do
        local pid = sim:value("players", row, "playerid")
        if pid and pid < 900000 then
            n = n + 1
            local code = (pid % 7 == 0) and 12 or (1 + pid % 3)
            pset(pid, "bodytypecode", code)
            pset(pid, "gender", pid % 2)
        end
    end
    local ok, msg = H.turbo().run("bodytypes", { examples = 3 })
    H.eq(ok, true, msg); H.has(msg, "outside the named set (12)")
    local rep = read_json("bodytypes_fc27.json")
    H.eq(rep.players, n)
    local by, total = {}, 0
    for _, c in ipairs(rep.codes) do by[c.code] = c; total = total + c.players end
    H.eq(total, n, "every player in exactly one group")
    H.eq(by[12].in_generic_set, false); H.eq(by[1].in_generic_set, true)
    H.eq(rep.codes_outside_generic_set[1], 12)
    H.ok(#by[1].examples <= 3 and #by[1].examples >= 1, "examples capped")
    for i = 2, #by[1].examples do H.ok(by[1].examples[i - 1].overall >= by[1].examples[i].overall, "best overall first") end
    H.eq(by[1].height.min, 180); H.eq(by[1].height.max, 180); H.eq(by[1].weight.mean, 75)
    local g = 0
    for _, v in pairs(by[1].gender) do g = g + v end
    H.eq(g, by[1].players, "gender counts add up")
    H.eq(rep.field.type, "int")
end)

H.case("bodytypes: refuses a database without the field", function()
    local sim2 = H.setup({ in_cm = true, le_27_1_2 = true })
    W.build(sim2, {})
    local ok, msg = H.turbo().run("bodytypes")
    H.eq(ok, false); H.has(msg, "no bodytypecode")
end)

H.finish()
