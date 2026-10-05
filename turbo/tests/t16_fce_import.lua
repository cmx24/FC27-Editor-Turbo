package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t16 FC Editor import: a preset made by tools/fce_import/fce_to_preset.py loads, maps cleanly and creates a player")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
local preset = require 'imports/turbo/core/preset'
local util = require 'imports/turbo/core/util'

-- the players columns of the real FC 27 schema that the fixture uses (name, bit depth, minimum) go into the mock table
local here = (debug.getinfo(1, "S").source:sub(2):gsub("\\", "/"):match("(.*/)") or "./")
local schema = preset.json().decode(H.read(here .. "../le27/fc27_db_schema.json"))
local fixture_doc = preset.json().decode(H.read(here .. "fixtures/fce_Test_Fixture_900123.json"))
local real = {}
for _, f in ipairs(schema.tables.players.fields) do real[f.name] = f end
local player_fields, not_in_game = {}, {}
for name in pairs(fixture_doc.players) do
    local f = real[name]
    if f then player_fields[#player_fields + 1] = { name = name, depth = f.depth, min = f.min } else not_in_game[#not_in_game + 1] = name end
end
W.build(sim, { playerloans = true, player_fields = player_fields })

-- fixture = an FC Editor export (FC 26) converted with --teamid 3; name and id replaced by fictitious ones
local fixture = (debug.getinfo(1, "S").source:sub(2):gsub("\\", "/"):match("(.*/)") or "./") .. "fixtures/fce_Test_Fixture_900123.json"
local path = H.out("players/Test_Fixture_900123.json")
os.execute(string.format("mkdir -p '%s'", H.out("players")))

local function pval(pid, field)
    local rec = sim:find_row("players", "playerid", pid)
    return rec and sim:value("players", rec, field) or nil
end

H.case("every column of the converted preset exists in the FC 27 schema (real column list, not the mock's)", function()
    table.sort(not_in_game)
    H.eq(#not_in_game, 0, table.concat(not_in_game, ", "))
end)

H.case("the converted preset is a Turbo player file with names, FC 27 skin tone and the original id", function()
    H.ok(util.write_file(path, H.read(fixture)), "fixture copied into turbo_output\\players")
    local parsed, err = preset.load(path)
    H.ok(parsed, err)
    H.eq(parsed.kind, "turbo_json")
    local row = preset.pick_row(parsed, {})
    H.eq(row.commonname, "Test Fixture"); H.eq(row.surname, "Fixture")
    H.eq(tonumber(row.skintonecode), 20, "FC 26 skin tone 2 became 20")
    H.eq(tonumber(row.playerid), 900123)
end)

H.case("every field maps onto this database: nothing skipped, nothing unknown, no name ids", function()
    local players = require('imports/turbo/core/db').get_table("players")
    local parsed = preset.load(path)
    local row = preset.pick_row(parsed, {})
    local fields, names, skipped, unknown = preset.map_row(players, row, {})
    H.eq(#skipped, 0, table.concat(skipped, "; "))
    H.eq(#unknown, 0, table.concat(unknown, ", "))
    H.eq(names.commonname, "Test Fixture")
    H.eq(fields.firstnameid, nil, "name ids never come from a file")
end)

H.case("create_player builds the new player from it at a club, with names and a fresh id", function()
    local ok, msg = H.turbo().run("create_player", { source = { file = path }, teamid = 4 })
    H.eq(ok, true, msg)
    local pid = tonumber(msg:match("new player (%d+) Test Fixture"))
    H.ok(pid, "message names the new player: " .. tostring(msg))
    H.ok(pid ~= 900123, "a free id, not the source id")
    H.eq(pval(pid, "skintonecode"), 20)
    H.eq(sim:find_row("editedplayernames", "playerid", pid) ~= nil, true, "names row")
end)

H.case("CMTracker preset: real face when the game has that head, else a generic head of the new id", function()
    local json = preset.json()
    local function make(name, real_id)
        local doc = { format = "turbo-player-preset", version = 1, name = name, playerid = 900001,
            names = { firstname = "Test", surname = name, commonname = "", playerjerseyname = name },
            players = { overallrating = 70, potential = 75, headclasscode = 1, preferredposition1 = 25 },
            links = {}, miniface = nil, cmtracker = { playerid = 900001, real_face_id = real_id } }
        local f = H.out("players/" .. name .. "_cmt.json")
        H.ok(util.write_file(f, json.encode(doc)), "preset written")
        return f
    end
    local ok, msg = H.turbo().run("create_player", { source = { file = make("Realface", 2005) }, teamid = 4 })
    H.eq(ok, true, msg)
    H.has(msg, "real face of player 2005")
    local pid = tonumber(msg:match("new player (%d+)"))
    H.eq(pval(pid, "headassetid"), 2005); H.eq(pval(pid, "headclasscode"), 0)
    ok, msg = H.turbo().run("create_player", { source = { file = make("Genericface", 999999) }, teamid = 4 })
    H.eq(ok, true, msg)
    H.has(msg, "generic head")
    pid = tonumber(msg:match("new player (%d+)"))
    H.eq(pval(pid, "headassetid"), pid); H.eq(pval(pid, "headclasscode"), 1)
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
