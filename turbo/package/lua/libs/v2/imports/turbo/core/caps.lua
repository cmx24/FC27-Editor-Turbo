-- FC 27 LE Turbo - which Turbo tools this Live Editor build can run.
-- Some tools call Live Editor natives that FC 27 Live Editor v27.1.2 does not ship (transfer bans, transfer / loan
-- list, player development). The transfer budget and player moves / deletion are done by Turbo itself when the
-- natives are missing (core/budget.lua, core/moves.lua), so they are not listed. The bridge publishes the result in bridge_state.json
-- ("unavailable": { tool = reason }) so the Turbo window can grey those buttons out and say why, instead of letting
-- the user click a button that can only fail. A Live Editor update that adds the natives lights them up again.

local env = require 'imports/turbo/core/env'

local M = {}

-- tool key (used by the Turbo window) -> Live Editor functions it needs
M.NEEDS = {
    transfer_bans = { "cGetTransferBans", "cAddTransferBan", "cRemoveTransferBan" },
    -- player moves (Players tab): transfer / loan / release / terminate loan / delete are done by Turbo itself when
    -- Live Editor lacks the native (core/moves.lua), so only the list flags are listed here
    move_transfer_list = { "AddPlayerToTransferList" },
    move_loan_list = { "AddPlayerToLoanList" },
    move_unlist = { "RemovePlayerFromLists" },
    development = { "PlayerDevelopmentManagerAddPlayer", "PlayerDevelopmentManagerSave" },
    form_morale = { "SetPlayerForm", "SetPlayerMorale", "SetPlayerFitness" },
    -- job offer creation calls the game through Turbo.dll (hook foundation); the DLL registers this Lua native
    job_offer = { "TurboJobOfferCreate" },
    -- job security / unsackable call the game through Turbo.dll too (core/manager_rules.h)
    manager_rules = { "TurboManagerRules" },
}

-- Player moves Turbo does itself in the career database when Live Editor has no native for them (core/moves.lua).
-- The Turbo window refuses those for the user's own club (they crashed a test career in FC 27), but not when Live
-- Editor's own native does the move.
M.TURBO_MADE = {
    move_transfer = "TransferPlayer", move_loan = "LoanPlayer", move_release = "ReleasePlayerFromTeam",
    move_terminate_loan = "TerminateLoan", delete_players = "DeletePlayer",
    -- new players (clone / create / import as new) are rows Turbo adds with InsertDBTableRow (features/create_player.lua)
    create_player = "CreatePlayer",
}

-- Sorted list of the tool keys Turbo does itself in this Live Editor build
function M.turbo_made()
    local out = {}
    for key, name in pairs(M.TURBO_MADE) do
        if not env.api(name) then out[#out + 1] = key end
    end
    table.sort(out)
    return out
end

-- { key = reason } for every tool this Live Editor build cannot run (empty table when all can)
function M.unavailable()
    local out = {}
    for key, names in pairs(M.NEEDS) do
        for _, name in ipairs(names) do
            local fn, why = env.api(name)
            if not fn then
                out[key] = why
                break
            end
        end
    end
    return out
end

return M
