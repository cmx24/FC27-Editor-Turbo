// FC 27 LE Turbo GUI - FC 26 Live Editor parity panels (Turbo 2.1):
//   - transfer columns in the Teams list and a club's Transfers tab (core/transfer_stats.h, read only)
//   - your career's scouts with "Best scouts" / "Restore" (core/scouts.h; Managers tab)
//   - quick field names for Turbo Tools > Bulk edit (release clause, contract, ...)
// No file I/O in the frame path: the transfer export is read on a click (and once, the first time the columns or the tab are
// shown); the database part is read again when app.gen changes.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/transfer_stats.h"

namespace turbo {

class App;

// Teams list: the "Transfers" check box (adds In / Out / Spent / Received / Net columns) and its Reload button
void transfer_columns_controls(App& app);
bool transfer_columns_on();
// The club's stats (nullptr = no move known); loads the data the first time it is asked for
const xfer::TeamStats* transfer_stats_of(App& app, int64_t teamid);
// Teams > (club) > Transfers tab body
void team_transfers_tab(App& app, int64_t teamid);
// "Transfers: 214 moves (export 2026_08_30 + database)" ("" before the first load)
const char* transfer_source_line();

// Managers tab: "Your scouts (career staff)" section
void scouts_section(App& app);

// Bulk edit: a small "Pick" combo next to a field-name box; writes the picked field name into buf. True when picked.
bool bulk_field_pick(const char* id, char* buf, size_t size);

}  // namespace turbo
