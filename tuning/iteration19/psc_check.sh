#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration19/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration19/wireroute.u8_checked.cpp code/validate.cpp -o "$out/wireroute.u8_checked"
cd "$out"
for input in few medium abundant; do
    for repeat in 1 2 3; do
        log="$input-r$repeat.log"
        printf 'Starting %s checked repeat %s\n' "$input" "$repeat"
        timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
            ./wireroute.u8_checked -f "$root/code/inputs/timeinput/${input}_wires.txt" \
            -b 16 -i 5 -p 0.1 > "$log" 2>&1
        grep -q 'Validate Passed' "$log"
        printf '%s repeat %s: ' "$input" "$repeat"
        grep -E 'Observed transient max occupancy|Total cost' "$log" | tr '\n' ' '
        printf '\n'
    done
done
printf 'Full logs: %s\n' "$out"
