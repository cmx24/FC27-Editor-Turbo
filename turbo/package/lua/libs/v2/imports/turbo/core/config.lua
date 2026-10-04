-- FC 27 LE Turbo - configuration (turbo_config.json in the Live Editor folder)

local util = require 'imports/turbo/core/util'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'

local M = {}

-- Defaults. turbo_config.json is merged on top of these.
M.DEFAULTS = {
    turbo = {
        dry_run = false,          -- true = report what would change, write nothing
        verbose = false,          -- true = debug lines in the log
        output_dir = "",          -- "" = <Live Editor folder>\turbo_output
        show_message_box = true,  -- pop a summary box after each run
    },
    gui = {
        -- true (default): Turbo.dll is loaded while the game starts but touches nothing until Live Editor reports that it
        -- has finished setting up the game ("Initial setup done" in its log) or Turbo's Lua side runs in game; it connects
        -- to the database on the first career-mode event (or when turbo_gui_load.lua runs). No game native is called at launch.
        -- false: nothing is loaded at launch; run turbo_gui_load.lua in game to load the GUI.
        autoload = true,
    },
    auto = {
        form_morale = {
            enabled = false,
            form = 100,           -- 0..100, 0 = leave alone
            morale = 100,         -- 0..100, 0 = leave alone
            fitness = 0,          -- 5..95, 0 = leave alone
            events = { "DAY_PASSED", "ABOUT_TO_ENTER_PREMATCH", "POST_LOAD_PREPARE" },
        },
        -- forced weekly growth (features/development.lua): listed players (and the squad with user_team) gain `weekly`
        -- points per attribute of their group each week while below their potential; no_decline puts back drops
        development = {
            enabled = false,
            players = {},
            user_team = false,
            weekly = 1,           -- 0..5
            no_decline = true,
            events = { "WEEK_PASSED", "POST_LOAD_PREPARE" },
        },
        pap_playstyles = {
            enabled = false,
            playstyles1 = "max",  -- "max" = every bit the trait1 field can hold, or an integer bitmask
            playstyles2 = 1535,   -- FC 26 script default
            events = { "DAY_PASSED", "USER_MATCH_COMPLETED", "USER_INTERNATIONAL_MATCH_COMPLETED",
                       "ENTERED_HUB_FIRST_TIME", "POST_LOAD_PREPARE" },
        },
    },
    modules = {
        custom_headassets = { map = {} },
        custom_tattoos = { field = "tattooleftarm", map = {} },
        delete_generated_players = { min_playerid = 460000, confirm = false },
        export_season_stats = { only_user_team = false },
        export_fixtures = {},
        export_transfer_history = {},
        extend_cpu_contracts = { years = 5 },
        extend_user_contracts = { years = 4 },
        headmodels = { mode = "capture", list_file = "headmodels_fc27.json", generic_for_others = true, min_list_size = 100 },
        transfer_bans = { mode = "list", ban_until = 20990101, exclude_user_team = false },
        squad_role = { role = 3, include_loaned_in = false, use_memory = true },
        team_jersey_numbers = { teamid = 0 },
        bulk_edit = { scope = { user_team = true }, filters = {}, set = {}, actions = {}, confirm_all = false },
        player_moves = { actions = {} },
        db_edit = { edits = {} },
        export_table = { tables = { "teams" }, max_rows = 0 },
        callnames = { actions = {} },
        player_presets = { mode = "export", playerids = {}, name = "", csv = true, json = true, miniface = true, preset_dir = "",
                           file = "", playerid = 0, groups = {}, row = 0, preset_playerid = 0 },
        create_player = { source = {}, teamid = 111592, jersey = 0, names = {}, set = {}, playerid = 0,
                          min_playerid = 0, max_playerid = 459999, allow_user_club = false },
        job_offer = { enabled = false, teamid = 0, confirm = false },
        reveal = { scope = { user_team = true }, confirm = false, allow_evict = false },
        development = { scope = {}, mode = "none", attributes = {}, delta = 0, potential = 0,
                        growthprofile = -1, confirm = false },
        youth = { mode = "list", playerid = 0, potential = 0, position = -1, tier = -1, variance = -1, confirm = false },
    },
}

