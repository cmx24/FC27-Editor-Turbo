package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t10 FC 27 Live Editor v27.1.2 API: wrappers without natives, placeholders, database fallbacks, self-test")

-- A Live Editor with exactly the natives FC 27 LE v27.1.2 has (globals dump from the game, 02-10-2026)
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, {})
local env = require 'imports/turbo/core/env'

H.case("v27.1.2: wrappers whose c-native is missing are reported, real natives are usable", function()
    local fn, why = env.api("TransferPlayer")
    H.eq(fn, nil, "TransferPlayer")
    H.has(why, "not available in this Live Editor build")
    H.has(why, "cTransferPlayer")
    fn, why = env.api("IsPlayerTransferListed")
    H.eq(fn, nil); H.has(why, "cIsPlayerTransferListed")
    fn, why = env.api("GetTransferBudget")
    H.eq(fn, nil, "deprecated stub"); H.has(why, "placeholder")
    H.ok(env.api("SetPlayerForm"), "a native is usable")
    H.ok(env.api("GetDBTableRows"), "a native is usable")
    fn, why = env.api("NoSuchFunction")
    H.eq(fn, nil); H.has(why, "NoSuchFunction is not available in this Live Editor build")
    H.ok(env.is_unavailable_message(why))
    H.eq(env.is_unavailable_message("player 5 not found"), false)
end)

H.case("v27.1.2: SetSquadRole is the FC 27 LE 'TODO' placeholder", function()
    require 'imports/career_mode/helpers'
    H.ok(type(_G["SetSquadRole"]) == "function", "LE defines it")
    local fn, why = env.api("SetSquadRole")
    H.eq(fn, nil); H.has(why, "placeholder")
end)

H.case("v27.1.2: transfer-list actions are refused before anything runs (no 'attempt to call a nil value')", function()
    local ok, msg = H.turbo().run("player_moves", { actions = { { action = "transfer_list", playerid = W.USER_PLAYERS[3] } } })
    H.eq(ok, false)
    H.has(msg, "AddPlayerToTransferList is not available in this Live Editor build")
    H.has(msg, "cAddPlayerToTransferList")
    H.eq(next(sim.transfer_listed), nil, "nothing listed")
end)

H.case("v27.1.2: the tools this build cannot run are published for the Turbo window", function()
    local caps = require 'imports/turbo/core/caps'
    local u = caps.unavailable()
    for _, k in ipairs({ "transfer_bans", "development_xp", "move_transfer_list", "move_loan_list", "move_unlist" }) do
        H.ok(u[k], k .. " unavailable")
    end
    -- Turbo's development works through the two development-plan natives v27.1.2 ships
    H.eq(u.development, nil, "development available through the plan natives")
    -- done by Turbo itself in 0.3.0 (core/budget.lua, core/moves.lua): not greyed out
    for _, k in ipairs({ "transfer_budget", "delete_players", "move_transfer", "move_loan", "move_release",
                         "move_terminate_loan" }) do
        H.eq(u[k], nil, k .. " available through Turbo")
    end
    H.eq(u.form_morale, nil, "form/morale natives exist")
    local bridge = require 'imports/turbo/bridge'
    local st = bridge.collect_state()
    H.ok(st.unavailable and st.unavailable.transfer_bans, "in bridge state")
    H.eq(st.unavailable.transfer_budget, nil)
    H.has(st.unavailable.transfer_bans, "cGetTransferBans")
end)

