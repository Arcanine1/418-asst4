#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration9/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"

for variant in task poll4 batch; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration9/wireroute.$variant.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done

cd "$out"
printf 'input\tvariant\trepeat\tseconds\tcost\n' > results.tsv
for input in medium abundant; do
    for repeat in 1 2 3; do
        if (( repeat == 2 )); then
            order=(batch poll4 task)
        else
            order=(task poll4 batch)
        fi
        for variant in "${order[@]}"; do
            log="$input-$variant-r$repeat.log"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 16 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
            seconds=$(sed -n 's/^Computation time (sec): //p' "$log" | tail -1)
            cost=$(sed -n 's/^Total cost: //p' "$log" | tail -1)
            printf '%s\t%s\t%s\t%s\t%s\n' "$input" "$variant" "$repeat" "$seconds" "$cost" >> results.tsv
            printf '%s: time=%s cost=%s VALID\n' "$log" "$seconds" "$cost"
            grep -E '^Max (route|receive|sync|stall) time:' "$log"
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
        groups[(row['input'], row['variant'])].append((float(row['seconds']), int(row['cost'])))
print('MEDIANS: input variant seconds cost')
for (input_name, variant), measurements in sorted(groups.items()):
    print(input_name, variant, f'{median(t for t, _ in measurements):.6f}',
          int(median(c for _, c in measurements)))
PY
printf 'Full logs: %s\n' "$out"
