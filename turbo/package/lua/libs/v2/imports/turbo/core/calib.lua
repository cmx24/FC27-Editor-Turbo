-- FC 27 LE Turbo - calibration cache for memory layouts that changed between FC 26 and FC 27.
-- Stored in <output dir>\turbo_calibration.json, keyed by Live Editor version + game module size.
-- Cached values are hints only: every run re-validates them against live memory.

local util = require 'imports/turbo/core/util'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'

local M = {}

local FILE = "turbo_calibration.json"

local function json()
    local ok, j = pcall(require, 'imports/external/json')
    if ok and type(j) == "table" then return j end
    return nil
end

local function path(out_dir)
    if not out_dir then return nil end
    return util.join(out_dir, FILE)
end

function M.load(out_dir)
    local p = path(out_dir)
    local j = json()
    if not p or not j or not util.file_exists(p) then return {} end
    local text = util.read_file(p)
    if not text then return {} end
    local ok, data = pcall(j.decode, text)
    if ok and type(data) == "table" then return data end
    log.warn("ignoring unreadable %s", p)
    return {}
end

function M.get(out_dir, feature)
    local all = M.load(out_dir)
    local entry = all[env.build_key()]
    if type(entry) ~= "table" then return nil end
    return entry[feature]
end

function M.put(out_dir, feature, value)
    local p = path(out_dir)
    local j = json()
    if not p or not j then return false end
    local all = M.load(out_dir)
    local key = env.build_key()
    all[key] = all[key] or {}
    all[key][feature] = value
    local ok, text = pcall(j.encode, all)
    if not ok then return false end
    return util.write_file(p, text)
end

return M
