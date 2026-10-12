package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t27 create_player with a typed player id (2.3): range, every per-player table, squad status table, kill switch, the game files' head of the id, log")

-- 7777: an entry in your club's squad status table (a player who left the club), in no database table
local STALE_STATUS = 7777
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, fc27_status = true, status_extra = { STALE_STATUS },
               player_fields = { { name = "headvariation", depth = 4 } } })

local util = require 'imports/turbo/core/util'
local log = require 'imports/turbo/core/log'
local cp = require 'imports/turbo/features/create_player'

local OUT = H.LE .. "/turbo_output"
os.execute(string.format("mkdir -p '%s/asset_catalog'", OUT))
local GATE = OUT .. "/asset_catalog/lua_gate.txt"
local KILL = OUT .. "/" .. cp.TYPED_ID_KILL_FILE
local HIDDEN = 90001   -- a head the game files ship and no row of the save uses (synthetic id)

local function prow(pid) return sim:find_row("players", "playerid", pid) end
local function pval(pid, field)
    local rec = prow(pid)
    return rec and sim:value("players", rec, field) or nil
end
local function set_gate(ids)
    local t = { "#turbo-asset-gate 1", "#build TEST0001-00001000", "#session sim" }
    for _, id in ipairs(ids) do t[#t + 1] = "head3d " .. id end
    H.ok(util.write_file(GATE, table.concat(t, "\r\n") .. "\r\n"), "gate written")
end
local function clear()
    for _, f in ipairs({ GATE, KILL, OUT .. "/asset_writes.log", OUT .. "/asset_hidden_off.txt" }) do os.remove(f) end
end
local function log_has(text)
    for _, l in ipairs(log.lines) do
        if l:find(text, 1, true) then return l end
    end
    return nil
end

-- the fake game call: records the payload and answers like Turbo.dll would (no row is written: ids stay free)
local calls = {}
local function native_on()
    calls = {}
    _G.TurboPlayerCreate = function(code, payload)
        calls[#calls + 1] = { code = code, payload = payload }
        return true, "player created through the game's database", "ok", 127, true
    end
end
local function native_off() _G.TurboPlayerCreate = nil end
local function run(o)
    local base = { source = { playerid = 2005 }, teamid = 4, names = { surname = "Typed" } }
    for k, v in pairs(o or {}) do base[k] = v end
    return H.turbo().run("create_player", base)
end

------------------------------------------------------------------ the id itself
H.case("check_typed_id: free, used in players, used only in another per-player table, out of range, not a number, player career", function()
    clear()
    H.eq(cp.check_typed_id(300100), nil, "a free id")
    H.has(cp.check_typed_id(1001), "already used: players, teamplayerlinks have rows")
    H.has(cp.check_typed_id(W.LOANED_IN), "playerloans")
    -- rows of ids no players row has (a player who left this career): each table refuses the id on its own
    H.ok(_G.InsertDBTableRow("playerloans", { playerid = 300201, teamidloanedfrom = 7, loandateend = 20270630 }), "loan row")
    H.has(cp.check_typed_id(300201), "player id 300201 is already used: playerloans has a row")
    H.ok(_G.InsertDBTableRow("editedplayernames", { playerid = 300202, firstname = "Old", surname = "Name", commonname = "", playerjerseyname = "NAME" }), "names row")
    H.has(cp.check_typed_id(300202), "editedplayernames has a row")
    H.has(cp.check_typed_id(0), "out of range: 1 to 459999")
    H.has(cp.check_typed_id(-4), "out of range")
    H.has(cp.check_typed_id(460000), "460000 and up is the game's own range")
    H.eq(cp.check_typed_id(459999), nil, "the top of the range")
    H.has(cp.check_typed_id(300100, 300000), "out of range: 1 to 300000", "max_playerid narrows it")
    H.has(cp.check_typed_id(3.5), "not a whole number")
    H.has(cp.check_typed_id(31399), "player-career player's id")
end)

H.case("check_typed_id: an entry in your club's squad status table refuses the id", function()
    clear()
    local why = cp.check_typed_id(STALE_STATUS)
    H.has(why, "squad status table (PlayerStatusManager)")
end)

H.case("run: a typed free id is the new player's id (database path), logged; the summary says it was typed", function()
    clear()
    native_off()
    log.reset()
    local ok, msg = run({ playerid = 300100 })
    H.eq(ok, true, msg)
    H.has(msg, "new player 300100"); H.has(msg, "player id typed")
    H.ok(prow(300100) ~= nil, "the row exists with that id")
    H.ok(log_has("create_player: player id 300100 (typed"), "log line: " .. table.concat(log.lines, " | "))
    -- the same id again: refused before anything is written
    local before = sim:count_calls("InsertDBTableRow")
    ok, msg = run({ playerid = 300100 })
    H.eq(ok, false); H.has(msg, "already used: players"); H.has(msg, "nothing was written")
    H.eq(sim:count_calls("InsertDBTableRow"), before, "nothing inserted")
end)

H.case("run: an automatic id is logged as automatic; 0 and none are automatic", function()
    clear()
    native_on()
    log.reset()
    local ok, msg = run({ playerid = 0 })
    H.eq(ok, true, msg)
    local pid = calls[1].payload.playerid
    H.ok(log_has("create_player: player id " .. pid .. " (automatic"), "log: " .. table.concat(log.lines, " | "))
    H.ok(not msg:find("player id typed", 1, true), "not typed: " .. msg)
    ok, msg = run({})
    H.eq(ok, true, msg); H.eq(#calls, 2)
end)

H.case("run: refusals before anything is written (text, fraction, negative, 460000, in use, squad status entry)", function()
    clear()
    native_on()
    for _, c in ipairs({ { "abc", "not a whole number" }, { 12.5, "not a whole number" }, { -3, "out of range" }, { 460000, "out of range" },
                         { 1001, "already used" }, { 300201, "playerloans" }, { STALE_STATUS, "squad status table" },
                         { 31399, "player-career" } }) do
        local ok, msg = run({ playerid = c[1] })
        H.eq(ok, false, tostring(c[1])); H.has(msg, c[2], tostring(c[1]))
    end
    H.eq(#calls, 0, "the game was never asked")
end)

H.case("run: the native path gets the typed id in the payload", function()
    clear()
    native_on()
    local ok, msg = run({ playerid = 300300 })
    H.eq(ok, true, msg)
    H.eq(calls[1].payload.playerid, 300300); H.eq(calls[1].payload.players.playerid, 300300)
end)

H.case("kill switch create_player_typed_id_off.txt: a typed id is refused, an automatic one works", function()
    clear()
    native_on()
    H.ok(util.write_file(KILL, ""), "kill switch")
    local ok, msg = run({ playerid = 300400 })
    H.eq(ok, false); H.has(msg, "typed player ids are switched off")
    ok, msg = run({})
    H.eq(ok, true, msg)
    clear()
end)

------------------------------------------------------------------ the game files' assets of the id
H.case("id_assets.head3d with a current gate: the head of the id is given (class 0, high quality, variation), logged", function()
    clear()
    native_off()
    set_gate({ HIDDEN })
    log.reset()
    local ok, msg = run({ playerid = HIDDEN, id_assets = { head3d = true, headvariation = 2, callname = true, miniface = true } })
    H.eq(ok, true, msg)
    H.has(msg, "the game files' 3D head of id " .. HIDDEN); H.has(msg, "his own callname recording")
    H.eq(pval(HIDDEN, "headassetid"), HIDDEN); H.eq(pval(HIDDEN, "headclasscode"), 0)
    H.eq(pval(HIDDEN, "hashighqualityhead"), 1); H.eq(pval(HIDDEN, "headvariation"), 2)
    local l = H.csv_lines(OUT .. "/asset_writes.log") or {}
    H.eq(#l, 1); H.has(l[1], "player " .. HIDDEN .. "\theadassetid\t(new player)\t" .. HIDDEN .. "\tTEST0001-00001000")
    H.ok(log_has("create_player: player id " .. HIDDEN .. " (typed"), "log")
    H.ok(log_has("3D head of the game files (asset catalog TEST0001-00001000)"), "log names the head: " .. table.concat(log.lines, " | "))
end)

H.case("id_assets.head3d without a trusted gate, or with a gate that lacks the head: refused, nothing written", function()
    clear()
    native_on()
    local ok, msg = run({ playerid = 90002, id_assets = { head3d = true, headvariation = 0 } })
    H.eq(ok, false); H.has(msg, "cannot be checked now"); H.has(msg, "nothing was written")
    set_gate({ HIDDEN })
    ok, msg = run({ playerid = 90002, id_assets = { head3d = true, headvariation = 0 } })
    H.eq(ok, false); H.has(msg, "head 90002 is not in the game files")
    H.ok(util.write_file(OUT .. "/asset_hidden_off.txt", ""), "hidden off")
    set_gate({ HIDDEN, 90003 })
    ok, msg = run({ playerid = 90003, id_assets = { head3d = true, headvariation = 0 } })
    H.eq(ok, false); H.has(msg, "hidden heads are switched off")
    H.eq(#calls, 0, "the game was never asked")
    H.eq(prow(90002), nil); H.eq(prow(90003), nil)
    clear()
end)

H.case("a typed id whose head the gate lists, without id_assets: refused as before (the GUI box is named)", function()
    clear()
    native_on()
    set_gate({ 90004 })
    local ok, msg = run({ playerid = 90004 })
    H.eq(ok, false); H.has(msg, "carries a game head"); H.has(msg, "Player ID box")
    -- a callname only (no head in the gate): nothing to refuse, the summary says he gets the recording
    ok, msg = run({ playerid = 90005, id_assets = { head3d = false, callname = true } })
    H.eq(ok, true, msg); H.has(msg, "his own callname recording")
    H.eq(calls[1].payload.playerid, 90005)
    H.ok(calls[1].payload.players.headassetid ~= 90005, "no head written for a callname-only id")
    clear()
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