local function decode_json(text)
    local ok_req, json = pcall(require, 'imports/external/json')
    if not ok_req or type(json) ~= "table" or type(json.decode) ~= "function" then
        return nil, "Live Editor json library (imports/external/json) not available"
    end
    local ok, result = pcall(json.decode, text)
    if not ok then return nil, tostring(result) end
    if type(result) ~= "table" then return nil, "top level of turbo_config.json must be an object" end
    return result
end

-- Settings saved by the Turbo GUI (<Live Editor>\turbo_output\gui_settings.json).
-- Only the "turbo" and "auto" sections are taken; they win over turbo_config.json because the
-- GUI writes them later. Returns true when the file was merged.
local GUI_SECTIONS = { "turbo", "auto" }

function M.gui_settings_path()
    local root = env.le_root()
    if not root then return nil end
    return util.join(util.join(root, "turbo_output"), "gui_settings.json")
end

local function merge_gui_settings(cfg, info)
    local path = M.gui_settings_path()
    if not path or not util.file_exists(path) then return false end
    local text = util.read_file(path)
    if not text or text == "" then return false end
    if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end
    local gs, err = decode_json(text)
    if not gs then
        info.gui_error = "gui_settings.json ignored: " .. tostring(err)
        return false
    end
    for _, section in ipairs(GUI_SECTIONS) do
        if type(gs[section]) == "table" and not util.is_array(gs[section]) then
            if section == "auto" then
                for name, block in pairs(gs.auto) do
                    if type(block) == "table" and type(cfg.auto[name]) == "table" then
                        util.deep_merge(cfg.auto[name], block)
                    end
                end
            else
                util.deep_merge(cfg[section], gs[section])
            end
        end
    end
    info.gui_settings = path
    return true
end

-- Returns cfg, info where info = { path=, loaded=bool, error=string|nil, gui_settings=path|nil }
function M.load()
    local cfg = util.deep_copy(M.DEFAULTS)
    local info = { path = env.config_path(), loaded = false, error = nil }

    if not info.path or not util.file_exists(info.path) then
        info.error = "turbo_config.json not found next to FCLiveEditor.DLL; using built-in defaults"
        merge_gui_settings(cfg, info)
        log.verbose = cfg.turbo.verbose == true
        return cfg, info
    end

    local text, rerr = util.read_file(info.path)
    if not text then
        info.error = "cannot read " .. tostring(info.path) .. ": " .. tostring(rerr)
        return cfg, info
    end

    -- Strip UTF-8 BOM (Notepad adds it)
    if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end

    local user, derr = decode_json(text)
    if not user then
        info.error = "turbo_config.json is not valid JSON: " .. tostring(derr)
        info.invalid = true
        merge_gui_settings(cfg, info)
        return cfg, info
    end

    util.deep_merge(cfg, user)

    -- Sections and module blocks must stay objects; restore defaults for anything else
    local fixed = {}
    for _, section in ipairs({ "turbo", "gui", "auto", "modules" }) do
        if type(cfg[section]) ~= "table" then
            cfg[section] = util.deep_copy(M.DEFAULTS[section])
            fixed[#fixed + 1] = section
        end
    end
    for _, section in ipairs({ "auto", "modules" }) do
        for name, default in pairs(M.DEFAULTS[section]) do
            if type(cfg[section][name]) ~= "table" then
                cfg[section][name] = util.deep_copy(default)
                fixed[#fixed + 1] = section .. "." .. name
            end
        end
    end
    if #fixed > 0 then
        info.error = "turbo_config.json entries must be objects; defaults used for: " .. table.concat(fixed, ", ")
    end

    merge_gui_settings(cfg, info)
    info.loaded = true
    log.verbose = cfg.turbo.verbose == true
    return cfg, info
end

return M
