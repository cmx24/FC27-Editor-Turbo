package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t11 Turbo-native tools: moves for every club (squad, shirt, loan and list checks), transfer budget, game images")

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

local function on_sheet(team, pid)
    for _, rec in ipairs(sim:rows("cm_teamsheets")) do
        if sim:value("cm_teamsheets", rec, "teamid") == team then
            for i = 0, 51 do
                if sim:value("cm_teamsheets", rec, "playerid" .. i) == pid then return true end
            end
        end
    end
    return false
end

-- Turbo.dll's game call for the lists (core/moves.lua M.list): a fake with a status per player
local list_calls, list_status = {}, {}
local function fake_list_native()
    list_status = {}
    list_calls = {}
    _G.TurboTransferList = function(code, pid, club)
        list_calls[#list_calls + 1] = { code = code, pid = pid, club = club }
        local before = list_status[pid] or 0
        if code == 6 then return true, string.format("player %d status %d", pid, before), "ok", before, before end
        if code == 3 then list_status[pid] = 0; return true, "removed", "ok", before, 0 end
        return false, "unexpected action " .. code, "failed", before, before
    end
end

H.case("your club, same safe path: transfer out comes off your lists and your team sheet, transfer in joins it", function()
    fake_list_native()
    local mine = W.USER_PLAYERS[5]
    list_status[mine] = 7   -- on your transfer list
    local ok, msg = moves_run({ { action = "transfer", playerid = mine, to_teamid = 2 } })
    H.eq(ok, true, msg)
    H.has(msg, "taken off your transfer / loan list first")
    H.eq(link_team(mine), 2)
    H.eq(list_calls[1].code, 6, "status read first"); H.eq(list_calls[2].code, 3, "then the game's remove")
    H.eq(list_calls[2].club, W.USER_TEAM)
    H.eq(on_sheet(W.USER_TEAM, mine), false, "off your team sheet")
    -- an AI player into your club: club link, a free shirt number, your team sheet
    local other = ai_player(3)
    ok, msg = moves_run({ { action = "transfer", playerid = other, to_teamid = W.USER_TEAM, months = 24, wage = 900 } })
    H.eq(ok, true, msg)
    H.eq(link_team(other), W.USER_TEAM)
    H.ok(on_sheet(W.USER_TEAM, other), "on your team sheet")
    local seen = {}
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == W.USER_TEAM then
            local j = sim:value("teamplayerlinks", rec, "jerseynumber")
            H.eq(seen[j], nil, "shirt " .. j .. " used twice at your club")
            seen[j] = true
        end
    end
    -- not listed: no remove call
    list_calls = {}
    ok, msg = moves_run({ { action = "loan", playerid = W.USER_PLAYERS[6], to_teamid = 3, months = 6 } })
    H.eq(ok, true, msg)
    H.eq(#list_calls, 1, "status only")
    H.eq(sim:value("playerloans", sim:find_row("playerloans", "playerid", W.USER_PLAYERS[6]), "teamidloanedfrom"), W.USER_TEAM)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = W.USER_PLAYERS[6] } })
    H.eq(ok, true, msg)
    H.eq(link_team(W.USER_PLAYERS[6]), W.USER_TEAM)
    H.ok(on_sheet(W.USER_TEAM, W.USER_PLAYERS[6]), "back on your team sheet")
    -- a player loaned TO your club goes back to his parent club (7)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = W.LOANED_IN } })
    H.eq(ok, true, msg)
    H.eq(link_team(W.LOANED_IN), 7)
    H.eq(sim:find_row("playerloans", "playerid", W.LOANED_IN), nil, "loan row deleted")
    -- release and delete one of your players
    ok, msg = moves_run({ { action = "delete", playerid = W.USER_PLAYERS[7], confirm = true } })
    H.eq(ok, true, msg)
    H.eq(sim:find_row("players", "playerid", W.USER_PLAYERS[7]), nil, "deleted")
    H.eq(on_sheet(W.USER_TEAM, W.USER_PLAYERS[7]), false)
    _G.TurboTransferList = nil
end)

