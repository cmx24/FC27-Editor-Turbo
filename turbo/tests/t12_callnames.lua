package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t12 callnames: playernamemap rows, display names (editedplayernames), name ids, dry run, validation")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, {})

-- FC 27 layout: playernamemap (commentaryid 20 bits min -1, playerid 19 bits min -1), editedplayernames (text 45 bytes)
sim:add_table({
    name = "playernamemap", short = "VGQZ",
    fields = { { name = "commentaryid", short = "cmid", depth = 20, min = -1 }, { name = "playerid", short = "pid_", depth = 19, min = -1 } },
    rows = { { playerid = 1001, commentaryid = 930001 }, { playerid = 1002, commentaryid = 930002 },
             { playerid = 0, commentaryid = 0, __invalid = true }, { playerid = 0, commentaryid = 0, __invalid = true },
             { playerid = 0, commentaryid = 0, __invalid = true } },
})
sim:add_table({
    name = "editedplayernames", short = "nQVU",
    fields = {
        { name = "playerid", short = "pid_", depth = 19, min = -1 },
        { name = "firstname", short = "fnam", type = "string", depth = 360 },
        { name = "surname", short = "snam", type = "string", depth = 360 },
        { name = "commonname", short = "cnam", type = "string", depth = 360 },
        { name = "playerjerseyname", short = "pjnm", type = "string", depth = 360 },
    },
    rows = { { playerid = 1003, firstname = "Ali", surname = "Zed", commonname = "", playerjerseyname = "ZED" },
             { playerid = 0, firstname = "", surname = "", commonname = "", playerjerseyname = "", __invalid = true },
             { playerid = 0, firstname = "", surname = "", commonname = "", playerjerseyname = "", __invalid = true } },
})

local function run(actions, opts)
    return H.turbo().run("callnames", { actions = actions }, opts)
end

local function map_of(pid)
    local rec = sim:find_row("playernamemap", "playerid", pid)
    return rec and sim:value("playernamemap", rec, "commentaryid") or nil
end

