package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t24 tactic set pieces (cm_teamsheets: captain, takers, corner supports, pre-set subs): guarded write, read-back, undo")

local sim = H.setup({ in_cm = true })
W.build(sim, {})

local function run(over) return H.turbo().run("tactic_setpieces", over) end
local function dbm() return require 'imports/turbo/core/db' end
local function row()
    local db = dbm()
    local t = db.get_table("cm_teamsheets")
    return t, db.find(t, "teamid", W.USER_TEAM)
end
local function val(f)
    local t, r = row()
    return t:GetRecordFieldValue(r, f)
end
local function setval(f, v)
    local t, r = row()
    t:SetRecordFieldValue(r, f, v)
end
local function out_file(name)
    local env = require 'imports/turbo/core/env'
    local cfg = require 'imports/turbo/core/config'
    return env.output_dir(cfg.load().turbo.output_dir) .. "/" .. name
end
local function exists(p) local f = io.open(p, "rb"); if f then f:close(); return true end; return false end
local function read_all(p) local f = io.open(p, "rb"); if not f then return nil end; local s = f:read("a"); f:close(); return s end

local P1, P2, P3 = W.USER_PLAYERS[1], W.USER_PLAYERS[2], W.USER_PLAYERS[3]
local OUTSIDER = 2001  -- a player of the other club's sheet

H.case("get: reads the 24 fields of your club's row and writes the current file", function()
    local ok, msg = run({ mode = "get" })
    H.eq(ok, true, msg); H.has(msg, "24 fields"); H.has(msg, "team " .. W.USER_TEAM)
    local text = read_all(out_file("tactic_setpieces_current.json"))
    H.ok(text ~= nil, "tactic_setpieces_current.json written")
    H.has(text, "captainid"); H.has(text, "cksupport9"); H.has(text, "customsub2out")
    H.eq(val("captainid"), P1); H.eq(val("penaltytakerid"), P2)
end)

H.case("set: every refusal writes nothing", function()
    local before = val("penaltytakerid")
    local ok, msg = run({ mode = "set", values = { penaltytakerid = P3 } })
    H.eq(ok, false); H.has(msg, "confirm")
    ok, msg = run({ mode = "set", confirm = true, values = { penaltytakerid = P3, notafield = 4 } })
    H.eq(ok, false); H.has(msg, "not a set-piece field")
    ok, msg = run({ mode = "set", confirm = true, values = { penaltytakerid = OUTSIDER } })
    H.eq(ok, false); H.has(msg, "not on your team sheet")
    ok, msg = run({ mode = "set", confirm = true, values = { penaltytakerid = 99999999 } })
    H.eq(ok, false); H.has(msg, "outside the field range")
    ok, msg = run({ mode = "set", confirm = true, values = { penaltytakerid = "abc" } })
    H.eq(ok, false)
    ok, msg = run({ mode = "set", confirm = true, values = {} })
    H.eq(ok, false); H.has(msg, "no values")
    ok, msg = run({ mode = "set", confirm = true, team = W.USER_TEAM + 7, values = { penaltytakerid = P3 } })
    H.eq(ok, false); H.has(msg, "your club is team")
    ok, msg = run({ mode = "set", confirm = true, values = { captainid = P3, penaltytakerid = OUTSIDER } })
    H.eq(ok, false); H.eq(val("captainid"), P1, "a refused set writes none of its fields")
    H.eq(val("penaltytakerid"), before)
    H.eq(exists(out_file("tactic_setpieces_undo.json")), false, "no journal for a refused set")
    ok, msg = run({ mode = "bogus" })
    H.eq(ok, false); H.has(msg, "mode must be")
end)

