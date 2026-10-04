package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t14 transfer / loan lists (Turbo.dll game call), Live Editor's list natives on top of it, transfer bans status")

-- FC 27 LE v27.1.2: the list natives are missing, as in the game
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local OP, STATUS, SEQ, RSEQ, ARGS, OUT, TEXT = 0x2020, 0x2024, 0x2028, 0x202C, 0x2030, 0x2050, 0x2060
local NAME = { [0] = "not listed", [7] = "transfer listed", [8] = "loan listed", [9] = "transfer and loan listed" }

-- A fake Turbo.dll + game: op 10 runs the list action on a contract-status table the way the game's helper does
-- (docs/re/transfer_lists.md): add 0 -> 7 / 8 (9 with the other list), remove clears both lists
local dll = { calls = {}, status = {}, user_team = W.USER_TEAM, answer = nil }
local function fake_turbo_game_call()
    local mb = TURBO_STATE.bridge.mailbox
    local seq = sim:r32(mb + SEQ)
    local c = { op = sim:r32(mb + OP), seq = seq, action = sim:r64(mb + ARGS), pid = sim:r64(mb + ARGS + 8),
                comm = sim:r64(mb + ARGS + 16), club = sim:r64(mb + ARGS + 24) }
    dll.calls[#dll.calls + 1] = c
    local st, text, before, after = 1, "", 0, 0
    if dll.answer then
        st, text, before, after = dll.answer.status, dll.answer.text, dll.answer.out0 or 0, dll.answer.out1 or 0
    elseif c.op ~= 10 then
        st, text = -1, "unknown game call op " .. c.op
    else
        before = dll.status[c.pid] or 0
        after = before
        if c.action ~= 6 and c.club ~= dll.user_team then
            st, text = -1, string.format("player %d plays for team %d, not your club (team %d): the game lists only your own players",
                c.pid, c.club, dll.user_team)
        elseif c.action == 1 then after = (before == 8) and 9 or 7
        elseif c.action == 2 then after = (before == 7) and 9 or 8
        elseif c.action >= 3 and c.action <= 5 then
            if before == 0 then st, text = -1, string.format("player %d is not on the transfer list or the loan list", c.pid) end
            after = 0
        end
        if st == 1 then
            dll.status[c.pid] = after
            text = c.action == 6 and string.format("player %d is %s", c.pid, NAME[before])
                or string.format("player %d: done, he is now %s", c.pid, NAME[after])
        end
    end
    sim:wstr(mb + TEXT, text)
    sim:w64(mb + OUT, before)
    sim:w64(mb + OUT + 8, after)
    sim:w32(mb + RSEQ, seq)
    sim:w32(mb + STATUS, st)
end

local moves = require 'imports/turbo/core/moves'
local caps = require 'imports/turbo/core/caps'
local env = require 'imports/turbo/core/env'
local bridge = require 'imports/turbo/bridge'
local function run(actions) return H.turbo().run("player_moves", { actions = actions }) end
-- one of your players, and a player of team 2 (world.lua: the other clubs' players are 2001, 2002, ...)
local mine, other = W.USER_PLAYERS[3], 2001

H.case("without Turbo.dll's game call: refused with the reason, the window greys the buttons out", function()
    H.ok(other ~= nil, "a player of another club in the world")
    local ok, msg = run({ { action = "transfer_list", playerid = mine } })
    H.eq(ok, false)
    H.has(msg, "cAddPlayerToTransferList")
    H.has(msg, "Turbo.dll game call")
    ok, msg = moves.list(mine, "transfer_list")
    H.eq(ok, false); H.has(msg, "TurboTransferList")
    local u = caps.unavailable()
    for _, k in ipairs({ "move_transfer_list", "move_loan_list", "move_unlist", "move_list_status" }) do
        H.ok(u[k], k .. " unavailable"); H.has(u[k], "Turbo.dll's game call provides it")
    end
    H.has(u.transfer_bans, "cGetTransferBans"); H.has(u.transfer_bans, "no transfer-ban list")
end)

H.case("bridge.install_natives defines TurboTransferList and Live Editor's missing list natives; the caps light up", function()
    TURBO_STATE.bridge.next_dll_check = 0
    H.ok(bridge.mailbox_address() ~= nil, "mailbox found through bridge_dll.json")
    local real_loadlib = package.loadlib
    package.loadlib = function(path, sym)
        if sym == "turbo_game_call" then return fake_turbo_game_call end
        if sym == "turbo_game_pump" then return function() end end
        return nil
    end
    H.eq(env.api("AddPlayerToTransferList"), nil, "LE's wrapper unusable before (verdict cached)")
    H.eq(bridge.install_natives(), true)
    H.eq(type(_G.TurboTransferList), "function")
    for _, n in ipairs({ "cAddPlayerToTransferList", "cAddPlayerToLoanList", "cRemovePlayerFromLists", "cRemovePlayerFromTransferList",
                         "cRemovePlayerFromLoanList", "cIsPlayerTransferListed", "cIsPlayerLoanListed" }) do
        H.eq(type(_G[n]), "function", n .. " defined")
    end
    H.ok(env.api("AddPlayerToTransferList"), "LE's wrapper usable now (cache reset)")
    local u = caps.unavailable()
    for _, k in ipairs({ "move_transfer_list", "move_loan_list", "move_unlist", "move_list_status" }) do H.eq(u[k], nil, k .. " available") end
    H.ok(u.transfer_bans, "transfer bans stay unavailable")
    local st = bridge.collect_state()
    H.eq(st.unavailable.move_transfer_list, nil, "bridge_state no longer lists it")
    package.loadlib = real_loadlib
end)

H.case("list one of your players: the game call gets op 10 with action, player, comm service and his club", function()
    local n0 = #dll.calls
    local ok, msg = run({ { action = "transfer_list", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "transfer listed")
    H.eq(#dll.calls, n0 + 1)
    local c = dll.calls[#dll.calls]
    H.eq(c.op, 10); H.eq(c.action, 1); H.eq(c.pid, mine); H.eq(c.club, W.USER_TEAM)
    H.eq(c.comm, sim.plugins[0x1297f047], "comm service passed")
    ok, msg = run({ { action = "loan_list", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "transfer and loan listed")
    local okm, text, status, before, after = moves.list(mine, "list_status")
    H.eq(okm, true, text); H.eq(status, "ok"); H.eq(before, 9); H.eq(after, 9)
    ok, msg = run({ { action = "list_status", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "transfer and loan listed")
    ok, msg = run({ { action = "unlist", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "not listed")
    H.eq(dll.status[mine], 0)
end)

H.case("refused before the game is called: another club's player, a loaned-in player, no career club", function()
    local n0 = #dll.calls
    local ok, msg = run({ { action = "transfer_list", playerid = other } })
    H.eq(ok, false); H.has(msg, "not your club")
    ok, msg = run({ { action = "loan_list", playerid = W.LOANED_IN } })
    H.eq(ok, false); H.has(msg, "on loan")
    ok, msg = moves.list(mine, "nonsense")
    H.eq(ok, false); H.has(msg, "unknown list action")
    ok, msg = moves.list(-4, "transfer_list")
    H.eq(ok, false); H.has(msg, "positive")
    local saved = moves.user_team
    moves.user_team = function() return 0 end
    ok, msg = moves.list(mine, "transfer_list")
    H.eq(ok, false); H.has(msg, "load a career")
    moves.user_team = saved
    H.eq(#dll.calls, n0, "the game was not called")
    -- the status query reads any player (nothing changes)
    local okq, text = moves.list(other, "list_status")
    H.eq(okq, true, text); H.has(text, "not listed")
    H.eq(#dll.calls, n0 + 1)
    -- dry run: checked, not called
    H.write_config({ turbo = { dry_run = true } })
    ok, msg = run({ { action = "transfer_list", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "dry run")
    H.eq(#dll.calls, n0 + 1, "dry run: no call")
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("the game's answer is reported: refusal text, queued, a selection of your squad", function()
    local ok, msg = run({ { action = "unlist", playerid = mine } })
    H.eq(ok, false); H.has(msg, "not on the transfer list or the loan list")
    dll.answer = { status = -1, text = "the game refused: player stayed not listed" }
    ok, msg = run({ { action = "transfer_list", playerid = mine } })
    H.eq(ok, false); H.has(msg, "the game refused")
    dll.answer = { status = 2, text = "queued for the game thread: it runs at the next game tick" }
    ok, msg = run({ { action = "transfer_list", playerid = mine } })
    H.eq(ok, true, msg); H.has(msg, "queued")
    -- the queued call finishes on the next event
    sim:w32(TURBO_STATE.bridge.mailbox + STATUS, 1)
    H.eq(bridge.check_game_call(), true)
    dll.answer = nil
    dll.status[mine] = 0
    -- a selection (scope + filters): every player of your squad that matches is listed, one game call each
    local n0 = #dll.calls
    ok, msg = run({ { action = "loan_list", scope = { user_team = true }, filters = { max_overall = 63 } } })
    H.eq(ok, true, msg)
    local listed = 0
    for i = n0 + 1, #dll.calls do
        if dll.calls[i].action == 2 then listed = listed + 1 end
    end
    H.ok(listed >= 1, "at least one player loan-listed")
    H.has(msg, string.format("loan_list x%d", listed))
end)

H.case("Live Editor's own wrappers work again on top of the game call (scripts written for FC 26 Live Editor)", function()
    local p = W.USER_PLAYERS[6]
    dll.status[p] = 0
    AddPlayerToTransferList(p)
    H.eq(dll.status[p], 7, "listed through LE's wrapper")
    H.eq(IsPlayerTransferListed(p), true)
    H.eq(IsPlayerLoanListed(p), false)
    AddPlayerToLoanList(p, W.USER_TEAM)
    H.eq(dll.status[p], 9)
    H.eq(IsPlayerLoanListed(p), true)
    RemovePlayerFromLists(p)
    H.eq(dll.status[p], 0)
    H.eq(IsPlayerTransferListed(p), false)
    -- another club's player: the wrapper does nothing (refused before the call)
    local n0 = #dll.calls
    AddPlayerToTransferList(other)
    H.eq(#dll.calls, n0)
    -- a native Live Editor ships itself is never replaced
    local mine_fn = function() return "le" end
    _G.cAddPlayerToTransferList = mine_fn
    local names = moves.install_le_natives()
    H.eq(_G.cAddPlayerToTransferList, mine_fn, "LE's own native kept")
    for _, n in ipairs(names) do H.ok(n ~= "cAddPlayerToTransferList", "not redefined") end
end)

H.case("transfer bans of one club / one player: refused in FC 27 (no ban natives, no game ban list) with the reason", function()
    for _, mode in ipairs({ "ban_team", "unban_team", "ban_player", "unban_player" }) do
        local ok, msg = H.turbo().run("transfer_bans", { mode = mode, id = 2, ban_until = 20990101 })
        H.eq(ok, false, mode); H.has(msg, "not available in this Live Editor build")
    end
    local ok, msg = H.turbo().run("transfer_bans", { mode = "nope" })
    H.eq(ok, false); H.has(msg, "ban_player")
end)

H.case("transfer bans of one club / one player with a Live Editor that ships the ban natives (FC 26 parity)", function()
    local s2 = H.setup({ in_cm = true })
    W.build(s2, {})
    local ok, msg = H.turbo().run("transfer_bans", { mode = "ban_team", id = 2, ban_until = 20990101 })
    H.eq(ok, true, msg); H.has(msg, "team"); H.has(msg, "20990101")
    ok, msg = H.turbo().run("transfer_bans", { mode = "ban_player", id = 2001, ban_until = 20280630 })
    H.eq(ok, true, msg); H.has(msg, "player")
    H.eq(s2:count_calls("cAddTransferBan"), 2)
    H.eq(s2.calls.cAddTransferBan[1][3], 0, "team ban type 0"); H.eq(s2.calls.cAddTransferBan[2][3], 1, "player ban type 1")
    ok, msg = H.turbo().run("transfer_bans", { mode = "unban_player", id = 2001 })
    H.eq(ok, true, msg)
    H.eq(s2.calls.cRemoveTransferBan[1][1], 2001); H.eq(s2.calls.cRemoveTransferBan[1][2], 1, "player type (LE's helper passes team)")
    H.eq(#s2.bans, 1, "the club ban stays")
    ok, msg = H.turbo().run("transfer_bans", { mode = "unban_team", id = 2 })
    H.eq(ok, true, msg); H.eq(#s2.bans, 0)
    H.eq(s2:count_calls("cSaveTransferBans"), 4, "saved after each change")
    ok, msg = H.turbo().run("transfer_bans", { mode = "ban_team", id = 999999, ban_until = 20990101 })
    H.eq(ok, false); H.has(msg, "not found")
    ok, msg = H.turbo().run("transfer_bans", { mode = "ban_player", id = 2001, ban_until = 20991399 })
    H.eq(ok, false); H.has(msg, "YYYYMMDD")
    ok, msg = H.turbo().run("transfer_bans", { mode = "ban_team", id = 0 })
    H.eq(ok, false); H.has(msg, "positive")
end)

H.finish()