H.case("set_playernamemap updates an existing row in place (no insert)", function()
    local inserts = sim:count_calls("InsertDBTableRow")
    local ok, msg = run({ { action = "set_playernamemap", playerid = 1002, commentaryid = 940002 } })
    H.ok(ok, msg)
    H.has(msg, "row updated")
    H.eq(map_of(1002), 940002, "commentaryid written")
    H.eq(sim:count_calls("InsertDBTableRow"), inserts, "no row inserted")
    H.eq(#sim:rows("playernamemap"), 2, "still two rows")
end)

H.case("set_playernamemap adds a row through InsertDBTableRow when the player has none", function()
    local ok, msg = run({ { action = "set_playernamemap", playerid = 1005, commentaryid = 930671 } })
    H.ok(ok, msg)
    H.has(msg, "row added")
    H.eq(map_of(1005), 930671, "new row carries the callname")
    H.eq(#sim:rows("playernamemap"), 3, "three rows now")
    local call = sim.calls.InsertDBTableRow[#sim.calls.InsertDBTableRow]
    H.eq(call[1], "playernamemap", "table name")
    H.eq(call[2].playerid, "1005", "fields are passed as text")
end)

H.case("remove_playernamemap deletes the row by its record address, idempotent", function()
    local ok, msg = run({ { action = "remove_playernamemap", playerid = 1005 } })
    H.ok(ok, msg)
    H.has(msg, "1 playernamemap row(s) removed")
    H.eq(map_of(1005), nil, "row gone")
    H.eq(#sim:rows("playernamemap"), 2, "two rows again")
    local call = sim.calls.DeleteDBTableRowByAddr[#sim.calls.DeleteDBTableRowByAddr]
    H.eq(call[1], "playernamemap", "table")
    H.ok(tostring(call[2]):match("^%d+$") ~= nil, "decimal record address")
    ok, msg = run({ { action = "remove_playernamemap", playerid = 1005 } })
    H.ok(ok, msg)
    H.has(msg, "no player-specific callname")
end)

H.case("set_display_name updates the editedplayernames row, or adds one", function()
    local ok, msg = run({ { action = "set_display_name", playerid = 1003, surname = "Zeta", commonname = "Zizi" } })
    H.ok(ok, msg)
    H.has(msg, "display name updated")
    local rec = sim:find_row("editedplayernames", "playerid", 1003)
    H.eq(sim:value("editedplayernames", rec, "surname"), "Zeta", "surname")
    H.eq(sim:value("editedplayernames", rec, "commonname"), "Zizi", "common name")
    H.eq(sim:value("editedplayernames", rec, "firstname"), "Ali", "first name kept")
    ok, msg = run({ { action = "set_display_name", playerid = 1004, firstname = "Yuri", surname = "Alberto" } })
    H.ok(ok, msg)
    H.has(msg, "display name added")
    rec = sim:find_row("editedplayernames", "playerid", 1004)
    H.ok(rec ~= nil, "row added")
    H.eq(sim:value("editedplayernames", rec, "surname"), "Alberto", "surname of the new row")
    H.eq(sim:value("editedplayernames", rec, "commonname"), "", "unset names are empty")
end)

H.case("every action is validated before the first one runs; ranges and text lengths are checked", function()
    local before = map_of(1001)
    local ok, msg = run({ { action = "set_playernamemap", playerid = 1001, commentaryid = 930009 },
                          { action = "set_playernamemap", playerid = 1002, commentaryid = 970000 } })
    H.ok(not ok, "refused")
    H.has(msg, "action 2: commentaryid must be 900000..965000")
    H.eq(map_of(1001), before, "nothing written")
    ok, msg = run({ { action = "set_playernamemap", playerid = 777777, commentaryid = 930009 } })
    H.ok(not ok, "unknown player refused")
    H.has(msg, "player 777777 not found")
    ok, msg = run({ { action = "set_display_name", playerid = 1003, surname = string.rep("x", 60) } })
    H.ok(not ok, "too long refused")
    H.has(msg, "surname accepts at most 45 bytes")
    ok, msg = run({ { action = "set_name_ids", playerid = 1003, lastnameid = 5 } })
    H.ok(not ok, "players without lastnameid refused")
    H.has(msg, "players has no lastnameid")
    ok, msg = run({ { action = "dance", playerid = 1003 } })
    H.ok(not ok, "unknown action refused")
    H.has(msg, "unknown action dance")
    ok, msg = run({})
    H.ok(ok, "no actions is fine")
    H.has(msg, "nothing to do")
end)

H.case("dry run writes nothing", function()
    local inserts, deletes = sim:count_calls("InsertDBTableRow"), sim:count_calls("DeleteDBTableRowByAddr")
    local before = map_of(1001)
    local ok, msg = run({ { action = "set_playernamemap", playerid = 1001, commentaryid = 950000 },
                          { action = "set_playernamemap", playerid = 1006, commentaryid = 950001 },
                          { action = "remove_playernamemap", playerid = 1002 },
                          { action = "set_display_name", playerid = 1006, surname = "Dry" } }, { dry = true })
    H.ok(ok, msg)
    H.has(msg, "[DRY RUN]")
    H.eq(map_of(1001), before, "unchanged")
    H.eq(map_of(1006), nil, "not added")
    H.eq(map_of(1002), 940002, "not removed")
    H.eq(sim:count_calls("InsertDBTableRow"), inserts, "no insert")
    H.eq(sim:count_calls("DeleteDBTableRowByAddr"), deletes, "no delete")
    H.eq(sim:find_row("editedplayernames", "playerid", 1006), nil, "no name row")
end)

H.case("runner script and mailbox command reach the module", function()
    H.write_config({ modules = { callnames = { actions = { { action = "set_playernamemap", playerid = 1001, commentaryid = 931000 } } } } })
    local boxes = #sim.boxes
    H.script("turbo_callnames")
    H.ok(#sim.boxes > boxes, "message box shown")
    H.eq(map_of(1001), 931000, "written by the runner script")
    local bridge = require 'imports/turbo/bridge'
    local ok, text = bridge.execute({ op = "run", module = "callnames",
        overrides = { actions = { { action = "set_playernamemap", playerid = 1001, commentaryid = 931001 } } } })
    H.ok(ok, tostring(text))
    H.eq(map_of(1001), 931001, "written by the mailbox command")
end)

-- a players table with name ids (FC 27 has them; the shared test world does not)
local sim2 = H.setup({ in_cm = false, le_27_1_2 = true })
sim2:add_table({
    name = "players", short = "plyr",
    fields = { { name = "playerid", short = "pid_", depth = 21 }, { name = "firstnameid", short = "fnid", depth = 17 },
               { name = "lastnameid", short = "lnid", depth = 17 }, { name = "commonnameid", short = "cnid", depth = 17 } },
    rows = { { playerid = 50, firstnameid = 1, lastnameid = 2, commonnameid = 0 } },
})

H.case("set_name_ids writes lastnameid / commonnameid with range checks (outside a career too)", function()
    local ok, msg = run({ { action = "set_name_ids", playerid = 50, lastnameid = 39795, commonnameid = 39795 } })
    H.ok(ok, msg)
    local rec = sim2:find_row("players", "playerid", 50)
    H.eq(sim2:value("players", rec, "lastnameid"), 39795, "lastnameid")
    H.eq(sim2:value("players", rec, "commonnameid"), 39795, "commonnameid")
    ok, msg = run({ { action = "set_name_ids", playerid = 50, lastnameid = 200000 } })
    H.ok(not ok, "out of range refused")
    H.has(msg, "outside the field range")
    H.eq(sim2:value("players", rec, "lastnameid"), 39795, "unchanged")
    ok, msg = run({ { action = "set_name_ids", playerid = 50 } })
    H.ok(not ok, "no id refused")
    H.has(msg, "no name id given")
end)

H.case("no unmapped memory reads", function() H.eq(sim2.unmapped_reads, 0) end)

H.finish()
