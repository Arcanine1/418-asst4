#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside your PSC compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration5/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
for v in tree direct; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration5/wireroute.$v.cpp" code/validate.cpp -o "$out/wireroute.$v"
done
cd "$out"
for input in few medium abundant; do
    for b in 4 16; do
        for r in 1 2 3; do
            versions='tree direct'
            if [ "$r" -eq 2 ]; then versions='direct tree'; fi
            for v in $versions; do
                log="$v-$input-b$b-n128-repeat$r.log"
                timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                    "./wireroute.$v" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                    -b "$b" -i 5 -p 0.1 > "$log" 2>&1
                grep -q 'Validate Passed' "$log"
                echo "$log"
                grep -E 'Computation time|Total cost|Validate|^Max (route|send|receive|sync) time' "$log"
            done
        done
    done
done | tee summary.txt
printf 'Full logs: %s\n' "$out"
