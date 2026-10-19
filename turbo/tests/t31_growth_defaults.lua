package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'

local sim = H.setup({ in_cm = true })
local W = require 'world'
W.build(sim, {})

print("t31 growth defaults: extra XP and no-decline are OFF by default (2.20 team-defaults)")

-- "Extra XP development" and "Does not decline" must never default to ON: not in the built-in defaults, not in the shipped
-- turbo_config.json (its bulk_edit example included). A later edit that flips either back fails here.
H.case("built-in defaults: the automatic development feature is off and does not decline-protect", function()
    local config = require 'imports/turbo/core/config'
    local d = config.DEFAULTS.auto.development
    H.eq(d.enabled, false, "auto.development.enabled")
    H.eq(d.no_decline, false, "auto.development.no_decline")
    H.eq(d.user_team, false, "auto.development.user_team")
end)

H.case("shipped turbo_config.json: the bulk_edit development example gives no extra XP and no decline protection", function()
    local config = require 'imports/turbo/core/config'
    local cfg, info = config.load()
    H.ok(info.loaded, "loaded: " .. tostring(info.error))
    local be = cfg.modules.bulk_edit
    H.ok(type(be) == "table", "modules.bulk_edit is shipped")
    local ex = be._example
    H.ok(type(ex) == "table", "bulk_edit._example is shipped")
    local dv = ex.actions and ex.actions.development
    H.ok(type(dv) == "table", "the example names the development action")
    H.eq(dv.no_decline, false, "example no_decline")
    H.eq(dv.xp_multiplier, 1.0, "example xp_multiplier (1.0 = no extra XP)")
    H.eq(dv.bonus_xp, 0, "example bonus_xp")
    -- the live (non-example) bulk_edit actions stay empty: nothing runs by default
    H.eq(next(be.actions or {}), nil, "bulk_edit.actions is empty")
    -- and the loaded auto section matches the built-in defaults
    H.eq(cfg.auto.development.no_decline, false, "loaded auto.development.no_decline")
    H.eq(cfg.auto.development.enabled, false, "loaded auto.development.enabled")
end)

H.finish()
