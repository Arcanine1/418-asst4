#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration14/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
variants=(adapt strong fine strongfine)
blends=(0.5 1.0 0.5 1.0)
thresholds=(0.1 0.1 0.05 0.05)
for idx in 0 1 2 3; do
    name=${variants[$idx]}
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        -DWR_ADAPT_BLEND="${blends[$idx]}" -DWR_SPLIT_THRESHOLD="${thresholds[$idx]}" \
        tuning/iteration14/wireroute.tunable.cpp code/validate.cpp -o "$out/wireroute.$name"
done
cd "$out"
for input in few medium abundant; do
    printf 'Starting %s n1\n' "$input"
    timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 1 \
        ./wireroute.adapt -f "$root/code/inputs/timeinput/${input}_wires.txt" \
        -b 16 -i 5 -p 0.1 > "$input-n1.log" 2>&1
    grep -q 'Validate Passed' "$input-n1.log"
    for repeat in 1 2 3; do
        for variant in "${variants[@]}"; do
            log="$input-$variant-r$repeat.log"
            printf 'Starting %s %s repeat %s\n' "$input" "$variant" "$repeat"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 16 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
            printf 'Finished %s %s repeat %s\n' "$input" "$variant" "$repeat"
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
    return time, cost

print('input variant median_sec speedup runs_sec costs')
for name in ('few', 'medium', 'abundant'):
    n1,_=result(f'{name}-n1.log')
    for variant in ('adapt', 'strong', 'fine', 'strongfine'):
        runs=[result(f'{name}-{variant}-r{r}.log') for r in (1,2,3)]
        med=median(time for time,_ in runs)
        print(name, variant, f'{med:.6f}', f'{n1/med:.2f}x',
              ','.join(f'{time:.6f}' for time,_ in runs),
              ','.join(str(cost) for _,cost in runs))
PY
printf 'Full logs: %s\n' "$out"
