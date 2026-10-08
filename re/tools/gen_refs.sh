#!/bin/sh
# Make reference traces from the original: demo 3's input replayed,
# invulnerable, on each given mission, 4000 steps, 4 at a time.
# Usage (from the repository root): re/tools/gen_refs.sh MISSION...
cd re || exit 1
run() {
    TF_INVULNERABLE=1 TF_DEMO_MISSION=$1 .venv/bin/python tools/trace_original.py 3 4000 \
        traces/demo3_m$1_inv.csv > /dev/null 2>&1 && echo "made demo3_m$1_inv"
}
n=0
for m in "$@"; do
    run "$m" &
    n=$((n + 1))
    if [ $n -ge 4 ]; then wait; n=0; fi
done
wait
