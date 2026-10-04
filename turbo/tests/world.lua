-- Builds an FC 27-like career world inside the simulated memory.
-- opts:
--   career_playercontract (bool)   add FC 26 contract table
--   playerloans (bool)             add playerloans with one loaned-in player
--   player_career (bool)           user manager reports Player Career with PAP id 1001
--   role_vec_off (int|false)       PlayerStatusManager role vector offset (default 0x18; false = none)
--   role_stride (int)              entry size (default 8)
--   fixtures_off / standings_off   FCEDataManager list offsets (default 0x60 / 0x88; false = broken)
--   storage_off (int|false)        TransferManager negotiations storage offset (default 0x1DD0)
--   fc27_transfer_lists (bool)     FC 27 layout: linked lists of moves in the TransferManager (seen in game)
--   fc27_user_fixtures (bool)      FC 27 layout: your club's remaining fixtures in the MainHubManager (seen in game)
--   budget (int|false)             your club's transfer budget in the FC 27 finance list (default 81497280; false = none)
--   development (bool)             the 34 attributes + growthprofile in players (value 40 + playerid % 30), leagueteamlinks
--                                  (league 13: teams 1-7, league 31: teams 8-14), career_youthplayers (the generated players)

local W = {}

W.USER_TEAM = 1
W.USER_PLAYERS = {}
for i = 1, 26 do W.USER_PLAYERS[i] = 1000 + i end
W.LOANED_IN = 1026
W.GENERATED = { 460001, 460002, 460003, 460004, 460005 }
W.TEAMS = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 241, 111592 }

W.ATTRS = { "acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength", "reactions",
    "aggression", "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling",
    "finishing", "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "defensiveawareness", "shotpower",
    "longshots", "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling",
    "gkkicking", "gkreflexes", "gkpositioning" }