H.case("v27.1.2: your squad includes cm_teamsheets.playerid0 (FC 27 LE's own helper skips it)", function()
    local game = require 'imports/turbo/core/game'
    local squad, count, source = game.user_squad()
    H.eq(source, "cm_teamsheets")
    H.eq(count, #W.USER_PLAYERS, "whole squad")
    H.ok(squad[W.USER_PLAYERS[1]], "playerid0 player")
    local le = _G["GetUserSeniorTeamPlayerIDs"]()
    H.eq(le[W.USER_PLAYERS[1]], nil, "LE's helper misses it (why Turbo has its own)")
end)

H.case("v27.1.2: without GetPlayersStats the database's league numbers are exported, labelled as such", function()
    local ok, msg = H.turbo().run("export_season_stats")
    H.eq(ok, true, msg)
    H.has(msg, "not live season statistics")
    H.has(msg, "turbo_database_league_stats_")
    local path = msg:match("saved to (.-%.csv)")
    local lines = H.csv_lines(path)
    -- every third player id has played (world.lua)
    local played = 0
    local rows = _G["GetDBTableRows"]("teamplayerlinks")
    for _, r in ipairs(rows) do if tonumber(r.leagueappearances.value) > 0 then played = played + 1 end end
    H.ok(played > 0, "world has stats")
    H.eq(#lines - 1, played, "one line per club link with stats")
    H.has(lines[1], "appearances")
    ok, msg = H.turbo().run("export_season_stats", { only_user_team = true })
    H.eq(ok, true, msg)
    local mine = 0
    for _, p in ipairs(W.USER_PLAYERS) do if p % 3 == 0 then mine = mine + 1 end end
    lines = H.csv_lines(msg:match("saved to (.-%.csv)"))
    H.eq(#lines - 1, mine, "only my club")
end)

H.case("v27.1.2: generated players are counted, and only deleted with confirm (Turbo's own delete)", function()
    local ok, msg = H.turbo().run("delete_generated_players", { min_playerid = 460000, confirm = false })
    H.eq(ok, true, msg)
    H.has(msg, #W.GENERATED .. " generated players found")
    H.ok(sim:find_row("players", "playerid", W.GENERATED[1]), "nobody deleted without confirm")
end)

H.case("v27.1.2: the self-test skips what this build cannot do and fails nothing else", function()
    local snapshot = {}
    for k, v in pairs(sim.mem) do snapshot[k] = v end
    H.script("turbo_selftest")
    local text = H.read(H.out("turbo_selftest.log"))
    H.ok(text, "log written")
    if os.getenv("TURBO_SHOW_SELFTEST") then print(text) end
    H.has(text, " 0 failed")
    H.has(text, "OK   budget restored")
    H.has(text, "SKIP transfer list + unlist (real)")
    H.has(text, "SKIP list transfer bans")
    local skipped = tonumber(text:match("(%d+) skipped"))
    H.ok(skipped and skipped >= 3, "skipped " .. tostring(skipped))
    local box = sim.boxes[#sim.boxes]
    H.has(box.text, "skipped")
    local changed = 0
    for _, t in pairs(sim.tables) do
        for a = t.first, t.first + t.rec_size * t.n - 1 do
            if sim.mem[a] ~= snapshot[a] then changed = changed + 1 end
        end
    end
    H.eq(changed, 0, "database bytes changed by the self-test")
end)

-- Full simulator (all natives): the same wrappers are usable; placeholders are found by reading the source
local sim2, le = H.setup({ in_cm = true })
W.build(sim2, {})
env = require 'imports/turbo/core/env'

H.case("all natives: wrappers are usable", function()
    H.ok(env.api("TransferPlayer"), "TransferPlayer")
    H.ok(env.api("AddPlayerToTransferList"), "AddPlayerToTransferList")
    local caps = require 'imports/turbo/core/caps'
    local u = caps.unavailable()
    H.eq(u.move_transfer_list, nil); H.eq(u.move_transfer, nil); H.eq(u.transfer_budget, nil); H.eq(u.transfer_bans, nil)
    -- job_offer needs Turbo.dll's own game-call native (TurboJobOfferCreate), never a Live Editor one
    H.ok(u.job_offer, "job_offer waits for Turbo.dll")
    H.ok(u.reveal, "reveal waits for Turbo.dll")
    H.ok(u.manager_rules, "manager_rules waits for Turbo.dll")
    u.job_offer, u.reveal, u.manager_rules = nil, nil, nil
    H.eq(next(u), nil, "every tool available with all natives")
end)

H.case("placeholder and wrapper detection reads only Live Editor's own library files", function()
    local dir = le .. "/lua/libs/v2/imports/fake"
    os.execute(string.format("mkdir -p '%s'", dir))
    local f = io.open(dir .. "/api.lua", "w")
    f:write([[
function FakeWorking(a)
    -- TODO: comments do not count
    return cTransferPlayer(a, 0, 1, 0, -1, 0, 12)
end

function FakePlaceholder()
    print("NOT IMPLEMENTED FakePlaceholder")
end

function FakeMissing(id)
    return cFakeNativeThatDoesNotExist(id)
end
]])
    f:close()
    dofile(dir .. "/api.lua")
    H.ok(env.api("FakeWorking"), "working wrapper")
    local fn, why = env.api("FakePlaceholder")
    H.eq(fn, nil); H.has(why, "placeholder")
    fn, why = env.api("FakeMissing")
    H.eq(fn, nil); H.has(why, "cFakeNativeThatDoesNotExist")
    -- the same code outside lua\libs is taken as it is
    local g = io.open(le .. "/elsewhere.lua", "w")
    g:write("function FakeOutside() return cFakeNativeThatDoesNotExist() end\n")
    g:close()
    dofile(le .. "/elsewhere.lua")
    H.ok(env.api("FakeOutside"), "not inspected")
end)

-- FC 27 transfer history: linked lists in the TransferManager (no FC 26 storage)
local sim3 = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim3, { storage_off = false, fc27_transfer_lists = true, fixtures_off = false, fc27_user_fixtures = true })

H.case("FC 27 transfer history: completed transfers and loans from the TransferManager lists, open offers left out", function()
    local ok, msg = H.turbo().run("export_transfer_history")
    H.eq(ok, true, msg)
    H.has(msg, "5 moves")
    H.has(msg, "transfers at TransferManager+0x2998")
    H.has(msg, "loans at TransferManager+0x29D8")
    local lines = H.csv_lines(msg:match("saved to (.-%.csv)"))
    H.eq(#lines, 6, "header + 5")
    H.has(lines[1], "type,date,playerid")
    local text = table.concat(lines, "\n")
    H.has(text, "transfer,20260702,2001,")
    H.has(text, ",5,") -- from team 5
    H.has(text, "4250000")
    H.has(text, "loan,20260708,2013,")
    H.eq(text:find("2003", 1, true), nil, "open offer left out")
    H.eq(text:find(",7500000", 1, true), nil, "open offer fee left out")
    -- remembered for the next run
    local calib = H.read(H.out("turbo_calibration.json")) or ""
    H.has(calib, "transfer_history_fc27")
end)

H.case("FC 27 transfer history: a broken list is not exported", function()
    local tm = W.tm
    local first = sim3:r64(tm + 0x2998)
    sim3:w64(first + 0x08, 0x12345678)   -- prev pointer no longer points back at the head
    os.remove(H.out("turbo_calibration.json"))
    local ok, msg = H.turbo().run("export_transfer_history")
    H.eq(ok, true, msg)
    H.has(msg, "2 moves")
    H.eq(msg:find("transfers at", 1, true), nil, "transfers list rejected")
end)

H.case("FC 27 fixtures: your club's remaining fixtures from the MainHubManager list", function()
    local ok, msg = H.turbo().run("export_fixtures")
    H.eq(ok, true, msg)
    H.has(msg, "3 upcoming fixtures of your club")
    H.has(msg, "MainHubManager+0x68")
    local lines = H.csv_lines(msg:match("saved to (.-%.csv)"))
    H.eq(#lines, 4, "header + 3")
    H.has(lines[2], "20260905,1400,5,")
    H.has(lines[3], "20260913,1130,1,")
    H.has(H.read(H.out("turbo_calibration.json")) or "", "fixtures_fc27")
end)

H.case("FC 27 fixtures: an entry that is not a real fixture stops the export", function()
    sim3:w32(W.hub_entries + 0x130 + 0x38, 20261399)   -- not a date
    os.remove(H.out("turbo_calibration.json"))
    local ok, msg = H.turbo().run("export_fixtures")
    H.eq(ok, false)
    H.has(msg, "fixture list was not found")
    sim3:w32(W.hub_entries + 0x130 + 0x38, 20260913)
end)

H.finish()
