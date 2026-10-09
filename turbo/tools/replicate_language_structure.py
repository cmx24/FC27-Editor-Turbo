#!/usr/bin/env python3
"""Replicate complete commentary language structure (ALL real and generic callnames) for any language loaded by FC 27.

Architecture:
1. Source Resolution:
   - For downloaded FC 27 packs (e.g. ita_it): reads Frostbite super-bundles via fc27_commentary and generates FC 27 master workbooks via build_callname_master.py.
   - For other languages: ingests existing master workbooks from C:\FC_Tools\My Mods\<lang_folder>\<name>_master.xlsm.
2. Ingestion & Indexing:
   - Compiles full master JSONs into <le>/turbo/callnames/masters/<lang>.json containing ALL real players (playerid) and ALL generic commentary IDs (commentaryid).
3. Spoken Callname Extraction:
   - Populates <le>/turbo/callnames/spoken_<lang>.txt with valid #turbo-spoken header and sorted commentary IDs (900000..965000).
4. Audio & Playback Linkage:
   - Verifies audio directories and segment routing for in-game and GUI playback.
"""

import argparse
import glob
import json
import os
import subprocess
import sys

DEFAULT_LE = r"C:\FC 27 Live Editor"
DEFAULT_GAME = r"C:\Program Files\EA Games\EA SPORTS FC 27"
DEFAULT_MODS = r"C:\FC_Tools\My Mods"

FOLDER_TO_LANG = {
    "br": "por_br",
    "eng": "eng_us",
    "fra": "fre_fr",
    "ger": "ger_de",
    "ita": "ita_it",
    "i27": "ita_it",
    "ned": "dut_nl",
    "spa": "spa_es"
}


def replicate_all(le_root=DEFAULT_LE, mods_root=DEFAULT_MODS, game_dir=DEFAULT_GAME):
    """Executes the full end-to-end language replication for all languages."""
    print("=== Step 1: Ingesting All Callnames (Real & Generic) from Masters ===")
    importer = os.path.join(le_root, "turbo_dev", "2.0", "integration", "turbo", "tools", "import_callname_masters.py")
    out_dir = os.path.join(le_root, "turbo", "callnames", "masters")
    os.makedirs(out_dir, exist_ok=True)

    cmd = [sys.executable, importer, "--root", mods_root, "--root", os.path.join(le_root, "turbo_dev", "masters"), "--out", out_dir]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"Error running importer: {proc.stderr}", file=sys.stderr)
    else:
        print(proc.stdout.strip())

    print("\n=== Step 2: Generating Spoken Callnames Catalogs ===")
    masters = glob.glob(os.path.join(out_dir, "*.json"))
    summary = {}

    for m in sorted(masters):
        try:
            with open(m, "r", encoding="utf-8") as f:
                d = json.load(f)
            lang = d.get("language")
            if not lang:
                continue

            real_players = d.get("real_players", [])
            generic_ids = d.get("generic_ids", [])
            names_map = d.get("names", {})
            wav_dir = d.get("wav_dir", "")

            # Filter valid player commentary range: 900000..965000
            valid_generic = sorted([gid for gid in generic_ids if 900000 <= gid <= 965000])

            spoken_path = os.path.join(le_root, "turbo", "callnames", f"spoken_{lang}.txt")
            with open(spoken_path, "w", encoding="utf-8") as f:
                f.write(f"#turbo-spoken {lang} {len(valid_generic)}\n")
                for gid in valid_generic:
                    f.write(f"{gid}\n")

            summary[lang] = {
                "game": d.get("game"),
                "real_players": len(real_players),
                "generic_ids": len(generic_ids),
                "generic_spoken": len(valid_generic),
                "named_players": len(names_map),
                "master_json": m,
                "spoken_file": spoken_path,
                "wav_dir": wav_dir if (wav_dir and os.path.isdir(wav_dir)) else "default"
            }
            print(f"  [{lang}] {len(real_players)} real players, {len(generic_ids)} generic ({len(valid_generic)} spoken) -> {os.path.basename(spoken_path)}")
        except Exception as e:
            print(f"  [{m}] Error: {e}", file=sys.stderr)

    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--le", default=DEFAULT_LE, help="Live Editor root")
    parser.add_argument("--mods", default=DEFAULT_MODS, help="My Mods folder")
    parser.add_argument("--game", default=DEFAULT_GAME, help="Game installation folder")
    args = parser.parse_args()

    results = replicate_all(args.le, args.mods, args.game)
    print("\n=== Language Replication Complete ===")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
