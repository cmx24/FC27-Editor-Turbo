-- Turbo feature: bodytypes (Turbo 2.0 phase 0, task 0b). READ-ONLY.
-- Walks the players table and writes turbo_output\bodytypes_fc27.json: for every distinct bodytypecode in use
--   { code, players, in_generic_set, height {min,max,mean}, weight {min,max,mean}, headclass {code: count},
--     gender {code: count}, examples [{playerid, name, overall}] (up to 5, best overall first) }
-- plus the field's legal range (GetDBMeta / table metadata) and the codes the GUI already names (field_labels.h: 1..9 and 11).
-- The body-type catalogue of Turbo 2.0 (core/bodytype_catalog, gallery UI) is generated from this file: nothing about
-- player-specific body models is guessed (docs/TURBO_2_0_PLAN.md section 6).
--   "bodytypes": { "examples": 5 }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local names = require 'imports/turbo/core/names'
local log = require 'imports/turbo/core/log'

local M = {}

-- codes named by turbogui/src/core/field_labels.h today (the generic height x build grid; 10 has no entry)
M.GENERIC_CODES = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 }
M.EXAMPLES = 5

local function bump(map, key)
    key = tostring(key)
    map[key] = (map[key] or 0) + 1
end

function M.run(ctx)
    local players, err = db.get_table("players")
    if not players then return false, tostring(err) end
    if not db.has_fields(players, { "playerid", "bodytypecode" }) then
        return false, "the players table has no bodytypecode field"
    end
    local wanted = util.to_int(ctx.cfg.examples) or M.EXAMPLES
    if wanted < 0 then wanted = 0 end
    if wanted > 20 then wanted = 20 end
    local has = {
        height = db.has_field(players, "height"), weight = db.has_field(players, "weight"),
        head = db.has_field(players, "headclasscode"), gender = db.has_field(players, "gender"),
        ovr = db.has_field(players, "overallrating"),
    }
    local ids = {}
    for _, f in ipairs(names.FIELDS) do ids[#ids + 1] = names.ID_OF[f] end

    local groups, total = {}, 0
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        local code = util.to_int(players:GetRecordFieldValue(rec, "bodytypecode"))
        if pid and pid > 0 and code ~= nil then
            total = total + 1
            local g = groups[code]
            if not g then
                g = { code = code, players = 0, headclass = {}, gender = {}, best = {},
                    hsum = 0, hn = 0, wsum = 0, wn = 0 }
                groups[code] = g
            end
            g.players = g.players + 1
            if has.height then
                local h = util.to_int(players:GetRecordFieldValue(rec, "height"))
                if h then
                    g.hmin = math.min(g.hmin or h, h)
                    g.hmax = math.max(g.hmax or h, h)
                    g.hsum, g.hn = g.hsum + h, g.hn + 1
                end
            end
            if has.weight then
                local w = util.to_int(players:GetRecordFieldValue(rec, "weight"))
                if w then
                    g.wmin = math.min(g.wmin or w, w)
                    g.wmax = math.max(g.wmax or w, w)
                    g.wsum, g.wn = g.wsum + w, g.wn + 1
                end
            end
            if has.head then bump(g.headclass, players:GetRecordFieldValue(rec, "headclasscode")) end
            if has.gender then bump(g.gender, players:GetRecordFieldValue(rec, "gender")) end
            if wanted > 0 then
                local ovr = has.ovr and (util.to_int(players:GetRecordFieldValue(rec, "overallrating")) or 0) or 0
                local best = g.best
                -- keep the `wanted` best overalls (ties: the lower player id stays)
                if #best < wanted or ovr > best[#best].ovr then
                    local ent = { playerid = pid, ovr = ovr, ids = {} }
                    for _, f in ipairs(ids) do
                        ent.ids[f] = db.has_field(players, f) and players:GetRecordFieldValue(rec, f) or nil
                    end
                    best[#best + 1] = ent
                    table.sort(best, function(a, b) if a.ovr ~= b.ovr then return a.ovr > b.ovr end return a.playerid < b.playerid end)
                    while #best > wanted do best[#best] = nil end
                end
            end
        end
    end

    local generic = util.set_of(M.GENERIC_CODES)
    local cache = {}
    local out_codes = {}
    for _, code in ipairs(util.sorted_keys(groups)) do
        local g = groups[code]
        local c = { code = code, players = g.players, in_generic_set = generic[code] == true,
            headclass = g.headclass, gender = g.gender, examples = {} }
        if g.hn > 0 then c.height = { min = g.hmin, max = g.hmax, mean = math.floor(g.hsum / g.hn * 10 + 0.5) / 10 } end
        if g.wn > 0 then c.weight = { min = g.wmin, max = g.wmax, mean = math.floor(g.wsum / g.wn * 10 + 0.5) / 10 } end
        for _, e in ipairs(g.best) do
            local n = names.names_of_ids(e.ids, cache)
            c.examples[#c.examples + 1] = { playerid = e.playerid, name = n and names.shown(n) or "", overall = e.ovr }
        end
        out_codes[#out_codes + 1] = c
    end

    local info = db.field_info(players, "bodytypecode")
    local outside = {}
    for _, c in ipairs(out_codes) do if not c.in_generic_set then outside[#outside + 1] = c.code end end
    local rep = {
        created = os.date("%Y-%m-%d %H:%M:%S"),
        players = total,
        field = info and { type = info.type, min = info.min, max = info.max } or nil,
        generic_codes = M.GENERIC_CODES,
        codes_outside_generic_set = outside,
        codes = out_codes,
    }
    local summary = string.format("%d players, %d distinct body type codes; %d outside the named set (%s)", total, #out_codes, #outside,
        #outside > 0 and table.concat(outside, ",") or "none")
    local json_ok, json = pcall(require, 'imports/external/json')
    if not ctx.out_dir or not json_ok or type(json) ~= "table" then return false, summary .. "; nothing written (no output folder or JSON library)" end
    local enc_ok, text = pcall(json.encode, rep)
    if not enc_ok then return false, summary .. "; cannot encode: " .. tostring(text) end
    local path = util.join(ctx.out_dir, "bodytypes_fc27.json")
    local wok, werr = util.write_file(path, text)
    if not wok then return false, summary .. "; not written: " .. tostring(werr) end
    log.info("bodytypes: %s -> %s", summary, path)
    return true, summary .. "; written to " .. path
end

return M
