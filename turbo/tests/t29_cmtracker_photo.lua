package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t29 create_player from CMTracker: the photo miniface only comes with his real 3D head (never a mixed result)")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local util = require 'imports/turbo/core/util'
local preset = require 'imports/turbo/core/preset'

local OUT = H.LE .. "/turbo_output"
os.execute(string.format("mkdir -p '%s/asset_catalog' '%s/players'", OUT, OUT))
local GATE = OUT .. "/asset_catalog/lua_gate.txt"
local HIDDEN3 = 90003   -- a head the game files ship and no row of the save uses (synthetic id)

local function set_gate(ids)
    local t = { "#turbo-asset-gate 1", "#build TEST0001-00001000", "#session sim" }
    for _, id in ipairs(ids) do t[#t + 1] = "head3d " .. id end
    H.ok(util.write_file(GATE, table.concat(t, "\r\n") .. "\r\n"), "gate written")
end
local function with_photo(name, real_id)
    local doc = { format = "turbo-player-preset", version = 1, name = name, playerid = 900001,
        names = { firstname = "Test", surname = name, commonname = "", playerjerseyname = name },
        players = { overallrating = 70, potential = 75, headclasscode = 1, preferredposition1 = 25 },
        links = {}, cmtracker = { playerid = 900001, real_face_id = real_id }, miniface = name .. "_photo.dds" }
    local f = H.out("players/" .. name .. "_cmt.json")
    H.ok(util.write_file(f, preset.json().encode(doc)), "preset with a photo")
    H.ok(util.write_file(H.out("players/" .. name .. "_photo.dds"), "DDS " .. string.rep("\0", 200)), "photo written")
    local ok, msg = H.turbo().run("create_player", { source = { file = f }, teamid = 4 })
    return ok, msg, tonumber(tostring(msg):match("new player (%d+)"))
end
local function pic(pid) return H.LE .. "/mods/legacy/data/ui/imgAssets/heads/p" .. tostring(pid) .. ".dds" end

H.case("generic head (the game files have no real face for him): the CMTracker photo is not used", function()
    set_gate({ HIDDEN3 })
    local ok, msg, pid = with_photo("PhotoGeneric", 2004)   -- 2004 is not in the gate
    H.eq(ok, true, msg); H.has(msg, "generic head"); H.has(msg, "CMTracker photo not used")
    H.ok(pid and not util.file_exists(pic(pid)), "no photo next to a generic head")
end)

H.case("real head (the gate lists it): the CMTracker photo comes with it", function()
    set_gate({ HIDDEN3 })
    local ok, msg, pid = with_photo("PhotoReal", HIDDEN3)
    H.eq(ok, true, msg); H.has(msg, "real face of player " .. HIDDEN3); H.has(msg, "miniface installed")
    H.ok(pid and util.file_exists(pic(pid)), "the photo with his real head")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
