-- FC 27 LE Turbo - player preset files: Live Editor's "player preset" CSV and Turbo's player JSON.
--
-- Live Editor preset CSV (FC 27 LE extensions\player_presets\<name>.csv, same layout as FC 26 LE player_presets):
--   header row = playerid, the four name columns (firstname, surname, playerjerseyname, commonname) and every
--   players-table field; one row per saved preset. Live Editor appends a row every time it saves, so the LAST row
--   is the newest. FC 26 LE's cards.csv has the same idea with extra columns (uid, name, revision, origin) and only
--   a part of the fields: unknown columns are ignored, missing fields keep their current value.
-- Turbo player JSON (turbo_output\players\<name>.json): every players-table field, the names, the club links and
-- the loan, plus the miniface copied next to it.
--
-- Values are only read here. Writing is done by features/player_presets.lua (import) and features/create_player.lua
-- with every value range-checked against the game's field metadata (core/db.lua).

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'

local M = {}

M.FORMAT_JSON = "turbo-player-preset"
M.NAME_FIELDS = { "firstname", "surname", "playerjerseyname", "commonname" }
-- players fields that are never copied from a file onto another player (identity and name ids)
M.ID_FIELDS = { playerid = true, firstnameid = true, lastnameid = true, commonnameid = true, playerjerseynameid = true }

-- Column order of FC 27 Live Editor's own preset export (v27.1.x), kept so a Turbo file looks exactly like one of
-- Live Editor's for its "Import from preset"
M.LE_HEADER = {
    "playerid", "firstname", "surname", "playerjerseyname", "commonname", "overallrating", "gkglovetypecode",
    "role5", "role2", "role1", "eyebrowcode", "skintypecode", "haircolorcode", "facialhairtypecode", "curve",
    "jerseystylecode", "agility", "tattooback", "accessorycode4", "gksavetype", "positioning", "tattooleftarm",
    "hairtypecode", "facepsdlayer0", "standingtackle", "preferredposition3", "longpassing", "penalties",
    "animfreekickstartposcode", "lipcolor", "isretiring", "longshots", "gkdiving", "icontrait2", "interceptions",
    "shoecolorcode2", "crossing", "potential", "gkreflexes", "finishingcode1", "reactions", "composure",
    "skinsurfacepack", "vision", "contractvaliduntil", "finishing", "dribbling", "slidingtackle", "accessorycode3",
    "preferredposition5", "accessorycolourcode1", "headtypecode", "driref", "sprintspeed", "undershortstyle",
    "height", "hasseasonaljersey", "tattoohead", "preferredposition2", "strength", "shoetypecode", "birthdate",
    "preferredposition1", "tattooleftleg", "skinmakeup", "ballcontrol", "phypos", "shotpower", "trait1",
    "socklengthcode", "weight", "hashighqualityhead", "eyedetail", "tattoorightarm", "icontrait1", "balance",
    "gender", "headassetid", "gkkicking", "defspe", "internationalrep", "preferredposition6", "shortpassing",
    "freekickaccuracy", "skillmoves", "faceposerpreset", "usercaneditname", "avatarpomid", "finishingcode2",
    "aggression", "acceleration", "paskic", "headingaccuracy", "iscustomized", "preferredposition7", "runningcode2",
    "modifier", "gkhandling", "eyecolorcode", "jerseysleevelengthcode", "sockstylecode", "accessorycolourcode3",
    "accessorycode1", "playerjointeamdate", "headclasscode", "tattoofront", "nationality", "preferredfoot",
    "sideburnscode", "weakfootabilitytypecode", "jumping", "personality", "gkkickstyle", "stamina", "firstnameid",
    "accessorycolourcode4", "gkpositioning", "headvariation", "skillmoveslikelihood", "trait2", "shohan",
    "skintonecode", "shortstyle", "role3", "smallsidedshoetypecode", "emotion", "runstylecode", "facepsdlayer1",
    "muscularitycode", "skincomplexion", "jerseyfit", "accessorycode2", "shoedesigncode", "shoecolorcode1",
    "hairstylecode", "bodytypecode", "animpenaltiesstartposcode", "pacdiv", "defensiveawareness", "runningcode1",
    "preferredposition4", "volleys", "accessorycolourcode2", "tattoorightleg", "facialhaircolorcode", "lastnameid",
    "playerjerseynameid", "commonnameid", "role4", "preferredposition5", "preferredposition6", "preferredposition7",
}

-- Older column names (FC 24 / FC 25 / FC 26 Live Editor files) -> FC 27 players field
M.RENAMES = {
    marking = "defensiveawareness",
    sliding = "slidingtackle",
    standing = "standingtackle",
    gkglovetype = "gkglovetypecode",
    jerseynumber = false,          -- a club-link value, not a players field
    teamid = false,
}

