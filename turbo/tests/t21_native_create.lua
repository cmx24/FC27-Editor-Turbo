package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t21 created players through the game's own INSERT (TurboPlayerCreate): payload, no database rows, fallback when off")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, player_fields = { { name = "wage", depth = 20 } } })

local util = require 'imports/turbo/core/util'
local json = require 'imports/external/json'
local moves = require 'imports/turbo/core/moves'
local bridge = require 'imports/turbo/bridge'

local USER, FA = W.USER_TEAM, 111592
local function run(overrides) return H.turbo().run("create_player", overrides) end
local function prow(pid) return sim:find_row("players", "playerid", pid) end
local function pval(pid, field)
    local rec = prow(pid)
    return rec and sim:value("players", rec, field) or nil
end

-- the team-sheet write of the database path is spied on (the native path must not call it)
local sheet_calls = 0
local real_add_to_sheet = moves.add_to_sheet
moves.add_to_sheet = function(...)
    sheet_calls = sheet_calls + 1
    return real_add_to_sheet(...)
end

-- the fake game call: records the payload and answers like Turbo.dll would
local calls, answer = {}, nil
local function install(a)
    calls, answer = {}, a or { true, "player created through the game's database", "ok", 127, true }
    _G.TurboPlayerCreate = function(code, payload)
        calls[#calls + 1] = { code = code, payload = payload }
        return table.unpack(answer)
    end
end

local function no_float(t, path)
    for k, v in pairs(t) do
        if type(v) == "table" then
            local bad = no_float(v, path .. "." .. tostring(k))
            if bad then return bad end
        elseif math.type(v) == "float" then
            return path .. "." .. tostring(k)
        end
    end
    return nil
end

H.case("native path into an AI club: the payload carries the whole row as integers, the names, form 3; no InsertDBTableRow", function()
    install()
    local before, sheets = sim:count_calls("InsertDBTableRow"), sheet_calls
    local ok, msg = run({ source = { playerid = 2005 }, teamid = 3, names = { firstname = "Neo", surname = "Native" } })
    H.eq(ok, true, msg)
    H.eq(#calls, 1, "one call")
    H.eq(calls[1].code, 1, "code 1 = create")
    local p = calls[1].payload
    H.ok(p.playerid > 0 and p.playerid < 460000, "a new id")
    H.eq(p.team, 3)
    H.eq(p.months, 0, "no contract record outside your club"); H.eq(p.wage, 0)
    H.eq(p.players.playerid, p.playerid)
    H.eq(p.players.overallrating, pval(2005, "overallrating"), "the source's fields are copied")
    H.eq(p.players.birthdate, pval(2005, "birthdate"))
    H.ok(p.players.contractvaliduntil >= 2027, "the contract values (moves.contract_values) are in the row")
    H.ok(p.players.playerjointeamdate ~= nil, "the join date is in the row")
    H.eq(p.names.firstname, "Neo"); H.eq(p.names.surname, "Native"); H.eq(p.names.playerid, nil, "names carry no playerid")
    H.eq(p.link.form, 3, "form 3 as the database path's link")
    H.eq(no_float(p, "payload"), nil, "integers and strings only")
    H.eq(sim:count_calls("InsertDBTableRow"), before, "no InsertDBTableRow")
    H.eq(sheet_calls, sheets, "no team-sheet write")
    H.eq(prow(p.playerid), nil, "no players row written by Lua (the fake game wrote none)")
    H.has(msg, "created by the game"); H.has(msg, "shirt chosen by the game")
end)

H.case("native path into your club: months and wage for the game's contract record; Free Agents: none", function()
    install()
    local ok, msg = run({ source = { playerid = 2005 }, teamid = USER })
    H.eq(ok, true, msg)
    local p = calls[1].payload
    H.eq(p.team, USER)
    H.ok(p.months >= 12 and p.months <= 120, "months for the contract record: " .. tostring(p.months))
    H.eq(p.wage, pval(2005, "wage") or 0, "the row's wage")
    H.has(msg, "the game's own move adds him"); H.ok(not msg:find("added to your team sheet", 1, true), "no team-sheet claim: " .. msg)
    install()
    ok, msg = run({ source = { playerid = 2005 }, teamid = FA })
    H.eq(ok, true, msg)
    H.eq(calls[1].payload.team, FA); H.eq(calls[1].payload.months, 0)
    H.eq(calls[1].payload.names, nil, "no names given: none sent")
end)

H.case("the game refuses (not off): nothing is written, the reason is returned", function()
    install({ false, "player id 2061 is in use: players has 1 row(s) for it (nothing was written)", "failed", 0, nil })
    local before = sim:count_calls("InsertDBTableRow")
    local ok, msg = run({ source = { playerid = 2005 }, teamid = 3 })
    H.eq(ok, false); H.has(msg, "the game did not create the player"); H.has(msg, "in use")
    H.eq(sim:count_calls("InsertDBTableRow"), before, "no database fallback after a refusal")
end)

H.case("the call is off: the database path runs exactly as before, with a note", function()
    install({ false, "player_create: off (opt-in: create turbo_output\\call_player_create_on.txt)", "off" })
    local before, sheets = sim:count_calls("InsertDBTableRow"), sheet_calls
    local ok, msg = run({ source = { playerid = 2005 }, teamid = USER, names = { surname = "Fallback" } })
    H.eq(ok, true, msg)
    H.eq(#calls, 1, "asked once")
    H.eq(sim:count_calls("InsertDBTableRow") - before, 3, "players + teamplayerlinks + editedplayernames rows")
    H.eq(sheet_calls - sheets, 1, "your team sheet written as before")
    H.has(msg, "player_create: off"); H.has(msg, "added through the database")
    local pid = calls[1].payload.playerid
    H.ok(prow(pid) ~= nil, "the row exists (database path)")
end)

H.case("TurboPlayerCreate missing: the database path, no note", function()
    _G.TurboPlayerCreate = nil
    local before = sim:count_calls("InsertDBTableRow")
    local ok, msg = run({ source = { playerid = 2005 }, teamid = 3 })
    H.eq(ok, true, msg)
    H.eq(sim:count_calls("InsertDBTableRow") - before, 2, "players + teamplayerlinks")
    H.ok(not msg:find("player_create", 1, true), "no note: " .. msg)
end)

H.case("a dry run asks nothing", function()
    install()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = run({ source = { playerid = 2005 }, teamid = 3 })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]")
    H.eq(#calls, 0, "no game call in a dry run")
end)

H.case("bridge.player_create: off without the opt-in file (nothing called), the payload file with it, the DLL's answers", function()
    local gc = {}
    local real_call = bridge.game_call
    local reply = { "ok", "created", 127, 1 }
    bridge.game_call = function(op, args, label)
        gc[#gc + 1] = { op = op, args = args, label = label }
        return table.unpack(reply)
    end
    local dir = bridge.dir()
    local opt_in = util.join(dir, "call_player_create_on.txt")
    os.remove(opt_in)
    local payload = { playerid = 300100, team = 3, months = 0, wage = 0, players = { playerid = 300100, overallrating = 70, birthdate = 150000 },
                      names = { firstname = "Jo\195\163o", surname = "", commonname = "" }, link = { form = 3 } }
    local ok, text, status = bridge.player_create(1, payload)
    H.eq(ok, false); H.eq(status, "off"); H.has(text, "opt-in: create turbo_output\\call_player_create_on.txt")
    H.eq(#gc, 0, "nothing called while off")
    H.eq(util.file_exists(util.join(dir, "turbo_player_create.json")), false, "no payload written while off")
    util.write_file(opt_in, "")
    local written, in_team
    ok, text, status, written, in_team = bridge.player_create(1, payload)
    H.eq(ok, true, text); H.eq(status, "ok"); H.eq(written, 127); H.eq(in_team, true)
    H.eq(#gc, 1); H.eq(gc[1].op, 12, "op 12")
    local doc = json.decode(util.read_file(util.join(dir, "turbo_player_create.json")))
    H.eq(gc[1].args[2], 1, "code"); H.eq(gc[1].args[3], doc.seq, "the seq of the file"); H.eq(gc[1].args[4], 300100, "playerid")
    H.eq(doc.playerid, 300100); H.eq(doc.team, 3); H.eq(doc.players.overallrating, 70); H.eq(doc.players.playerid, 300100)
    H.eq(doc.names.firstname, "Jo\195\163o"); H.eq(doc.names.surname, nil, "empty names are not sent"); H.eq(doc.link.form, 3)
    local raw = util.read_file(util.join(dir, "turbo_player_create.json"))
    H.ok(not raw:find("%d%.%d"), "no float in the file: " .. raw)
    -- the next request has the next seq; code 9 is a check
    ok = bridge.player_create(9, payload)
    H.eq(ok, true); H.eq(gc[2].args[2], 9); H.eq(gc[2].args[3], doc.seq + 1)
    -- refused on the Lua side: floats with a fraction, long names, bad codes, ids out of range (nothing called)
    local n = #gc
    payload.players.overallrating = 70.5
    ok, text = bridge.player_create(1, payload)
    H.eq(ok, false); H.has(text, "is a float")
    payload.players.overallrating = 70.0
    ok = bridge.player_create(1, payload)
    H.eq(ok, true, "an integral float is sent as an integer")
    n = #gc
    payload.names.surname = string.rep("x", 45)
    ok, text = bridge.player_create(1, payload)
    H.eq(ok, false); H.has(text, "45 bytes long")
    payload.names.surname = ""
    ok, text = bridge.player_create(2, payload)
    H.eq(ok, false); H.has(text, "does not know that player-creation action")
    payload.playerid = 460000
    ok, text = bridge.player_create(1, payload)
    H.eq(ok, false); H.has(text, "out of range")
    payload.playerid = 300100
    H.eq(#gc, n, "nothing called for a refused payload")
    -- the DLL answers off (out[0] = -1) or the call block is unavailable: "off" (the caller keeps the database path)
    reply = { "failed", "player_create: off (kill switch turbo_output\\call_player_create_off.txt is present)", -1, -1 }
    ok, text, status = bridge.player_create(1, payload)
    H.eq(ok, false); H.eq(status, "off"); H.has(text, "kill switch")
    reply = { "failed", "unknown game call op 12", -1, 0 }
    ok, text, status = bridge.player_create(1, payload)
    H.eq(status, "off", "an older Turbo.dll without op 12 is off too")
    reply = { "unavailable", "the Turbo GUI is not running" }
    ok, text, status = bridge.player_create(1, payload)
    H.eq(ok, false); H.eq(status, "off"); H.has(text, "not running")
    -- a refusal after validation is a failure, not off
    reply = { "failed", "player 300100 is in use", 0, -1 }
    ok, text, status = bridge.player_create(1, payload)
    H.eq(ok, false); H.eq(status, "failed")
    reply = { "queued", "queued for the game thread" }
    ok, text, status = bridge.player_create(1, payload)
    H.eq(ok, true); H.eq(status, "queued")
    os.remove(opt_in)
    bridge.game_call = real_call
end)

H.case("install_natives defines TurboPlayerCreate as bridge.player_create", function()
    TURBO_STATE.bridge.natives_installed = nil
    _G.TurboPlayerCreate = nil
    local ok, installed = pcall(bridge.install_natives)
    H.ok(ok, tostring(installed))
    if installed == true then
        H.eq(_G.TurboPlayerCreate, bridge.player_create, "the global is the bridge function")
    else
        H.eq(_G.TurboPlayerCreate, nil, "not installed: not defined")
    end
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
