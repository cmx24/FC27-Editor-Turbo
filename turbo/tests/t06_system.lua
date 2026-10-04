package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t06 probe, every runner script, global dry run, missing pieces")

local sim = H.setup({ in_cm = true })
W.build(sim, { career_playercontract = true, playerloans = true })

H.case("probe in career mode writes a full report", function()
    local ok, msg = H.script("turbo_probe")
    ok, msg = H.turbo().run("probe")
    H.eq(ok, true, msg)
    local files = H.ls_out("^turbo_probe_.*%.txt$")
    H.ok(#files >= 1, "report file")
    local rep = H.read(H.out(files[#files]))
    H.has(rep, "LE_VERSION: v27.1.0")
    H.has(rep, "User team: 1 (Arsenal)")
    H.has(rep, "User squad: 26 players from cm_teamsheets")
    H.has(rep, "Current date: 2027-01-15")
    H.has(rep, "Event DAY_PASSED -> 15 (FC27 name 15, FC26 name nil)")
    H.has(rep, "TABLE players rows=")
    H.has(rep, "contractvaliduntil[0..2047]")
    H.has(rep, "TABLE manager: table not found: manager")
    H.has(rep, "Manager FCEGameModesFCECareerModeUserManager (type 129): 0x")
    H.has(rep, "squad_role memory: vector at PlayerStatusManager+0x18, entry 8 bytes, 26/26 squad matches")
    H.has(rep, "export_fixtures memory: fixtures +0x60 (4), standings +0x88")
    H.has(rep, "export_transfer_history memory: storage at TransferManager+0x1DD0")
    H.has(rep, "Functions missing")
end)

H.case("probe outside career mode skips memory checks", function()
    sim.in_cm = false
    local ok, msg = H.turbo().run("probe")
    H.eq(ok, true, msg)
    local files = H.ls_out("^turbo_probe_.*%.txt$")
    local rep = H.read(H.out(files[#files]))
    H.has(rep, "In career mode: false")
    H.has(rep, "Memory checks skipped")
    sim.in_cm = true
end)

H.case("global dry run: no memory byte and no game API call changes", function()
    H.write_config({
        turbo = { dry_run = true },
        auto = { form_morale = { form = 100, morale = 100, fitness = 50 } },
        modules = {
            custom_headassets = { map = { ["1001"] = 5 } },
            custom_tattoos = { map = { ["1001"] = 5 } },
            delete_generated_players = { confirm = true },
            bulk_edit = { scope = { user_team = true }, set = { potential = 99 }, actions = { form = 50 } },
            player_moves = { actions = { { action = "release", playerid = 2001 } } },
            db_edit = { edits = { { table = "teams", where = { teamid = 1 }, set = { transferbudget = 5 } } } },
            create_player = { source = { playerid = 2001 }, teamid = 3 },
        },
    })
    local snapshot = {}
    for k, v in pairs(sim.mem) do snapshot[k] = v end
    local api_before = {}
    for k, v in pairs(sim.calls) do api_before[k] = #v end
    for _, name in ipairs({ "form_morale", "custom_headassets", "custom_tattoos", "delete_generated_players",
        "extend_cpu_contracts", "extend_user_contracts", "squad_role", "bulk_edit", "player_moves", "db_edit",
        "create_player" }) do
        local ok, msg = H.turbo().run(name)
        H.eq(ok, true, name .. ": " .. tostring(msg))
        H.has(msg, "[DRY RUN]", name)
    end
    for _, mode in ipairs({ "ban_all_teams", "unban_all_teams" }) do
        local ok, msg = H.turbo().run("transfer_bans", { mode = mode })
        H.eq(ok, true, msg)
    end
    local changed = 0
    for k, v in pairs(sim.mem) do if snapshot[k] ~= v then changed = changed + 1 end end
    for k, v in pairs(snapshot) do if sim.mem[k] ~= v then changed = changed + 1 end end
    H.eq(changed, 0, "bytes changed in dry run")
    for k, v in pairs(sim.calls) do
        H.eq(#v, api_before[k] or 0, "API calls to " .. k .. " in dry run")
    end
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("every runner script executes and reports through a message box", function()
    local p = io.popen(string.format("ls '%s/lua/scripts'", H.LE))
    local n = 0
    for f in p:lines() do
        local name = f:gsub("%.lua$", "")
        local before = #sim.boxes
        local ok, err = pcall(H.script, name)
        H.ok(ok, name .. " raised: " .. tostring(err))
        if name ~= "turbo_enable_auto" then
            H.ok(#sim.boxes > before, name .. " showed no message box")
        end
        n = n + 1
    end
    p:close()
    H.eq(n, 36, "runner scripts")
    H.eq(sim.box_format_violations or 0, 0, "message boxes with an unescaped percent sign (would crash Live Editor)")
    for _, b in ipairs(sim.boxes) do
        H.ok(not tostring(b.text):find("crashed"), "crash reported: " .. tostring(b.text))
    end
end)

H.case("message box text with a percent sign is escaped for Live Editor", function()
    local util = require 'imports/turbo/core/util'
    local before = #sim.boxes
    local v0 = sim.box_format_violations or 0
    util.message_box("Turbo 100%", "50% done in C:\\x%s\\%d")
    H.eq(#sim.boxes, before + 1, "box shown")
    H.eq(sim.box_format_violations or 0, v0, "no printf violation")
    H.eq(sim.boxes[#sim.boxes].text, "50% done in C:\\x%s\\%d", "shown text")
    H.eq(sim.boxes[#sim.boxes].title, "Turbo 100%", "shown title")
    local long = util.message_box_text(string.rep("a", 5000))
    H.ok(#long <= util.MESSAGE_BOX_MAX + 10, "long text cut")
end)

H.case("turbo_selftest.lua: every tool runs, dry runs write nothing, the reversible checks are undone", function()
    local sim2 = H.setup({ in_cm = true })
    W.build(sim2, { career_playercontract = true })
    sim2.transfer_budget = 4242
    local snapshot = {}
    for k, v in pairs(sim2.mem) do snapshot[k] = v end
    H.script("turbo_selftest")
    local text = H.read(H.out("turbo_selftest.log"))
    H.ok(text, "log written")
    H.has(text, " passed, 0 failed")
    H.eq(sim2.transfer_budget, 4242, "budget restored")
    H.eq(next(sim2.transfer_listed), nil, "nobody left on the transfer list")
    H.eq(#(sim2.calls.cAddTransferBan or {}), 0, "no ban written")
    -- game memory: only Turbo's own allocations may differ (nothing in the database records)
    local changed = 0
    for _, t in pairs(sim2.tables) do
        for a = t.first, t.first + t.rec_size * t.n - 1 do
            if sim2.mem[a] ~= snapshot[a] then changed = changed + 1 end
        end
    end
    H.eq(changed, 0, "database bytes changed by the self-test")
end)

H.case("message boxes can be switched off", function()
    H.write_config({ turbo = { show_message_box = false } })
    local before = #sim.boxes
    H.turbo().run("probe")
    H.eq(#sim.boxes, before, "no box")
    H.write_config({ turbo = { show_message_box = true } })
end)

H.case("a missing native gives a clear message, not a crash", function()
    local saved = cGetTransferBans
    cGetTransferBans = nil
    local ok, msg = H.turbo().run("transfer_bans", { mode = "list" })
    H.eq(ok, false); H.has(msg, "not available in this Live Editor build")
    cGetTransferBans = saved
end)

H.case("unknown module name", function()
    local ok, msg = H.turbo().run("sharpness")
    H.eq(ok, false); H.has(msg, "unknown Turbo module")
end)

H.case("module errors are caught and reported", function()
    local mod = require 'imports/turbo/features/team_jersey_numbers'
    local saved = mod.run
    mod.run = function() error("boom") end
    local ok, msg = H.turbo().run("team_jersey_numbers")
    H.eq(ok, false); H.has(msg, "crashed"); H.has(msg, "boom")
    mod.run = saved
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

-- Live Editor folder without turbo_config.json: defaults, nothing enabled, runs still work
sim = H.setup({ in_cm = true, no_config = true })
W.build(sim, {})

H.case("missing config: defaults used, warning logged, probe still runs", function()
    local enabled = H.turbo().boot()
    H.eq(#enabled, 0, "nothing auto")
    local ok, msg = H.turbo().run("probe")
    H.eq(ok, true, msg)
    local warned = false
    for _, l in ipairs(sim.logs) do if l.text:find("turbo_config.json not found") then warned = true end end
    H.ok(warned, "warning logged")
end)

H.finish()
