-- FC 27 LE Turbo - entry point
-- Scripts in lua\scripts call TURBO.run("<module>"); lua\autorun\turbo_boot.lua calls TURBO.boot().

local version = require 'imports/turbo/core/version'
local log = require 'imports/turbo/core/log'
local game = require 'imports/turbo/core/game'   -- loaded early: captures native GetCurrentDate/GetUserTeamID
local env = require 'imports/turbo/core/env'
local config = require 'imports/turbo/core/config'
local events = require 'imports/turbo/core/events'
local trace = require 'imports/turbo/core/trace'

local M = { version = version.version }

local function util_join(a, b) return (require 'imports/turbo/core/util').join(a, b) end

-- name -> { path, kind = "action" | "auto", needs_cm = bool, desc }
M.MODULES = {
    probe                    = { path = 'imports/turbo/features/probe',                    kind = "action", needs_cm = false, desc = "Report LE version, API natives, DB tables/fields, calibration status" },
    form_morale              = { path = 'imports/turbo/features/form_morale',              kind = "both",   needs_cm = true,  desc = "Set user-squad form/morale/fitness (auto: daily)" },
    pap_playstyles           = { path = 'imports/turbo/features/pap_playstyles',           kind = "both",   needs_cm = true,  desc = "All playstyles for your player in Player Career (auto: re-apply)" },
    custom_headassets        = { path = 'imports/turbo/features/custom_headassets',        kind = "action", needs_cm = false, desc = "Assign head models from a playerid->headassetid map" },
    custom_tattoos           = { path = 'imports/turbo/features/custom_tattoos',           kind = "action", needs_cm = false, desc = "Assign tattoos from a playerid->tattoo map" },
    delete_generated_players = { path = 'imports/turbo/features/delete_generated_players', kind = "action", needs_cm = false, desc = "Delete generated players (playerid >= threshold)" },
    export_season_stats      = { path = 'imports/turbo/features/export_season_stats',      kind = "action", needs_cm = true,  desc = "Season stats to CSV" },
    export_fixtures          = { path = 'imports/turbo/features/export_fixtures',          kind = "action", needs_cm = true,  desc = "Fixtures and results to CSV (memory layout auto-calibrated)" },
    export_transfer_history  = { path = 'imports/turbo/features/export_transfer_history',  kind = "action", needs_cm = true,  desc = "Season transfer history to CSV (memory layout auto-calibrated)" },
    extend_cpu_contracts     = { path = 'imports/turbo/features/extend_cpu_contracts',     kind = "action", needs_cm = false, desc = "Extend contracts of players outside your club" },
    extend_user_contracts    = { path = 'imports/turbo/features/extend_user_contracts',    kind = "action", needs_cm = true,  desc = "Extend contracts of your club's players" },
    headmodels               = { path = 'imports/turbo/features/headmodels',               kind = "action", needs_cm = false, desc = "Capture the FC 27 head-model list, or apply it" },
    transfer_bans            = { path = 'imports/turbo/features/transfer_bans',            kind = "action", needs_cm = true,  desc = "List, ban all or unban all teams" },
    squad_role               = { path = 'imports/turbo/features/squad_role',               kind = "action", needs_cm = true,  desc = "Set squad role for your club's players" },
    team_jersey_numbers      = { path = 'imports/turbo/features/team_jersey_numbers',      kind = "action", needs_cm = false, desc = "List kit numbers of a team" },
    bulk_edit                = { path = 'imports/turbo/features/bulk_edit',                kind = "action", needs_cm = false, desc = "Filter players and set fields, fitness, form, morale, development" },
    player_moves             = { path = 'imports/turbo/features/player_moves',             kind = "action", needs_cm = true,  desc = "Transfer, loan, release, list or unlist players" },
    db_edit                  = { path = 'imports/turbo/features/db_edit',                  kind = "action", needs_cm = false, desc = "Edit any DB table rows matching conditions" },
    export_table             = { path = 'imports/turbo/features/export_table',             kind = "action", needs_cm = false, desc = "Dump DB tables (rows + field ranges) to CSV" },
}

local function message_box(cfg, title, text, opts)
    if opts and opts.silent then return end
    if cfg and cfg.turbo and cfg.turbo.show_message_box == false then return end
    local util = require 'imports/turbo/core/util'
    util.message_box(title, text)
end

local function load_module(name)
    local spec = M.MODULES[name]
    if not spec then return nil, "unknown Turbo module: " .. tostring(name) end
    local ok, mod = pcall(require, spec.path)
    if not ok then return nil, "cannot load " .. spec.path .. ": " .. tostring(mod) end
    return mod, spec
end

-- Build the run context handed to modules
local function make_ctx(cfg, name)
    local out_dir = env.output_dir(cfg.turbo.output_dir)
    local util = require 'imports/turbo/core/util'
    local mcfg = util.deep_copy((cfg.auto and cfg.auto[name]) or {})
    util.deep_merge(mcfg, (cfg.modules and cfg.modules[name]) or {})
    return {
        name = name,
        cfg = mcfg,
        all = cfg,
        dry = cfg.turbo.dry_run == true,
        out_dir = out_dir,
    }
end

