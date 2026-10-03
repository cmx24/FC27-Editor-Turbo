package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t11 Turbo-native tools (0.3.0): own-club safety gate, moves, transfer budget, game images")

-- FC 27 LE v27.1.2: none of the move / budget natives exist, so Turbo's own implementations run
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local function link_team(pid)
    local rec = sim:find_row("teamplayerlinks", "playerid", pid)
    return rec and sim:value("teamplayerlinks", rec, "teamid") or nil
end

-- a player of another club (not yours, not Free Agents)
local function ai_player(team)
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == team then
            local pid = sim:value("teamplayerlinks", rec, "playerid")
            local loans = sim.tables.playerloans and sim:find_row("playerloans", "playerid", pid)
            if not loans and pid < 460000 then return pid end
        end
    end
    error("no player at team " .. team)
end

local function db_bytes()
    local snap = {}
    for _, t in pairs(sim.tables) do
        for a = t.first, t.first + t.rec_size * t.n - 1 do snap[a] = sim.mem[a] end
    end
    return snap
end

local function changed_since(snap)
    local n = 0
    for a, v in pairs(snap) do if sim.mem[a] ~= v then n = n + 1 end end
    return n
end

local function moves_run(actions)
    return H.turbo().run("player_moves", { actions = actions })
end

H.case("safety: no move into or out of your club, and nothing is written", function()
    local mine = W.USER_PLAYERS[5]
    local other = ai_player(2)
    local snap = db_bytes()
    local cases = {
        { action = "transfer", playerid = mine, to_teamid = 2 },
        { action = "transfer", playerid = other, to_teamid = W.USER_TEAM },
        { action = "loan", playerid = mine, to_teamid = 3, months = 6 },
        { action = "loan", playerid = other, to_teamid = W.USER_TEAM, months = 6 },
        { action = "release", playerid = mine },
        { action = "delete", playerid = mine, confirm = true },
    }
    for _, a in ipairs(cases) do
        local ok, msg = moves_run({ a })
        H.eq(ok, false, a.action .. " " .. tostring(msg))
        H.has(msg, "your own club")
    end
    H.eq(changed_since(snap), 0, "database bytes changed")
    H.eq(link_team(mine), W.USER_TEAM)
    H.eq(link_team(other), 2)
    H.ok(sim:find_row("players", "playerid", mine), "not deleted")
end)

H.case("safety: ending the loan of a player loaned TO your club is refused (his club link would move)", function()
    local ok, msg = moves_run({ { action = "terminate_loan", playerid = W.LOANED_IN } })
    H.eq(ok, false, msg)
    H.has(msg, "your own club")
    H.ok(sim:find_row("playerloans", "playerid", W.LOANED_IN), "loan row kept")
end)

H.case("safety: outside a career (club unknown) every Turbo move is refused", function()
    sim.in_cm = false
    local moves = require 'imports/turbo/core/moves'
    local ok, msg = moves.transfer(ai_player(2), 3, {}, true)
    H.eq(ok, false); H.has(msg, "load a career")
    sim.in_cm = true
end)

H.case("transfer between two other clubs: club link, shirt number, contract", function()
    local pid = ai_player(2)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 3, months = 24, wage = 1200 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 3)
    local prec = sim:find_row("players", "playerid", pid)
    H.ok(sim:value("players", prec, "contractvaliduntil") >= 2027, "contract end set")
    -- shirt number unique at the new club
    local seen = {}
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == 3 then
            local j = sim:value("teamplayerlinks", rec, "jerseynumber")
            H.eq(seen[j], nil, "shirt " .. j .. " used twice")
            seen[j] = true
        end
    end
end)

H.case("loan between two other clubs, then end it: playerloans row added and removed", function()
    local pid = ai_player(4)
    local ok, msg = moves_run({ { action = "loan", playerid = pid, to_teamid = 5, months = 6 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 5)
    local lrec = sim:find_row("playerloans", "playerid", pid)
    H.ok(lrec, "loan row")
    H.eq(sim:value("playerloans", lrec, "teamidloanedfrom"), 4)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = pid } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 4)
    H.eq(sim:find_row("playerloans", "playerid", pid), nil, "loan row deleted")
end)

H.case("release and delete of another club's player", function()
    local pid = ai_player(6)
    local ok, msg = moves_run({ { action = "release", playerid = pid } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 111592)
    ok, msg = moves_run({ { action = "delete", playerid = pid } })
    H.eq(ok, false); H.has(msg, "confirm")
    ok, msg = moves_run({ { action = "delete", playerid = pid, confirm = true } })
    H.eq(ok, true, msg)
    H.eq(sim:find_row("players", "playerid", pid), nil, "players row gone")
    H.eq(sim:find_row("teamplayerlinks", "playerid", pid), nil, "club link gone")
end)

H.case("dry run writes nothing", function()
    local pid = ai_player(7)
    local snap = db_bytes()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 8 } })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg)
    H.eq(changed_since(snap), 0, "bytes changed in a dry run")
end)

