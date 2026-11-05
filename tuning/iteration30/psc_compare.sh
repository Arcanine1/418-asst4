#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration30/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\n' "$out"
for variant in baseline order h8 h16; do
    source=$variant; group=0
    if [[ $variant == h* ]]; then source=hierarchy; group=${variant#h}; fi
    printf 'Compiling %s\n' "$variant"
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        -DWR_GROUP_SIZE="$group" "tuning/iteration30/wireroute.$source.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done
cd "$out"
run_case() {
    local input=$1 variant=$2 ranks=$3 log=$4
    printf 'Starting %s %s n%s\n' "$input" "$variant" "$ranks"
    if timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 \
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3 \
        -np "$ranks" "./wireroute.$variant" \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for input in few medium; do
    variants=(baseline order h8 h16)
    for variant in "${variants[@]}"; do
        run_case "$input" "$variant" 1 "$input-$variant-n1.log"
    done
    for repeat in 1 2 3; do
        for offset in 0 1 2 3; do
            variant=${variants[$(( (offset + repeat - 1) % 4 ))]}
            run_case "$input" "$variant" 128 "$input-$variant-r$repeat.log"
        done
    done
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median
def result(path):
    text=Path(path).read_text()
    return (float(re.search(r'Computation time \(sec\): ([\d.]+)',text).group(1)),
            int(re.search(r'Total cost: (\d+)',text).group(1)),text)
limits={'few':3.400,'medium':11.134}
print('input variant n1_sec below_table1_B1_time median_n128_sec speedup runs_sec costs')
for name in ('few','medium'):
    variants=('baseline','order','h8','h16')
    for variant in variants:
        n1,_,_=result(f'{name}-{variant}-n1.log')
        runs=[result(f'{name}-{variant}-r{r}.log') for r in (1,2,3)]
        med=median(t for t,_,_ in runs)
        print(name,variant,f'{n1:.6f}',n1<limits[name],f'{med:.6f}',f'{n1/med:.2f}x',
              ','.join(f'{t:.6f}' for t,_,_ in runs),','.join(str(c) for _,c,_ in runs))
        metrics=[]
        for label in ('Max route time:','Average route time:','Max sync time:','Max split time:'):
            value=median(float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1)) for _,_,text in runs)
            metrics.append(f'{value:.6f}')
        print('  route_max route_avg sync_max split_max:',*metrics)
PY
printf 'Full logs: %s\n' "$out"
