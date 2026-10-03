-- FC 27 LE Turbo - game images for the Turbo window (minifaces, manager faces, tattoo previews).
-- The images are FC 27 "legacy files" (data/ui/imgAssets/...). Only Live Editor's LegacyFileExport can read them, and
-- Live Editor runs Lua only on career-mode events (or when a script is executed), so the Turbo window asks for images
-- through a file instead of a command:
--   turbo_output\cache\legacy\want.txt     written by the GUI: "#gen <n>" then one legacy path per line, most wanted
--                                         first (a new <n> = the GUI emptied its cache: everything is looked at again)
--   turbo_output\cache\legacy\<path>       the exported file (LegacyFileExport creates the folders)
--   turbo_output\cache\legacy\missing.txt  paths the game does not have (one per line, appended)
--   turbo_output\cache\legacy\status.txt   "exported <n> missing <n> waiting <n>" after every run
-- Every career-mode event exports what it can in a quarter of a second; turbo_images.lua (Lua Engine) does a long run.

local env = require 'imports/turbo/core/env'
local util = require 'imports/turbo/core/util'

local M = {}

M.MAX_LINES = 60000

TURBO_STATE = TURBO_STATE or { listeners = {} }

local function state()
    TURBO_STATE.legacy = TURBO_STATE.legacy or { done = {}, exported = 0, missing = 0 }
    return TURBO_STATE.legacy
end

function M.dir()
    local out = env.output_dir()
    if not out then return nil end
    return util.join(util.join(out, "cache"), "legacy")
end

-- Only plain relative paths under data/ (no "..", no drive letters) are exported
function M.valid_path(p)
    if type(p) ~= "string" or #p < 6 or #p > 200 then return false end
    if p:sub(1, 5) ~= "data/" then return false end
    if p:find("..", 1, true) or p:find(":", 1, true) or p:find("\\", 1, true) then return false end
    return p:match("^[%w_/%.%-]+$") ~= nil
end

local function clock()
    if type(os) == "table" and type(os.clock) == "function" then return os.clock() end
    return 0
end

local function dest(dir, p)
    return dir .. "/" .. p
end

-- Export wanted images for up to `seconds`. Returns exported, missing, waiting (or nil, error)
function M.pump(seconds)
    local exist, export = _G["LegacyFileExist"], _G["LegacyFileExport"]
    if type(exist) ~= "function" or type(export) ~= "function" then
        return nil, "LegacyFileExist / LegacyFileExport are not available in this Live Editor build"
    end
    local dir = M.dir()
    if not dir then return nil, "Live Editor folder not found" end
    local text = util.read_file(dir .. "/want.txt")
    if not text or text == "" then return 0, 0, 0 end
    local st = state()
    local t0 = clock()
    local exported, missing, waiting, lines = 0, 0, 0, 0
    local missing_lines = {}
    local gen = text:match("^#gen (%d+)")
    if gen and gen ~= st.gen then
        st.gen = gen
        st.done = {}
    end
    for line in text:gmatch("[^\r\n]+") do
        lines = lines + 1
        if lines > M.MAX_LINES then break end
        local p = util.trim(line)
        if p:sub(1, 1) ~= "#" and not st.done[p] and M.valid_path(p) then
            if clock() - t0 >= seconds then
                waiting = waiting + 1
            else
                local d = dest(dir, p)
                if util.file_exists(d) then
                    st.done[p] = true
                else
                    local oke, has = pcall(exist, p)
                    if oke and has then
                        local okx, res = pcall(export, p, d)
                        if okx and res ~= false and util.file_exists(d) then
                            exported = exported + 1
                        else
                            missing = missing + 1
                            missing_lines[#missing_lines + 1] = p
                        end
                    else
                        missing = missing + 1
                        missing_lines[#missing_lines + 1] = p
                    end
                    st.done[p] = true
                end
            end
        end
    end
    if #missing_lines > 0 then
        local f = io.open(dir .. "/missing.txt", "ab")
        if f then
            f:write(table.concat(missing_lines, "\n"), "\n")
            f:close()
        end
    end
    st.exported, st.missing = st.exported + exported, st.missing + missing
    if exported > 0 or missing > 0 or waiting ~= st.last_waiting then
        st.last_waiting = waiting
        util.write_file(dir .. "/status.txt", string.format("exported %d missing %d waiting %d", st.exported, st.missing, waiting))
    end
    return exported, missing, waiting
end

-- Forget what this session already handled (the GUI cleared its cache)
function M.reset()
    state().done = {}
end

return M