H.case("delete generated players keeps the ones in your club", function()
    -- move one generated player into your club, as the youth academy would
    local g = W.GENERATED[1]
    local rec = sim:find_row("teamplayerlinks", "playerid", g)
    sim:set_field(rec, sim.tables.teamplayerlinks.teamid, W.USER_TEAM)
    local ok, msg = H.turbo().run("delete_generated_players", { min_playerid = 460000, confirm = false })
    H.eq(ok, true, msg)
    H.has(msg, (#W.GENERATED - 1) .. " generated players found")
    H.has(msg, "1 in your club are kept")
    ok, msg = H.turbo().run("delete_generated_players", { min_playerid = 460000, confirm = true })
    H.eq(ok, true, msg)
    H.ok(sim:find_row("players", "playerid", g), "your generated player kept")
    for i = 2, #W.GENERATED do
        H.eq(sim:find_row("players", "playerid", W.GENERATED[i]), nil, "generated " .. W.GENERATED[i] .. " deleted")
    end
end)

H.case("transfer budget: read, set, add, range; both copies written", function()
    local ok, msg = H.turbo().run("transfer_budget", { mode = "get" })
    H.eq(ok, true, msg); H.has(msg, "81497280")
    ok, msg = H.turbo().run("transfer_budget", { mode = "add", amount = 10000000 })
    H.eq(ok, true, msg)
    H.eq(sim:r32(W.BUDGET_A), 91497280); H.eq(sim:r32(W.BUDGET_B), 91497280)
    ok, msg = H.turbo().run("transfer_budget", { mode = "set", amount = 3000000000 })
    H.eq(ok, false, msg)
    H.eq(sim:r32(W.BUDGET_A), 91497280, "unchanged after a refused value")
    ok, msg = H.turbo().run("transfer_budget", { mode = "set", amount = 81497280 })
    H.eq(ok, true, msg)
    H.eq(sim:r32(W.BUDGET_B), 81497280)
end)

H.case("transfer budget: an entry for another club is never written", function()
    local budget = require 'imports/turbo/core/budget'
    sim:w32(W.BUDGET_A + 0x10 + 0x28, 2)   -- E + 0x28 now says club 2
    local v, err = budget.get()
    H.eq(v, nil); H.has(err, "no finance entry for club 1")
    local ok = budget.set(5)
    H.eq(ok, false)
    H.eq(sim:r32(W.BUDGET_A), 81497280)
    sim:w32(W.BUDGET_A + 0x10 + 0x28, W.USER_TEAM)
end)

H.case("game images: wanted files are exported, missing ones listed, bad paths ignored", function()
    local legacy = require 'imports/turbo/core/legacy'
    local dir = legacy.dir()
    os.execute(string.format("mkdir -p '%s'", dir))
    sim.legacy_files["data/ui/imgAssets/heads/p1001.dds"] = "DDS one"
    sim.legacy_files["data/ui/imgAssets/tattoo/item_7_0.dds"] = "DDS two"
    local f = io.open(dir .. "/want.txt", "wb")
    f:write("#gen 5\ndata/ui/imgAssets/heads/p1001.dds\ndata/ui/imgAssets/heads/p9.dds\n../secret.txt\n" ..
            "data/../x.dds\nC:/x.dds\ndata/ui/imgAssets/tattoo/item_7_0.dds\n")
    f:close()
    local e, m, w = legacy.pump(5)
    H.eq(e, 2); H.eq(m, 1); H.eq(w, 0)
    H.eq(H.read(dir .. "/data/ui/imgAssets/heads/p1001.dds"), "DDS one")
    H.has(H.read(dir .. "/missing.txt"), "data/ui/imgAssets/heads/p9.dds")
    H.has(H.read(dir .. "/status.txt"), "exported 2 missing 1")
    H.eq(sim:count_calls("LegacyFileExport"), 2, "bad paths never reach Live Editor")
    -- the same list again: nothing new is asked
    e, m = legacy.pump(5)
    H.eq(e, 0); H.eq(m, 0)
    H.eq(sim:count_calls("LegacyFileExport"), 2)
    -- turbo_images.lua reports through a message box
    local before = #sim.boxes
    H.script("turbo_images")
    H.ok(#sim.boxes > before, "message box")
    H.has(sim.boxes[#sim.boxes].text, "Images exported")
end)

H.case("game images: time budget leaves the rest waiting", function()
    local legacy = require 'imports/turbo/core/legacy'
    local dir = legacy.dir()
    local lines = { "#gen 6" }
    for i = 1, 20 do
        local p = string.format("data/ui/imgAssets/heads/p%d.dds", 5000 + i)
        sim.legacy_files[p] = "x"
        lines[#lines + 1] = p
    end
    local f = io.open(dir .. "/want.txt", "wb"); f:write(table.concat(lines, "\n")); f:close()
    local e, _, w = legacy.pump(0)
    H.eq(e, 0); H.eq(w, 20, "nothing exported with no time")
    e, _, w = legacy.pump(5)
    H.eq(e, 20); H.eq(w, 0)
end)

H.case("game images: without LegacyFileExport the reason is given", function()
    local legacy = require 'imports/turbo/core/legacy'
    local saved = LegacyFileExport
    LegacyFileExport = nil
    local e, err = legacy.pump(1)
    LegacyFileExport = saved
    H.eq(e, nil); H.has(err, "LegacyFileExport")
end)

H.finish()
