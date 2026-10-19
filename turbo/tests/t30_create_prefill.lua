package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t30 create player from a database player (2.19 create-prefill): the copy covers every players column and the names; only 'set' changes")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
-- every kind of players column the copy must carry: the 34 attributes (development), name ids, positions 2-4, appearance codes
W.build(sim, { playerloans = true, development = true, player_fields = {
    { name = "firstnameid", depth = 17 }, { name = "lastnameid", depth = 17 }, { name = "commonnameid", depth = 17 },
    { name = "playerjerseynameid", depth = 17 }, { name = "preferredposition2", depth = 5, min = -1 },
    { name = "preferredposition3", depth = 5, min = -1 }, { name = "preferredposition4", depth = 5, min = -1 },
    { name = "skillmoves", depth = 3 }, { name = "weakfootabilitytypecode", depth = 3, min = 1 }, { name = "attackingworkrate", depth = 2 },
    { name = "haircolorcode", depth = 4 }, { name = "hairtypecode", depth = 14 }, { name = "headtypecode", depth = 14 },
    { name = "headvariation", depth = 5 }, { name = "skintonecode", depth = 7 }, { name = "eyecolorcode", depth = 7 },
    { name = "facialhairtypecode", depth = 10 }, { name = "shoetypecode", depth = 12 }, { name = "bodytypecode", depth = 9, min = 1 },
    { name = "gender", depth = 1 }, { name = "wage", depth = 20 } } })

local db = require 'imports/turbo/core/db'
local DATE = require 'imports/core/date'

local SRC = 2005
local function run(overrides) return H.turbo().run("create_player", overrides) end
local function rec_of(pid) return sim:find_row("players", "playerid", pid) end
local function pval(pid, field)
    local rec = rec_of(pid)
    return rec and sim:value("players", rec, field) or nil
end
local function edited(pid)
    local rec = sim:find_row("editedplayernames", "playerid", pid)
    if not rec then return nil end
    local out = {}
    for _, f in ipairs({ "firstname", "surname", "commonname", "playerjerseyname" }) do out[f] = sim:value("editedplayernames", rec, f) end
    return out
