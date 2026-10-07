-- Turbo feature: probe_tactics (Turbo 2.0 phase 0, task 0a). READ-ONLY: no database write, no memory write.
-- Writes turbo_output\probe_tactics.json (and probe_tactics.txt, the same in plain text) with
--   1) every table whose name matches tactic / mentalit / instruction / formation / teamsheet (cfg.patterns replaces the list)
--      with its row count and fields (type, range), so the phase 0 review can tell which tactic tables exist in FC 27,
--   2) one cm_teamsheets row of your club: every slot playerid0..playerid52 as it is (-1 = empty), plus the other fields,
--   3) the squad role diagnostic that separates the two suspected causes of the "roles skip older players" bug
--      (docs/TURBO_2_0_PLAN.md section 2): sheet slots, club links, and every entry of the PlayerStatusManager role list
--      with player id, age, role byte, and whether the player is on the sheet / on the club links.
--      Run it in the career where the bug shows, BEFORE and AFTER pressing the squad role button, and after a season reset.
-- Needs a career save for sections 2 and 3 (sections 1 works anywhere the database is loaded).
--   "probe_tactics": { "patterns": ["tactic", "mentalit"], "max_fields": 400 }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local log = require 'imports/turbo/core/log'

local M = {}

M.PATTERNS = { "tactic", "mentalit", "instruction", "formation", "teamsheet" }
M.MAX_FIELDS = 400

local function field_desc(tbl, name)
    local info = db.field_info(tbl, name)
    if not info then return { name = name } end
    local d = { name = name, type = info.type }
    if info.type == "int" then
        d.min, d.max = info.min, info.max
    elseif info.type == "string" then
        d.max_len = info.max_len
    end
    return d
end

