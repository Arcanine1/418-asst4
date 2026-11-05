#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration24/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
for variant in tpp2 tpp4 tpp8 tpp16 few16 few16collect few32collect; do
    threshold=0.1
    min_wires=1024
    case "$variant" in
        tpp*) tpp=${variant#tpp} ;;
        few16) tpp=16; threshold=0.0 ;;
        few16collect) tpp=16; threshold=0.0; min_wires=0 ;;
        few32collect) tpp=32; threshold=0.0; min_wires=0 ;;
    esac
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        -DWR_TASKS_PER_PROCESSOR="$tpp" -DWR_SPLIT_THRESHOLD="$threshold" \
        -DWR_COLLECTIVE_MIN_WIRES="$min_wires" \
        tuning/iteration24/wireroute.tunable.cpp code/validate.cpp -o "$out/wireroute.$variant"
done
cd "$out"
for input in few medium; do
    if [[ $input == few ]]; then
        variants=(tpp4 tpp8 few16 few16collect few32collect)
    else
        variants=(tpp4 tpp8 tpp16 tpp2)
    fi
    for variant in "${variants[@]}"; do
        printf 'Starting %s %s n1\n' "$input" "$variant"
        timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np 1 \
            "./wireroute.$variant" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
            -b 16 -i 5 -p 0.1 > "$input-$variant-n1.log" 2>&1
        grep -q 'Validate Passed' "$input-$variant-n1.log"
    done
    for repeat in 1 2 3; do
        for variant in "${variants[@]}"; do
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
    variants=('tpp4','tpp8','few16','few16collect','few32collect') if name=='few' else ('tpp4','tpp8','tpp16','tpp2')
    for variant in variants:
        n1,_=result(f'{name}-{variant}-n1.log')
        runs=[result(f'{name}-{variant}-n128-r{r}.log') for r in (1,2,3)]
        med=median(time for time,_ in runs)
        print(name,variant,f'{n1:.6f}',f'{med:.6f}',f'{n1/med:.2f}x',
              ','.join(f'{time:.6f}' for time,_ in runs),
              ','.join(str(cost) for _,cost in runs))
PY
printf 'Full logs: %s\n' "$out"
