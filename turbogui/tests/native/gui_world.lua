-- Builds an FC 27-like database for the Turbo GUI native tests inside the Turbo Lua test simulator,
-- starts Turbo's bridge the way turbo_gui_load.lua does (so the real bridge writes bridge_meta.json /
-- bridge_state.json), and dumps:
--   world.img       the simulated game memory (pages)
--   expected.json   every value of every table as read by Live Editor's own Lua T3DB library,
--                   plus the player/team/manager facts the GUI model must derive
--   LE\turbo_output\bridge_*.json
--
-- usage: lua5.4 gui_world.lua build <out_dir>
--        lua5.4 gui_world.lua verify_writes <out_dir>   (reads after_writes.img + writes.json)
--        lua5.4 gui_world.lua mailbox <out_dir>         (reads mailbox_in.img + mailbox.json)

local HERE = debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./"
local TURBO_TESTS = os.getenv("TURBO_TESTS") or (HERE .. "../../../turbo/tests")
package.path = TURBO_TESTS .. "/?.lua;" .. TURBO_TESTS .. "/mock/?.lua;" .. package.path

local mode, OUT = arg[1], arg[2]
assert(mode and OUT, "usage: gui_world.lua build|verify_writes|mailbox <out_dir>")

local H = require 'h'
local PAGE = 4096