-- Groups a user can pick when importing onto an existing player. Every players field not listed below is
-- "appearance" (head, hair, skin, kit, animations, ...).
M.GROUPS = { "profile", "attributes", "positions", "playstyles", "appearance", "contract", "names", "miniface" }
local GROUP_OF = {}
local function group(name, list) for _, f in ipairs(list) do GROUP_OF[f] = name end end
group("profile", { "overallrating", "potential", "birthdate", "height", "weight", "nationality", "gender",
    "preferredfoot", "weakfootabilitytypecode", "skillmoves", "skillmoveslikelihood", "internationalrep",
    "personality", "isretiring", "growthprofile", "modifier", "usercaneditname", "iscustomized", "avatarpomid" })
group("attributes", { "acceleration", "sprintspeed", "positioning", "finishing", "shotpower", "longshots", "volleys",
    "penalties", "vision", "crossing", "freekickaccuracy", "shortpassing", "longpassing", "curve", "agility",
    "balance", "reactions", "ballcontrol", "dribbling", "composure", "interceptions", "headingaccuracy",
    "defensiveawareness", "standingtackle", "slidingtackle", "jumping", "stamina", "strength", "aggression",
    "gkdiving", "gkhandling", "gkkicking", "gkpositioning", "gkreflexes",
    "pacdiv", "shohan", "paskic", "driref", "defspe", "phypos" })
