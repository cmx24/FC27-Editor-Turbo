-- Turbo core: the game asset gate, Lua side of the asset catalog (docs/asset-catalog.md, sections 5 and 7).
-- The Turbo GUI writes turbo_output\asset_catalog\lua_gate.txt only while its catalog matches the running game:
--   #turbo-asset-gate 1
--   #build 6AC07E31-2145C000
--   #session <the GUI session of bridge_dll.json>
--   head3d 41
--   head3d 250            (one "<family> <id>" line per id; Lua uses head3d, and miniface / callname lines when present)
-- The gate is trusted ("current") only when every one of these holds:
--   * no kill switch turbo_output\asset_catalog_off.txt (present = "off": the catalog is ignored, today's rules);
--   * the file exists, starts with the header, has no malformed line and lists at least one head3d id;
--   * its #session equals the session of a fresh turbo_output\bridge_dll.json (the GUI of THIS game session wrote it).
--     A gate left by an earlier game session (a title update, the GUI not running) is never trusted.
-- Otherwise the status is "none", "bad" or "stale", the features log "unchecked" and apply today's rules (with the
-- create_player fix: a real face only when the DB row of that player has headclasscode 0 and headassetid == playerid).
-- turbo_output\asset_hidden_off.txt: the gate stays on, but heads no row of the save uses ("hidden") are refused.
-- turbo_output\asset_gate_allow.txt: ids the user vouches for (heads added by mods): one id (or "head3d <id>") per line.
-- Every write of a head by a gated feature is appended to turbo_output\asset_writes.log (tab separated, section 6).
-- The file is read on each call of load() (a feature run), never per frame: about 6,000 lines, a few milliseconds.

local util = require 'imports/turbo/core/util'
local log = require 'imports/turbo/core/log'
local env = require 'imports/turbo/core/env'

local M = {}

M.HEADER = "#turbo-asset-gate 1"
M.GATE_DIR = "asset_catalog"
M.GATE_FILE = "lua_gate.txt"
M.CATALOG_OFF = "asset_catalog_off.txt"
M.HIDDEN_OFF = "asset_hidden_off.txt"
M.ALLOW_FILE = "asset_gate_allow.txt"
M.WRITE_LOG = "asset_writes.log"
M.DLL_FILE = "bridge_dll.json"
M.DLL_FRESH_SECONDS = 15        -- same rule as bridge.lua: an older bridge_dll.json is left over from another session
M.MAX_ID = 0x7FFFFFFF

-- <Live Editor>\turbo_output, or nil
function M.out_dir()
    local root = env.le_root()
    if not root then return nil end
    return util.join(root, "turbo_output")
end

local function parse_id(s)
    if type(s) ~= "string" or not s:match("^%d+$") or #s > 10 then return nil end
    local v = math.tointeger(tonumber(s))
    if not v or v < 0 or v > M.MAX_ID then return nil end
    return v
end