local function players_spec(rows, extra)
    local spec = {
        name = "players", short = "plyr",
        fields = {
            { name = "playerid", short = "pid_", depth = 21 },
            { name = "overallrating", short = "ovr_", depth = 7 },
            { name = "potential", short = "pot_", depth = 7 },
            { name = "preferredposition1", short = "pp1_", depth = 5 },
            { name = "birthdate", short = "bday", depth = 18 },
            { name = "contractvaliduntil", short = "cvu_", depth = 11 },
            { name = "hashighqualityhead", short = "hqh_", depth = 1 },
            { name = "headclasscode", short = "hcc_", depth = 2 },
            { name = "headassetid", short = "hai_", depth = 21 },
            { name = "tattooleftarm", short = "tla_", depth = 10 },
            { name = "trait1", short = "tr1_", depth = 30 },
            { name = "icontrait1", short = "itr1", depth = 30 },
            { name = "trait2", short = "tr2_", depth = 14 },
            { name = "icontrait2", short = "itr2", depth = 14 },
            { name = "isretiring", short = "iret", depth = 1 },
            { name = "socklengthcode", short = "sock", depth = 2 },
            { name = "height", short = "hgt_", depth = 7, min = 130 },
            { name = "weight", short = "wgt_", depth = 7, min = 30 },
            { name = "nationality", short = "nat_", depth = 12 },
            { name = "preferredfoot", short = "pft_", depth = 1, min = 1 },
        },
        rows = rows,
    }
    for _, f in ipairs(extra or {}) do spec.fields[#spec.fields + 1] = f end
    return spec
end

function W.build(sim, opts)
    opts = opts or {}
    sim.user_team = W.USER_TEAM
    local DATE = require 'imports/core/date'
    local function gdays(y, m, d)
        local x = DATE:new()
        x.year, x.month, x.day = y, m, d
        return x:ToGregorianDays()
    end

    -- teams
    sim.team_names = { [1] = "Arsenal", [7] = "Everton", [241] = "Inter, Milano", [111592] = "Free Agents" }
    local team_rows = {}
    for _, tid in ipairs(W.TEAMS) do
        team_rows[#team_rows + 1] = { teamid = tid, teamname = sim.team_names[tid] or ("Team " .. tid), transferbudget = 1000000 }
    end
    sim:add_table({
        name = "teams", short = "lyxL",
        fields = {
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "teamname", short = "tnm_", type = "string", depth = 8 * 30 },
            { name = "transferbudget", short = "tbud", depth = 31 },
        },
        rows = team_rows,
    })

    -- players + links
    local prow, links, player_team = {}, {}, {}
    local key = 1
    local function add_player(pid, tid, ovr, pot, pos, by, jersey, extra)
        local r = {
            playerid = pid, overallrating = ovr, potential = pot, preferredposition1 = pos,
            birthdate = gdays(by, 3, 10), contractvaliduntil = 2028,
            hashighqualityhead = (pid % 2 == 0) and 1 or 0, headclasscode = (pid % 2 == 0) and 0 or 1,
            headassetid = (pid % 2 == 0) and pid or 0, tattooleftarm = 0, trait1 = 0, icontrait1 = 0,
            trait2 = 0, icontrait2 = 0, isretiring = 0, socklengthcode = 0,
            height = 180, weight = 75, nationality = 14, preferredfoot = 1,
        }
        for k, v in pairs(extra or {}) do r[k] = v end
        prow[#prow + 1] = r
        -- FC 27 league stats kept per club link: every third player has played
        local played = (pid % 3 == 0)
        links[#links + 1] = { artificialkey = key, teamid = tid, playerid = pid, jerseynumber = jersey,
            leagueappearances = played and 2 or 0, leaguegoals = played and (pid % 4) or 0,
            yellows = played and (pid % 2) or 0, reds = 0 }
        key = key + 1
        player_team[pid] = tid
    end
    for i, pid in ipairs(W.USER_PLAYERS) do
        add_player(pid, W.USER_TEAM, 60 + i, 70 + i, (i == 1) and 0 or (i % 27) + 1, 1995 + (i % 12), i)
    end
    local pid = 2000
    for _, tid in ipairs(W.TEAMS) do
        if tid ~= W.USER_TEAM and tid ~= 111592 then
            for j = 1, 4 do
                pid = pid + 1
                add_player(pid, tid, 55 + j, 65 + j, j, 1990 + j, j + 1)
            end
        end
    end
    for _, g in ipairs(W.GENERATED) do add_player(g, 5, 50, 80, 25, 2009, 40) end
    -- one deleted row that must be skipped by every iteration
    prow[#prow + 1] = { playerid = 999999, overallrating = 1, potential = 1, preferredposition1 = 0,
        birthdate = gdays(2000, 1, 1), contractvaliduntil = 2026, height = 180, weight = 75, nationality = 14,
        preferredfoot = 1, __invalid = true }
    -- spare (invalid) records: InsertDBTableRow reuses them (create_player adds players / links / names rows)
    for _ = 1, 8 do
        prow[#prow + 1] = { __invalid = true }
        links[#links + 1] = { __invalid = true }
    end
    sim.player_team = player_team
    local extra = nil
    if opts.development then
        extra = { { name = "growthprofile", short = "grpf", depth = 4 } }
        for i, a in ipairs(W.ATTRS) do extra[#extra + 1] = { name = a, short = string.format("a%03d", i), depth = 7 } end
        for _, r in ipairs(prow) do
            if r.playerid then
                r.growthprofile = 2
                for _, a in ipairs(W.ATTRS) do r[a] = 40 + r.playerid % 30 end
            end
        end
    end
    sim:add_table(players_spec(prow, extra))
    if opts.development then
        local ltl = {}
        for _, tid in ipairs(W.TEAMS) do
            if tid >= 1 and tid <= 14 then ltl[#ltl + 1] = { leagueid = tid <= 7 and 13 or 31, teamid = tid } end
        end
        sim:add_table({ name = "leagueteamlinks", short = "ltl_",
            fields = { { name = "leagueid", short = "lid_", depth = 15 }, { name = "teamid", short = "tid_", depth = 18 } }, rows = ltl })
        local yrows = {}
        for i = 1, 3 do yrows[#yrows + 1] = { playerid = W.GENERATED[i], potentialvariance = 3, playertier = 1, monthsinsquad = 2 } end
        sim:add_table({ name = "career_youthplayers", short = "cyp_",
            fields = { { name = "playerid", short = "pid_", depth = 19 }, { name = "potentialvariance", short = "ptvr", depth = 3 },
                       { name = "playertier", short = "pltr", depth = 2 }, { name = "monthsinsquad", short = "mnsq", depth = 6 } },
            rows = yrows })
    end
    -- opts.edited_names = false: the test adds its own editedplayernames table
    if opts.edited_names ~= false then
        sim:add_table({
            name = "editedplayernames", short = "edpn",
            fields = {
                { name = "playerid", short = "pid_", depth = 21 },
                { name = "firstname", short = "fnam", type = "string", depth = 8 * 45 },
                { name = "surname", short = "snam", type = "string", depth = 8 * 45 },
                { name = "commonname", short = "cnam", type = "string", depth = 8 * 45 },
                { name = "playerjerseyname", short = "pjnm", type = "string", depth = 8 * 45 },
            },
            rows = { { __invalid = true }, { __invalid = true }, { __invalid = true }, { __invalid = true },
                     { __invalid = true }, { __invalid = true }, { __invalid = true }, { __invalid = true } },
        })
    end

    sim:add_table({
        name = "teamplayerlinks", short = "tpl_",
        fields = {
            { name = "artificialkey", short = "akey", depth = 16 },
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "playerid", short = "pid_", depth = 21 },
            { name = "jerseynumber", short = "jnum", depth = 7 },
            { name = "leagueappearances", short = "lapp", depth = 7 },
            { name = "leaguegoals", short = "lgls", depth = 7 },
            { name = "yellows", short = "ylws", depth = 7 },
            { name = "reds", short = "reds", depth = 7 },
        },
        rows = links,
    })

    -- cm_teamsheets: user team listed, -1 terminated. FC 27 numbers the slots playerid0..playerid51 (seen in game:
    -- playerid0 is the goalkeeper; FC 27 LE's own GetUserSeniorTeamPlayerIDs starts at 1 and misses that player)
    local sheet_fields = { { name = "teamid", short = "tid_", depth = 18 } }
    local sheet = { teamid = W.USER_TEAM }
    local other = { teamid = 2 }
    for i = 0, 51 do
        sheet_fields[#sheet_fields + 1] = { name = "playerid" .. i, short = string.format("p%03d", i), depth = 22, min = -1 }
        sheet["playerid" .. i] = W.USER_PLAYERS[i + 1] or -1
        other["playerid" .. i] = (i < 4) and (2001 + i) or -1
    end
    sim:add_table({ name = "cm_teamsheets", short = "cmts", fields = sheet_fields, rows = { other, sheet } })

    if opts.career_playercontract then
        local rows = {}
        for _, p in ipairs(W.USER_PLAYERS) do
            rows[#rows + 1] = { playerid = p, contract_status = (p == W.LOANED_IN) and 1 or 0,
                contract_date = 20250701, last_status_change_date = 20250701, duration_months = 24, playerrole = 2 }
        end
        sim:add_table({
            name = "career_playercontract", short = "cpct",
            fields = {
                { name = "playerid", short = "pid_", depth = 21 },
                { name = "contract_status", short = "csts", depth = 4 },
                { name = "contract_date", short = "cdat", depth = 25 },
                { name = "last_status_change_date", short = "lscd", depth = 25 },
                { name = "duration_months", short = "dmon", depth = 8 },
                { name = "playerrole", short = "prol", depth = 4 },
            },
            rows = rows,
        })
    end

    if opts.playerloans then
        sim:add_table({
            name = "playerloans", short = "ploa",
            fields = {
                { name = "playerid", short = "pid_", depth = 21 },
                { name = "teamidloanedfrom", short = "tilf", depth = 18 },
                { name = "loandateend", short = "lden", depth = 25 },
            },
            -- spare (invalid) records: InsertDBTableRow reuses them, as the game's tables keep free records
            rows = { { playerid = W.LOANED_IN, teamidloanedfrom = 7, loandateend = 20270630 },
                     { __invalid = true }, { __invalid = true }, { __invalid = true } },
        })
    end

    sim:add_table({
        name = "formations", short = "frmt",
        fields = {
            { name = "teamid", short = "tid_", depth = 18 },
            { name = "formationid", short = "fid_", depth = 9 },
            { name = "offset1x", short = "o1x_", type = "float" },
        },
        rows = { { teamid = 1, formationid = 3, offset1x = 0.25 }, { teamid = 2, formationid = 4, offset1x = 0.5 } },
    })

    -- managers -----------------------------------------------------------
    local user_mgr = sim:add_manager(129, 0x400)
    local user_info = sim:alloc(0x400, 16)
    sim:w64(user_mgr + 0x18, user_info)
    sim:w32(user_info + 0x1F4, W.USER_TEAM)
    sim:w32(user_info + 0x268, 0)
    sim:w32(user_mgr + 0x2F, opts.player_career and 1 or 0)
    sim:w32(user_mgr + 0x34, opts.player_career and 1001 or 0)
    -- FC 27 finance list (core/budget.lua, seen in game 03-10-2026): *(user_info+0x2F8) -> list, *(list+0x08) -> E,
    -- E+0x28 club id, budget at E-0x10 and E+0x08
    if opts.budget ~= false then
        local list = sim:alloc(0x40, 8)
        local block = sim:alloc(0x80, 16)
        local e = block + 0x20
        sim:w64(user_info + 0x2F8, list)
        sim:w64(list + 0x08, e)
        sim:w32(e + 0x28, W.USER_TEAM)
        local v = opts.budget or 81497280
        sim:w32(e - 0x10, v)
        sim:w32(e + 0x08, v)
        W.BUDGET_A, W.BUDGET_B = e - 0x10, e + 0x08
    end

    local cal = sim:add_manager(24, 0x100)
    sim:w32(cal + 0x34, sim.date.day)
    sim:w32(cal + 0x38, sim.date.month)
    sim:w32(cal + 0x3C, sim.date.year)

    -- PlayerStatusManager role vector
    local psm = sim:add_manager(87, 0x400)
    -- noise: a vector of unrelated ids at +0x08
    local nb = sim:make_vector(psm + 0x08, 30, 8)
    for i = 0, 29 do sim:w32(nb + i * 8, 5000 + i) sim:w32(nb + i * 8 + 4, 1) end
    W.role_entries = {}
    if opts.role_vec_off ~= false then
        local off = opts.role_vec_off or 0x18
        local stride = opts.role_stride or 8
        local b = sim:make_vector(psm + off, #W.USER_PLAYERS, stride)
        for i, p in ipairs(W.USER_PLAYERS) do
            local e = b + (i - 1) * stride
            sim:w32(e, p)
            sim:w32(e + 4, 3)
            W.role_entries[p] = e + 4
        end
    end

    -- TransferManager negotiations storage
    local tm = sim:add_manager(127, 0x4000)
    W.tm = tm
    if opts.storage_off ~= false then
        local st = sim:alloc(0x60, 16)
        sim:w64(tm + (opts.storage_off or 0x1DD0), st)
        local sizes = { [0x08] = 0xB8, [0x10] = 0xB0, [0x18] = 0xB8, [0x20] = 0x98, [0x28] = 0xA0,
                        [0x30] = 0xF8, [0x38] = 0x98, [0x40] = 0xA8, [0x48] = 0x98 }
        local vecs = {}
        for off, _ in pairs(sizes) do
            local v = sim:alloc(0x20, 16)
            sim:w64(st + off, v)
            vecs[off] = v
        end
        -- ai_player_transfers (+0x10): player 2001 from team 2 to team 3, accepted on 20260801
        local b = sim:make_vector(vecs[0x10], 1, 0xB0)
        sim:w32(b, 2001) sim:w32(b + 4, 3) sim:w32(b + 8, 2)
        sim:w8(b + 0x67, 1) sim:w8(b + 0x6C, 1) sim:w32(b + 0x70 + 0xC, 20260801)
        -- ai_club_transfers (+0x08): same deal, seller accepted, fee 12,500,000 at ptr(+0x28) - 0xC
        local c = sim:make_vector(vecs[0x08], 1, 0xB8)
        sim:w32(c, 2001) sim:w32(c + 4, 3) sim:w32(c + 8, 2)
        sim:w8(c + 0x6E, 1)
        local offers = sim:alloc(0x40, 16)
        sim:w32(offers + 0x20 - 0xC, 12500000)
        sim:w64(c + 0x28, offers + 0x20)
        -- ai_player_loans (+0x20): player 2005 loaned from team 3 to team 4 on 20260815
        local l = sim:make_vector(vecs[0x20], 1, 0x98)
        sim:w32(l, 2005) sim:w32(l + 4, 4) sim:w32(l + 8, 3)
        sim:w8(l + 0x52, 1) sim:w8(l + 0x57, 1) sim:w32(l + 0x58, 20260815)
        -- user_player_transfers (+0x38): user buys 2009 from team 4 (action list, last action 4 = buyer accepted)
        local u = sim:make_vector(vecs[0x38], 1, 0x98)
        sim:w32(u, 2009) sim:w32(u + 4, 1) sim:w32(u + 8, 4)
        local acts = sim:alloc(0x40, 16)
        sim:w64(u + 0x50, acts)
        sim:w64(u + 0x58, acts + 0x18)
        sim:w32(acts + 0x18 - 0xC, 20260820)
        sim:w8(acts + 0x18 - 0xC + 0x8, 4)
        -- user_club_transfers (+0x28): fee 3,000,000 via requests (+0x40) - 0x28 + 0xC
        local uc = sim:make_vector(vecs[0x28], 1, 0xA0)
        sim:w32(uc, 2009) sim:w32(uc + 4, 1) sim:w32(uc + 8, 4)
        local uacts = sim:alloc(0x40, 16)
        sim:w64(uc + 0x58, uacts)
        sim:w64(uc + 0x60, uacts + 0x0C)
        sim:w8(uacts + 0x08, 4)
        local reqs = sim:alloc(0x60, 16)
        sim:w32(reqs + 0x28 - 0x28 + 0xC, 3000000)
        sim:w64(uc + 0x40, reqs + 0x28)
        -- the other vectors stay empty
        for _, off in ipairs({ 0x18, 0x30, 0x40, 0x48 }) do sim:make_vector(vecs[off], 0, sizes[off]) end
    end

    -- FC 27: moves kept in linked lists inside the TransferManager ({first, last} head; node +0x00 next, +0x08 prev,
    -- +0x10 player, +0x14 club he moves to, +0x18 club he leaves, +0x24 date, +0x30 fee or loan length)
    if opts.fc27_transfer_lists then
        local function list(head_off, entries)
            local head = tm + head_off
            local prev = head
            local nodes = {}
            for _, e in ipairs(entries) do
                local n = sim:alloc(0x40, 16)
                sim:w64(n + 0x08, prev)
                if prev ~= head then sim:w64(prev, n) else sim:w64(head, n) end
                sim:w32(n + 0x10, e[1]) sim:w32(n + 0x14, e[2]) sim:w32(n + 0x18, e[3])
                sim:w32(n + 0x24, e[4]) sim:w32(n + 0x30, e[5])
                nodes[#nodes + 1] = n
                prev = n
            end
            sim:w64(prev, head)          -- last node -> head
            sim:w64(head + 0x08, prev)   -- head.last
            return nodes
        end
        -- open offers: players still at the +0x18 club (left out of the history)
        list(0x2978, { { 2003, 7, 2, 20260828, 7500000 }, { 2004, 8, 2, 20260829, 820000 } })
        -- completed transfers: players now at the +0x14 club (2001 at team 2, 2006 at team 3, 2010 at team 4)
        list(0x2998, { { 2001, 2, 5, 20260702, 4250000 }, { 2006, 3, 9, 20260715, 1200000 },
                       { 2010, 4, 111592, 20260720, 0 } })
        -- completed loans (+0x30 = loan length, small numbers)
        list(0x29D8, { { 2013, 5, 6, 20260708, 12 }, { 2014, 5, 7, 20260709, 24 } })
        -- an offer for one of the user's players
        list(0x29F8, { { W.USER_PLAYERS[5], 6, W.USER_TEAM, 20260822, 6500000 } })
    end

    -- FCEDataManager fixtures / standings
    local iface = sim:alloc(0x40, 16)
    sim.plugins[0x0a613b9a] = iface   -- ENUM_djb2IFCEInterface_CLSS
    -- chain {0x18, 0x10, 0x08, 0x00}: ptr(iface+0x18)=h1, ptr(h1+0x10)=h2, ptr(h2+0x08)=h3, ptr(h3+0x00)=dm
    local h1, h2, h3 = sim:alloc(0x20, 16), sim:alloc(0x20, 16), sim:alloc(0x20, 16)
    local dm = sim:alloc(0x500, 16)
    sim:w64(iface + 0x18, h1)
    sim:w64(h1 + 0x10, h2)
    sim:w64(h2 + 0x08, h3)
    sim:w64(h3 + 0x00, dm)
    W.dm = dm
    if opts.fixtures_off ~= false then
        -- standings: 4 teams in competition 100 (indexes 0..3), 4 unused slots
        local st_list = sim:alloc(0x40, 16)
        local st_items = sim:alloc(0x18 * 12, 16)
        sim:w64(st_list + 0x28, st_items)
        sim:w32(st_list + 0x1C, 12)
        local st_teams = { 1, 2, 3, 241, 5, 6, 7, 8, 9, 10 }
        for i = 0, 9 do
            local a = st_items + i * 0x18
            sim:w16(a, i) sim:w16(a + 2, 100) sim:w32(a + 4, st_teams[i + 1]) sim:w16(a + 0x14, 10 - i) sim:w8(a + 0x16, 1)
        end
        local fx_list = sim:alloc(0x40, 16)
        local fx_items = sim:alloc(0x18 * 6, 16)
        sim:w64(fx_list + 0x28, fx_items)
        sim:w32(fx_list + 0x1C, 6)
        local fixtures = {
            { 20260815, 1500, 0, 3, 2, 1, true }, { 20260815, 1730, 1, 2, 0, 0, true },
            { 20260822, 2000, 3, 0, 0, 0, false }, { 20260822, 1500, 2, 1, 0, 0, false },
        }
        for i, f in ipairs(fixtures) do
            local a = fx_items + (i - 1) * 0x18
            sim:w32(a, f[1]) sim:w16(a + 4, f[2]) sim:w16(a + 6, i) sim:w16(a + 8, 100)
            sim:w16(a + 0xA, f[3]) sim:w16(a + 0xC, f[4]) sim:w8(a + 0xF, f[5]) sim:w8(a + 0x11, f[6])
            sim:w8(a + 0x13, f[7] and 1 or 0) sim:w8(a + 0x14, 1)
        end
        sim:w64(dm + (opts.fixtures_off or 0x60), fx_list)
        sim:w64(dm + (opts.standings_off or 0x88), st_list)
    else
        -- broken: lists hold garbage
        local junk = sim:alloc(0x40, 16)
        sim:w32(junk + 0x1C, 3)
        sim:w64(junk + 0x28, sim:alloc(0x100, 16))
        sim:w64(dm + 0x60, junk)
        sim:w64(dm + 0x88, junk)
    end

    -- FC 27: your club's remaining fixtures, MainHubManager +0x60 count, +0x68 -> 0x130-byte entries
    if opts.fc27_user_fixtures then
        local hub = sim:add_manager(58, 0x400)
        local list = {
            { 20260905, 1400, 1118, 5, W.USER_TEAM }, { 20260913, 1130, 1118, W.USER_TEAM, 3 },
            { 20260919, 1400, 1118, 241, W.USER_TEAM },
        }
        local entries = sim:alloc(0x130 * #list, 16)
        for i, f in ipairs(list) do
            local e = entries + (i - 1) * 0x130
            sim:w32(e + 0x38, f[1]) sim:w32(e + 0x3C, f[2]) sim:w32(e + 0x2C, f[3])
            sim:w32(e + 0x128, f[4]) sim:w32(e + 0x12C, f[5])
        end
        sim:w32(hub + 0x60, #list)
        sim:w64(hub + 0x68, entries)
        W.hub_entries = entries
    end

    -- season stats
    sim.stats = {
        { teamid = 1, playerid = 1010, compobjid = 100, avg = 1450, goals = 12, yellow = 1, red = 0, app = 20, assists = 5,
          clean_sheets = 0, motm = 3, two_yellow = 0, goals_conceded = 0, saves = 0 },
        { teamid = 241, playerid = 2055, compobjid = 100, avg = 680, goals = 3, yellow = 2, red = 1, app = 10, assists = 1,
          clean_sheets = 0, motm = 0, two_yellow = 0, goals_conceded = 0, saves = 0 },
        { teamid = 1, playerid = 1001, compobjid = 100, avg = 0, goals = 0, yellow = 0, red = 0, app = 0, assists = 0,
          clean_sheets = 0, motm = 0, two_yellow = 0, goals_conceded = 0, saves = 0 },
    }
    return W
end

return W
