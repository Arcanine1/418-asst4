#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration12/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
for variant in baseline shuffle adaptive; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration12/wireroute.$variant.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done
cd "$out"
for input in few medium abundant; do
    timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 1 \
        ./wireroute.baseline -f "$root/code/inputs/timeinput/${input}_wires.txt" \
        -b 16 -i 5 -p 0.1 > "$input-n1.log" 2>&1
    grep -q 'Validate Passed' "$input-n1.log"
    for repeat in 1 2 3; do
        for variant in baseline adaptive shuffle; do
            log="$input-$variant-r$repeat.log"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 128 \
                "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 16 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
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

print('input variant median_sec speedup_vs_baseline_n1 runs_sec costs')
for name in ('few', 'medium', 'abundant'):
    n1,_=result(f'{name}-n1.log')
    for variant in ('baseline', 'adaptive', 'shuffle'):
        runs=[result(f'{name}-{variant}-r{r}.log') for r in (1,2,3)]
        med=median(time for time,_ in runs)
        print(name, variant, f'{med:.6f}', f'{n1/med:.2f}x',
              ','.join(f'{time:.6f}' for time,_ in runs),
              ','.join(str(cost) for _,cost in runs))
PY
printf 'Full logs: %s\n' "$out"