local function lines(text)
    local out = {}
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do out[#out + 1] = (line:gsub("\r$", "")) end
    return out
end

-- Parse a gate file. Returns { build, session, fam = { head3d = {id=true}, ... }, count = { head3d = n } } or nil, error
function M.parse(text)
    if type(text) ~= "string" or text == "" then return nil, "empty file" end
    text = text:gsub("^\239\187\191", "")   -- UTF-8 BOM
    local ls = lines(text)
    local i = 1
    while i <= #ls and util.trim(ls[i]) == "" do i = i + 1 end
    if util.trim(ls[i] or "") ~= M.HEADER then return nil, "the first line is not \"" .. M.HEADER .. "\"" end
    local g = { fam = {}, count = {} }
    local declared
    for n = i + 1, #ls do
        local line = util.trim(ls[n])
        if line ~= "" then
            local key, val = line:match("^#(%a+)%s*(.-)$")
            if key then
                if key == "build" then g.build = val
                elseif key == "session" then g.session = val
                elseif key == "count" then
                    declared = parse_id(val)
                    if not declared then return nil, string.format("line %d: bad #count", n) end
                end   -- other "#" lines are comments
            elseif line:sub(1, 1) == "#" then
                -- comment
            else
                local fam, id = line:match("^([%a_][%w_]*)%s+(%S+)$")
                local v = parse_id(id)
                if not fam or not v then return nil, string.format("line %d is not \"<family> <id>\": %s", n, line:sub(1, 60)) end
                local set = g.fam[fam]
                if not set then set = {}; g.fam[fam] = set; g.count[fam] = 0 end
                if not set[v] then set[v] = true; g.count[fam] = g.count[fam] + 1 end
            end
        end
    end
    if (g.count.head3d or 0) == 0 then return nil, "the gate lists no head3d id" end
    if declared and declared ~= g.count.head3d then
        return nil, string.format("#count %d but %d head3d ids (file cut short?)", declared, g.count.head3d)
    end
    return g
end

-- Ids the user vouches for: "<id>" or "head3d <id>" per line, "#" comments. Returns a set (bad lines are skipped).
function M.parse_allow(text)
    local set, bad = {}, 0
    if type(text) ~= "string" then return set, bad end
    for _, raw in ipairs(lines(text)) do
        local line = util.trim(raw)
        if line ~= "" and line:sub(1, 1) ~= "#" then
            local id = line:match("^head3d%s+(%S+)$") or line
            local v = parse_id(id)
            if v then set[v] = true else bad = bad + 1 end
        end
    end
    return set, bad
end

-- Session of a fresh bridge_dll.json (the GUI of this game session), or nil
function M.current_session(out_dir, now)
    if not out_dir then return nil end
    local text = util.read_file(util.join(out_dir, M.DLL_FILE))
    if type(text) ~= "string" then return nil end
    local session = text:match('"session"%s*:%s*"([^"]*)"')
    local updated = tonumber(text:match('"updated"%s*:%s*(%-?%d+)'))
    if not session or session == "" or not updated then return nil end
    if math.abs((now or os.time()) - updated) > M.DLL_FRESH_SECONDS then return nil end
    return session
end

local function switch_on(out_dir, name)
    return out_dir ~= nil and util.file_exists(util.join(out_dir, name))
end

-- The gate state for one feature run.
-- opts: out_dir (default <LE>\turbo_output), session (default: bridge_dll.json), now (os.time())
-- Returns { status = "current" | "off" | "none" | "bad" | "stale", reason, build, heads (set or nil), fam,
--           allow (set), hidden_off (bool), out_dir }
function M.load(opts)
    opts = opts or {}
    local out = opts.out_dir or M.out_dir()
    local st = { status = "none", allow = {}, hidden_off = false, out_dir = out }
    if not out then
        st.reason = "no turbo_output folder"
        return st
    end
    if switch_on(out, M.CATALOG_OFF) then
        st.status, st.reason = "off", "asset catalog switched off (turbo_output\\" .. M.CATALOG_OFF .. ")"
        return st
    end
    st.hidden_off = switch_on(out, M.HIDDEN_OFF)
    local atext = util.read_file(util.join(out, M.ALLOW_FILE))
    if atext then
        local bad
        st.allow, bad = M.parse_allow(atext)
        if bad > 0 then log.warn("asset gate: %d unreadable line(s) in turbo_output\\%s skipped", bad, M.ALLOW_FILE) end
    end
    local path = util.join(util.join(out, M.GATE_DIR), M.GATE_FILE)
    local text = util.read_file(path)
    if not text then
        st.reason = "no asset gate (the Turbo GUI writes turbo_output\\" .. M.GATE_DIR .. "\\" .. M.GATE_FILE ..
            " while its asset catalog matches the game)"
        return st
    end
    local g, perr = M.parse(text)
    if not g then
        st.status, st.reason = "bad", "asset gate not readable: " .. tostring(perr)
        return st
    end
    st.build, st.fam = g.build, g.fam
    local session = opts.session
    if session == nil then session = M.current_session(out, opts.now) end
    if not g.session or g.session == "" then
        st.status, st.reason = "stale", "asset gate has no #session line"
    elseif not session then
        st.status, st.reason = "stale", "the Turbo GUI is not running in this game session (bridge_dll.json missing or old): " ..
            "the asset gate cannot be matched to it"
    elseif session ~= g.session then
        st.status, st.reason = "stale", string.format("asset gate written by another Turbo GUI session (%s, now %s)",
            g.session, session)
    else
        st.status = "current"
        st.heads = g.fam.head3d
        st.reason = string.format("checked against the game files (asset catalog %s)", g.build or "?")
    end
    return st
end

function M.trusted(st)
    return type(st) == "table" and st.status == "current" and st.heads ~= nil
end

-- Key written to asset_writes.log
function M.key(st)
    if M.trusted(st) then return st.build or "?" end
    return "unchecked"
end

-- Heads the save uses. used = players.headassetid (any class) + manager.headassetid; real = playerids whose own row
-- has headclasscode 0 and headassetid == playerid (the DB proves the game ships that real head).
function M.db_heads()
    local db = require 'imports/turbo/core/db'
    local used, real = {}, {}
    local players = db.get_table("players")
    if players and db.has_field(players, "headassetid") then
        local hcc = db.has_field(players, "headclasscode")
        local pidf = db.has_field(players, "playerid")
        for rec in db.records(players) do
            local h = players:GetRecordFieldValue(rec, "headassetid")
            if h then
                used[h] = true
                if hcc and pidf and players:GetRecordFieldValue(rec, "headclasscode") == 0
                    and players:GetRecordFieldValue(rec, "playerid") == h then
                    real[h] = true
                end
            end
        end
    end
    local mgr = db.get_table("manager")
    if mgr and db.has_field(mgr, "headassetid") then
        for rec in db.records(mgr) do
            local h = mgr:GetRecordFieldValue(rec, "headassetid")
            if h then used[h] = true end
        end
    end
    return { used = used, real = real }
end

-- May `id` be written as a class-0 head?
--   st: load(); heads: db_heads() (or nil: built when needed); strict: true = create_player's rule when unchecked
--   (the DB must prove the head), false = today's rule when unchecked (any id, range checked by the caller).
-- Returns ok, reason (always a short text), hidden (true when no row of the save uses the id)
function M.check_head(st, id, heads, strict)
    local function dbh()
        heads = heads or M.db_heads()
        return heads
    end
    id = math.tointeger(id)
    local unchecked = "asset gate unchecked: " .. tostring(st and st.reason or "no gate")
    if not M.trusted(st) and not strict then return true, unchecked, id ~= nil and not dbh().used[id] end
    if not id or id <= 0 then return false, "no head id", false end
    if M.trusted(st) then
        local proved = st.heads[id] or st.allow[id]
        if not proved then
            return false, string.format("head %d is not in the game files (asset catalog %s)", id, st.build or "?"), false
        end
        local hidden = not dbh().used[id]
        if hidden and st.hidden_off then
            return false, string.format("head %d is used by no player of this save and hidden heads are switched off " ..
                "(turbo_output\\%s)", id, M.HIDDEN_OFF), true
        end
        return true, st.heads[id] and "in the game files" or ("vouched for in turbo_output\\" .. M.ALLOW_FILE), hidden
    end
    -- unchecked, create_player's rule: the DB must prove the real head
    if dbh().real[id] then return true, "the save has his real head; " .. unchecked, false end
    if st and st.status ~= "off" and st.allow and st.allow[id] then
        return true, "vouched for in turbo_output\\" .. M.ALLOW_FILE .. "; " .. unchecked, not dbh().used[id]
    end
    return false, string.format("player %d has no real head in this save (headclasscode 0); %s", id, unchecked), false
end

-- Ids a new player should not get (they carry a game head, miniface or callname recording): a set, empty unless trusted
function M.taken_ids(st)
    local out = {}
    if not M.trusted(st) then return out end
    for fam, set in pairs(st.fam or {}) do
        if fam == "head3d" or fam == "miniface" or fam:match("^callname") then
            for id in pairs(set) do out[id] = fam end
        end
    end
    return out
end

-- Local time, ISO 8601 with the UTC offset (2026-10-12T10:03:11-04:00)
function M.iso_now(t)
    t = t or os.time()
    local loc, utc = os.date("*t", t), os.date("!*t", t)
    utc.isdst = loc.isdst
    local off = math.floor(os.difftime(os.time(loc), os.time(utc)) / 60 + 0.5)
    local sign = off < 0 and "-" or "+"
    off = math.abs(off)
    return os.date("%Y-%m-%dT%H:%M:%S", t) .. string.format("%s%02d:%02d", sign, off // 60, off % 60)
end

-- Append one line to turbo_output\asset_writes.log: time, target, field, old, new, catalog key (tab separated)
function M.log_write(st, target, field, old, new)
    local out = (st and st.out_dir) or M.out_dir()
    if not out then return false end
    local line = table.concat({ M.iso_now(), tostring(target), tostring(field), tostring(old), tostring(new), M.key(st) }, "\t")
    local f = io.open(util.join(out, M.WRITE_LOG), "ab")
    if not f then
        log.warn("asset gate: %s not writable (%s %s %s -> %s)", M.WRITE_LOG, tostring(target), tostring(field),
            tostring(old), tostring(new))
        return false
    end
    f:write(line, "\r\n")
    f:close()
    return true
end

return M
