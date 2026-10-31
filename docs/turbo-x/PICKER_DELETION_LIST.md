# Step 7, second half: code to delete after the owner's in-game proof of the one picker

Line numbers at commit 6458db3 (tx-picker). Delete only after the owner reports apply + undo working through the visual picker for hair, beard, accessory and tattoo on LEICESTER.

turbogui/src/ui/ui_images.cpp
- 1433-1558 `tattoo_editor` (incl. the `##tattoopick` modal); keep 1381-1432 (`kTattooAreas`, `TattooRow`, `g_tattoos`, `build_tattoos`: the merged layer uses them).
- 1790-1798 `tile_tag`, 1800-1826 `gear_cell`, 1828-1838 `gear_tooltip` (+ forward decls at 1386-1389).
- 1953-1956 `gallery_pick_path`, 1958-1968 `gallery_pick_list` (tested by tests/native/test_gear_pictures.h:60-85: drop those cases with them or keep the two pure helpers), 1976-1984 `text_tile`, 1986-2004 `gallery_picture`, 2006-2019 `gallery_picture_cell`.
- 2189-2229 `HairFilter`, `hair_combo`, `hair_matches`; 2231-2436 `item_galleries` (incl. the `##gallerypick` modal and the "Socks and sleeves" block).
- ui_images.h: 118-123 (`tattoo_editor`, `item_galleries` decls), 143-161 (`GalleryPick`, `gallery_pick_path`, `gallery_pick_list`, `gallery_picture`, `gallery_picture_cell`); `tag`/`tip` members of `GalleryEntry` (no reader left).

turbogui/src/ui/ui_faces3d_gate.cpp
- 161-259 `matches` + `gallery_picker` (the `##f3pick` modal), `GateUi::hair_search/beard_search/hair_length` (47-48), the `#include "ui_images.h"` if nothing else needs it; the TURBO-X comments.

Tests to update at the same time: tests/native/test_one_picker.h op_sites row "old-outfit-popup"; gui_labels.golden.txt entries for `##f3pick`, `##gallerypick`, `##tattoopick`, `Close##f3pickclose` (regenerate with TURBO_GUI_LABELS_UPDATE=1).

Proof after deletion: `grep -rn "TURBO-X: unreachable\|item_galleries(\|tattoo_editor(\|gallery_picker(" turbogui/src` returns nothing; the build script compiles; the owner repeats the four apply+undo checks on the installed DLL.
