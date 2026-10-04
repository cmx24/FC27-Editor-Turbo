-- Turbo feature: job security and unsackable for YOUR manager in a Manager Career, like FC 26 Live Editor's job
-- security / unsackable switches. Feature-flagged: nothing happens unless modules.manager_rules.enabled is true.
--   "manager_rules": { "enabled": false, "job_security": "", "unsackable": null, "keep": true, "confirm": false }
--
--   job_security  "safe" | "very insecure" (locked: the game's own update keeps producing that level) |
--                 "okay" | "insecure" (the middle of the band) | 0..100 (a score) | "game" (the game's own score again)
--   unsackable    true / false (nil = leave alone): Turbo.dll refuses the game's JobSwitchManager::SackManager call
--   keep          true: the unsackable choice is written to turbo_output\manager_rules_keep.json and Turbo switches it
--                 on again in every game session (the switch lives in Turbo.dll, never in the save)
--
-- How it works (docs/re/manager_rules.md): the score 0..100 sits in the career's ClubObjectivesManager (Live Editor
-- manager type 133). The game recomputes it as clamp(objectives part + a saved "addon", 0, 100); Turbo.dll writes the
-- addon and calls the game's own ClubObjectivesManager::UpdateJobSecurityScore on the game thread, so the score, the
-- level and the board's level-change event all come from the game. The addon is saved with the career (the game resets
-- it when you take a new job). The sack is JobSwitchManager::SackManager (type 54), which Turbo.dll's hook refuses
-- while unsackable is on. Both go through TurboManagerRules(sub, address, value, team), a Lua function the bridge
-- defines once it finds Turbo.dll's turbo_game_call export (caps key "manager_rules": the Turbo window greys the
-- buttons until then).

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local mem = require 'imports/turbo/core/mem'
local log = require 'imports/turbo/core/log'

local M = {}

M.NATIVE = "TurboManagerRules"
M.COM_TYPE = 133   -- ENUM_FCEGameModesFCECareerModeClubObjectivesManager
M.JSM_TYPE = 54    -- ENUM_FCEGameModesFCECareerModeJobSwitchManager
-- sub-ops of game call op 4 (turbogui/src/core/manager_rules.h)
M.SUB_GET, M.SUB_SET_LEVEL, M.SUB_SET_SCORE, M.SUB_RESTORE, M.SUB_UNSACKABLE, M.SUB_FLAGS = 1, 2, 3, 4, 5, 6
M.KEEP_FILE = "manager_rules_keep.json"
-- ClubObjectivesManager / JobSwitchManager offsets read here (same as manager_rules.h; [H] in docs/re/manager_rules.md)
M.OFF = { owner = 0x108, manager_mode = 0x110, user_team = 0x114, addon = 0x118, prev = 0x120, score = 0x124,
          lvl_insecure = 0x27C, lvl_okay = 0x280, lvl_safe = 0x284, sack_pending = 0x1E0, sacked = 0x1E1 }
M.COM_VTABLE, M.JSM_VTABLE = 0x14B019370, 0x14B016598   -- FC27.exe 1.0.140.64835 (the DLL resolves them by signature)

M.LEVELS = { ["very insecure"] = 0, insecure = 1, okay = 2, ok = 2, safe = 3 }
M.LEVEL_NAMES = { [0] = "very insecure", [1] = "insecure", [2] = "okay", [3] = "safe" }

-- "safe" -> { level = 3 }; 0..100 -> { score = n }; "game" -> { restore = true }; nil when not understood
function M.parse_job_security(v)
    if v == nil or v == "" then return nil end
    local n = util.to_int(v)
    if n then return { score = n } end
    if type(v) ~= "string" then return nil end
    local key = v:lower():gsub("[-_]", " ")
    if key == "game" or key == "restore" or key == "off" then return { restore = true } end
    local lvl = M.LEVELS[key]
    if lvl then return { level = lvl } end
    return nil
end

-- ---------------------------------------------------------------- the "keep" file (turbo_output)
local keep_cache
function M.keep_path()
    local root = env.le_root()
    if not root then return nil end
    return util.join(util.join(root, "turbo_output"), M.KEEP_FILE)
end

function M.read_keep()
    if keep_cache then return keep_cache end
    local path = M.keep_path()
    local data = {}
    if path and util.file_exists(path) then
        local okj, json = pcall(require, 'imports/external/json')
        local ok, d = false, nil
        if okj then ok, d = pcall(json.decode, util.read_file(path) or "") end
        if ok and type(d) == "table" then data = d end
    end
    keep_cache = data
    return data
end

function M.write_keep(patch)
    local path = M.keep_path()
    if not path then return false, "Live Editor folder unknown" end
    local okj, json = pcall(require, 'imports/external/json')
    if not okj then return false, "json library missing" end
    local data = util.deep_copy(M.read_keep())
    for k, v in pairs(patch) do data[k] = v end
    local okw, err = util.write_file(path, json.encode(data))
    if not okw then return false, tostring(err) end
    keep_cache = data
    return true
end

function M.reset_cache() keep_cache = nil; M._applied = nil end

