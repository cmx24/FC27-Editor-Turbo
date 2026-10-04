-- FC 27 LE Turbo - career-mode event dispatch
-- FC 27 Live Editor's Lua files renamed the event constants from ENUM_CM_EVENT_MSG_* to
-- ENUM_FCEGameModesCM_EVENT_MSG_* (same numeric IDs). The FC 27 bundled scripts still use the
-- old names, which no FC 27 Lua file defines. Turbo resolves both names.

local log = require 'imports/turbo/core/log'

local M = {}

M.EVENT_NAME = "post__CareerModeEvent"

-- Turbo.dll's synthetic career-mode event (turbogui/src/core/gamethread.h kSyntheticCareerEvent): sent from the game's
-- per-frame tick while the Turbo window has a command waiting, so Live Editor runs this dispatcher at once instead of
-- on the next real event. It carries no game event: only the GUI bridge tap acts on it (bridge.on_career_event), every
-- id-keyed listener ignores it because no feature registers this id.
M.SYNTHETIC_ID = 0x7E7E0001

function M.is_synthetic(event_id)
    return event_id == M.SYNTHETIC_ID
end

local enums_loaded = false
local function load_enums()
    if enums_loaded then return end
    pcall(require, 'imports/career_mode/enums')
    enums_loaded = true
end

-- "DAY_PASSED" -> numeric event id or nil
function M.resolve(name)
    load_enums()
    if type(name) ~= "string" then return nil end
    local v = _G["ENUM_FCEGameModesCM_EVENT_MSG_" .. name]
    if type(v) ~= "number" then v = _G["ENUM_CM_EVENT_MSG_" .. name] end
    if type(v) ~= "number" then return nil end
    return math.tointeger(v)
end

-- names -> set of ids, list of unknown names
function M.resolve_set(names)
    local set, unknown = {}, {}
    for _, n in ipairs(names or {}) do
        local id = M.resolve(n)
        if id then set[id] = true else unknown[#unknown + 1] = tostring(n) end
    end
    return set, unknown
end

-- Shared state survives re-running scripts inside the same Live Editor session.
TURBO_STATE = TURBO_STATE or { listeners = {} }

-- listeners[name] = { ids = {[event_id]=true}, fn = function(event_id) }
function M.set_listener(name, ids, fn)
    TURBO_STATE.listeners[name] = { ids = ids, fn = fn }
end

function M.clear_listener(name)
    TURBO_STATE.listeners[name] = nil
end

function M.listeners()
    return TURBO_STATE.listeners
end

-- Taps run on every career-mode event, whatever its id (used by the GUI bridge)
function M.set_tap(name, fn)
    TURBO_STATE.taps = TURBO_STATE.taps or {}
    TURBO_STATE.taps[name] = fn
end

function M.clear_tap(name)
    if TURBO_STATE.taps then TURBO_STATE.taps[name] = nil end
end

-- Called by Live Editor for every career-mode event.
-- Never nests: game code that Turbo calls from a handler (a game call such as the job offer's MakeOffer) can post
-- career-mode events on the same thread before it returns, and Live Editor runs this dispatcher for them at once. The
-- nested event is ignored, so a mailbox command / action still running is never picked up and run again (1.1.3: the
-- job offer ran itself recursively until the game's stack overflowed).
function M.dispatch(_events_manager, event_id, _event)
    if TURBO_STATE.dispatching then return end
    TURBO_STATE.dispatching = true
    local ok, err = pcall(M.dispatch_now, event_id)
    TURBO_STATE.dispatching = false
    if not ok then log.error("career event %s: %s", tostring(event_id), tostring(err)) end
end

function M.dispatch_now(event_id)
    for name, fn in pairs(TURBO_STATE.taps or {}) do
        local ok, err = pcall(fn, event_id)
        if not ok then log.error("%s failed on event %s: %s", name, tostring(event_id), tostring(err)) end
    end
    for name, l in pairs(TURBO_STATE.listeners) do
        if l.ids[event_id] then
            local ok, err = pcall(l.fn, event_id)
            if not ok then log.error("%s failed on event %s: %s", name, tostring(event_id), tostring(err)) end
        end
    end
end

-- Register the single Turbo dispatcher once per Live Editor session.
-- Returns true when the dispatcher is registered.
function M.ensure_registered()
    if type(AddEventHandler) ~= "function" then
        return false, "AddEventHandler is not available in this Live Editor build"
    end
    if TURBO_STATE.dispatcher == nil then
        -- Stable function identity: Live Editor ignores duplicate (event, function) pairs
        TURBO_STATE.dispatcher = function(a, b, c) return M.dispatch(a, b, c) end
        TURBO_STATE.registered = false
    end
    if not TURBO_STATE.registered then
        local ok, err = pcall(AddEventHandler, M.EVENT_NAME, TURBO_STATE.dispatcher)
        if not ok then return false, tostring(err) end
        TURBO_STATE.registered = true
    end
    return true
end

return M
