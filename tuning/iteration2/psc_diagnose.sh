#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside your PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration2/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    code/wireroute.cpp code/validate.cpp -o "$out/wireroute"
cd "$out"
{ hostname; date; mpirun --version; lscpu; } > environment.txt 2>&1
export WR_RANK_TIMINGS=1
for repeat in 1 2 3; do
    for n in 64 128; do
        log="abundant-n$n-repeat$repeat.log"
        timeout 180 mpirun --nooversubscribe --bind-to core --map-by core --report-bindings \
            -x WR_RANK_TIMINGS -np "$n" ./wireroute \
            -f "$root/code/inputs/timeinput/abundant_wires.txt" -b 4 -i 5 -p 0.1 > "$log" 2>&1
        grep -q 'Validate Passed' "$log"
        echo "$log"
        grep -E 'Initialization time|Computation time|Total cost|Validate|^(Max|Average) .*time' "$log"
        echo 'Five ranks with the highest routing times:'
        grep '^Rank timing:' "$log" | sort -t= -k3,3nr | sed -n '1,5p'
    done
done | tee summary.txt
printf 'Full logs and binding reports: %s\n' "$out"
