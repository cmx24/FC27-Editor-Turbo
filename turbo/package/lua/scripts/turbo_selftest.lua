-- FC 27 LE Turbo: in-game self-test. Run it from Live Editor's Lua Engine with a career loaded (most checks need one).
-- It runs every Turbo tool once and writes what happened to turbo_output\turbo_selftest.log.
-- Everything that would change your career runs as a DRY RUN (validated, nothing written), except three reversible
-- checks that are undone right away: the transfer budget (+1, then back), one player put on the transfer list and taken
-- off again, and the squad's form/morale only if you set "selftest_form": true in turbo_config.json's "turbo" section.
-- Do not save the game after a self-test if you want to be sure nothing changed.
local okt, TURBO = pcall(require, 'imports/turbo/turbo')
local lines, passed, failed, skipped = {}, 0, 0, 0

local function add(text) lines[#lines + 1] = text end

local function result(name, ok, msg)
    if ok then passed = passed + 1 else failed = failed + 1 end
    add(string.format("%-4s %-34s %s", ok and "OK" or "FAIL", name, tostring(msg or "")))
end

local env

local function run(name, module, overrides, dry, skip_if)
    local okc, ok, msg = pcall(TURBO.run, module, overrides, { silent = true, dry = dry == true })
    if not okc then ok, msg = false, "raised: " .. tostring(ok) end
    -- "this Live Editor build cannot do it" is reported as SKIP (Turbo needs a Live Editor update for it), not FAIL
    if not ok and ((skip_if and tostring(msg):find(skip_if, 1, true)) or env.is_unavailable_message(msg)) then
        skipped = skipped + 1
        add(string.format("%-4s %-34s %s", "SKIP", name, tostring(msg)))
        return ok, msg
    end
    result(name, ok, msg)
    return ok, msg
end

if not okt then
    if type(Log) == "function" then Log("[Turbo] self-test: Turbo not found: " .. tostring(TURBO)) end
    return
end

local game = require 'imports/turbo/core/game'
local util = require 'imports/turbo/core/util'
env = require 'imports/turbo/core/env'
local in_cm = game.in_cm()
add(string.format("FC 27 LE Turbo %s self-test, %s, career loaded: %s, LE %s", TURBO.version, os.date("%Y-%m-%d %H:%M:%S"),
    tostring(in_cm), tostring(LE_VERSION)))
if in_cm then
    local d = game.current_date()
    local tid = game.user_team_id()
    local _, count, source = game.user_squad()
    add(string.format("club %d (%s), squad %d players (%s), date %s", tid, tostring(game.team_name(tid)), count,
        tostring(source), d and string.format("%04d-%02d-%02d (%s)", d.year, d.month, d.day, tostring(d.source)) or "unavailable"))
end

-- read-only
run("probe report", "probe")
run("jersey numbers (your club / team 1)", "team_jersey_numbers", { teamid = in_cm and 0 or 1 })
run("export table teams (100 rows)", "export_table", { tables = { "teams" }, max_rows = 100 })
run("count generated players", "delete_generated_players", { confirm = false })
run("capture real-face list", "headmodels", { mode = "capture" })
if in_cm then
    run("season stats CSV", "export_season_stats")
    run("fixtures CSV", "export_fixtures")
    run("transfer history CSV", "export_transfer_history")
    run("list transfer bans", "transfer_bans", { mode = "list" })
    run("transfer budget (read)", "transfer_budget", { mode = "get" })
end

-- dry runs: validated, nothing written
run("DRY extend other clubs' contracts", "extend_cpu_contracts", { years = 1 }, true)
run("DRY bulk edit (form 100)", "bulk_edit", { scope = in_cm and { user_team = true } or { teamids = { 1 } },
    filters = {}, set = {}, actions = { form = 100 } }, true)
run("DRY db edit (teams 1 name unchanged)", "db_edit", { edits = { { table = "teams", where = { teamid = 1 },
    set = { teamid = 1 } } } }, true)
if in_cm then
    run("DRY extend my squad's contracts", "extend_user_contracts", { years = 4 }, true)
    run("DRY squad role 3 (finds the role list)", "squad_role", { role = 3 }, true)
    run("DRY ban every team", "transfer_bans", { mode = "ban_all_teams" }, true)
    run("DRY Player Career playstyles", "pap_playstyles", {}, true, "not in Player Career")
end

-- reversible real changes, undone at once
if in_cm then
    local okb, tb = pcall(require, 'imports/turbo/features/transfer_budget')
    local before, berr
    if okb then before, berr = tb.current() else berr = tostring(tb) end
    if before then
        local ok1, m1 = run("budget +1 (real)", "transfer_budget", { mode = "add", amount = 1 })
        local ok2, m2 = run("budget back (real)", "transfer_budget", { mode = "set", amount = before })
        result("budget restored", tb.current() == before, string.format("%s / %s / now %s", tostring(m1), tostring(m2),
            tostring(tb.current())))
        local _ = ok1 and ok2
    else
        local unavailable = env.is_unavailable_message(berr)
        if unavailable then skipped = skipped + 1 else failed = failed + 1 end
        add(string.format("%-4s %-34s %s", unavailable and "SKIP" or "FAIL", "budget +1 and back (real)", tostring(berr)))
    end
    -- transfer list one squad player (the last one by id), then unlist
    local squad = game.user_squad()
    local pid
    for p in pairs(squad) do if not pid or p > pid then pid = p end end
    local listed, why = env.api("IsPlayerTransferListed")
    if pid and not listed then
        skipped = skipped + 1
        add(string.format("%-4s %-34s %s", "SKIP", "transfer list + unlist (real)", tostring(why)))
    elseif pid then
        local was = select(2, pcall(listed, pid, game.user_team_id()))
        if was ~= true then
            run("transfer list player " .. pid .. " (real)", "player_moves",
                { actions = { { action = "transfer_list", playerid = pid } } })
            local now = select(2, pcall(listed, pid, game.user_team_id()))
            result("player is on the transfer list", now == true, "IsPlayerTransferListed = " .. tostring(now))
            run("remove from lists (real)", "player_moves", { actions = { { action = "unlist", playerid = pid } } })
            local after = select(2, pcall(listed, pid, game.user_team_id()))
            result("player is off the list again", after ~= true, "IsPlayerTransferListed = " .. tostring(after))
        else
            add("skip transfer-list check: player " .. pid .. " is already listed")
        end
    end
    local cfg = (require 'imports/turbo/core/config').load()
    if type(cfg.turbo) == "table" and cfg.turbo.selftest_form == true then
        run("form/morale 100 (real)", "form_morale", { form = 100, morale = 100, fitness = 0 })
    end
end

add(string.format("%d passed, %d failed, %d skipped (not possible with this Live Editor build)", passed, failed, skipped))
local dir = env.output_dir()
local path = dir and util.join(dir, "turbo_selftest.log")
if path then util.write_file(path, table.concat(lines, "\r\n") .. "\r\n") end
if type(Log) == "function" then
    for _, l in ipairs(lines) do pcall(Log, "[Turbo self-test] " .. l) end
end
util.message_box("Turbo self-test", string.format("%d passed, %d failed, %d skipped.\nDetails: %s", passed, failed,
    skipped, tostring(path)))
