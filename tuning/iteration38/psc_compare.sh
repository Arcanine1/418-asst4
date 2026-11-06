#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation with 128 cores}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration38/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\n' "$out"
for variant in baseline reduce pool; do
    printf 'Compiling %s\n' "$variant"
    sha256sum "tuning/iteration38/wireroute.$variant.cpp" >> "$out/source.sha256"
    mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration38/wireroute.$variant.cpp" code/validate.cpp -o "$out/wireroute.$variant"
done
mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas -DWR_CHECK_OCCUPANCY \
    tuning/iteration38/wireroute.pool.cpp code/validate.cpp -o "$out/wireroute.checked"
mpirun --version > "$out/mpi-version.txt"
cd "$out"
run_case() {
    local input=$1 variant=$2 ranks=$3 batch=$4 log=$5 group=16
    if [[ $input == few ]]; then group=8; fi
    printf 'Starting %s %s n%s B%s (%s)\n' "$input" "$variant" "$ranks" "$batch" "$log"
    if timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 \
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3 \
        -x WR_REDUCE_GROUP="$group" -np "$ranks" "./wireroute.$variant" \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for input in few medium abundant; do
    variants=(baseline reduce pool)
    if [[ $input == abundant ]]; then variants=(baseline pool); fi
    count=${#variants[@]}
    for variant in "${variants[@]}"; do
        run_case "$input" "$variant" 1 1 "$input-$variant-n1-b1.log"
    done
    for repeat in 1 2 3; do
        for ((offset=0; offset<count; offset++)); do
            variant=${variants[$(( (offset + repeat - 1) % count ))]}
            run_case "$input" "$variant" 1 16 "$input-$variant-n1-r$repeat.log"
        done
    done
    for repeat in 1 2 3 4 5; do
        for ((offset=0; offset<count; offset++)); do
            variant=${variants[$(( (offset + repeat - 1) % count ))]}
            run_case "$input" "$variant" 128 16 "$input-$variant-n128-r$repeat.log"
        done
    done
    run_case "$input" checked 128 16 "$input-checked.log"
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median
def result(path):
    text=Path(path).read_text()
    return (float(re.search(r'Computation time \(sec\): ([\d.]+)',text).group(1)),
            int(re.search(r'Total cost: (\d+)',text).group(1)),text)
limits={'few':3.400,'medium':11.134,'abundant':26.779}
cost_ref={'few':29240,'medium':267589,'abundant':1539198}
print('input variant B1_n1 B1_limit_pass B16_n1_median n128_median speedup costs_within_5pct runs_sec costs')
for name in ('few','medium','abundant'):
    variants=('baseline','reduce','pool') if name!='abundant' else ('baseline','pool')
    for variant in variants:
        b1,_,_=result(f'{name}-{variant}-n1-b1.log')
        serial=[result(f'{name}-{variant}-n1-r{r}.log')[0] for r in (1,2,3)]
        n1=median(serial)
        runs=[result(f'{name}-{variant}-n128-r{r}.log') for r in (1,2,3,4,5)]
        med=median(t for t,_,_ in runs)
        cost_ok=all(abs(c/cost_ref[name]-1)<=0.05 for _,c,_ in runs)
        print(name,variant,f'{b1:.6f}',b1<limits[name],f'{n1:.6f}',f'{med:.6f}',f'{n1/med:.2f}x',cost_ok,
              ','.join(f'{t:.6f}' for t,_,_ in runs),','.join(str(c) for _,c,_ in runs))
        metrics=[]
        for label in ('Max route time:','Average route time:','Max receive time:','Max sync time:'):
            metrics.append(median(float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1)) for _,_,text in runs))
        print('  route_max route_avg receive_max sync_max:',*(f'{v:.6f}' for v in metrics))
        print('  serial_runs='+','.join(f'{t:.6f}' for t in serial))
    check=Path(f'{name}-checked.log').read_text()
    print(name,'pool_checked_transient_max='+re.search(r'Observed transient max occupancy: (\d+)',check).group(1))
PY
printf 'Full logs: %s\n' "$out"