-- ---------------------------------------------------------------- run
-- Validates the request. Returns plan or nil, reason.
function M.plan(cfg)
    if cfg.enabled ~= true then
        return nil, "manager rules are off: set \"manager_rules\": {\"enabled\": true} in turbo_config.json (feature flag)"
    end
    if not game.in_cm() then return nil, "manager rules need a loaded Manager Career" end
    local plan = { steps = {} }
    local js = cfg.job_security
    if js ~= nil and js ~= "" then
        local p = M.parse_job_security(js)
        if not p then return nil, "job_security must be safe, okay, insecure, very insecure, game or a score 0..100" end
        if p.score and (p.score < 0 or p.score > 100) then return nil, "job_security score must be 0..100" end
        plan.job_security = p
        plan.steps[#plan.steps + 1] = "job security -> " .. tostring(js)
    end
    if cfg.unsackable ~= nil then
        if type(cfg.unsackable) ~= "boolean" then return nil, "unsackable must be true or false" end
        plan.unsackable = cfg.unsackable
        plan.steps[#plan.steps + 1] = "unsackable " .. (cfg.unsackable and "on" or "off")
    end
    if #plan.steps == 0 then return nil, "nothing to do: set job_security or unsackable" end
    plan.keep = cfg.keep ~= false
    if cfg.confirm ~= true then
        return nil, string.format("would apply: %s; set \"confirm\": true to do it", table.concat(plan.steps, ", "))
    end
    return plan
end

local function call(native, sub, addr, value, team)
    local ok, res, msg, status, out0, out1 = pcall(native, sub, addr or 0, value or 0, team or 0)
    if not ok then return false, "manager rules call failed: " .. tostring(res) end
    if res ~= true then return false, tostring(msg or res) end
    return true, tostring(msg), status, out0, out1
end
M._call = call

function M.run(ctx)
    local plan, why = M.plan(ctx.cfg or {})
    if not plan then return false, why end
    if ctx.dry then
        return true, "dry run: would apply " .. table.concat(plan.steps, ", ")
    end
    local native, reason = env.api(M.NATIVE)
    if not native then
        return false, "manager rules need Turbo.dll's game-call support, which is not available in this Live Editor build (" ..
            tostring(reason) .. ")"
    end
    if not mem.map_available() then return false, mem.NO_MAP end
    local team = game.user_team_id()
    local out = {}
    if plan.job_security then
        local com = mem.manager(M.COM_TYPE)
        if not com then return false, "the career's ClubObjectivesManager was not found (load a Manager Career first)" end
        local p = plan.job_security
        local sub, value = M.SUB_SET_LEVEL, p.level
        if p.restore then sub, value = M.SUB_RESTORE, 0 elseif p.score then sub, value = M.SUB_SET_SCORE, p.score end
        local ok, msg, status = call(native, sub, com, value, team)
        if not ok then return false, "job security: " .. tostring(msg) end
        out[#out + 1] = (status == "queued" and "job security queued: " or "") .. tostring(msg)
    end
    if plan.unsackable ~= nil then
        local jsm = mem.manager(M.JSM_TYPE) or 0
        local ok, msg, status = call(native, M.SUB_UNSACKABLE, jsm, plan.unsackable and 1 or 0, team)
        if not ok then return false, "unsackable: " .. tostring(msg) end
        out[#out + 1] = (status == "queued" and "queued: " or "") .. tostring(msg)
        if plan.keep or plan.unsackable == false then
            local okk, kerr = M.write_keep({ unsackable = plan.unsackable })
            if okk then
                if plan.unsackable then out[#out + 1] = "kept: switched on again in every game session" end
            else
                log.warn("manager rules: keep file not written: %s", tostring(kerr))
            end
        end
        M._applied = true
    end
    return true, table.concat(out, "; ")
end

-- What the Turbo window shows (bridge_state.json "manager_rules"): read through core/mem.lua's checked reads, nothing
-- is called. nil outside a career or without the memory map.
function M.state()
    if not game.in_cm() or not mem.map_available() then return nil end
    local st = {}
    local com = mem.manager(M.COM_TYPE)
    -- only a ClubObjectivesManager whose serialised block points back at it, in manager mode, with a 0..100 score
    if com and mem.chain(com, { M.OFF.owner }) == com and mem.byte(com + M.OFF.manager_mode) == 1 then
        local score = mem.int(com + M.OFF.score)
        if score and score >= 0 and score <= 100 then
            st.score = score
            st.addon = mem.int(com + M.OFF.addon) or 0
            local ins, okay, safe = mem.int(com + M.OFF.lvl_insecure), mem.int(com + M.OFF.lvl_okay), mem.int(com + M.OFF.lvl_safe)
            if ins and okay and safe and ins >= 0 and okay >= ins and safe >= okay and safe > 0 and safe <= 100 then
                st.insecure, st.okay, st.safe = ins, okay, safe
                local lvl = (score >= safe and 3) or (score < ins and 0) or (score < okay and 1) or 2
                st.level = M.LEVEL_NAMES[lvl]
            end
            st.locked = (st.addon >= 100 and "safe") or (st.addon <= -100 and "very insecure") or nil
        end
    end
    local jsm = mem.manager(M.JSM_TYPE)
    if jsm then
        local p, s = mem.byte(jsm + M.OFF.sack_pending), mem.byte(jsm + M.OFF.sacked)
        if p ~= nil and s ~= nil and p <= 1 and s <= 1 then
            st.sack_pending, st.sacked = p == 1, s == 1
        end
    end
    st.keep_unsackable = M.read_keep().unsackable == true
    return st
end

-- Kept unsackable: switched on again once per game session (Turbo.dll's switch starts off) as soon as the native is
-- there. Called by the bridge on every career-mode event; returns true when it made the call.
function M.reapply()
    if M._applied or not game.in_cm() then return false end
    if M.read_keep().unsackable ~= true then return false end
    local native = env.api(M.NATIVE)
    if not native then return false end   -- the DLL is not up yet: try again on the next event
    if not mem.map_available() then return false end
    M._applied = true
    local ok, msg = call(native, M.SUB_UNSACKABLE, mem.manager(M.JSM_TYPE) or 0, 1, game.user_team_id())
    if ok then log.info("manager rules: kept unsackable switched on: %s", tostring(msg))
    else log.warn("manager rules: kept unsackable not switched on: %s", tostring(msg)) end
    return true
end

return M