do
    local pos = {}
    for i = 1, 7 do pos[#pos + 1] = "preferredposition" .. i end
    for i = 1, 9 do pos[#pos + 1] = "role" .. i end
    group("positions", pos)
end
group("playstyles", { "trait1", "trait2", "icontrait1", "icontrait2" })
group("contract", { "contractvaliduntil", "wage", "releaseclause", "playerjointeamdate" })

function M.group_of(field)
    if M.ID_FIELDS[field] then return "ids" end
    return GROUP_OF[field] or "appearance"
end

-- ---------------------------------------------------------------- CSV
-- RFC 4180: fields separated by commas, quoted fields may hold commas, quotes ("") and line breaks
function M.parse_csv(text)
    local rows, row, cell = {}, {}, {}
    local i, n = 1, #text
    local quoted = false
    if text:sub(1, 3) == "\239\187\191" then i = 4 end
    local function push_cell() row[#row + 1] = table.concat(cell); cell = {} end
    local function push_row()
        push_cell()
        if #row > 1 or (row[1] or "") ~= "" then rows[#rows + 1] = row end
        row = {}
    end
    while i <= n do
        local c = text:sub(i, i)
        if quoted then
            if c == '"' then
                if text:sub(i + 1, i + 1) == '"' then cell[#cell + 1] = '"'; i = i + 1 else quoted = false end
            else
                cell[#cell + 1] = c
            end
        elseif c == '"' then
            quoted = true
        elseif c == "," then
            push_cell()
        elseif c == "\r" then
            -- ignored (CRLF)
        elseif c == "\n" then
            push_row()
        else
            cell[#cell + 1] = c
        end
        i = i + 1
    end
    if #cell > 0 or #row > 0 then push_row() end
    return rows
end

local function trim(s) return (tostring(s):gsub("^%s+", ""):gsub("%s+$", "")) end

-- Parse a Live Editor preset CSV. Returns { kind = "le_csv", header = {...}, rows = { {col = text} }, count } or nil, err
function M.parse_le_csv(text)
    local raw = M.parse_csv(text)
    if #raw < 1 then return nil, "the CSV file is empty" end
    local header = {}
    for i, h in ipairs(raw[1]) do header[i] = trim(h):lower() end
    local has_pid, has_field = false, false
    for _, h in ipairs(header) do
        if h == "playerid" then has_pid = true end
        if h == "overallrating" or h == "potential" or h == "preferredposition1" then has_field = true end
    end
    if not has_pid or not has_field then
        return nil, "not a Live Editor player preset (the header row needs playerid and players fields such as overallrating)"
    end
    local rows = {}
    for r = 2, #raw do
        local line = raw[r]
        local row = {}
        for i, h in ipairs(header) do
            local v = line[i]
            if v ~= nil and h ~= "" then row[h] = trim(v) end
        end
        if next(row) ~= nil then rows[#rows + 1] = row end
    end
    if #rows == 0 then return nil, "the preset file has a header but no player rows" end
    return { kind = "le_csv", header = header, rows = rows, count = #rows }
end

local function json_lib()
    local ok, j = pcall(require, 'imports/external/json')
    if not ok or type(j) ~= "table" then return nil, "Live Editor json library (imports/external/json) not available" end
    return j
end

-- Parse a Turbo player JSON. Returns { kind = "turbo_json", doc = <table>, rows = { players-fields + names }, count = 1 }
function M.parse_turbo_json(text)
    local j, jerr = json_lib()
    if not j then return nil, jerr end
    local ok, doc = pcall(j.decode, text)
    if not ok or type(doc) ~= "table" then return nil, "not valid JSON: " .. tostring(doc) end
    if doc.format ~= M.FORMAT_JSON or type(doc.players) ~= "table" then
        return nil, "not a Turbo player file (format " .. M.FORMAT_JSON .. ")"
    end
    local row = {}
    for k, v in pairs(doc.players) do row[tostring(k):lower()] = v end
    if type(doc.names) == "table" then
        for _, f in ipairs(M.NAME_FIELDS) do
            if doc.names[f] ~= nil then row[f] = tostring(doc.names[f]) end
        end
    end
    row.playerid = row.playerid or doc.playerid
    return { kind = "turbo_json", doc = doc, rows = { row }, count = 1, miniface = doc.miniface }
end

-- Read and parse a preset file by its content (CSV or JSON). Returns parsed or nil, err
function M.load(path)
    if type(path) ~= "string" or path == "" then return nil, "no preset file given" end
    local text, rerr = util.read_file(path)
    if not text then return nil, "cannot read " .. path .. ": " .. tostring(rerr) end
    local probe = text:gsub("^\239\187\191", ""):match("^%s*(.)")
    local parsed, perr
    if probe == "{" then parsed, perr = M.parse_turbo_json(text) else parsed, perr = M.parse_le_csv(text) end
    if not parsed then return nil, perr end
    parsed.path = path
    parsed.dir = path:match("^(.*)[\\/][^\\/]*$") or "."
    return parsed
end

-- Pick the row to use: opts.row (1-based index), opts.preset_playerid (newest row of that player), else the newest row
function M.pick_row(parsed, opts)
    opts = opts or {}
    local rows = parsed.rows
    local idx = util.to_int(opts.row)
    if idx and idx ~= 0 then
        if idx < 1 or idx > #rows then return nil, string.format("row %d does not exist (the file has %d rows)", idx, #rows) end
        return rows[idx], idx
    end
    local want = util.to_int(opts.preset_playerid)
    if want and want > 0 then
        for r = #rows, 1, -1 do
            if util.to_int(rows[r].playerid) == want then return rows[r], r end
        end
        return nil, string.format("no row for player %d in the file", want)
    end
    return rows[#rows], #rows
end

-- Map a preset row onto the players table: { field = validated value }, names = { firstname = ... },
-- skipped = { "col=value (reason)" }, unknown = { col, ... }. Columns of other games are renamed (M.RENAMES),
-- id fields are left out unless opts.keep_ids, unknown columns are ignored, every value is range-checked.
function M.map_row(players, row, opts)
    opts = opts or {}
    local fields, names, skipped, unknown = {}, {}, {}, {}
    for col, text in pairs(row) do
        local f = col
        if M.RENAMES[f] ~= nil then f = M.RENAMES[f] end
        if f == false then
            -- not a players field
        elseif col == "firstname" or col == "surname" or col == "commonname" or col == "playerjerseyname" then
            names[col] = tostring(text)
        elseif M.ID_FIELDS[f] and not opts.keep_ids then
            -- never copied onto another player
        elseif db.has_field(players, f) then
            if tostring(text) ~= "" then
                local v, err = db.validate(players, f, text)
                if v == nil then skipped[#skipped + 1] = string.format("%s=%s (%s)", f, tostring(text), tostring(err))
                else fields[f] = v end
            end
        elseif col ~= "uid" and col ~= "name" and col ~= "revision" and col ~= "origin" and col ~= "" then
            unknown[#unknown + 1] = col
        end
    end
    table.sort(unknown)
    table.sort(skipped)
    return fields, names, skipped, unknown
end

-- A short description of a row for previews and summaries
function M.describe(row)
    local name = row.commonname
    if name == nil or name == "" then name = trim((row.firstname or "") .. " " .. (row.surname or "")) end
    if name == "" then name = row.name or ("player " .. tostring(row.playerid or "?")) end
    return string.format("%s (OVR %s, POT %s, pos %s)", name, tostring(row.overallrating or "?"),
        tostring(row.potential or "?"), tostring(row.preferredposition1 or "?"))
end

-- A file name from a player name: letters, digits, '-' and '_' only
function M.safe_name(s)
    s = tostring(s or ""):gsub("[^%w%-_]+", "_"):gsub("^_+", ""):gsub("_+$", "")
    if s == "" then s = "player" end
    return s:sub(1, 60)
end

function M.json()
    return json_lib()
end

return M