H.case("set: writes, reads back, journals the old values", function()
    local ok, msg = run({ mode = "set", confirm = true, preset = "T1", team = W.USER_TEAM,
        values = { captainid = P3, penaltytakerid = -1, leftcornerkicktakerid = P2, cksupport1 = P3, customsub0in = P2, customsub0out = P1 } })
    H.eq(ok, true, msg); H.has(msg, "6 field"); H.has(msg, "read back")
    H.eq(val("captainid"), P3); H.eq(val("penaltytakerid"), -1); H.eq(val("leftcornerkicktakerid"), P2)
    H.eq(val("cksupport1"), P3); H.eq(val("customsub0in"), P2); H.eq(val("customsub0out"), P1)
    local j = read_all(out_file("tactic_setpieces_undo.json"))
    H.ok(j ~= nil, "journal written"); H.has(j, "\"preset\":\"T1\""); H.has(j, "before"); H.has(j, "after")
    ok, msg = run({ mode = "set", confirm = true, values = { captainid = P3 } })
    H.eq(ok, true, msg); H.has(msg, "nothing to change")
end)

H.case("undo: puts the old values back, leaves a field changed since, renames the journal", function()
    setval("cksupport1", P1)  -- changed since Turbo wrote it
    local ok, msg = run({ mode = "undo", confirm = true })
    H.eq(ok, false); H.has(msg, "left alone"); H.has(msg, "cksupport1")
    H.eq(val("captainid"), P1, "captain back"); H.eq(val("penaltytakerid"), P2, "penalty back"); H.eq(val("leftcornerkicktakerid"), -1)
    H.eq(val("cksupport1"), P1, "the field changed since stays as the user has it")
    H.eq(val("customsub0in"), -1); H.eq(val("customsub0out"), -1)
    -- a journal with a field left alone is kept: a second undo is a no-op for the rest and still reports the field
    ok, msg = run({ mode = "undo", confirm = true })
    H.eq(ok, false)
end)

H.case("undo: a complete undo renames the journal; nothing left to undo afterwards", function()
    os.remove(out_file("tactic_setpieces_undo.json"))
    os.remove(out_file("tactic_setpieces_undo.json.done"))
    local ok, msg = run({ mode = "set", confirm = true, values = { captainid = P2 } })
    H.eq(ok, true, msg)
    ok, msg = run({ mode = "undo", confirm = true })
    H.eq(ok, true, msg); H.has(msg, "1 field")
    H.eq(val("captainid"), P1)
    H.eq(exists(out_file("tactic_setpieces_undo.json")), false); H.eq(exists(out_file("tactic_setpieces_undo.json.done")), true)
    ok, msg = run({ mode = "undo", confirm = true })
    H.eq(ok, false); H.has(msg, "no set-piece journal")
    ok, msg = run({ mode = "undo" })
    H.eq(ok, false); H.has(msg, "confirm")
end)

H.case("a read-back mismatch puts every field back and fails", function()
    local db = dbm()
    local real = db.set
    db.set = function(tbl, rec, field, value, dry)
        if field == "penaltytakerid" and value == P3 then return true end  -- the game "accepts" the write but keeps the old value
        return real(tbl, rec, field, value, dry)
    end
    local ok, msg = run({ mode = "set", confirm = true, values = { captainid = P3, penaltytakerid = P3 } })
    db.set = real
    H.eq(ok, false); H.has(msg, "read-back mismatch"); H.has(msg, "penaltytakerid"); H.has(msg, "put back")
    H.eq(val("captainid"), P1, "the other field was put back"); H.eq(val("penaltytakerid"), P2)
end)

H.case("a failing write puts the earlier ones back", function()
    local db = dbm()
    local real = db.set
    local n = 0
    db.set = function(tbl, rec, field, value, dry)
        n = n + 1
        if n == 2 and not dry then return false, "Live Editor refused to write " .. field end
        return real(tbl, rec, field, value, dry)
    end
    local ok, msg = run({ mode = "set", confirm = true, values = { captainid = P3, penaltytakerid = P3, cksupport2 = P3 } })
    db.set = real
    H.eq(ok, false); H.has(msg, "write failed"); H.has(msg, "put back")
    H.eq(val("captainid"), P1); H.eq(val("penaltytakerid"), P2); H.eq(val("cksupport2"), -1)
end)

H.case("dry run writes nothing", function()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = run({ mode = "set", confirm = true, values = { captainid = P3 } })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg); H.has(msg, "dry run")
    H.eq(val("captainid"), P1)
end)

H.case("no career: refused", function()
    sim.in_cm = false
    local ok, msg = run({ mode = "get" })
    sim.in_cm = true
    H.eq(ok, false)
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
