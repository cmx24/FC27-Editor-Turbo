package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t26 asset gate (lua_gate.txt): trusted only for this GUI session, kill switches, custom_headassets and create_player gated, write log")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local util = require 'imports/turbo/core/util'
local agate = require 'imports/turbo/core/asset_gate'
local preset = require 'imports/turbo/core/preset'
local cp = require 'imports/turbo/features/create_player'

local OUT = H.LE .. "/turbo_output"
os.execute(string.format("mkdir -p '%s/asset_catalog' '%s/players'", OUT, OUT))
local GATE = OUT .. "/asset_catalog/lua_gate.txt"
local SESSION = "sim"   -- the session the simulated GUI writes into bridge_dll.json (mock/sim.lua enable_gui)
local HIDDEN = 90001    -- a head the game files ship and no row of the save uses (synthetic id)
local HIDDEN2 = 90002
local HIDDEN3 = 90003   -- used by create_player only (custom_headassets gives HIDDEN to a player: then it is no longer hidden)

local function write(path, text) H.ok(util.write_file(path, text), "written " .. path) end
local function rm(path) os.remove(path) end
local function gate_text(ids, session, extra)
    local t = { "#turbo-asset-gate 1", "#build TEST0001-00001000" }
    if session then t[#t + 1] = "#session " .. session end
    for _, id in ipairs(ids) do t[#t + 1] = "head3d " .. id end
    for _, l in ipairs(extra or {}) do t[#t + 1] = l end
    return table.concat(t, "\r\n") .. "\r\n"
end
local function set_gate(ids, session, extra) write(GATE, gate_text(ids, session == nil and SESSION or session, extra)) end
local function clear()
    for _, f in ipairs({ GATE, OUT .. "/asset_catalog_off.txt", OUT .. "/asset_hidden_off.txt", OUT .. "/asset_gate_allow.txt",
        OUT .. "/asset_writes.log" }) do rm(f) end
end
local function pval(pid, field)
    local rec = sim:find_row("players", "playerid", pid)
    return rec and sim:value("players", rec, field) or nil
end
local function heads(map)
    H.write_config({ modules = { custom_headassets = { map = {} } } })   -- write_config merges maps: start from an empty one
    H.write_config({ modules = { custom_headassets = { map = map } } })
    return H.turbo().run("custom_headassets")
end
local function log_lines()
    return H.csv_lines(OUT .. "/asset_writes.log") or {}
end

------------------------------------------------------------------ the file
H.case("parse: header, build, session, head3d set; CRLF and BOM; other families kept", function()
    local g, err = agate.parse("\239\187\191" .. gate_text({ 41, 250, 41 }, "abc", { "miniface 7", "# comment" }))
    H.ok(g, err)
    H.eq(g.build, "TEST0001-00001000"); H.eq(g.session, "abc")
    H.ok(g.fam.head3d[41] and g.fam.head3d[250], "heads"); H.eq(g.count.head3d, 2, "duplicates counted once")
    H.ok(g.fam.miniface[7], "miniface family kept")
end)

H.case("parse refuses: no header, a malformed line, no head3d id, a #count that does not match (file cut short)", function()
    local g, err = agate.parse("head3d 41\n")
    H.eq(g, nil); H.has(err, "first line")
    g, err = agate.parse("#turbo-asset-gate 1\nhead3d 41\nhead3d 4x1\n")
    H.eq(g, nil); H.has(err, "line 3")
    g, err = agate.parse("#turbo-asset-gate 1\nhead3d -5\n")
    H.eq(g, nil)
    g, err = agate.parse("#turbo-asset-gate 1\n#build K\n")
    H.eq(g, nil); H.has(err, "no head3d")
    g, err = agate.parse("#turbo-asset-gate 1\n#count 3\nhead3d 1\nhead3d 2\n")
    H.eq(g, nil); H.has(err, "#count 3")
    g = agate.parse("#turbo-asset-gate 2\nhead3d 1\n")
    H.eq(g, nil, "another version is refused")
end)

H.case("allow file: ids or head3d lines, comments, bad lines skipped", function()
    local set, bad = agate.parse_allow("# mods\r\n123\r\nhead3d 456\r\nabc\r\n\r\n")
    H.ok(set[123] and set[456], "ids"); H.eq(bad, 1)
end)

H.case("load: none without a file, current for this session, stale for another session / no session line / no fresh bridge_dll.json", function()
    clear()
    local st = agate.load()
    H.eq(st.status, "none"); H.has(st.reason, "lua_gate.txt")
    set_gate({ HIDDEN })
    st = agate.load()
    H.eq(st.status, "current", st.reason); H.ok(agate.trusted(st)); H.eq(agate.key(st), "TEST0001-00001000")
    set_gate({ HIDDEN }, "OTHER")
    st = agate.load()
    H.eq(st.status, "stale"); H.has(st.reason, "another Turbo GUI session"); H.eq(agate.key(st), "unchecked")
    set_gate({ HIDDEN }, false)
    st = agate.load()
    H.eq(st.status, "stale"); H.has(st.reason, "#session")
    set_gate({ HIDDEN })
    st = agate.load({ now = os.time() + 3600 })   -- bridge_dll.json an hour old: the GUI is gone
    H.eq(st.status, "stale"); H.has(st.reason, "not running")
    write(GATE, "#turbo-asset-gate 1\nhead3d x\n")
    st = agate.load()
    H.eq(st.status, "bad"); H.eq(agate.trusted(st), false)
end)

H.case("kill switches: asset_catalog_off.txt ignores the gate (and the allow file); asset_hidden_off.txt is read", function()
    clear()
    set_gate({ HIDDEN })
    write(OUT .. "/asset_gate_allow.txt", "777\n")
    write(OUT .. "/asset_hidden_off.txt", "")
    local st = agate.load()
    H.eq(st.status, "current"); H.eq(st.hidden_off, true); H.ok(st.allow[777], "allow read")
    write(OUT .. "/asset_catalog_off.txt", "")
    st = agate.load()
    H.eq(st.status, "off"); H.has(st.reason, "asset_catalog_off.txt"); H.eq(st.allow[777], nil)
    H.eq(agate.trusted(st), false)
    clear()
end)

H.case("write log: local ISO time with offset, tab separated, catalog key", function()
    H.ok(agate.iso_now():match("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d[%+%-]%d%d:%d%d$"), agate.iso_now())
    clear()
    set_gate({ HIDDEN })
    H.ok(agate.log_write(agate.load(), "player 1", "headassetid", 1, HIDDEN))
    local l = log_lines()
    H.eq(#l, 1)
    local f = {}
    for v in (l[1] .. "\t"):gmatch("([^\t]*)\t") do f[#f + 1] = v end
    H.eq(#f, 6); H.eq(f[2], "player 1"); H.eq(f[3], "headassetid"); H.eq(f[4], "1"); H.eq(f[5], tostring(HIDDEN))
    H.eq(f[6], "TEST0001-00001000")
    clear()
end)

------------------------------------------------------------------ custom_headassets
H.case("custom_headassets, current gate: a head the game files lack is refused before anything is written", function()
    clear()
    set_gate({ HIDDEN, 2004 })
    local ok, msg = heads({ ["1001"] = HIDDEN, ["1003"] = 55555 })
    H.eq(ok, false); H.has(msg, "head 55555 is not in the game files"); H.has(msg, "nothing written")
    H.eq(pval(1001, "headassetid") ~= HIDDEN, true, "1001 untouched")
    H.eq(#log_lines(), 0, "nothing logged")
end)

H.case("custom_headassets, current gate: hidden head applied, every changed field logged with the build key", function()
    clear()
    set_gate({ HIDDEN, 2004 })
    local before = pval(1003, "headassetid")
    local ok, msg = heads({ ["1003"] = HIDDEN })
    H.eq(ok, true, msg); H.has(msg, "updated 1 of 1"); H.has(msg, "checked against the game files")
    H.eq(pval(1003, "headassetid"), HIDDEN); H.eq(pval(1003, "headclasscode"), 0)
    local l = log_lines()
    H.ok(#l >= 1, "logged")
    local found = false
    for _, line in ipairs(l) do
        if line:find("player 1003\theadassetid\t" .. tostring(before) .. "\t" .. HIDDEN .. "\tTEST0001-00001000", 1, true) then found = true end
    end
    H.ok(found, table.concat(l, " | "))
    -- a second run changes nothing and logs nothing new
    local n = #l
    ok, msg = heads({ ["1003"] = HIDDEN })
    H.eq(ok, true, msg); H.eq(#log_lines(), n, "no new line when nothing changes")
end)

H.case("custom_headassets: asset_hidden_off.txt refuses a head no row uses, a used one passes; the allow file vouches", function()
    clear()
    set_gate({ HIDDEN2, 2004 })
    write(OUT .. "/asset_hidden_off.txt", "")
    local ok, msg = heads({ ["1005"] = HIDDEN2 })
    H.eq(ok, false); H.has(msg, "hidden heads are switched off")
    H.eq(pval(1005, "headassetid") ~= HIDDEN2, true, "untouched")
    ok, msg = heads({ ["1005"] = 2004 })
    H.eq(ok, true, msg)
    H.eq(pval(1005, "headassetid"), 2004)
    rm(OUT .. "/asset_hidden_off.txt")
    ok, msg = heads({ ["1007"] = 77777 })
    H.eq(ok, false); H.has(msg, "not in the game files")
    write(OUT .. "/asset_gate_allow.txt", "77777\n")
    ok, msg = heads({ ["1007"] = 77777 })
    H.eq(ok, true, msg); H.eq(pval(1007, "headassetid"), 77777)
end)

H.case("custom_headassets without a trusted gate (none, stale, switched off): today's rules, said in the summary", function()
    clear()
    local ok, msg = heads({ ["1009"] = 66601 })
    H.eq(ok, true, msg); H.has(msg, "heads unchecked"); H.eq(pval(1009, "headassetid"), 66601)
    set_gate({ HIDDEN }, "OTHER")
    ok, msg = heads({ ["1009"] = 66602 })
    H.eq(ok, true, msg); H.has(msg, "another Turbo GUI session"); H.eq(pval(1009, "headassetid"), 66602)
    set_gate({ HIDDEN })
    write(OUT .. "/asset_catalog_off.txt", "")
    ok, msg = heads({ ["1009"] = 66603 })
    H.eq(ok, true, msg); H.has(msg, "asset_catalog_off.txt"); H.eq(pval(1009, "headassetid"), 66603)
    local l = log_lines()
    H.ok(#l > 0 and l[#l]:find("\tunchecked$") ~= nil, "unchecked key: " .. tostring(l[#l]))
    clear()
end)

------------------------------------------------------------------ create_player
local function cm_preset(name, real_id)
    local doc = { format = "turbo-player-preset", version = 1, name = name, playerid = 900001,
        names = { firstname = "Test", surname = name, commonname = "", playerjerseyname = name },
        players = { overallrating = 70, potential = 75, headclasscode = 1, preferredposition1 = 25 },
        links = {}, cmtracker = { playerid = 900001, real_face_id = real_id } }
    local f = H.out("players/" .. name .. "_cmt.json")
    H.ok(util.write_file(f, preset.json().encode(doc)), "preset written")
    return f
end
local function create(name, real_id, extra)
    local o = { source = { file = cm_preset(name, real_id) }, teamid = 4 }
    for k, v in pairs(extra or {}) do o[k] = v end
    local ok, msg = H.turbo().run("create_player", o)
    return ok, msg, tonumber(tostring(msg):match("new player (%d+)"))
end

H.case("create_player, current gate: the real face iff the gate lists the head; a hidden one is logged", function()
    clear()
    set_gate({ HIDDEN, HIDDEN3 })
    local ok, msg, pid = create("Gated", HIDDEN3)
    H.eq(ok, true, msg); H.has(msg, "real face of player " .. HIDDEN3)
    H.eq(pval(pid, "headassetid"), HIDDEN3); H.eq(pval(pid, "headclasscode"), 0)
    local l = log_lines()
    H.eq(#l, 1); H.has(l[1], "player " .. pid .. "\theadassetid\t(new player)\t" .. HIDDEN3 .. "\tTEST0001-00001000")
    -- a head a row of the save already uses (custom_headassets gave HIDDEN to 1003 above): not hidden, not logged
    ok, msg, pid = create("GatedUsed", HIDDEN)
    H.eq(ok, true, msg); H.eq(pval(pid, "headassetid"), HIDDEN); H.eq(#log_lines(), 1, "not hidden: no line")
    -- 2004 is a DB real head, but the gate (the game files) is the proof while it is current
    ok, msg, pid = create("NotInGate", 2004)
    H.eq(ok, true, msg); H.has(msg, "generic head"); H.has(msg, "not in the game files")
    H.eq(pval(pid, "headclasscode"), 1); H.eq(pval(pid, "headassetid"), pid)
end)

H.case("create_player without a gate: real face only when his own row has class 0 and his own head (DB proof)", function()
    clear()
    local ok, msg, pid = create("DbReal", 2004)
    H.eq(ok, true, msg); H.has(msg, "real face of player 2004"); H.eq(pval(pid, "headclasscode"), 0)
    ok, msg, pid = create("DbGeneric", 2005)
    H.eq(ok, true, msg); H.has(msg, "generic head"); H.has(msg, "unchecked")
    H.eq(pval(pid, "headclasscode"), 1)
    ok, msg, pid = create("Unknown", HIDDEN)
    H.eq(ok, true, msg); H.has(msg, "generic head"); H.eq(pval(pid, "headclasscode"), 1)
    H.eq(#log_lines(), 0, "nothing logged")
end)

H.case("create_player, current gate: a new id never carries another player's game head (free id skips it; typed id refused)", function()
    clear()
    local top = assert(cp.free_playerid(0, cp.GENERATED_MIN - 1))
    set_gate({ HIDDEN, top, top + 1 })
    local ok, msg, pid = create("SkipHead", 0)
    H.eq(ok, true, msg); H.eq(pid, top + 2, "ids with a game head are passed over")
    ok, msg = create("TypedHead", 0, { playerid = top + 1 })
    H.eq(ok, false); H.has(msg, "carries a game head")
    -- his own real face id is allowed as his player id
    ok, msg, pid = create("OwnHead", top + 1, { playerid = top + 1 })
    H.eq(ok, true, msg); H.eq(pid, top + 1); H.eq(pval(pid, "headassetid"), top + 1); H.eq(pval(pid, "headclasscode"), 0)
    clear()
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)


H.finish()