end
local function new_pid(msg) return tonumber(tostring(msg):match("new player (%d+)")) end
local function columns()
    local out = {}
    for name, c in pairs(sim.tables.players) do
        if type(c) == "table" and c.depth and c.bitoff then out[#out + 1] = name end
    end
    table.sort(out)
    return out
end
local function gdays(y, m, d)
    local x = DATE:new()
    x.year, x.month, x.day = y, m, d
    return x:ToGregorianDays()
end

-- the source gets a distinct value in every integer column (never the field's minimum, so a column the copy missed shows)
local COLS = columns()
local SKIP = { playerid = true, birthdate = true, playerjointeamdate = true, contractvaliduntil = true }
do
    local rec = rec_of(SRC)
    for i, f in ipairs(COLS) do
        local c = sim.tables.players[f]
        if not SKIP[f] and c.typ ~= 0 and c.typ ~= 13 and c.typ ~= 4 then
            local span = (1 << math.min(c.depth, 20)) - 1
            sim:set_field(rec, c, c.min + 1 + (i * 7) % math.max(1, span - 1))
        end
    end
    sim:set_field(rec, sim.tables.players.birthdate, gdays(1993, 11, 23))
end

H.case("copy without 'set': every players column equals the source (playerid, join date and an expired contract excepted)", function()
    H.ok(#COLS >= 60, "the world has the attribute, name id, position and appearance columns: " .. #COLS)
    local ok, msg = run({ source = { playerid = SRC }, teamid = 3, set = {} })
    H.eq(ok, true, msg)
    local pid = new_pid(msg)
    H.ok(pid and pid ~= SRC, "a new id: " .. tostring(pid))
    local differ = {}
    for _, f in ipairs(COLS) do
        if f ~= "playerid" and f ~= "playerjointeamdate" and pval(pid, f) ~= pval(SRC, f) then
            differ[#differ + 1] = string.format("%s %s ~= %s", f, tostring(pval(pid, f)), tostring(pval(SRC, f)))
        end
    end
    H.eq(table.concat(differ, "; "), "", "no column differs from the source")
    H.eq(pval(pid, "birthdate"), gdays(1993, 11, 23), "the exact birth date (day precision)")
    H.ok(pval(pid, "playerjointeamdate") ~= pval(SRC, "playerjointeamdate"), "he joins his club today (the only players column not copied)")
    H.eq(edited(pid), nil, "a source without a name row: no name row; his name ids are copied")
    -- the read-back (copy check) in the summary: every column but his own (id, join date, contract) equals the source's
    local n = #COLS - 3
    H.has(msg, string.format("copy check: %d of %d columns equal player %d, 0 of 0 set field(s) as written, names copied", n, n, SRC))
    H.ok(not msg:find("DIFFER", 1, true), "nothing differs: " .. msg)
end)

H.case("names: a source's editedplayernames row is copied exactly; names given replace it; clear_names removes the common name", function()
    local ok, msg = run({ source = { playerid = SRC }, teamid = 4,
        names = { firstname = "Ann", surname = "Bee", commonname = "Cee", playerjerseyname = "DEE" } })
    H.eq(ok, true, msg)
    local a = new_pid(msg)
    H.eq(edited(a).playerjerseyname, "DEE")
    ok, msg = run({ source = { playerid = a }, teamid = 4 })
    H.eq(ok, true, msg)
    local b = new_pid(msg)
    local ea, eb = edited(a), edited(b)
    for _, f in ipairs({ "firstname", "surname", "commonname", "playerjerseyname" }) do H.eq(eb[f], ea[f], "name row copied: " .. f) end
    for _, f in ipairs({ "firstnameid", "lastnameid", "commonnameid", "playerjerseynameid" }) do H.eq(pval(b, f), pval(a, f), "name id copied: " .. f) end
    -- the whole set of shown names when one changed (the GUI sends all four): the shirt does not follow the new surname
    ok, msg = run({ source = { playerid = a }, teamid = 4,
        names = { firstname = "Ann", surname = "Zed", commonname = "Cee", playerjerseyname = "DEE" } })
    H.eq(ok, true, msg)
    local c = edited(new_pid(msg))
    H.eq(c.surname, "Zed"); H.eq(c.playerjerseyname, "DEE", "the shirt name as sent")
    -- an emptied common name: removed from the row and its name id cleared
    ok, msg = run({ source = { playerid = a }, teamid = 4, names = { firstname = "Ann", surname = "Bee", playerjerseyname = "DEE" },
        clear_names = { "commonname" } })
    H.eq(ok, true, msg)
    local d = new_pid(msg)
    H.eq(edited(d).commonname, "", "common name removed"); H.eq(edited(d).firstname, "Ann")
    H.eq(pval(d, "commonnameid"), 0, "common name id cleared")
    ok, msg = run({ source = { playerid = a }, teamid = 4, clear_names = { "surname" } })
    H.eq(ok, false); H.has(msg, "only the common name can be cleared")
end)

H.case("'set' with one field: only that column differs from the source", function()
    local want = pval(SRC, "potential") == 90 and 91 or 90
    local ok, msg = run({ source = { playerid = SRC }, teamid = 5, set = { potential = want } })
    H.eq(ok, true, msg)
    local pid = new_pid(msg)
    local differ = {}
    for _, f in ipairs(COLS) do
        if f ~= "playerid" and f ~= "playerjointeamdate" and pval(pid, f) ~= pval(SRC, f) then differ[#differ + 1] = f end
    end
    H.eq(table.concat(differ, ","), "potential", "only potential differs")
    H.eq(pval(pid, "potential"), want)
    H.has(msg, "1 of 1 set field(s) as written"); H.ok(not msg:find("DIFFER", 1, true), msg)
end)

H.case("the game's own way (TurboPlayerCreate): the payload carries every integer players column of the source", function()
    local got
    _G.TurboPlayerCreate = function(_, payload)
        got = payload
        return true, "created", "ok", 1, true
    end
    local ok, msg = run({ source = { playerid = SRC }, teamid = 3 })
    _G.TurboPlayerCreate = nil
    H.eq(ok, true, msg)
    H.ok(got ~= nil, "called")
    local players = db.get_table("players")
    local missing = {}
    for _, f in ipairs(db.field_names(players)) do
        local info = db.field_info(players, f)
        if info.type == "int" and got.players[f] == nil then missing[#missing + 1] = f end
        if f ~= "playerid" and f ~= "playerjointeamdate" and info.type == "int" and got.players[f] ~= nil and got.players[f] ~= pval(SRC, f) then
            missing[#missing + 1] = f .. "(value)"
        end
    end
    H.eq(table.concat(missing, ","), "", "every integer column, with the source's value")
end)

H.finish()
