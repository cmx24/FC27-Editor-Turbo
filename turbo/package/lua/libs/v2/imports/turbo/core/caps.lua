-- FC 27 LE Turbo - which Turbo tools this Live Editor build can run.
-- Some tools call Live Editor natives that FC 27 Live Editor v27.1.2 does not ship (transfer bans, transfer / loan
-- list, the FC 26 development XP multiplier). The transfer budget and player moves / deletion are done by Turbo itself when the
-- natives are missing (core/budget.lua, core/moves.lua), so they are not listed. The bridge publishes the result in bridge_state.json
-- ("unavailable": { tool = reason }) so the Turbo window can grey those buttons out and say why, instead of letting
-- the user click a button that can only fail. A Live Editor update that adds the natives lights them up again.

local env = require 'imports/turbo/core/env'

local M = {}

-- tool key (used by the Turbo window) -> Live Editor functions it needs
M.NEEDS = {
    transfer_bans = { "cGetTransferBans", "cAddTransferBan", "cRemoveTransferBan" },
    -- player moves (Players tab): transfer / loan / release / terminate loan / delete are done by Turbo itself when
    -- Live Editor lacks the native (core/moves.lua), so only the list actions are listed here; Turbo.dll's game call
    -- provides them (M.PROVIDED below)
    move_transfer_list = { "AddPlayerToTransferList" },
    move_loan_list = { "AddPlayerToLoanList" },
    move_unlist = { "RemovePlayerFromLists" },
    move_list_status = { "IsPlayerTransferListed" },
    -- player development (features/development.lua): Turbo grows players in the players table and in the game's own
    -- development plan of your club's players, through the two plan natives FC 27 Live Editor v27.1.2 ships
    development = { "PlayerHasDevelopementPlan", "PlayerSetValueInDevelopementPlan" },
    -- FC 26 Live Editor's XP multiplier (bulk edit's "development" action): v27.1.2 does not ship these natives
    development_xp = { "PlayerDevelopmentManagerAddPlayer", "PlayerDevelopmentManagerSave" },
    form_morale = { "SetPlayerForm", "SetPlayerMorale", "SetPlayerFitness" },
    -- job offer creation and the reveal of player data call the game through Turbo.dll (hook foundation); the DLL
    -- registers these Lua natives
    job_offer = { "TurboJobOfferCreate" },
    reveal = { "TurboRevealPlayerData" },
    -- job security / unsackable call the game through Turbo.dll too (core/manager_rules.h)
    manager_rules = { "TurboManagerRules" },
}

-- Tools a Turbo native makes available whatever Live Editor has (defined by bridge.install_natives once Turbo.dll's
-- game-call export is found): tool key -> Turbo native
M.PROVIDED = {
    move_transfer_list = "TurboTransferList", move_loan_list = "TurboTransferList", move_unlist = "TurboTransferList",
    move_list_status = "TurboTransferList",
}

-- Why a tool stays unavailable even with Turbo.dll (appended to Live Editor's reason)
M.NOTES = {
    transfer_bans = "FC 27 has no transfer-ban list Turbo could call (docs/re/transfer_lists.md section 6)",
    move_transfer_list = "Turbo.dll's game call provides it while the Turbo GUI runs in a career",
    move_loan_list = "Turbo.dll's game call provides it while the Turbo GUI runs in a career",
    move_unlist = "Turbo.dll's game call provides it while the Turbo GUI runs in a career",
    move_list_status = "Turbo.dll's game call provides it while the Turbo GUI runs in a career",
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
        local provider = M.PROVIDED[key]
        if not (provider and type(_G[provider]) == "function") then
            for _, name in ipairs(names) do
                local fn, why = env.api(name)
                if not fn then
                    out[key] = M.NOTES[key] and (tostring(why) .. "; " .. M.NOTES[key]) or why
                    break
                end
            end
        end
    end
    return out
end

return M
