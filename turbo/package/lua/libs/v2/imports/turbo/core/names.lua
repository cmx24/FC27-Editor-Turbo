-- FC 27 LE Turbo - player name texts.
--
-- A player's names come from two places:
--   the game's own: players.firstnameid / lastnameid / commonnameid / playerjerseynameid -> playernames.name
--     (compressed text in FC 27: only Live Editor's GetDBTableRows decodes it, ~0.3 s for the 43,000 names);
--   an editedplayernames row (firstname, surname, commonname, playerjerseyname), which outranks the ids everywhere:
--     lists, match graphics and the shirt (an empty playerjerseyname prints no name on the shirt).
-- The game shows the common name when there is one, else "first last".
--
-- Turbo 1.2.0 and older exported a player without an editedplayernames row as his shown name in commonname, the
-- first name and surname empty (as Live Editor's own export does). Imported back with the Names group, that wrote an
-- editedplayernames row turning "Jacopo Segre" into a common name and blanking his shirt name. M.from_file recovers
-- the real names of such a file, M.restates_game_name finds the rows it left behind (player_presets "repair_names").

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'

local M = {}

M.FIELDS = { "firstname", "surname", "commonname", "playerjerseyname" }
M.ID_OF = { firstname = "firstnameid", surname = "lastnameid", commonname = "commonnameid", playerjerseyname = "playerjerseynameid" }

local function text(v)
    if v == nil then return "" end
    return util.trim(tostring(v))
end

local function cell_value(row, key)
    local cell = util.index(row, key)
    if util.is_object(cell) then cell = util.index(cell, "value") end
    return cell
end

-- nameid -> text of every playernames row, read once per command (pass the same cache table) or nil, why
function M.texts(cache)
    if cache and cache.texts then return cache.texts end
    if cache and cache.texts_err then return nil, cache.texts_err end
    local get_rows = _G["GetDBTableRows"]
    local out, err
    if type(get_rows) ~= "function" then
        err = "GetDBTableRows is not available in this Live Editor build"
    else
        local ok, rows = pcall(get_rows, "playernames")
        if not ok or not util.is_object(rows) then
            err = "the playernames table could not be read: " .. tostring(rows)
        else
            out = {}
            for i = 1, util.len(rows) do
                local row = util.index(rows, i)
                local id = util.to_int(tonumber(cell_value(row, "nameid")))
                local name = cell_value(row, "name")
                if id and id > 0 and type(name) == "string" then out[id] = name end
            end
        end
    end
    if cache then cache.texts, cache.texts_err = out, err end
    return out, err
end

-- The name shown for a set of names: the common name, else "first last"
function M.shown(n)
    if not n then return "" end
    local common = text(n.commonname)
    if common ~= "" then return common end
    return util.trim(text(n.firstname) .. " " .. text(n.surname))
end

local function same_text(a, b) return text(a):lower() == text(b):lower() end

-- Only a shown name: common name filled, first name and surname empty (what the old export wrote)
function M.shown_only(n)
    return n ~= nil and text(n.commonname) ~= "" and text(n.firstname) == "" and text(n.surname) == ""
end

-- The names of a set of name ids ({ firstnameid = .., lastnameid = .., ... } read by get(idfield)) or nil, why
local function names_of_ids(get, cache)
    local texts, err = M.texts(cache)
    if not texts then return nil, err end
    local out, any = {}, false
    for _, f in ipairs(M.FIELDS) do
        local id = util.to_int(get(M.ID_OF[f]))
        local t = (id and id > 0) and texts[id] or nil
        out[f] = t and text(t) or ""
        if out[f] ~= "" and f ~= "playerjerseyname" then any = true end
    end
    if not any then return nil, "no name ids with a text" end
    return out
end

-- The names of the name ids in a { firstnameid = .., lastnameid = .., commonnameid = .., playerjerseynameid = .. }
-- table (a players row's values), or nil, why
function M.names_of_ids(ids, cache)
    if type(ids) ~= "table" then return nil, "no name ids" end
    return names_of_ids(function(f) return ids[f] end, cache)
end

-- The game's own names of a player (his name ids), or nil, why
function M.game_names(players, prec, cache)
    if not players or not prec then return nil, "no player" end
    return names_of_ids(function(f)
        if not db.has_field(players, f) then return nil end
        return players:GetRecordFieldValue(prec, f)
    end, cache)
end

-- The editedplayernames row of a player as { firstname = .., ... } and its record, or nil
function M.edited(pid)
    local edited = db.get_table("editedplayernames")
    if not edited or not db.has_field(edited, "playerid") then return nil end
    local rec = db.find(edited, "playerid", pid)
    if not rec then return nil end
    local out = {}
    for _, f in ipairs(M.FIELDS) do
        out[f] = db.has_field(edited, f) and text(edited:GetRecordFieldValue(rec, f)) or ""
    end
    return out, rec, edited
end

-- The names a player has now: his editedplayernames row, else his name ids' texts. Returns names, source or nil, why
function M.current(pid, players, prec, cache)
    local row = M.edited(pid)
    if row then return row, "editedplayernames" end
    local g, err = M.game_names(players, prec, cache)
    if g then return g, "name ids" end
    return nil, err
end

-- The names a preset file means. A shown-name-only entry (the old export) is replaced by the texts of the file's own
-- name ids when they make exactly that shown name (the same game database). row = the file's row (lower-case keys).
-- Returns names, recovered (true when the ids were used)
function M.from_file(names, row, cache)
    local out = {}
    for _, f in ipairs(M.FIELDS) do out[f] = text(names and names[f]) end
    if not M.shown_only(out) or type(row) ~= "table" then return out, false end
    local g = M.names_of_ids(row, cache)
    if g and same_text(M.shown(g), out.commonname) then return g, true end
    return out, false
end

-- True when names a and b are the same (first name, surname, common name; the shirt name only when both have one)
function M.same(a, b)
    if not a or not b then return false end
    for _, f in ipairs({ "firstname", "surname", "commonname" }) do
        if not same_text(a[f], b[f]) then return false end
    end
    local ja, jb = text(a.playerjerseyname), text(b.playerjerseyname)
    return ja == "" or jb == "" or same_text(ja, jb)
end

-- True when importing names onto a player would change nothing: the same names, or a shown-name-only entry equal to
-- the name he shows now
function M.unchanged(file_names, cur)
    if not cur then return false end
    if M.same(file_names, cur) then return true end
    return M.shown_only(file_names) and same_text(file_names.commonname, M.shown(cur))
end

-- The shirt name for an editedplayernames row: the one given, else the surname, else the common name
function M.jersey(n)
    local j = text(n.playerjerseyname)
    if j ~= "" then return j end
    local s = text(n.surname)
    if s ~= "" then return s end
    return text(n.commonname)
end

-- True when an editedplayernames row (names) only restates the player's own game name as a common name: first name and
-- surname empty, the common name equal to the name his ids show. game = M.game_names of the player.
function M.restates_game_name(row, game)
    if not M.shown_only(row) or not game then return false end
    return same_text(row.commonname, M.shown(game))
        or same_text(row.commonname, util.trim(text(game.firstname) .. " " .. text(game.surname)))
end

return M
