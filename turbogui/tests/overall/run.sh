#!/usr/bin/env bash
# Builds and runs the overall-formula / archetype test with g++ only. Optional argument: the players TSV (UTF-8, header row,
# columns = players fields; default C:/FC_Tools/FC Editor/_temp/players.txt). Without the TSV only the synthetic checks run.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/ovr
g++ -std=c++17 -O1 -Wall -Wextra -Isrc tests/overall/test_overall.cpp src/core/overall.cpp src/core/archetypes.cpp -o build/ovr/test_overall.exe
./build/ovr/test_overall.exe "$@"
