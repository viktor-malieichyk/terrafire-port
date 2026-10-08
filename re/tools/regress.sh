#!/bin/sh
# Compare the port against the stored reference traces from the original
# (re/traces/, made by trace_original.py). Name: demo<D>[_m<M>][_inv].csv
#   D: demo replayed, M: TF_DEMO_MISSION override, _inv: TF_INVULNERABLE=1.
# Usage (from the repository root): re/tools/regress.sh [port binary]
BIN=${1:-port/build/terrafire}
TMP=${TMPDIR:-/tmp}/tf_regress.$$
fail=0
for ref in re/traces/demo*.csv; do
    name=$(basename "$ref" .csv)
    demo=$(echo "$name" | sed 's/^demo\([0-9]*\).*/\1/')
    mission=$(echo "$name" | sed -n 's/.*_m\([0-9]*\).*/\1/p')
    inv=$(echo "$name" | grep -q '_inv' && echo 1)
    steps=$(($(wc -l < "$ref") + 100))
    env ${mission:+TF_DEMO_MISSION=$mission} ${inv:+TF_INVULNERABLE=1} \
        "$BIN" --trace "$demo" "$steps" "$TMP.csv" . > /dev/null 2>&1
    result=$(python3 re/tools/trace_compare.py "$ref" "$TMP.csv" | tail -1)
    case "$result" in all*) ;; *) fail=1 ;; esac
    printf '%-20s %s\n' "$name" "$result"
done
rm -f "$TMP.csv"
exit $fail
