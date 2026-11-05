#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration23/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
for variant in baseline interleave; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration23/wireroute.$variant.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done
cd "$out"
for input in few medium; do
    for variant in baseline interleave; do
        printf 'Starting %s %s n1\n' "$input" "$variant"
        timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 1 \
            "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
            -b 16 -i 5 -p 0.1 > "$input-$variant-n1.log" 2>&1
        grep -q 'Validate Passed' "$input-$variant-n1.log"
    done
    for repeat in 1 2 3; do
        for variant in baseline interleave; do
            log="$input-$variant-n128-r$repeat.log"
            printf 'Starting %s %s n128 repeat %s\n' "$input" "$variant" "$repeat"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 16 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
            printf 'Finished %s %s n128 repeat %s\n' "$input" "$variant" "$repeat"
        done
    done
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median

def result(path):
    text=Path(path).read_text()
    time=float(re.search(r'Computation time \(sec\): ([\d.]+)', text).group(1))
    cost=int(re.search(r'Total cost: (\d+)', text).group(1))
    return time,cost

print('input variant n1_sec median_n128_sec speedup runs_sec costs')
for name in ('few','medium'):
    for variant in ('baseline','interleave'):
        n1,_=result(f'{name}-{variant}-n1.log')
        runs=[result(f'{name}-{variant}-n128-r{r}.log') for r in (1,2,3)]
        med=median(time for time,_ in runs)
        print(name,variant,f'{n1:.6f}',f'{med:.6f}',f'{n1/med:.2f}x',
              ','.join(f'{time:.6f}' for time,_ in runs),
              ','.join(str(cost) for _,cost in runs))
PY
printf 'Full logs: %s\n' "$out"