-- Run one module. Returns ok(bool), summary(string)
-- opts.silent = true: no message box (used when the Turbo GUI runs the module)
function M.run(name, overrides, opts)
    log.reset()
    local cfg, info = config.load()
    if info.error then
        if info.invalid then
            log.error("%s", info.error)
            message_box(cfg, "Turbo: config error", info.error, opts)
            return false, info.error
        end
        log.warn("%s", info.error)
    end
    if type(overrides) == "table" then
        cfg.modules[name] = cfg.modules[name] or {}
        for k, v in pairs(overrides) do cfg.modules[name][k] = v end
    end

    local mod, spec = load_module(name)
    if not mod then
        log.error("%s", spec)
        message_box(cfg, "Turbo: error", spec, opts)
        return false, spec
    end

    if spec.needs_cm and not game.in_cm() then
        local msg = name .. " needs a loaded career save (Career Mode)."
        log.warn("%s", msg)
        message_box(cfg, "Turbo: " .. name, msg, opts)
        return false, msg
    end

    local ctx = make_ctx(cfg, name)
    log.info("run %s (Turbo %s)%s", name, M.version, ctx.dry and " [DRY RUN - no writes]" or "")

    local ok, res_ok, summary = pcall(mod.run, ctx)
    if not ok then
        local msg = string.format("%s crashed: %s", name, tostring(res_ok))
        log.error("%s", msg)
        message_box(cfg, "Turbo: error", msg, opts)
        return false, msg
    end

    summary = tostring(summary or "")
    if ctx.dry then summary = "[DRY RUN] " .. summary end
    if res_ok then log.info("%s done: %s", name, summary) else log.warn("%s stopped: %s", name, summary) end
    message_box(cfg, "Turbo: " .. name, summary, opts)
    return res_ok == true, summary
end

-- Configure automatic features from turbo_config.json. Safe to call any number of times.
--
-- LAUNCH SAFETY: lua\autorun\turbo_boot.lua runs while Live Editor is still initialising the game
-- (opts.at_launch). At that point boot never calls a game native or reads game memory: it reads the config,
-- registers Live Editor's documented career-event handler when something needs it and, with gui.autoload
-- (default true), loads Turbo.dll in "launch" mode: the DLL touches nothing until Live Editor reports
-- "Initial setup done" in its log (or this Lua side runs in game). The bridge (game database) starts on the
-- first career-mode event or from turbo_gui_load.lua, when the game is fully running.
function M.boot(opts)
    opts = opts or {}
    trace.step("boot: start" .. (opts.at_launch and " (game launch)" or ""))
    log.reset()
    local cfg, info = config.load()
    if info.error then log.warn("%s", info.error) end

    local enabled = {}
    for name, spec in pairs(M.MODULES) do
        if spec.kind == "both" then
            local acfg = cfg.auto and cfg.auto[name]
            local mod = load_module(name)
            if mod and type(mod.auto) == "function" and type(acfg) == "table" and acfg.enabled == true then
                local ids, unknown = events.resolve_set(acfg.events)
                if #unknown > 0 then log.warn("%s: unknown event names ignored: %s", name, table.concat(unknown, ", ")) end
                local ctx = make_ctx(cfg, name)
                events.set_listener(name, ids, function(event_id) mod.auto(ctx, event_id) end)
                enabled[#enabled + 1] = name
                -- Apply once right away when a career is already loaded (never at game launch)
                if not opts.at_launch and game.in_cm() then pcall(mod.auto, ctx, -1) end
            else
                events.clear_listener(name)
            end
        end
    end

    -- Turbo GUI: remember the effective settings for the GUI; with gui.autoload, arm the bridge for the first career-mode
    -- event and, at launch, load Turbo.dll in "launch" mode (it waits for Live Editor's "Initial setup done")
    local gui = type(cfg.gui) == "table" and cfg.gui or {}
    local okb, bridge = pcall(require, 'imports/turbo/bridge')
    if okb and type(bridge) == "table" then
        bridge.set_settings(cfg)
        if gui.autoload ~= false then
            bridge.arm(cfg)
            if opts.at_launch then
                -- The crash guard of the last start is still there: say so where the user looks (Live Editor's Logger).
                -- Pure Lua file I/O, nothing else (launch safety).
                local root = env.le_root()
                local flag = root and util_join(util_join(root, "turbo_output"), "turbo_gui_start.flag") or nil
                local fh = flag and io.open(flag, "rb")
                if fh then
                    local why = fh:read("l") or ""
                    fh:close()
                    if why:sub(1, 7) == "RETRY: " then
                        log.warn("Turbo GUI stays off: its last two starts did not finish (%s). Delete turbo_output\\turbo_gui_start.flag to try again; see turbo_output\\turbo_gui.log", why)
                    else
                        log.warn("Turbo GUI's last start did not finish (%s): trying once more. See turbo_output\\turbo_gui.log", why)
                    end
                end
                trace.step("boot: loading turbo\\Turbo.dll (it waits until Live Editor reports Initial setup done)")
                local okl, lok, lmsg = pcall(bridge.load_gui, "launch")
                local why = okl and lmsg or lok
                trace.step("boot: load_gui returned " .. tostring(okl and lok) .. " - " .. tostring(why))
                if okl and lok then
                    log.info("Turbo GUI loaded: it starts when Live Editor has finished setting up the game; then press F8")
                else
                    log.warn("Turbo GUI not loaded at launch: %s (run turbo_gui_load.lua in game)", tostring(why))
                end
            end
        end
    else
        log.error("cannot load the Turbo GUI bridge: %s", tostring(bridge))
    end

    -- Live Editor's career-event handler is only registered when something needs it
    if next(TURBO_STATE.listeners) ~= nil or next(TURBO_STATE.taps or {}) ~= nil then
        local ok, err = events.ensure_registered()
        if not ok then log.error("cannot register career events: %s", tostring(err)) end
    end

    table.sort(enabled)
    log.info("Turbo %s ready. Auto features: %s", M.version, #enabled > 0 and table.concat(enabled, ", ") or "none")
    trace.step("boot: done")
    return enabled
end

return M
