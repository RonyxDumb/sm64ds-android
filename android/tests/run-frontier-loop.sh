#!/usr/bin/env bash
# Frontier loop: run the castle probe (fast build), link the reported missing
# handle's recovered unit(s) with frontier_batch.py (structor frames with
# structor_frames.py), rebuild, repeat. Stops
# when no automatic unit exists (hand work needed), on a build/projection
# error, or when the requested frames complete.
#
# Usage: SM64DS_GAME_BUILD=DIR android/tests/run-frontier-loop.sh <rom> [max-iterations]
#   probe env passes through (SM64DS_STAGE_CHARACTER, SM64DS_STAGE_FRAMES, ...).
set -uo pipefail
cd "$(dirname "$0")/../.."
ROM=${1:?rom path}
MAX=${2:-20}
export SM64DS_PROBE_FAST=${SM64DS_PROBE_FAST:-1}
for ((i=1; i<=MAX; i++)); do
    out=$(python3 android/tools/lift_stage.py 2>&1 >/dev/null) || { echo "$out" | tail -5; exit 2; }
    out=$(android/tests/run-stage-probe.sh "$ROM" 2>&1)
    status=$(echo "$out" | grep -E 'Stage frame|Stage frames completed| error|Error' | head -3)
    echo "[$i] $status"
    if echo "$out" | grep -q 'Stage frames completed'; then exit 0; fi
    if echo "$out" | grep -qE '^[^ ]*\.cpp:[0-9]+:[0-9]+: error|Traceback'; then
        echo "$out" | grep -E 'error|Error' | head -10; exit 2
    fi
    addr=$(echo "$out" | grep -oE 'Unlinked native code handle: [0-9]+' | grep -oE '[0-9]+$' | head -1)
    [[ -z $addr ]] && { echo "$out" | tail -8; exit 3; }
    plan=$(python3 android/tools/frontier_batch.py "$addr" --apply 2>&1)
    echo "$plan" | sed 's/^/    /'
    if ! echo "$plan" | grep -q '^appended'; then
        # Straight-line structor frames come from the cartridge code.
        structs=$(echo "$plan" | awk '/^STRUCT/{print $2}')
        [[ -z $structs ]] && { echo "no automatic unit for $addr"; exit 1; }
        frames=$(python3 android/tools/structor_frames.py --rom "$ROM" --add $structs --recurse 2>&1)
        echo "$frames" | sed 's/^/    /'
        echo "$frames" | grep -q '^REJECT' && { echo "structor frame needs hand work"; exit 1; }
    fi
done