H.case("your club: a failed remove from your list stops the move before anything is written", function()
    _G.TurboTransferList = function(code, pid, club)
        if code == 6 then return true, "status", "ok", 9, 9 end
        return false, "the game refused", "failed", 9, 9
    end
    local mine = W.USER_PLAYERS[8]
    local snap = db_bytes()
    local ok, msg = moves_run({ { action = "release", playerid = mine } })
    H.eq(ok, false, msg); H.has(msg, "the game's remove failed")
    H.eq(changed_since(snap), 0, "nothing written")
    H.eq(link_team(mine), W.USER_TEAM)
    _G.TurboTransferList = nil
end)

H.case("squad rules: full squad, minimum squad, last goalkeeper, loans; a refusal writes nothing", function()
    local moves = require 'imports/turbo/core/moves'
    local snap = db_bytes()
    -- 1001 is your only goalkeeper (world.lua: preferredposition1 0)
    local ok, msg = moves.transfer(W.USER_PLAYERS[1], 2, {}, false)
    H.eq(ok, false); H.has(msg, "only goalkeeper")
    -- the club he joins is full
    local max0, min0 = moves.MAX_SQUAD, moves.MIN_SQUAD
    moves.MAX_SQUAD = moves.squad_size(4)
    ok, msg = moves.transfer(ai_player(2), 4, {}, false)
    H.eq(ok, false); H.has(msg, "the most a squad holds")
    ok, msg = moves.loan(W.USER_PLAYERS[9], 4, 6, false)
    H.eq(ok, false); H.has(msg, "the most a squad holds")
    moves.MAX_SQUAD = max0
    -- a match squad never drops below the minimum
    moves.MIN_SQUAD = moves.squad_size(W.USER_TEAM)
    ok, msg = moves.release(W.USER_PLAYERS[9], false)
    H.eq(ok, false); H.has(msg, "fewer than")
    ok, msg = moves.delete(W.USER_PLAYERS[9], false)
    H.eq(ok, false); H.has(msg, "fewer than")
    moves.MIN_SQUAD = min0
    H.eq(changed_since(snap), 0, "nothing written by a refused move")
    -- a free agent cannot be loaned; Free Agents is not a loan club
    local fa = ai_player(9)
    ok, msg = moves.release(fa, false)
    H.eq(ok, true, msg)
    ok, msg = moves.loan(fa, 3, 6, false)
    H.eq(ok, false); H.has(msg, "free agent")
    ok, msg = moves.loan(ai_player(10), moves.FREE_AGENTS, 6, false)
    H.eq(ok, false); H.has(msg, "not a club")
    -- a loaned player: loaned again is refused; a transfer ends the loan (his parent club sells)
    local pid = ai_player(11)
    ok, msg = moves.loan(pid, 12, 6, false)
    H.eq(ok, true, msg)
    ok, msg = moves.loan(pid, 13, 6, false)
    H.eq(ok, false); H.has(msg, "already on loan")
    ok, msg = moves.transfer(pid, 13, {}, false)
    H.eq(ok, true, msg); H.has(msg, "loan from 11 ended")
    H.eq(link_team(pid), 13)
    H.eq(sim:find_row("playerloans", "playerid", pid), nil, "loan row deleted")
end)

H.case("your team sheet: a starter who leaves is replaced by the first substitute, the other starters keep their slots", function()
    local function sheet()
        for _, rec in ipairs(sim:rows("cm_teamsheets")) do
            if sim:value("cm_teamsheets", rec, "teamid") == W.USER_TEAM then
                local ids = {}
                for i = 0, 51 do ids[i] = sim:value("cm_teamsheets", rec, "playerid" .. i) end
                return ids
            end
        end
    end
    local before = sheet()
    local leaving = before[2]
    local ok, msg = moves_run({ { action = "transfer", playerid = leaving, to_teamid = 2 } })
    H.eq(ok, true, msg)
    H.has(msg, "list status not read")   -- no Turbo GUI in this test: said in the summary
    local after = sheet()
    H.eq(after[2], before[11], "the first substitute takes his slot")
    H.eq(after[11], before[12], "the bench moves up")
    for _, i in ipairs({ 0, 1, 3, 4, 10 }) do H.eq(after[i], before[i], "starter slot " .. i .. " kept") end
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
