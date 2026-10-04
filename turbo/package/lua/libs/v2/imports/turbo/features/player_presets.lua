-- Turbo feature: export players to preset files and import preset files onto an existing player.
--   "player_presets": {
--     "mode": "export",                         -- one file set per player
--     "playerids": [158023, 20801],             -- (or "playerid": 158023)
--     "name": "",                               -- file name without extension ("" = the player's name + id)
--     "csv": true,                              -- Live Editor preset CSV (its "Import from preset" reads it)
--     "json": true,                             -- Turbo player JSON (every field, names, club links, loan, miniface)
--     "miniface": true,                         -- copy the miniface next to the JSON
--     "preset_dir": "",                         -- "" = <Live Editor>\extensions\player_presets
--     "json_dir": ""                            -- "" = <output>\players (Turbo JSON and miniface)
--   }
--   "player_presets": {
--     "mode": "import", "file": "C:\\...\\rossi.csv", "playerid": 158023,
--     "groups": ["profile", "attributes", "positions", "playstyles", "appearance", "contract", "names", "miniface"],
--     "row": 0, "preset_playerid": 0           -- which row of a multi-row CSV (0 = the newest row)
--   }
-- Live Editor CSV columns are read by name: FC 26 / FC 25 files work too (renamed fields are mapped, unknown ones
-- skipped), every value is range-checked, and a value outside the FC 27 field range is skipped and reported.
-- JSON files go to <output>\players\. Importing onto a NEW player is features/create_player.lua.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local csv = require 'imports/turbo/core/csv'
local env = require 'imports/turbo/core/env'
local game = require 'imports/turbo/core/game'
local preset = require 'imports/turbo/core/preset'
local version = require 'imports/turbo/core/version'

local M = {}

M.MINIFACE_PATH = "data/ui/imgAssets/heads/p%d.dds"

-- True when the folder exists, without starting a process. os.rename(dir, dir) succeeds for a folder nobody holds
-- open, but fails for one in use (the Live Editor folder while the game runs); opening a folder as a file fails with
-- EACCES (13) on Windows where a missing path fails with ENOENT (2), and succeeds on POSIX.
local function dir_exists(dir)
    if os.rename(dir, dir) then return true end
    local f, _, code = io.open(dir, "rb")
    if f then
        f:close()
        return true
    end
    return code == 13
end
M.dir_exists = dir_exists

-- Creates a folder and its parents. Works with cmd.exe (Live Editor's Lua) and sh (the test harness): every prefix of
-- the path that does not exist yet is created on its own. cmd.exe opens a console window, which takes the game out of
-- full screen (1.1.0 playtest: "Export goes to the desktop and blinks"): the Turbo GUI creates its folders itself
-- before it sends a command, so this only runs mkdir for a folder that really is missing.
local function mkdir(dir)
    if not dir or dir == "" then return false end
    local pos = 1
    while true do
        local s = dir:find("[\\/]", pos + 1)
        local prefix = s and dir:sub(1, s - 1) or dir
        if #prefix > 0 and not prefix:match("^%a:$") and not dir_exists(prefix) then
            os.execute(string.format('mkdir "%s"', prefix))
        end
        if not s then break end
        pos = s
    end
    return true
end
M.mkdir = mkdir

function M.preset_dir(cfg)
    local d = type(cfg.preset_dir) == "string" and cfg.preset_dir ~= "" and cfg.preset_dir or nil
    if d then return d end
    local root = env.le_root()
    if not root then return nil end
    return util.join(util.join(root, "extensions"), "player_presets")
end

-- Folder of the Turbo JSON (and miniface): json_dir, else <output>\players
function M.json_dir(ctx)
    local d = ctx.cfg.json_dir
    if type(d) == "string" and d ~= "" then return d end
    return util.join(ctx.out_dir, "players")
end

-- Name texts of a player: the editedplayernames row, else the game's display name as commonname (like LE's export)
local function names_of(pid)
    local out = { firstname = "", surname = "", commonname = "", playerjerseyname = "" }
    local edited = db.get_table("editedplayernames")
    if edited and db.has_field(edited, "playerid") then
        local rec = db.find(edited, "playerid", pid)
        if rec then
            for _, f in ipairs(preset.NAME_FIELDS) do
                if db.has_field(edited, f) then out[f] = tostring(edited:GetRecordFieldValue(rec, f) or "") end
            end
            return out, true
        end
    end
    out.commonname = game.player_name(pid)
    if out.commonname == tostring(pid) then out.commonname = "" end
    return out, false
end

-- Every field of the players row as { field = value }
local function row_values(players, rec)
    local vals = {}
    for _, f in ipairs(db.field_names(players)) do vals[f] = players:GetRecordFieldValue(rec, f) end
    return vals
end

-- Where the player's miniface can be found right now: custom file (mods\legacy), the Turbo picture cache, or the
-- game's legacy file through Live Editor (exported straight into dest). Returns true when dest was written.
local function copy_miniface(pid, dest, dry)
    local rel = string.format(M.MINIFACE_PATH, pid)
    local root = env.le_root()
    local candidates = {}
    if root then candidates[#candidates + 1] = util.join(util.join(root, "mods"), "legacy") .. "\\" .. rel:gsub("/", "\\") end
    local okl, legacy = pcall(require, 'imports/turbo/core/legacy')
    if okl and legacy.dir() then candidates[#candidates + 1] = util.join(legacy.dir(), rel) end
    for _, src in ipairs(candidates) do
        local data = util.file_exists(src) and util.read_file(src)
        if data and #data > 128 then
            if dry then return true end
            return util.write_file(dest, data)
        end
    end
    if type(LegacyFileExist) == "function" and type(LegacyFileExport) == "function" then
        local oke, exists = pcall(LegacyFileExist, rel)
        if oke and exists then
            if dry then return true end
            local okx, done = pcall(LegacyFileExport, rel, dest)
            return okx and done ~= false and util.file_exists(dest)
        end
    end
    return false
end

local function export_one(ctx, players, pid, opts)
    local rec = db.find(players, "playerid", pid)
    if not rec then return nil, string.format("player %d not found", pid) end
    local vals = row_values(players, rec)
    local names = names_of(pid)
    local base = opts.name
    if not base or base == "" then
        local n = names.commonname ~= "" and names.commonname or util.trim(names.firstname .. " " .. names.surname)
        if n == "" then n = game.player_name(pid) end
        base = preset.safe_name(n) .. "_" .. pid
    end
    local made = {}

    if opts.csv then
        local dir = M.preset_dir(ctx.cfg)
        if not dir then return nil, "no preset folder (Live Editor folder unknown)" end
        local row = {}
        for _, col in ipairs(preset.LE_HEADER) do
            if names[col] ~= nil then row[col] = names[col]
            elseif vals[col] ~= nil then row[col] = vals[col]
            else row[col] = "" end
        end
        local path = util.join(dir, base .. ".csv")
        if not ctx.dry then
            mkdir(dir)
            local ok, err = csv.write(path, preset.LE_HEADER, { row })
            if not ok then return nil, "cannot write " .. path .. ": " .. tostring(err) end
        end
        made[#made + 1] = path
    end

    if opts.json then
        local j, jerr = preset.json()
        if not j then return nil, jerr end
        local dir = M.json_dir(ctx)
        local path = util.join(dir, base .. ".json")
        local links = {}
        local lt = db.get_table("teamplayerlinks")
        if lt and db.has_fields(lt, { "teamid", "playerid" }) then
            local okm, moves = pcall(require, 'imports/turbo/core/moves')
            local nat = okm and moves.national_teams() or {}
            for lrec in db.records(lt) do
                if lt:GetRecordFieldValue(lrec, "playerid") == pid then
                    local tid = lt:GetRecordFieldValue(lrec, "teamid")
                    links[#links + 1] = {
                        teamid = tid, teamname = game.team_name(tid), national = nat[tid] == true,
                        jerseynumber = db.has_field(lt, "jerseynumber") and lt:GetRecordFieldValue(lrec, "jerseynumber") or nil,
                        position = db.has_field(lt, "position") and lt:GetRecordFieldValue(lrec, "position") or nil,
                    }
                end
            end
        end
        local loan
        local loans = db.get_table("playerloans")
        if loans and db.has_field(loans, "playerid") then
            local lrec = db.find(loans, "playerid", pid)
            if lrec then
                loan = {}
                for _, f in ipairs(db.field_names(loans)) do loan[f] = loans:GetRecordFieldValue(lrec, f) end
            end
        end
        local doc = {
            format = preset.FORMAT_JSON, version = 1, turbo = version.version,
            exported = os.date("%Y-%m-%d %H:%M:%S"), playerid = pid,
            name = names.commonname ~= "" and names.commonname or util.trim(names.firstname .. " " .. names.surname),
            players = vals, names = names, links = links, loan = loan,
        }
        if doc.name == "" then doc.name = game.player_name(pid) end
        local mini
        if opts.miniface then
            mini = base .. ".dds"
            local dest = util.join(dir, mini)
            if not ctx.dry then mkdir(dir) end
            if copy_miniface(pid, dest, ctx.dry) then doc.miniface = mini else mini = nil end
        end
        if not ctx.dry then
            mkdir(dir)
            local text = j.encode(doc)
            local ok, err = util.write_file(path, text)
            if not ok then return nil, "cannot write " .. path .. ": " .. tostring(err) end
        end
        made[#made + 1] = path
        if mini then made[#made + 1] = mini end
    end
    return made
end

local function run_export(ctx)
    local cfg = ctx.cfg
    local ids = {}
    if type(cfg.playerids) == "table" then
        for _, v in ipairs(cfg.playerids) do
            local pid = util.to_int(v)
            if not pid then return false, "playerids must be integers" end
            ids[#ids + 1] = pid
        end
    end
    if cfg.playerid ~= nil and cfg.playerid ~= 0 then   -- 0 = the config default (not a player)
        local pid = util.to_int(cfg.playerid)
        if not pid then return false, "playerid must be an integer" end
        ids[#ids + 1] = pid
    end
    if #ids == 0 then return false, "no player given (playerid or playerids)" end
    local opts = { csv = cfg.csv ~= false, json = cfg.json ~= false, miniface = cfg.miniface ~= false, name = cfg.name }
    if #ids > 1 then opts.name = nil end   -- one name per player
    if not opts.csv and not opts.json then return false, "nothing to write: csv and json are both off" end
    if not ctx.out_dir then return false, "no writable output folder" end
    local players, err = db.get_table("players")
    if not players then return false, err end

    local files, failed = {}, {}
    for _, pid in ipairs(ids) do
        local made, merr = export_one(ctx, players, pid, opts)
        if made then
            for _, f in ipairs(made) do files[#files + 1] = f end
        else
            failed[#failed + 1] = string.format("%d: %s", pid, tostring(merr))
        end
    end
    local parts = {}
    if #ids == 1 and #files > 0 then
        parts[#parts + 1] = "exported player " .. ids[1] .. ": " .. table.concat(files, ", ")
    elseif #files > 0 then
        parts[#parts + 1] = string.format("exported %d players (%d files)", #ids - #failed, #files)
        if opts.csv then parts[#parts + 1] = "CSV in " .. tostring(M.preset_dir(cfg)) end
        if opts.json then parts[#parts + 1] = "JSON in " .. M.json_dir(ctx) end
    end
    if #failed > 0 then parts[#parts + 1] = "failed: " .. table.concat(failed, "; ") end
    return #failed == 0, table.concat(parts, "; ")
end

-- ---------------------------------------------------------------- import
local function group_set(cfg)
    local set = {}
    if type(cfg.groups) == "table" then
        local any = false
        for k, v in pairs(cfg.groups) do
            any = true
            if type(k) == "number" then set[tostring(v)] = true elseif v == true then set[k] = true end
        end
        if any then return set end
    end
    for _, g in ipairs(preset.GROUPS) do set[g] = true end
    return set
end

-- Writes the names into editedplayernames (existing row updated, else a row inserted). Validated first.
local function plan_names(pid, names, dry)
    local edited = db.get_table("editedplayernames")
    if not edited or not db.has_field(edited, "playerid") then return nil, "editedplayernames table not available" end
    local row = {}
    for _, f in ipairs(preset.NAME_FIELDS) do
        if db.has_field(edited, f) then
            local v, err = db.validate(edited, f, names[f] or "")
            if v == nil then return nil, err end
            row[f] = v
        end
    end
    local rec = db.find(edited, "playerid", pid)
    return function()
        if dry then return true end
        if rec then
            for f, v in pairs(row) do
                local ok, err = db.set(edited, rec, f, v)
                if not ok then return false, err end
            end
            return true
        end
        local insert = _G["InsertDBTableRow"]
        if type(insert) ~= "function" then return false, "InsertDBTableRow is not available in this Live Editor build" end
        local srow = { playerid = tostring(pid) }
        for f, v in pairs(row) do srow[f] = tostring(v) end
        local okc, res = pcall(insert, "editedplayernames", srow)
        -- the table object caches its record count (t3db TABLE:Load reads written_records once), so a table that
        -- had no rows before the insert must be opened again to see the new row (seen in game, 03-10-2026)
        local fresh = db.get_table("editedplayernames")
        if not okc or type(res) ~= "table" or not fresh or not db.find(fresh, "playerid", pid) then
            return false, "could not add the editedplayernames row: " .. (type(res) == "table" and "row not found after the insert" or tostring(res))
        end
        return true
    end
end

-- Install a miniface file as the player's custom miniface (<LE>\mods\legacy\...), backing up the old one
function M.install_miniface(pid, src, dry)
    local root = env.le_root()
    if not root then return false, "Live Editor folder unknown" end
    local data = util.file_exists(src) and util.read_file(src)
    if not data or #data < 128 or data:sub(1, 4) ~= "DDS " then return false, "not a DDS miniface: " .. tostring(src) end
    if dry then return true end
    local rel = string.format(M.MINIFACE_PATH, pid):gsub("/", "\\")
    local dir = util.join(util.join(root, "mods"), "legacy")
    for part in rel:gmatch("([^\\]+)\\") do
        dir = util.join(dir, part)
        mkdir(dir)
    end
    local dest = util.join(dir, rel:match("([^\\]+)$"))
    if util.file_exists(dest) then
        local bdir = util.join(env.output_dir() or root, "miniface_backups")
        mkdir(bdir)
        local old = util.read_file(dest)
        if old then util.write_file(util.join(bdir, string.format("p%d_%s.dds", pid, os.date("%Y%m%d_%H%M%S"))), old) end
    end
    return util.write_file(dest, data)
end

local function run_import(ctx)
    local cfg = ctx.cfg
    local pid = util.to_int(cfg.playerid)
    if not pid or pid < 0 then return false, "playerid must be the id of an existing player" end
    local players, err = db.get_table("players")
    if not players then return false, err end
    local rec = db.find(players, "playerid", pid)
    if not rec then return false, string.format("player %d not found", pid) end

    local parsed, perr = preset.load(cfg.file)
    if not parsed then return false, perr end
    local row, ridx = preset.pick_row(parsed, cfg)
    if not row then return false, ridx end
    local fields, names, skipped, unknown = preset.map_row(players, row, {})
    local groups = group_set(cfg)

    -- plan: every players write validated, names and miniface prepared, before anything is written
    local plan, counts = {}, {}
    for f, v in pairs(fields) do
        local g = preset.group_of(f)
        if groups[g] then
            plan[#plan + 1] = { f = f, v = v }
            counts[g] = (counts[g] or 0) + 1
        end
    end
    table.sort(plan, function(a, b) return a.f < b.f end)
    local names_fn
    if groups.names then
        local any = false
        for _, f in ipairs(preset.NAME_FIELDS) do if (names[f] or "") ~= "" then any = true end end
        if any then
            local fn, nerr = plan_names(pid, names, ctx.dry)
            if not fn then return false, nerr end
            names_fn = fn
        end
    end
    local mini_src
    if groups.miniface and parsed.kind == "turbo_json" and type(parsed.miniface) == "string" and parsed.miniface ~= "" then
        mini_src = util.join(parsed.dir, parsed.miniface)
        if not util.file_exists(mini_src) then return false, "miniface file missing next to the JSON: " .. mini_src end
    end
    if #plan == 0 and not names_fn and not mini_src then
        return false, "nothing to import for the chosen groups" .. (#skipped > 0 and (" (skipped: " .. table.concat(skipped, ", ") .. ")") or "")
    end

    if not ctx.dry then
        for _, w in ipairs(plan) do
            local ok, werr = db.set(players, rec, w.f, w.v)
            if not ok then return false, string.format("players.%s: %s", w.f, tostring(werr)) end
        end
        if names_fn then
            local ok, nerr = names_fn()
            if not ok then return false, nerr end
        end
        if mini_src then
            local ok, merr = M.install_miniface(pid, mini_src, false)
            if not ok then return false, merr end
        end
    end
    local parts = {}
    for _, g in ipairs(preset.GROUPS) do if counts[g] then parts[#parts + 1] = string.format("%s %d", g, counts[g]) end end
    if names_fn then parts[#parts + 1] = "names" end
    if mini_src then parts[#parts + 1] = "miniface" end
    local msg = string.format("player %d <- %s (row %d of %d, %s): %d fields written [%s]", pid,
        tostring(parsed.path):match("([^\\/]+)$"), ridx, parsed.count, preset.describe(row), #plan, table.concat(parts, ", "))
    if #skipped > 0 then msg = msg .. "; skipped out-of-range: " .. table.concat(skipped, ", ") end
    if #unknown > 0 then msg = msg .. "; ignored columns: " .. table.concat(unknown, ", ") end
    return true, msg
end

function M.run(ctx)
    local mode = ctx.cfg.mode or "export"
    if mode == "export" then return run_export(ctx) end
    if mode == "import" then return run_import(ctx) end
    return false, "unknown mode " .. tostring(mode) .. " (export | import)"
end

return M
