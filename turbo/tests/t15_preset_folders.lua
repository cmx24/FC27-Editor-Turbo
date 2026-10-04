package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t15 player presets: folders without a console window (no cmd.exe mkdir for a folder that exists), json_dir")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local pp = require 'imports/turbo/features/player_presets'
local preset = require 'imports/turbo/core/preset'

local function run(mod, overrides) return H.turbo().run(mod, overrides) end

-- Counts os.execute calls while fn runs (each one is a cmd.exe console window in game); os.rename can be made to fail
-- like it does in game for a folder in use (the Live Editor folder while FC 27 runs)
local function count_exec(fn, rename_fails)
    local real_exec, real_rename = os.execute, os.rename
    local calls = {}
    os.execute = function(cmd)
        calls[#calls + 1] = cmd
        return real_exec(cmd)
    end
    if rename_fails then os.rename = function() return nil, "Permission denied", 13 end end
    local ok, err = pcall(fn)
    os.execute, os.rename = real_exec, real_rename
    if not ok then error(err, 0) end
    return calls
end

H.case("dir_exists: existing folder, missing folder, folder that cannot be renamed (in use)", function()
    H.eq(pp.dir_exists(H.LE), true, "Live Editor folder")
    H.eq(pp.dir_exists(H.LE .. "/no_such_folder_t15"), false, "missing")
    local calls = count_exec(function() H.eq(pp.dir_exists(H.LE), true, "in use: still found without a process") end, true)
    H.eq(#calls, 0)
end)

H.case("mkdir: no process for folders that exist, even when they cannot be renamed", function()
    local calls = count_exec(function() pp.mkdir(H.LE .. "/turbo_output") end, true)
    H.eq(#calls, 0, "existing folder: " .. table.concat(calls, " | "))
    local target = H.LE .. "/t15_new/a/b"
    calls = count_exec(function() pp.mkdir(target) end, true)
    H.eq(#calls, 3, "one mkdir per missing level, none for the existing parents: " .. table.concat(calls, " | "))
    H.eq(pp.dir_exists(target), true, "created")
    calls = count_exec(function() pp.mkdir(target) end, true)
    H.eq(#calls, 0, "second time: nothing to create")
end)

H.case("export: folders made beforehand (as the Turbo GUI does) -> no process at all; JSON into json_dir", function()
    local jdir = H.LE .. "/t15_json"
    local cdir = H.LE .. "/t15_csv"
    os.execute(string.format("mkdir -p '%s' '%s'", jdir, cdir))
    local ok, msg
    local calls = count_exec(function()
        ok, msg = run("player_presets", { mode = "export", playerid = 1001, name = "Chosen_Name", miniface = false,
                                          preset_dir = cdir, json_dir = jdir })
    end, true)
    H.eq(ok, true, msg)
    H.eq(#calls, 0, "no console window: " .. table.concat(calls, " | "))
    local doc = preset.json().decode(H.read(jdir .. "/Chosen_Name.json"))
    H.eq(doc.playerid, 1001, "JSON in the chosen folder")
    H.eq(#H.csv_lines(cdir .. "/Chosen_Name.csv"), 2, "CSV in the chosen folder")
    H.eq(H.read(H.out("players/Chosen_Name.json")), nil, "not in the default folder")
end)

H.case("export: list into json_dir, the message names the folder", function()
    local jdir = H.LE .. "/t15_json"
    local ok, msg = run("player_presets", { mode = "export", playerids = { 1001, 1002 }, csv = false, miniface = false, json_dir = jdir })
    H.eq(ok, true, msg)
    H.has(msg, "JSON in " .. jdir)
    H.ok(H.read(jdir .. "/Player_1002_1002.json") ~= nil, "second player's JSON")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