-- ------------------------------------------------------------------ image I/O
local function save_image(sim, path)
    local pages = {}
    for p in pairs(sim.pages) do pages[#pages + 1] = p end
    table.sort(pages)
    local f = assert(io.open(path, "wb"))
    f:write("TSIM", string.pack("<I4I8", 1, #pages))
    for _, p in ipairs(pages) do
        local base = p * PAGE
        local bytes = {}
        for i = 0, PAGE - 1 do bytes[i + 1] = string.char(sim.mem[base + i] or 0) end
        f:write(string.pack("<I8", p), table.concat(bytes))
    end
    f:close()
end

local function load_image(sim, path)
    local f = assert(io.open(path, "rb"))
    local data = f:read("a")
    f:close()
    assert(data:sub(1, 4) == "TSIM", "bad image " .. path)
    local _, n, pos = string.unpack("<I4I8", data, 5)
    sim.mem, sim.pages = {}, {}
    for _ = 1, n do
        local p
        p, pos = string.unpack("<I8", data, pos)
        sim.pages[p] = true
        local base = p * PAGE
        for i = 0, PAGE - 1 do
            local b = data:byte(pos + i)
            if b ~= 0 then sim.mem[base + i] = b end
        end
        pos = pos + PAGE
    end
end

-- ------------------------------------------------------------------ world
local DATE
local function gdays(y, m, d)
    local x = DATE:new()
    x.year, x.month, x.day = y, m, d
    return x:ToGregorianDays()
end

local NAMES = {   -- nameid -> name
    [1] = "Bukayo", [2] = "Saka", [3] = "Martin", [4] = "\195\152degaard", [5] = "William", [6] = "Saliba",
    [7] = "Declan", [8] = "Rice", [9] = "Jordan", [10] = "Pickford", [11] = "Lautaro", [12] = "Mart\195\173nez",
    [13] = "Gabriel", [14] = "Jesus", [15] = "Gabriel Jesus", [16] = "Harry", [17] = "Kane", [18] = "Ben",
    [19] = "White", [20] = "Generic",
}

-- players: id, first, last, common, team(s), ovr, pot, pos, birth y/m/d, extra
local PLAYERS = {
    { 1001, 1, 2, 0, { 1, 1318 }, 88, 91, 23, 2001, 9, 5 },
    { 1002, 3, 4, 0, { 1 }, 89, 90, 18, 1998, 12, 17 },
    { 1003, 5, 6, 0, { 1 }, 87, 89, 5, 2001, 3, 26 },
    { 1004, 7, 8, 0, { 1, 1318 }, 87, 88, 10, 1999, 1, 14 },
    { 1005, 13, 14, 15, { 1 }, 80, 80, 25, 1997, 4, 3 },
    { 1006, 18, 19, 0, { 1 }, 83, 84, 3, 1997, 10, 8 },
    { 2001, 9, 10, 0, { 7, 1318 }, 83, 83, 0, 1994, 3, 7 },
    { 2002, 20, 20, 0, { 7 }, 70, 75, 14, 2003, 1, 16 },
    { 3001, 11, 12, 0, { 241 }, 89, 89, 25, 1997, 8, 22 },
    { 3002, 20, 20, 0, { 241 }, 60, 85, 16, 2009, 1, 15 },   -- turns 18 on the game date (2027-01-15)
    { 3003, 20, 20, 0, { 241 }, 61, 84, 16, 2009, 1, 16 },   -- turns 18 the next day
    { 4001, 16, 17, 0, { 111592 }, 90, 90, 25, 1993, 7, 28 }, -- free agent
}
local DELETED_PLAYER = 9999

local TEAMS = {   -- id, name, ovr, league
    { 1, "Arsenal", 84, 13 }, { 7, "Everton", 75, 13 }, { 241, "Inter, Milano", 83, 31 },
    { 1318, "England", 85, 78 }, { 111592, "Free Agents", 50, 76 },
}

local MANAGERS = {   -- managerid, first, surname, teamid
    { 501, "Mikel", "Arteta", 1 }, { 502, "David", "Moyes", 7 }, { 503, "Cristian", "Chivu", 241 },
    { 504, "Thomas", "Tuchel", 1318 },
}

local function build_world(sim)
    sim.user_team = 1   -- Live Editor's GetUserTeamID (the user-manager memory walk needs the GUI's memory map)
    DATE = require 'imports/core/date'
    -- teams
    local trows = {}
    for _, t in ipairs(TEAMS) do
        trows[#trows + 1] = { teamid = t[1], teamname = t[2], overallrating = t[3], transferbudget = 1000000 * t[1] % 2000000000,
                              domesticprestige = 5, clubworth = 900000 }
    end
    sim:add_table({
        name = "teams", short = "lyxL",
        fields = {
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "teamname", short = "tnm_", type = "string", depth = 8 * 30 },
            { name = "overallrating", short = "ovr_", depth = 7 },
            { name = "transferbudget", short = "tbud", depth = 31 },
            { name = "domesticprestige", short = "dpre", depth = 5 },
            { name = "clubworth", short = "cwor", depth = 31 },
        },
        rows = trows,
    })
    -- league tables (Competitions tab): Arsenal 3W 1D 0L 9:2, Everton 1W 1D 2L 3:6 in league 13; points left stale
    local LT = { [1] = { 2, 1, 0, 1, 0, 0, 5, 1, 4, 1, 0, 2 }, [7] = { 1, 0, 1, 0, 1, 1, 2, 2, 1, 4, 9, 1 } }
    local lrows = {}
    for _, t in ipairs(TEAMS) do
        local v = LT[t[1]] or { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }
        lrows[#lrows + 1] = { leagueid = t[4], teamid = t[1], homewins = v[1], homedraws = v[2], homelosses = v[3],
                              awaywins = v[4], awaydraws = v[5], awaylosses = v[6], homegf = v[7], homega = v[8],
                              awaygf = v[9], awayga = v[10], points = v[11], currenttableposition = v[12],
                              nummatchesplayed = 0 }
    end
    sim:add_table({
        name = "leagueteamlinks", short = "ltl_",
        fields = { { name = "leagueid", short = "lid_", depth = 11 }, { name = "teamid", short = "tid_", depth = 18 },
                   { name = "homewins", short = "hwin", depth = 5 }, { name = "homedraws", short = "hdrw", depth = 5 },
                   { name = "homelosses", short = "hlos", depth = 5 }, { name = "awaywins", short = "awin", depth = 5 },
                   { name = "awaydraws", short = "adrw", depth = 5 }, { name = "awaylosses", short = "alos", depth = 5 },
                   { name = "homegf", short = "hgf_", depth = 8 }, { name = "homega", short = "hga_", depth = 8 },
                   { name = "awaygf", short = "agf_", depth = 8 }, { name = "awayga", short = "aga_", depth = 8 },
                   { name = "points", short = "pnts", depth = 12 }, { name = "nummatchesplayed", short = "nmp_", depth = 6 },
                   { name = "currenttableposition", short = "ctp_", depth = 8, min = 1 } },
        rows = lrows,
    })
    sim:add_table({
        name = "leagues", short = "lgs_",
        fields = { { name = "leagueid", short = "lid_", depth = 11 }, { name = "leaguename", short = "lnam", type = "string", depth = 8 * 32 } },
        rows = { { leagueid = 13, leaguename = "English Premier League" }, { leagueid = 31, leaguename = "Serie A" } },
    })
    -- tattoos (picker): id, area flags
    sim:add_table({
        name = "tattoo", short = "ttoo",
        fields = { { name = "tattooid", short = "ttid", depth = 11 }, { name = "tattooleftarm", short = "tla_", depth = 1 },
                   { name = "tattoohead", short = "thd_", depth = 1 } },
        rows = { { tattooid = 11, tattooleftarm = 1, tattoohead = 0 }, { tattooid = 12, tattooleftarm = 0, tattoohead = 1 },
                 { tattooid = 13, tattooleftarm = 1, tattoohead = 0 } },
    })
    local nrows = {}
    for id = 1, 20 do nrows[#nrows + 1] = { nameid = id, name = NAMES[id], commentaryid = 900000 + id } end
    sim:add_table({
        name = "playernames", short = "pnms",
        fields = {
            { name = "nameid", short = "nid_", depth = 17 },
            { name = "name", short = "nam_", type = "compressed", depth = 8 * 32 },  -- FC 27: compressed text
            { name = "commentaryid", short = "cmid", depth = 20 },
        },
        rows = nrows,
    })

    local prow, links = {}, {}
    for i, p in ipairs(PLAYERS) do
        prow[#prow + 1] = {
            playerid = p[1], firstnameid = p[2], lastnameid = p[3], commonnameid = p[4], playerjerseynameid = p[3],
            overallrating = p[6], potential = p[7], preferredposition1 = p[8], preferredposition2 = (i % 3 == 0) and -1 or 14,
            preferredfoot = 1 + (i % 2), weakfootabilitytypecode = 3, skillmoves = 3, height = 175 + i, weight = 70 + i,
            birthdate = gdays(p[9], p[10], p[11]), playerjointeamdate = gdays(2020, 7, 1),
            acceleration = 60 + i, sprintspeed = 61 + i, finishing = 62 + i, shortpassing = 63 + i, dribbling = 64 + i,
            standingtackle = 30 + i, strength = 50 + i, gkdiving = 10 + i,
            trait1 = (i == 1) and 5 or 0, icontrait1 = (i == 1) and 1 or 0, trait2 = (i == 2) and 3 or 0, icontrait2 = 0,
            haircolorcode = i % 10, headassetid = p[1], hashighqualityhead = (i <= 6) and 1 or 0,
            headclasscode = (i <= 6) and 0 or 1, headtypecode = 100 + i, headvariation = i % 4, skintonecode = i,
            tattooleftarm = 0, tattoohead = 0,
            contractvaliduntil = 2028 + (i % 3), isretiring = (i == 3) and 1 or 0, nationality = 14,
        }
        for k, tid in ipairs(p[5]) do
            links[#links + 1] = { artificialkey = #links + 1, teamid = tid, playerid = p[1], jerseynumber = (k == 1) and (i + 1) or (i + 10),
                                  position = (k == 1) and (i % 29) or 28 }
        end
    end
    prow[#prow + 1] = { playerid = DELETED_PLAYER, firstnameid = 20, lastnameid = 20, overallrating = 50, potential = 50,
                        preferredposition1 = 0, preferredposition2 = -1, preferredfoot = 1, weakfootabilitytypecode = 1,
                        skillmoves = 1, height = 180, weight = 80, birthdate = gdays(2000, 1, 1), playerjointeamdate = gdays(2020, 1, 1),
                        contractvaliduntil = 2027, nationality = 14, __invalid = true }
    sim:add_table({
        name = "players", short = "plyr",
        fields = {
            { name = "playerid", short = "pid_", depth = 21 },
            { name = "firstnameid", short = "fnid", depth = 17 },
            { name = "lastnameid", short = "lnid", depth = 17 },
            { name = "commonnameid", short = "cnid", depth = 17 },
            { name = "playerjerseynameid", short = "pjni", depth = 17 },
            { name = "overallrating", short = "ovr_", depth = 7 },
            { name = "potential", short = "pot_", depth = 7 },
            { name = "preferredposition1", short = "pp1_", depth = 5 },
            { name = "preferredposition2", short = "pp2_", depth = 5, min = -1 },
            { name = "preferredfoot", short = "pfot", depth = 2, min = 1 },
            { name = "weakfootabilitytypecode", short = "wfab", depth = 3, min = 1 },
            { name = "skillmoves", short = "skmv", depth = 3 },
            { name = "height", short = "hgt_", depth = 7, min = 130 },
            { name = "weight", short = "wgt_", depth = 7, min = 30 },
            { name = "birthdate", short = "bday", depth = 18 },
            { name = "playerjointeamdate", short = "pjtd", depth = 18 },
            { name = "acceleration", short = "acc_", depth = 7 },
            { name = "sprintspeed", short = "spd_", depth = 7 },
            { name = "finishing", short = "fin_", depth = 7 },
            { name = "shortpassing", short = "spas", depth = 7 },
            { name = "dribbling", short = "drib", depth = 7 },
            { name = "standingtackle", short = "stak", depth = 7 },
            { name = "strength", short = "str_", depth = 7 },
            { name = "gkdiving", short = "gkdv", depth = 7 },
            { name = "trait1", short = "tr1_", depth = 30 },
            { name = "icontrait1", short = "itr1", depth = 30 },
            { name = "trait2", short = "tr2_", depth = 14 },
            { name = "icontrait2", short = "itr2", depth = 14 },
            { name = "haircolorcode", short = "hcol", depth = 4 },
            { name = "headassetid", short = "hai_", depth = 21 },
            { name = "hashighqualityhead", short = "hqh_", depth = 1 },
            { name = "headclasscode", short = "hcc_", depth = 2 },
            { name = "headtypecode", short = "htc_", depth = 14 },
            { name = "headvariation", short = "hvar", depth = 5 },
            { name = "skintonecode", short = "stc_", depth = 7 },
            { name = "tattooleftarm", short = "tla_", depth = 10 },
            { name = "tattoohead", short = "thd_", depth = 10 },
            { name = "contractvaliduntil", short = "cvu_", depth = 11 },
            { name = "isretiring", short = "iret", depth = 1 },
            { name = "nationality", short = "nat_", depth = 8 },
        },
        rows = prow,
    })
    sim:add_table({
        name = "teamplayerlinks", short = "tpl_",
        fields = {
            { name = "artificialkey", short = "akey", depth = 16 },
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "playerid", short = "pid_", depth = 21 },
            { name = "jerseynumber", short = "jnum", depth = 7, min = 1 },
            { name = "position", short = "pos_", depth = 5 },
        },
        rows = links,
    })
    -- FC 27 playernamemap: a player-specific callname (commentary id) that wins over the common / last name's
    sim:add_table({
        name = "playernamemap", short = "VGQZ",
        fields = { { name = "commentaryid", short = "cmid", depth = 20, min = -1 }, { name = "playerid", short = "pid_", depth = 19, min = -1 } },
        rows = { { playerid = 1003, commentaryid = 900010 }, { playerid = 2001, commentaryid = 950000 },
                 { playerid = 0, commentaryid = 0, __invalid = true }, { playerid = 0, commentaryid = 0, __invalid = true } },
    })
    sim:add_table({
        name = "editedplayernames", short = "edpn",
        fields = {
            { name = "playerid", short = "pid_", depth = 21 },
            { name = "firstname", short = "fnam", type = "string", depth = 8 * 24 },
            { name = "surname", short = "snam", type = "string", depth = 8 * 24 },
            { name = "commonname", short = "cnam", type = "string", depth = 8 * 24 },
            { name = "playerjerseyname", short = "pjnm", type = "string", depth = 8 * 24 },
        },
        rows = { { playerid = 3002, firstname = "Ali", surname = "Zed", commonname = "", playerjerseyname = "ZED" },
                 { playerid = 3003, firstname = "Bo", surname = "Yu", commonname = "Bobo", playerjerseyname = "BOBO" } },
    })
    local mrows = {}
    for _, m in ipairs(MANAGERS) do
        mrows[#mrows + 1] = { managerid = m[1], firstname = m[2], surname = m[3], teamid = m[4], nationality = 14, headassetid = 7000 + m[1] }
    end
    sim:add_table({
        name = "manager", short = "mngr",
        fields = {
            { name = "managerid", short = "mid_", depth = 16 },
            { name = "firstname", short = "fnam", type = "string", depth = 8 * 20 },
            { name = "surname", short = "snam", type = "string", depth = 8 * 20 },
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "nationality", short = "nat_", depth = 8 },
            { name = "headassetid", short = "hai_", depth = 19 },
        },
        rows = mrows,
    })
    -- floats live in formations in the real database
    sim:add_table({
        name = "formations", short = "frmt",
        fields = {
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "formationname", short = "fnam", type = "string", depth = 8 * 16 },
            { name = "offset1x", short = "o1x_", type = "float" },
            { name = "offset1y", short = "o1y_", type = "float" },
        },
        rows = { { teamid = 1, formationname = "4-3-3", offset1x = 0.5, offset1y = 0.0625 },
                 { teamid = 7, formationname = "4-4-2", offset1x = 0.25, offset1y = 0.875 } },
    })
    -- user career tables Turbo's Lua side reads (cm_teamsheets: user squad)
    local sheet = { teamid = 1 }
    for k = 1, 51 do sheet["playerid" .. k] = (k <= 6) and (1000 + k) or -1 end
    local sheet_fields = { { name = "teamid", short = "tid_", depth = 18 } }
    for k = 1, 51 do sheet_fields[#sheet_fields + 1] = { name = "playerid" .. k, short = string.format("p%03d", k), depth = 22, min = -1 } end
    sim:add_table({ name = "cm_teamsheets", short = "cmts", fields = sheet_fields, rows = { sheet } })

    -- A second database node (the game keeps several T3DB databases chained at +0x18)
    local node2 = sim:alloc(0x20, 8)
    sim:w64(sim.db_node + 0x18, node2)
    sim:w64(node2 + 0x10, 0)
    sim:w64(node2 + 0x18, 0)
    sim.db_node, sim.last_table = node2, nil
    sim:add_table({
        name = "version", short = "vers",
        fields = { { name = "major", short = "majr", depth = 8 }, { name = "minor", short = "minr", depth = 8 } },
        rows = { { major = 27, minor = 1 } },
    })

    -- Career managers the Lua side needs for user_team_id (same layout as the Turbo Lua tests)
    local um = sim:add_manager(129, 0x400)
    local ud = sim:alloc(0x400, 16)
    sim:w64(um + 0x18, ud)
    sim:w32(ud + 0x1F4, 1)
    sim:w32(ud + 0x268, 0)
    sim:w32(um + 0x2F, 0)
    sim:w32(um + 0x34, 0)
    local cal = sim:add_manager(24, 0x100)
    sim:w32(cal + 0x34, sim.date.day)
    sim:w32(cal + 0x38, sim.date.month)
    sim:w32(cal + 0x3C, sim.date.year)
    sim.player_team = {}
    for _, p in ipairs(PLAYERS) do sim.player_team[p[1]] = p[5][1] end
end

-- ------------------------------------------------------------------ expectations
local function dump_expected(sim)
    local json = require 'imports/external/json'
    local out = { tables = {}, model = {} }
    local meta = sim.meta
    for short, name in pairs(meta.shortname_name_tables_map) do
        local t = LE.db:GetTable(name)
        assert(t, "LE.db cannot open " .. name)
        local recs = {}
        local rec = t:GetFirstRecord()
        while rec ~= nil and rec > 0 do
            local idx = (rec - t.first_record) // t.record_size
            local vals = {}
            for fname, fld in pairs(t.fields) do
                local v
                if fld.type == 4 then
                    -- LE's FIELD:GetFloat packs with "i4" and fails on negative floats; decode directly
                    v = string.unpack("<f", string.pack("<I4", (MEMORY:ReadQword(rec + fld.offset) >> fld.startbit) & 0xFFFFFFFF))
                else
                    v = t:GetRecordFieldValue(rec, fname)
                end
                vals[fname] = v
            end
            recs[#recs + 1] = { idx = idx, values = vals }
            rec = t:GetNextValidRecord()
        end
        local fields = {}
        for fname, fld in pairs(t.fields) do
            fields[fname] = { type = fld.type, depth = fld.fld_desc.depth, min = fld.fld_desc.min, offset = fld.offset, startbit = fld.startbit }
        end
        out.tables[name] = { short = short, record_size = t.record_size, written = t.written_records,
                             first_record = string.format("0x%X", t.first_record), fields = fields, records = recs }
    end
    -- model facts
    local function full(first, last, common)
        if common and common ~= 0 then return NAMES[common] end
        return NAMES[first] .. " " .. NAMES[last]
    end
    local today = { 2027, 1, 15 }
    local players = {}
    for _, p in ipairs(PLAYERS) do
        local age = today[1] - p[9]
        if today[2] < p[10] or (today[2] == p[10] and today[3] < p[11]) then age = age - 1 end
        local name = full(p[2], p[3], p[4])
        if p[1] == 3002 then name = "Ali Zed" elseif p[1] == 3003 then name = "Bobo" end
        local club = 0
        for _, tid in ipairs(p[5]) do if tid ~= 1318 then club = tid break end end
        players[#players + 1] = { playerid = p[1], name = name, club = club, overall = p[6], potential = p[7], position = p[8],
                                  age = age, birth = { p[9], p[10], p[11] }, birthdays = gdays(p[9], p[10], p[11]) }
    end
    local teams = {}
    for _, t in ipairs(TEAMS) do teams[#teams + 1] = { teamid = t[1], name = t[2], overall = t[3], league = t[4] } end
    local managers = {}
    for _, m in ipairs(MANAGERS) do managers[#managers + 1] = { managerid = m[1], name = m[2] .. " " .. m[3], teamid = m[4] } end
    out.model = { players = players, teams = teams, managers = managers, deleted_player = DELETED_PLAYER, today = today }
    -- date conversions from LE's date library for a range of days
    local dates = {}
    for _, d in ipairs({ { 1900, 1, 1 }, { 1970, 1, 1 }, { 2000, 2, 29 }, { 2004, 12, 31 }, { 2026, 6, 30 }, { 2027, 1, 15 }, { 2099, 12, 31 } }) do
        local g = gdays(d[1], d[2], d[3])
        local back = DATE:new()
        back:FromGregorianDays(g)
        dates[#dates + 1] = { y = d[1], m = d[2], d = d[3], days = g, back = { back.year, back.month, back.day } }
    end
    out.dates = dates
    local f = assert(io.open(OUT .. "/expected.json", "wb"))
    f:write(json.encode(out))
    f:close()
end

-- ------------------------------------------------------------------ main
local sim = H.setup({ in_cm = true, no_gui = true })  -- the C++ side plays Turbo.dll here (mailbox, memory map)
sim.date = { day = 15, month = 1, year = 2027 }
build_world(sim)
package.loadlib = function() return true end

local function copy(src, dst)
    local a = assert(io.open(src, "rb"))
    local d = a:read("a")
    a:close()
    local b = assert(io.open(dst, "wb"))
    b:write(d)
    b:close()
end

if mode == "build" then
    os.execute(string.format("mkdir -p '%s/LE/turbo_output'", OUT))
    local TURBO = require 'imports/turbo/turbo'
    TURBO.boot()
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
    assert(require('imports/turbo/bridge').start())
    copy(H.out("bridge_meta.json"), OUT .. "/LE/turbo_output/bridge_meta.json")
    copy(H.out("bridge_state.json"), OUT .. "/LE/turbo_output/bridge_state.json")
    copy(H.out("bridge_names.txt"), OUT .. "/LE/turbo_output/bridge_names.txt")
    dump_expected(sim)
    save_image(sim, OUT .. "/world.img")
    print("world built: " .. OUT)

elseif mode == "verify_writes" then
    -- writes.json: [{table, idx, field, value}] made by the C++ test; everything else must be unchanged
    local json = require 'imports/external/json'
    local exp = json.decode(assert(io.open(OUT .. "/expected.json", "rb")):read("a"))
    local writes = json.decode(assert(io.open(OUT .. "/writes.json", "rb")):read("a"))
    load_image(sim, OUT .. "/after_writes.img")
    LE.db:Reset()
    local changed = {}
    for _, w in ipairs(writes) do changed[w.table .. "#" .. w.idx .. "#" .. w.field] = w.value end
    local bad, checked = 0, 0
    for name, et in pairs(exp.tables) do
        local t = LE.db:GetTable(name)
        for _, r in ipairs(et.records) do
            local rec = t.first_record + r.idx * t.record_size
            for fname, ev in pairs(r.values) do
                local fld = t.fields[fname]
                local v
                if fld.type == 4 then
                    v = string.unpack("<f", string.pack("<I4", (MEMORY:ReadQword(rec + fld.offset) >> fld.startbit) & 0xFFFFFFFF))
                else
                    v = t:GetRecordFieldValue(rec, fname)
                end
                local want = changed[name .. "#" .. r.idx .. "#" .. fname]
                if want == nil then want = ev end
                local same
                if type(want) == "number" and type(v) == "number" then same = math.abs(want - v) < 1e-6 else same = (want == v) end
                checked = checked + 1
                if not same then
                    bad = bad + 1
                    print(string.format("MISMATCH %s[%d].%s: Lua reads %s, expected %s", name, r.idx, fname, tostring(v), tostring(want)))
                end
            end
            -- the deleted flag must be untouched
            if not t:IsRecordValid(rec) then
                bad = bad + 1
                print(string.format("MISMATCH %s[%d] became invalid", name, r.idx))
            end
        end
    end
    print(string.format("VERIFY_WRITES checked=%d mismatches=%d writes=%d", checked, bad, #writes))
    os.exit(bad == 0 and 0 or 1)

elseif mode == "commands" then
    -- Every command the GUI test clicked (gui_commands.json), run through Turbo's real bridge in the full Lua test
    -- world (career with fixtures, transfer storage, squad-role vector, generated players, head models); Player
    -- Career commands run in a Player Career world.
    local W = require 'world'
    local json = require 'imports/external/json'
    local list = json.decode(assert(io.open(OUT .. "/gui_commands.json", "rb")):read("a"))
    local function fresh(opts)
        local s = H.setup({ in_cm = true })
        W.build(s, opts)
        package.loadlib = function() return true end
        require('imports/turbo/turbo').boot()
        return require 'imports/turbo/bridge'
    end
    local out = {}
    local function run(bridge, c)
        local okx, ok, text = pcall(bridge.execute, c.cmd)
        if not okx then ok, text = false, "error: " .. tostring(ok) end
        out[#out + 1] = { label = c.label, ok = ok == true, text = tostring(text or "") }
    end
    local bridge = fresh({ career_playercontract = true, playerloans = true })
    local pap = {}
    for _, c in ipairs(list) do
        if c.cmd.module == "pap_playstyles" then pap[#pap + 1] = c else run(bridge, c) end
    end
    if #pap > 0 then
        bridge = fresh({ player_career = true, career_playercontract = true, playerloans = true })
        for _, c in ipairs(pap) do run(bridge, c) end
    end
    local g = assert(io.open(OUT .. "/gui_commands_out.json", "wb"))
    g:write(json.encode(out))
    g:close()
    print("commands processed: " .. #out)

elseif mode == "legacy" then
    -- The GUI's want.txt (core/legacy.cpp) read by Turbo's Lua side (core/legacy.lua): legacy_in.json lists the files
    -- the simulated game has; exported files are copied into the GUI test's Live Editor folder
    local json = require 'imports/external/json'
    local inp = json.decode(assert(io.open(OUT .. "/legacy_in.json", "rb")):read("a"))
    local TURBO = require 'imports/turbo/turbo'
    TURBO.boot()
    -- contents arrive as hex (binary DDS files do not fit in JSON text)
    for path, hex in pairs(inp.files) do
        sim.legacy_files[path] = (hex:gsub("%x%x", function(b) return string.char(tonumber(b, 16)) end))
    end
    local legacy = require 'imports/turbo/core/legacy'
    local dir = legacy.dir()
    os.execute(string.format("mkdir -p '%s'", dir))
    copy(OUT .. "/LE/turbo_output/cache/legacy/want.txt", dir .. "/want.txt")
    local e, m, w = legacy.pump(5)
    os.execute(string.format("cp -r '%s/.' '%s/LE/turbo_output/cache/legacy/'", dir, OUT))
    local g = assert(io.open(OUT .. "/legacy_out.json", "wb"))
    g:write(json.encode({ exported = e, missing = m, waiting = w }))
    g:close()
    print(string.format("legacy exported %s missing %s waiting %s", tostring(e), tostring(m), tostring(w)))

elseif mode == "mailbox" then
    local json = require 'imports/external/json'
    local mb = json.decode(assert(io.open(OUT .. "/mailbox.json", "rb")):read("a"))
    local TURBO = require 'imports/turbo/turbo'
    TURBO.boot()
    load_image(sim, OUT .. "/mailbox_in.img")
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
    require('imports/turbo/bridge').start()
    local f = assert(io.open(H.out("bridge_dll.json"), "wb"))
    f:write(json.encode({ mailbox = mb.mailbox, session = "native", gui_version = "test", updated = os.time() }))
    f:close()
    TURBO_STATE.bridge.next_dll_check = 0
    local before = sim:count_calls("SetPlayerForm")
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local calls = {}
    for i = before + 1, sim:count_calls("SetPlayerForm") do calls[#calls + 1] = sim.calls.SetPlayerForm[i] end
    -- Turbo's Lua reader on the readable-memory map the C++ side published (core/memmap.cpp): same answers
    local mem = require 'imports/turbo/core/mem'
    local readable = {}
    for i, a in ipairs(mb.probe_addrs or {}) do
        readable[i] = mem.readable(math.tointeger(tonumber((a[1]:gsub("^0[xX]", "")), 16)), a[2]) and 1 or 0
    end
    save_image(sim, OUT .. "/mailbox_out.img")
    local g = assert(io.open(OUT .. "/mailbox_calls.json", "wb"))
    g:write(json.encode({ set_player_form = calls, boxes = #sim.boxes, readable = readable,
                          unmapped_reads = sim.unmapped_reads }))
    g:close()
    print("mailbox processed")
end
H.finish()
