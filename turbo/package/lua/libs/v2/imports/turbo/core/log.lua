-- FC 27 LE Turbo - logging
-- Writes through Live Editor's LOGGER (v2) when present, else Log (v1), else print.
-- Every line is prefixed with "[Turbo]" so it is easy to find in Logs\live_editor_<date>.log

local M = {
    verbose = false,
    lines = {},        -- lines collected for the current run (used for summaries/tests)
    max_lines = 2000,
}

local PREFIX = "[Turbo] "

local function emit(level, text)
    local msg = PREFIX .. tostring(text)

    if #M.lines < M.max_lines then
        M.lines[#M.lines + 1] = level .. " " .. msg
    end

    if type(LOGGER) == "table" then
        if level == "DEBUG" and type(LOGGER.LogDebug) == "function" then
            LOGGER:LogDebug(msg)
            return
        elseif level == "INFO" and type(LOGGER.LogInfo) == "function" then
            LOGGER:LogInfo(msg)
            return
        elseif level == "WARN" and type(LOGGER.LogWarn) == "function" then
            LOGGER:LogWarn(msg)
            return
        elseif level == "ERROR" and type(LOGGER.LogError) == "function" then
            LOGGER:LogError(msg)
            return
        end
    end

    if type(Log) == "function" then
        Log(msg)
    else
        print(msg)
    end
end

function M.reset()
    M.lines = {}
end

function M.debug(fmt, ...)
    if not M.verbose then return end
    emit("DEBUG", string.format(fmt, ...))
end

function M.info(fmt, ...)
    emit("INFO", string.format(fmt, ...))
end

function M.warn(fmt, ...)
    emit("WARN", string.format(fmt, ...))
end

function M.error(fmt, ...)
    emit("ERROR", string.format(fmt, ...))
end

return M