local function matching_tables(patterns)
    local probe = require 'imports/turbo/features/probe'
    local names, source = probe.all_table_names()
    local out = {}
    for _, name in ipairs(names) do
        local lower = name:lower()
        for _, p in ipairs(patterns) do
            if lower:find(p:lower(), 1, true) then
                out[#out + 1] = name
                break
            end
        end
    end
    return out, source, #names
end

local function section_tables(patterns, max_fields)
    local out = { patterns = patterns, tables = {} }
    local names, source, total = matching_tables(patterns)
    out.source, out.table_count = source, total
    for _, name in ipairs(names) do
        local tbl, err = db.get_table(name)
        local t = { name = name }
        if tbl then
            t.rows = tonumber(tbl.written_records)
            t.fields = {}
            for i, f in ipairs(db.field_names(tbl)) do
                if i > max_fields then
                    t.fields_cut = true
                    break
                end
                t.fields[#t.fields + 1] = field_desc(tbl, f)
            end
        else
            t.error = tostring(err)
        end
        out.tables[#out.tables + 1] = t
    end
    return out
end

local function today_days()
    local d = game.current_date()
    if not d or not d.year then return nil end
    return util.gregorian_days_from_date(d.year, d.month, d.day)
end

local function age_of(birth, today)
    birth = util.to_int(birth)
    if not birth or birth <= 0 or not today then return nil end
    local by, bm, bd = util.date_from_gregorian_days(birth)
    local ty, tm, tdd = util.date_from_gregorian_days(today)
    local age = ty - by
    if tm < bm or (tm == bm and tdd < bd) then age = age - 1 end
    return age
end

-- one cm_teamsheets row of the user's club: all fields, slots as they are
local function section_sheet(teamid)
    local out = { teamid = teamid }
    local sheets, err = db.get_table("cm_teamsheets")
    if not sheets then
        out.error = tostring(err)
        return out
    end
    out.rows_total = tonumber(sheets.written_records)
    for rec in db.records(sheets) do
        if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
            out.fields, out.slots = {}, {}
            for _, f in ipairs(db.field_names(sheets)) do
                local v = sheets:GetRecordFieldValue(rec, f)
                local idx = f:match("^playerid(%d+)$")
                if idx then
                    out.slots[#out.slots + 1] = { slot = tonumber(idx), playerid = v }
                else
                    out.fields[f] = v
                end
            end
            table.sort(out.slots, function(a, b) return a.slot < b.slot end)
            -- gaps: empty slots that come after the first filled one
            local filled, gaps, last_filled = 0, {}, nil
            for _, s in ipairs(out.slots) do
                if s.playerid and s.playerid > 0 then
                    filled = filled + 1
                    last_filled = s.slot
                end
            end
            for _, s in ipairs(out.slots) do
                if not (s.playerid and s.playerid > 0) and last_filled and s.slot < last_filled then gaps[#gaps + 1] = s.slot end
            end
            out.filled, out.gap_slots, out.last_filled_slot = filled, gaps, last_filled
            break
        end
    end
    if not out.slots then out.error = "no cm_teamsheets row for team " .. tostring(teamid) end
    return out
end

local function user_links(teamid)
    local out, n = {}, 0
    local links = db.get_table("teamplayerlinks")
    if not links or not db.has_fields(links, { "teamid", "playerid" }) then return out, 0 end
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then
            local pid = links:GetRecordFieldValue(rec, "playerid")
            if pid and pid > 0 and not out[pid] then
                out[pid] = true
                n = n + 1
            end
        end
    end
    return out, n
end

-- the role list with ages
local function section_roles(teamid, sheet)
    local out = { entries = {} }
    local squad, count, source, info = game.user_squad()
    out.squad = { count = count, source = source, sheet = info and info.sheet, links = info and info.links,
        sheet_gaps = info and info.gaps }
    local links, nlinks = user_links(teamid)
    out.links = nlinks
    if not mem.map_available() then
        out.error = mem.NO_MAP
        return out
    end
    local role = require 'imports/turbo/features/squad_role'
    local layout, err = role.locate(squad, count, nil)
    if not layout then
        out.error = tostring(err)
        return out
    end
    out.layout = { offset = layout.offset, size = layout.size, role_off = layout.role_off, count = layout.count,
        matched = layout.matched, bad_role_bytes = layout.bad }
    local on_sheet = {}
    for _, s in ipairs(sheet.slots or {}) do
        if s.playerid and s.playerid > 0 then on_sheet[s.playerid] = s.slot end
    end
    local ptbl = db.get_table("players")
    local today = today_days()
    out.today_days = today
    local birth, ovr = {}, {}
    if ptbl and db.has_fields(ptbl, { "playerid", "birthdate" }) then
        local wanted = {}
        for _, e in ipairs(select(3, role.psm_members(layout))) do wanted[e.pid] = true end
        for rec in db.records(ptbl) do
            local pid = ptbl:GetRecordFieldValue(rec, "playerid")
            if wanted[pid] or links[pid] then
                birth[pid] = ptbl:GetRecordFieldValue(rec, "birthdate")
                ovr[pid] = db.has_field(ptbl, "overallrating") and ptbl:GetRecordFieldValue(rec, "overallrating") or nil
            end
        end
    end
    local members, n, list = role.psm_members(layout)
    out.members = n
    local ages_by_role = {}
    for _, e in ipairs(list) do
        local age = age_of(birth[e.pid], today)
        out.entries[#out.entries + 1] = { index = e.index, playerid = e.pid, role = e.role, age = age, ovr = ovr[e.pid],
            on_sheet_slot = on_sheet[e.pid], on_links = links[e.pid] == true }
        if age then
            local key = tostring(e.role)
            local b = ages_by_role[key] or { n = 0, min = 99, max = 0 }
            b.n = b.n + 1
            if age < b.min then b.min = age end
            if age > b.max then b.max = age end
            ages_by_role[key] = b
        end
    end
    out.age_by_role_byte = ages_by_role
    -- players of the club with no role list entry, and the reverse
    out.links_without_entry, out.entries_outside_links = {}, {}
    for pid in pairs(links) do if not members[pid] then out.links_without_entry[#out.links_without_entry + 1] = pid end end
    for _, e in ipairs(list) do if not links[e.pid] then out.entries_outside_links[#out.entries_outside_links + 1] = e.pid end end
    table.sort(out.links_without_entry)
    table.sort(out.entries_outside_links)
    return out
end

local function text_of(rep)
    local L = {}
    local function add(fmt, ...) L[#L + 1] = string.format(fmt, ...) end
    add("Turbo probe_tactics (read-only) %s", rep.created)
    add("")
    add("TABLES matching %s: %d of %d tables (via %s)", table.concat(rep.tables.patterns, ", "), #rep.tables.tables,
        rep.tables.table_count or 0, tostring(rep.tables.source))
    for _, t in ipairs(rep.tables.tables) do
        add("  %s: rows=%s, fields=%d%s", t.name, tostring(t.rows), t.fields and #t.fields or 0, t.error and (" ERROR " .. t.error) or "")
        for _, f in ipairs(t.fields or {}) do
            add("      %s %s%s", f.name, tostring(f.type), f.max and string.format(" [%s..%s]", tostring(f.min), tostring(f.max)) or "")
        end
    end
    if rep.sheet then
        add("")
        local s = rep.sheet
        add("CM_TEAMSHEETS team %s: %s", tostring(s.teamid), s.error and ("ERROR " .. s.error) or
            string.format("%d filled slots, last filled slot %s, empty slots before it: %s", s.filled or 0, tostring(s.last_filled_slot),
                table.concat(s.gap_slots or {}, ",")))
        local slots = {}
        for _, x in ipairs(s.slots or {}) do slots[#slots + 1] = string.format("%d=%s", x.slot, tostring(x.playerid)) end
        add("  slots: %s", table.concat(slots, " "))
        for _, k in ipairs(util.sorted_keys(s.fields or {})) do add("  %s = %s", k, tostring(s.fields[k])) end
    end
    if rep.roles then
        local r = rep.roles
        add("")
        add("ROLE LIST: squad %s players from %s (sheet %s, club links %s, sheet gaps %s)", tostring(r.squad.count), tostring(r.squad.source),
            tostring(r.squad.sheet), tostring(r.squad.links), tostring(r.squad.sheet_gaps))
        if r.error then
            add("  not read: %s", r.error)
        else
            add("  vector at PlayerStatusManager+0x%X, entry %d bytes, role byte at +%d, %d entries, %d players, %d squad matches, %d role bytes out of range",
                r.layout.offset, r.layout.size, r.layout.role_off, r.layout.count, r.members, r.layout.matched, #(r.layout.bad_role_bytes or {}))
            for _, e in ipairs(r.entries) do
                add("  #%d pid %d role %s age %s ovr %s sheet slot %s links %s", e.index, e.playerid, tostring(e.role), tostring(e.age),
                    tostring(e.ovr), tostring(e.on_sheet_slot), tostring(e.on_links))
            end
            add("  club players with no entry: %s", table.concat(r.links_without_entry, ","))
            add("  entries outside the club links: %s", table.concat(r.entries_outside_links, ","))
        end
    end
    return table.concat(L, "\r\n") .. "\r\n"
end

function M.run(ctx)
    local patterns = M.PATTERNS
    if type(ctx.cfg.patterns) == "table" and #ctx.cfg.patterns > 0 then
        patterns = {}
        for _, p in ipairs(ctx.cfg.patterns) do if type(p) == "string" and p ~= "" then patterns[#patterns + 1] = p end end
        if #patterns == 0 then patterns = M.PATTERNS end
    end
    local max_fields = util.to_int(ctx.cfg.max_fields) or M.MAX_FIELDS
    if not db.available() then return false, "Live Editor's database API is not loaded" end

    local rep = { created = os.date("%Y-%m-%d %H:%M:%S"), tables = section_tables(patterns, max_fields) }
    local notes = {}
    if game.in_cm() then
        local teamid = game.user_team_id()
        rep.sheet = section_sheet(teamid)
        rep.roles = section_roles(teamid, rep.sheet)
        if rep.roles.error then notes[#notes + 1] = "role list not read: " .. rep.roles.error end
    else
        notes[#notes + 1] = "no career save loaded: tables only"
    end

    local json_ok, json = pcall(require, 'imports/external/json')
    if not ctx.out_dir then return false, "no writable output folder" end
    local wrote = {}
    if json_ok and type(json) == "table" then
        local ok, text = pcall(json.encode, rep)
        if ok then
            local p = util.join(ctx.out_dir, "probe_tactics.json")
            if util.write_file(p, text) then wrote[#wrote + 1] = p end
        end
    end
    local p = util.join(ctx.out_dir, "probe_tactics.txt")
    if util.write_file(p, text_of(rep)) then wrote[#wrote + 1] = p end
    local summary = string.format("%d tactic-like tables found", #rep.tables.tables)
    if rep.sheet and rep.sheet.slots then
        summary = summary .. string.format("; cm_teamsheets: %d filled slots, %d empty before the last", rep.sheet.filled or 0, #(rep.sheet.gap_slots or {}))
    end
    if rep.roles and rep.roles.members then
        summary = summary .. string.format("; role list: %d players, %d club players without an entry", rep.roles.members, #rep.roles.links_without_entry)
    end
    if #notes > 0 then summary = summary .. "; " .. table.concat(notes, "; ") end
    summary = summary .. (#wrote > 0 and ("; written to " .. table.concat(wrote, ", ")) or "; nothing written")
    log.info("probe_tactics: %s", summary)
    return #wrote > 0, summary
end

return M
