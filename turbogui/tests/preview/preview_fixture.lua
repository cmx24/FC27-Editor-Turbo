-- tools/gui-preview: swaps the data block of tests/native/gui_world.lua for the preview host's fake squad (preview_fixture.json).
-- Called from gui_world.lua build_world when TURBO_PREVIEW_FIXTURE names the json file. Mutates the caller's tables in place.
-- Everything else (schemas, attributes derived from the row index, kits, fixtures, managers) stays gui_world.lua's.
local M = {}

-- FC position ids (the game's preferredposition values)
local POS = { GK = 0, RB = 3, CB = 5, LB = 7, RWB = 2, LWB = 8, CDM = 10, RM = 12, CM = 14, LM = 16, CAM = 18, RW = 23, ST = 25, LW = 27, CF = 21 }

local function read(path)
    local f = assert(io.open(path, "rb"), "cannot open the fixture " .. tostring(path))
    local d = f:read("a")
    f:close()
    return d
end

function M.apply(path, w)
    local json = require 'imports/external/json'
    local fx = json.decode(read(path))
    local NAMES, PLAYERS, TEAMS, MANAGERS = w.NAMES, w.PLAYERS, w.TEAMS, w.MANAGERS
    -- names: keep ids 1..20 out of the way (the world's own generic name 20 stays), fixture names from 21
    local ids = {}
    local function name_id(s)
        if not ids[s] then
            NAMES[#NAMES + 1] = s
            ids[s] = #NAMES
        end
        return ids[s]
    end
    for i = #PLAYERS, 1, -1 do PLAYERS[i] = nil end
    local club = fx.club.teamid
    for _, p in ipairs(fx.players) do
        local y, m, d = p.born:match("^(%d+)-(%d+)-(%d+)$")
        local pos = assert(POS[p.pos], "unknown position " .. tostring(p.pos))
        PLAYERS[#PLAYERS + 1] = {
            p.id, name_id(p.first), name_id(p.last), p.common and name_id(p.common) or 0, { p.team or club },
            p.ovr, p.pot, pos, tonumber(y), tonumber(m), tonumber(d),
        }
    end
    -- teams: rename by id (the user club and the two clubs the world's managers coach); the national team and free agents stay
    local by_id = {}
    for _, t in ipairs(TEAMS) do by_id[t[1]] = t end
    local function set_team(t)
        local row = by_id[t.teamid]
        assert(row, "the world has no team " .. tostring(t.teamid))
        row[2], row[3] = t.name, t.overall
        if t.league then row[4] = t.league end
    end
    set_team(fx.club)
    for _, t in ipairs(fx.teams or {}) do set_team(t) end
    -- the world's managers (Arteta, Moyes, Chivu) keep their ids; the user club's manager gets a fake name too
    for _, m in ipairs(MANAGERS) do
        if m[4] == club then m[2], m[3] = "Alan", "Whitcombe" end
    end
end

return M
