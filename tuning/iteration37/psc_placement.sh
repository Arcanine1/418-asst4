#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation with 128 cores}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration37/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\nBuilding the unchanged final source\n' "$out"
sha256sum code/wireroute.cpp | tee "$out/source.sha256"
mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    code/wireroute.cpp code/validate.cpp -o "$out/wireroute"
mpirun --version > "$out/mpi-version.txt"
if command -v lscpu >/dev/null; then lscpu > "$out/hardware.txt" 2>&1 || true; fi
cd "$out"
flags=(--nooversubscribe --bind-to core
    --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
    --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
placements=()
for placement in core socket numa; do
    if timeout 30 mpirun "${flags[@]}" --map-by "$placement" --report-bindings -np 1 /bin/true \
        > "probe-$placement.log" 2>&1; then
        placements+=("$placement")
    else
        printf 'Placement %s unavailable; see probe-%s.log\n' "$placement" "$placement"
        if [[ $placement == core ]]; then tail -n 30 "probe-$placement.log" >&2; exit 1; fi
    fi
done
printf '%s\n' "${placements[@]}" > placements.txt
run_case() {
    local input=$1 placement=$2 ranks=$3 log=$4
    printf 'Starting %s mapping=%s n%s (%s)\n' "$input" "$placement" "$ranks" "$log"
    if timeout 180 mpirun "${flags[@]}" --map-by "$placement" --report-bindings \
        -x WR_REDUCE_GROUP=16 -np "$ranks" ./wireroute \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for input in medium abundant; do
    # The serial configuration is fixed. Only 128-rank placement changes.
    for repeat in 1 2 3; do run_case "$input" core 1 "$input-n1-r$repeat.log"; done
    count=${#placements[@]}
    for repeat in 1 2 3 4 5; do
        for ((offset=0; offset<count; offset++)); do
            placement=${placements[$(( (offset + repeat - 1) % count ))]}
            run_case "$input" "$placement" 128 "$input-$placement-r$repeat.log"
        done
    done
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median
def result(path):
    text=Path(path).read_text()
    def value(label):
        return float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1))
    return value('Computation time (sec):'),int(value('Total cost:')),text
placements=Path('placements.txt').read_text().split()
references={'medium':267589,'abundant':1539198}
limits={'medium':11.134,'abundant':26.779}
print('input mapping n1_core_median B16_n1_below_Table1_time n128_median speedup costs_within_5pct runs_sec costs')
for name in ('medium','abundant'):
    serial=[result(f'{name}-n1-r{r}.log')[0] for r in (1,2,3)]
    n1=median(serial)
    print(name,'serial_runs='+','.join(f'{t:.6f}' for t in serial))
    for placement in placements:
        runs=[result(f'{name}-{placement}-r{r}.log') for r in (1,2,3,4,5)]
        med=median(t for t,_,_ in runs)
        cost_ok=all(abs(c/references[name]-1)<=0.05 for _,c,_ in runs)
        print(name,placement,f'{n1:.6f}',n1<limits[name],f'{med:.6f}',f'{n1/med:.2f}x',cost_ok,
              ','.join(f'{t:.6f}' for t,_,_ in runs),','.join(str(c) for _,c,_ in runs))
        metrics=[]
        for label in ('Max route time:','Average route time:','Max receive time:','Max sync time:'):
            metrics.append(median(float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1)) for _,_,text in runs))
        print('  route_max route_avg receive_max sync_max:',*(f'{v:.6f}' for v in metrics))
PY
printf 'Full logs and binding reports: %s\n' "$out"
