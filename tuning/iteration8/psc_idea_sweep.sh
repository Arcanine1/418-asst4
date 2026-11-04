#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside your PSC compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration8/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
variants=(baseline fanout8 fanout16 tasks64_cap tasks64_no_cap)
for variant in "${variants[@]}"; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration8/wireroute.$variant.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done
cd "$out"
for input in medium abundant; do
    for batch in 16; do
        for repeat in 1 2 3; do
            if (( repeat == 2 )); then
                order=(tasks64_no_cap tasks64_cap fanout16 fanout8 baseline)
            else
                order=(baseline fanout8 fanout16 tasks64_cap tasks64_no_cap)
            fi
            for variant in "${order[@]}"; do
                log="$variant-$input-b$batch-r$repeat.log"
                timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                    "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                    -b "$batch" -i 5 -p 0.1 > "$log" 2>&1
                grep -q 'Validate Passed' "$log"
                time=$(sed -n 's/^Computation time (sec): //p' "$log" | tail -1)
                cost=$(sed -n 's/^Total cost: //p' "$log" | tail -1)
                printf '%s\t%s\t%s\t%s\t%s\n' "$input" "$batch" "$variant" "$repeat" "$time" >> results.tsv
                printf '%s\t%s\t%s\t%s\t%s\n' "$input" "$batch" "$variant" "$repeat" "$cost" >> costs.tsv
                echo "$log time=$time cost=$cost VALID"
            done
        done
done
done
python3 - <<'PY'
import csv, statistics
rows=list(csv.reader(open('results.tsv'), delimiter='\t'))
groups={}
for input_name, batch, variant, repeat, elapsed in rows:
    groups.setdefault((input_name,batch,variant), []).append(float(elapsed))
print('MEDIAN RESULTS (seconds)')
print('input\tbatch\tvariant\tmedian_sec\truns')
for key, values in sorted(groups.items()):
    print(f'{key[0]}\t{key[1]}\t{key[2]}\t{statistics.median(values):.6f}\t{",".join(f"{x:.6f}" for x in values)}')
PY
printf 'Full logs and tables: %s\n' "$out"
