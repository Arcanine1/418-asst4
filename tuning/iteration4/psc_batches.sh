#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside your PSC compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration4/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    code/wireroute.cpp code/validate.cpp -o "$out/wireroute"
cd "$out"
for b in 4 16; do
    for input in few medium abundant; do
        for n in 1 128; do
            repeats=3
            if [ "$n" -eq 1 ]; then repeats=1; fi
            for ((r=1; r<=repeats; r++)); do
                log="$input-b$b-n$n-repeat$r.log"
                timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np "$n" \
                    ./wireroute -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                    -b "$b" -i 5 -p 0.1 > "$log" 2>&1
                grep -q 'Validate Passed' "$log"
                echo "$log"
                grep -E 'Computation time|Total cost|Validate|^Max (route|sync) time' "$log"
                echo 'Effective batch sizes:'
                sed -n 's/.*batch_size: \([0-9]*\).*/\1/p' "$log" | sort -nu
            done
        done
    done
done | tee summary.txt
printf 'Full logs: %s\n' "$out"
