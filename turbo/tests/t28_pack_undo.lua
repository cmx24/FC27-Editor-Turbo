package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t28 season pack apply then undo through the REAL player_moves handler (Live Editor's native, stale view, Turbo's fallback)")

local sim = H.setup({ in_cm = true })
W.build(sim, { playerloans = true })

local function link_team(pid)
    local rec = sim:find_row("teamplayerlinks", "playerid", pid)
    return rec and sim:value("teamplayerlinks", rec, "teamid") or nil
end
local function moves_run(actions) return H.turbo().run("player_moves", { actions = actions }) end
local function tr(pid, to) return { action = "transfer", playerid = pid, to_teamid = to, fee = 0, wage = 0 } end

-- the pack of the in-game test: two players each way between two AI clubs; the undo is what season_pack.cpp builds (rows
-- reversed, each player back to his old club, fee 0 / wage 0, the four in one player_moves command)
local PACK = { { pid = 2001, from = 2, to = 3 }, { pid = 2002, from = 2, to = 3 }, { pid = 2005, from = 3, to = 2 }, { pid = 2006, from = 3, to = 2 } }
local function apply(label)
    local acts = {}
    for _, r in ipairs(PACK) do acts[#acts + 1] = tr(r.pid, r.to) end
    local ok, msg = moves_run(acts)
    H.eq(ok, true, label .. " apply: " .. tostring(msg))
    for _, r in ipairs(PACK) do H.eq(link_team(r.pid), r.to, label .. " applied " .. r.pid) end
end
local function undo(label)
    local acts = {}
    for k = #PACK, 1, -1 do acts[#acts + 1] = tr(PACK[k].pid, PACK[k].from) end
    return moves_run(acts)
end
local function reset()
    for _, r in ipairs(PACK) do
        local rec = sim:find_row("teamplayerlinks", "playerid", r.pid)
        sim:set_field(rec, sim.tables.teamplayerlinks.teamid, r.from)
        sim.player_team[r.pid] = r.from
    end
    sim.native_view = nil
end

H.case("Live Editor's native, each move looked up fresh: apply then undo puts the four players back", function()
    reset(); sim.native_stale_view = false
    apply("fresh")
    local ok, msg = undo("fresh")
    H.eq(ok, true, msg)
    for _, r in ipairs(PACK) do H.eq(link_team(r.pid), r.from, "undone " .. r.pid) end
end)

H.case("2026-10-16: the native's stale view ('Can't transfer player from 1 to 1') no longer fails the undo silently", function()
    reset(); sim.native_stale_view = true
    apply("stale")
    local before = sim:count_calls("cTransferPlayer_error")
    local ok, msg = undo("stale")
    H.eq(ok, true, msg)
    for _, r in ipairs(PACK) do H.eq(link_team(r.pid), r.from, "undone " .. r.pid) end
    H.eq(sim:count_calls("cTransferPlayer_error"), before, "his real club is passed as from_teamid: the native never refuses")
    sim.native_stale_view = false
end)

H.case("a native that moves nobody: Turbo's own move does it and says so; a refusal of both is a failed action", function()
    reset(); sim.native_stale_view = false
    apply("fallback")
    local real = _G.cTransferPlayer
    _G.cTransferPlayer = function(...) end   -- answers, moves nobody
    local ok, msg = undo("fallback")
    H.eq(ok, true, msg)
    H.has(msg, "Turbo's own move did it instead")
    for _, r in ipairs(PACK) do H.eq(link_team(r.pid), r.from, "undone by Turbo's own move " .. r.pid) end
    -- both refuse: a move nobody did is a failed action
    local moves = require 'imports/turbo/core/moves'
    local real_t = moves.transfer
    moves.transfer = function() return false, "refused for the test" end
    local ok2, msg2 = moves_run({ tr(2001, 3) })
    H.eq(ok2, false, "a move nobody did is a failure, not 'transfer x1'")
    H.has(msg2, "left player 2001 at team 2"); H.has(msg2, "refused for the test"); H.has(msg2, "1 calls failed")
    H.eq(link_team(2001), 2, "he did not move")
    moves.transfer = real_t
    _G.cTransferPlayer = real
end)

H.finish()
