#!/usr/bin/env bash
# Rebuilds the exact inputs used in README "Results" from Tardis's free
# first-of-month sample (no API key): Binance USDS-M futures, BTCUSDT, 2026-10-01.
#   data/l2.csv     first 3,000,000 incremental_book_L2 rows, minus the half-cut final message
#   data/trades.csv trades inside the L2 window (full day kept as data/trades_full.csv)
#   data/snap5.csv  book_snapshot_5 rows inside the L2 window (validator reference)
#   data/btc.evt    binary event file built by mdr_convert
# Then compare against results/checksums_2026-10-11.txt.
# Usage: cmake --build build && scripts/get_data.sh
set -euo pipefail
B=https://datasets.tardis.dev/v1/binance-futures
DAY=2026/10/01
mkdir -p data
fetch() { curl -sfL "$B/$1/$DAY/BTCUSDT.csv.gz" | gzip -dc; }

echo "L2: first 3,000,000 rows ..."
{ fetch incremental_book_L2 || true; } | head -n 3000001 > data/l2_raw.csv   # header + 3M rows
last=$(tail -n 1 data/l2_raw.csv | cut -d, -f4)                              # local_timestamp of the cut message
awk -F, -v L="$last" '$4 != L' data/l2_raw.csv > data/l2.csv && rm data/l2_raw.csv
E=$(tail -n 1 data/l2.csv | cut -d, -f4)                                     # end of the L2 window

echo "trades: full day, then trim to the L2 window ..."
fetch trades > data/trades_full.csv
awk -F, -v E="$E" 'NR==1 || $4+0 <= E+0' data/trades_full.csv > data/trades.csv

echo "book_snapshot_5: rows inside the L2 window ..."
{ fetch book_snapshot_5 || true; } | awk -F, -v E="$E" 'NR==1 {print; next} $4+0 > E+0 {exit} {print}' > data/snap5.csv

./build/mdr_convert data/l2.csv data/trades.csv data/btc.evt
sha256sum data/l2.csv data/trades.csv data/snap5.csv data/btc.evt
echo "compare with results/checksums_2026-10-11.txt"
