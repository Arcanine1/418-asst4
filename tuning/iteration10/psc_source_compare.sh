#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration10/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"

mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration8/wireroute.baseline.cpp code/validate.cpp -o "$out/wireroute.2d"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration9/wireroute.task.cpp code/validate.cpp -o "$out/wireroute.flat"

cd "$out"
printf 'input\tversion\trepeat\tseconds\tcost\n' > results.tsv
for input in medium abundant; do
    for repeat in 1 2 3; do
        if (( repeat == 2 )); then order=(flat 2d); else order=(2d flat); fi
        for version in "${order[@]}"; do
            log="$input-$version-r$repeat.log"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                "./wireroute.$version" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 16 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
            seconds=$(sed -n 's/^Computation time (sec): //p' "$log" | tail -1)
            cost=$(sed -n 's/^Total cost: //p' "$log" | tail -1)
            printf '%s\t%s\t%s\t%s\t%s\n' "$input" "$version" "$repeat" "$seconds" "$cost" >> results.tsv
            printf '%s: time=%s cost=%s VALID\n' "$log" "$seconds" "$cost"
        done
    done
done

python3 - <<'PY'
import csv
from collections import defaultdict
from statistics import median
groups = defaultdict(list)
with open('results.tsv', newline='') as stream:
    for row in csv.DictReader(stream, delimiter='\t'):
        groups[(row['input'], row['version'])].append((float(row['seconds']), int(row['cost'])))
print('MEDIANS: input version seconds cost')
for (input_name, version), values in sorted(groups.items()):
    print(input_name, version, f'{median(t for t, _ in values):.6f}', int(median(c for _, c in values)))
PY
printf 'Full logs: %s\n' "$out"
