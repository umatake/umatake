#!/usr/bin/env bash
# USI protocol smoke test for Umatake.
# Verifies the engine responds correctly to the core USI handshake and search.
set -u
cd "$(dirname "$0")/.."

ENGINE=./umatake
[ -x "$ENGINE" ] || { echo "build first: make"; exit 2; }

fail=0
check() { # <description> <pattern> <haystack-file>
    if grep -q "$2" "$3"; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected /$2/)"
        fail=1
    fi
}

OUT=$(mktemp)
printf 'usi\nisready\nposition startpos\ngo depth 8\nquit\n' | "$ENGINE" > "$OUT" 2>&1

check "id name Umatake" "id name Umatake" "$OUT"
check "usiok"           "^usiok"          "$OUT"
check "readyok"         "^readyok"        "$OUT"
check "info depth"      "info depth"      "$OUT"
check "bestmove"        "^bestmove "      "$OUT"

# bestmove must be a legal-looking USI move (not resign) from startpos
BM=$(grep "^bestmove " "$OUT" | tail -1 | awk '{print $2}')
if [[ "$BM" =~ ^([1-9][a-i][1-9][a-i]\+?|[PLNSBRG]\*[1-9][a-i])$ ]]; then
    echo "PASS: bestmove format ($BM)"
else
    echo "FAIL: bestmove format ($BM)"
    fail=1
fi

# sfen round-trip via 'd'
OUT2=$(mktemp)
printf 'position startpos moves 7g7f 3c3d\nd\nquit\n' | "$ENGINE" > "$OUT2" 2>&1
check "sfen output present" "sfen:" "$OUT2"

rm -f "$OUT" "$OUT2"
[ $fail -eq 0 ] && echo "USI TEST: ALL PASSED" || echo "USI TEST: FAILURES"
exit $fail
