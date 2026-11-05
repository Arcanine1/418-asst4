#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration31/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\nCompiling one executable for all groups\n' "$out"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration31/wireroute.groups.cpp code/validate.cpp -o "$out/wireroute.groups"
cd "$out"
run_case() {
    local input=$1 group=$2 ranks=$3 log=$4
    printf 'Starting %s group%s n%s\n' "$input" "$group" "$ranks"
    if timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 \
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3 \
        -x WR_REDUCE_GROUP="$group" -np "$ranks" ./wireroute.groups \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for input in few medium; do
    for repeat in 1 2 3; do
        run_case "$input" 1 1 "$input-n1-r$repeat.log"
    done
    if [[ $input == few ]]; then groups=(4 8 16); else groups=(8 16 32 64); fi
    count=${#groups[@]}
    for repeat in 1 2 3; do
        for ((offset=0; offset<count; offset++)); do
            group=${groups[$(( (offset + repeat - 1) % count ))]}
            run_case "$input" "$group" 128 "$input-g$group-r$repeat.log"
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
            int(re.search(r'Total cost: (\d+)',text).group(1)))
limits={'few':3.400,'medium':11.134}
print('input group n1_median below_table1_B1_time n128_median speedup runs_sec costs')
for name in ('few','medium'):
    serial=[result(f'{name}-n1-r{r}.log')[0] for r in (1,2,3)]
    n1=median(serial)
    print(name,'n1_runs='+','.join(f'{t:.6f}' for t in serial))
    groups=(4,8,16) if name=='few' else (8,16,32,64)
    for group in groups:
        runs=[result(f'{name}-g{group}-r{r}.log') for r in (1,2,3)]
        med=median(t for t,_ in runs)
        print(name,group,f'{n1:.6f}',n1<limits[name],f'{med:.6f}',f'{n1/med:.2f}x',
              ','.join(f'{t:.6f}' for t,_ in runs),','.join(str(c) for _,c in runs))
PY
printf 'Full logs: %s\n' "$out"
