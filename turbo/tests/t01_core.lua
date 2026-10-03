package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'

local sim = H.setup({ in_cm = true })
local W = require 'world'
W.build(sim, {})

print("t01 core: env, config, db, game")

H.case("LE libraries loaded and t3db reads simulated tables", function()
    H.ok(type(LE) == "table" and type(LE.db) == "table", "LE global")
    local t = LE.db:GetTable("teams")
    H.ok(t ~= nil, "teams table")
    H.eq(t.name, "teams", "table name")
    local rec = t:GetFirstRecord()
    H.eq(t:GetRecordFieldValue(rec, "teamid"), 1, "first teamid")
    H.eq(t:GetRecordFieldValue(rec, "teamname"), "Arsenal", "first teamname")
end)

H.case("env finds the LE root from the module path and the config file", function()
    local env = require 'imports/turbo/core/env'
    H.eq(env.le_root(), H.LE, "le root")
    H.eq(env.config_path(), H.LE .. "/turbo_config.json", "config path")
    H.eq(env.output_dir(""), H.LE .. "/turbo_output", "output dir")
end)

H.case("config loads shipped JSON and merges defaults", function()
    local config = require 'imports/turbo/core/config'
    local cfg, info = config.load()
    H.ok(info.loaded, "loaded: " .. tostring(info.error))
    H.eq(cfg.modules.extend_cpu_contracts.years, 5, "years")
    H.eq(cfg.auto.form_morale.enabled, false, "auto off")
    H.eq(#cfg.auto.form_morale.events, 3, "events")
end)

H.case("invalid JSON config stops runs with a clear error", function()
    local f = io.open(H.LE .. "/turbo_config.json", "rb"); local good = f:read("a"); f:close()
    f = io.open(H.LE .. "/turbo_config.json", "wb"); f:write("{ broken"); f:close()
    local TURBO = H.turbo()
    local ok, msg = TURBO.run("probe")
    H.eq(ok, false, "run refused")
    H.has(msg, "not valid JSON", "message")
    f = io.open(H.LE .. "/turbo_config.json", "wb"); f:write(good); f:close()
end)

H.case("wrong-typed sections fall back to defaults with a warning", function()
    local f = io.open(H.LE .. "/turbo_config.json", "rb"); local good = f:read("a"); f:close()
    f = io.open(H.LE .. "/turbo_config.json", "wb")
    f:write('{"turbo": true, "modules": {"bulk_edit": 5, "extend_cpu_contracts": {"years": 2}}}')
    f:close()
    local config = require 'imports/turbo/core/config'
    local cfg, info = config.load()
    H.ok(info.loaded, "loaded")
    H.has(info.error, "turbo, modules.bulk_edit")
    H.eq(cfg.turbo.dry_run, false, "turbo defaults")
    H.eq(type(cfg.modules.bulk_edit), "table", "bulk_edit defaults")
    H.eq(cfg.modules.extend_cpu_contracts.years, 2, "valid entry kept")
    local ok, msg = H.turbo().run("probe")
    H.eq(ok, true, msg)
    f = io.open(H.LE .. "/turbo_config.json", "wb"); f:write(good); f:close()
end)

H.case("BOM-prefixed config is accepted", function()
    local f = io.open(H.LE .. "/turbo_config.json", "rb"); local good = f:read("a"); f:close()
    f = io.open(H.LE .. "/turbo_config.json", "wb"); f:write("\239\187\191" .. good); f:close()
    local config = require 'imports/turbo/core/config'
    local _, info = config.load()
    H.ok(info.loaded, "loaded with BOM: " .. tostring(info.error))
    f = io.open(H.LE .. "/turbo_config.json", "wb"); f:write(good); f:close()
end)

H.case("db.validate enforces field ranges from metadata", function()
    local db = require 'imports/turbo/core/db'
    local p = db.get_table("players")
    H.eq(db.validate(p, "contractvaliduntil", 2047), 2047, "max ok")
    local v, err = db.validate(p, "contractvaliduntil", 2048)
    H.eq(v, nil, "over max refused")
    H.has(err, "outside the field range 0..2047")
    local v2, err2 = db.validate(p, "nosuchfield", 1)
    H.eq(v2, nil); H.has(err2, "no field nosuchfield")
    local t = db.get_table("teams")
    local v3, err3 = db.validate(t, "teamname", string.rep("x", 31))
    H.eq(v3, nil); H.has(err3, "at most 30 bytes")
    local f = db.get_table("formations")
    H.eq(db.validate(f, "offset1x", 1), 1.0, "float")
end)

H.case("db.records skips invalid rows and visits every valid row once", function()
    local db = require 'imports/turbo/core/db'
    local p = db.get_table("players")
    local seen, n = {}, 0
    for rec in db.records(p) do
        local pid = p:GetRecordFieldValue(rec, "playerid")
        H.ok(not seen[pid], "duplicate " .. pid)
        seen[pid] = true
        n = n + 1
    end
    H.eq(n, #sim:rows("players"), "valid rows")
    H.ok(not seen[999999], "invalid row skipped")
end)

H.case("game: user team, squad, date", function()
    local game = require 'imports/turbo/core/game'
    H.eq(game.user_team_id(), 1, "user team")
    local squad, count, source = game.user_squad()
    H.eq(count, 26, "squad size")
    H.eq(source, "cm_teamsheets", "source")
    H.ok(squad[1001] and squad[1026], "members")
    local d = game.current_date()
    H.eq(d.int, 20270115, "date")
end)

H.case("Windows-style roots from package.path and LE_DATA_PATH are recognised", function()
    local env = require 'imports/turbo/core/env'
    local util = require 'imports/turbo/core/util'
    local saved_path, saved_data = package.path, LE_DATA_PATH
    package.path = "C:\\Games\\LE27\\lua\\libs\\v2\\?.lua;" .. saved_path
    LE_DATA_PATH = "D:\\Mods\\LE"
    local c = table.concat(env.candidate_roots(), "|")
    H.has(c, "C:\\Games\\LE27", "from package.path")
    H.has(c, "D:\\Mods\\LE", "from LE_DATA_PATH")
    H.has(c, "C:\\FC 27 Live Editor", "default install folder")
    package.path, LE_DATA_PATH = saved_path, saved_data
    H.eq(util.join("C:\\FC 27 Live Editor", "turbo_config.json"), "C:\\FC 27 Live Editor\\turbo_config.json", "backslash join")
    H.eq(util.join("C:\\FC 27 Live Editor\\", "x"), "C:\\FC 27 Live Editor\\x", "trailing slash")
end)

H.case("no unmapped memory reads so far", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.case("gregorian day numbers: exact inverse of LE's ToGregorianDays (LE's FromGregorianDays is a day off for many dates)", function()
    local util = require 'imports/turbo/core/util'
    local DATE = require 'imports/core/date'
    local bad = 0
    for days = 1, 200000, 7 do
        local y, m, d = util.date_from_gregorian_days(days)
        local x = DATE:new()
        x.year, x.month, x.day = y, m, d
        if x:ToGregorianDays() ~= days or util.gregorian_days_from_date(y, m, d) ~= days then bad = bad + 1 end
    end
    H.eq(bad, 0, "round trip")
    H.eq(select(1, util.date_from_gregorian_days(1)), 1582)
    local y, m, d = util.date_from_gregorian_days(util.gregorian_days_from_date(2000, 2, 29))
    H.eq(y * 10000 + m * 100 + d, 20000229, "leap day")
    local le = DATE:new()
    le:FromGregorianDays(util.gregorian_days_from_date(2000, 2, 29))
    H.eq(le.month * 100 + le.day, 301, "LE's own FromGregorianDays returns March 1 (documented LE bug)")
end)

H.finish()
