Turbo callnames: spoken-id lists per commentary language
=========================================================

The Turbo GUI (Players > Callname) shows which name the commentary speaks for a player and lets you assign another
spoken one. Which commentary ids have audio depends on the commentary language the game loaded (ita_it, eng_us,
por_br, ...). Turbo detects the language from the game folder (commentary\commentaryfull_<lang>) and reads the list
of spoken ids from this folder:

    turbo\callnames\spoken_<lang>.txt

Format: an optional first line "#turbo-spoken <lang> <count>", then one commentary id (900000..965000) per line;
anything after a tab, space or '#' on a line is a comment. Make one with

    python scripts/callnames_spoken_list.py <lang> "<pSIMPLE_SURNAME Selection export>.csv" -o "<Live Editor>\turbo\callnames\spoken_<lang>.txt"

from a FIFA Editor Tool export (Data Explorer > Sound > Speech > LocCommentary > <lang> > SoundWaves > <lang>_FULL >
pSIMPLE_SURNAME > "Export Data Set", saved as CSV) or any CSV with a commentaryid / surname_ID column.

Without a list for the loaded language the GUI falls back to every commentary id playernames uses and marks the
pickers "unverified". See docs/callnames.md in the repository for the details and the evidence.

spoken_por_br.txt here comes from the FC 26 PT-BR generic surname bank (1,703 segments, exported with the FIFA Editor
Tool in 2026-08); FC 27 keeps the commentary ids of commentarynames, but check a few in game before relying on it.
