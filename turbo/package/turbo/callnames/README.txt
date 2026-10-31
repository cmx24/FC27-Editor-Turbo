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

Players with their OWN recording (bound to the player id: the game says it whatever callname he has) are read from
the game's audio service and from the master list of the language in

    turbo\callnames\masters\<lang>.json

made by turbo\tools\import_callname_masters.py (in the repository) from a master workbook: an FC 27 master built
from the game (<name>_master_fc27.xlsm) when there is one, else your FC 26 list (<name>_master.xlsm); the file's
"game" field ("fc27" / "fc26") says which, and the Callname tab names it ("FC 27 master" / "your FC 26 list"). The
Callname tab warns and asks for a confirmation before writing a callname for such a player, and does not keep it for
the next career loads. See docs/callnames.md section 9.
For every installed commentary language at once (master + spoken list + a wav check):
turbo\tools\replicate_language_structure.py (in the repository; a dry run; add --write to write the files into this
folder).

spoken_por_br.txt here comes from the FC 26 PT-BR generic surname bank (1,703 segments, exported with the FIFA Editor
Tool in 2026-08); FC 27 keeps the commentary ids of commentarynames, but check a few in game before relying on it.

Coverage check (does Turbo know every name the commentary can speak?): from the repository, with the extracted wavs
(a folder, flat or in generic\ real\ real_link\ real_high\ subfolders) and the four FIFA Editor Tool selection exports
"p<BANK> Selection.xlsx" (pSIMPLE_SURNAME, pPLAYER_NAMES_SIMPLE, pPLAYER_NAMES_LINK, pPLAYER_NAMES_HIGH) of the language:

    python turbo/tools/callname_coverage.py "<wav folder>" "<folder of the 4 xlsx>" "<Live Editor>\turbo\callnames\masters\<lang>.json" --spoken "<Live Editor>\turbo\callnames\spoken_<lang>.txt" [--db <folder of commentarynames.txt, playernames.txt, playernamemap.txt>]

It prints, per bank, the game's set (wav + selection row), Turbo's set and both differences (exit code 1 when Turbo
misses one). Add --write-spoken spoken_<lang>.txt and --write-master <lang>.json to rebuild both from the game's data.
spoken_eng_us.txt (4,844 ids) was rebuilt that way on 2026-10-31 from the FC 27 eng_us wavs + selection tables; its
17 generic names above 965000 (980001..980034) are known from the master list.
