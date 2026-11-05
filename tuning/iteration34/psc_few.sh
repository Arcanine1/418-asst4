#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration34/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\n' "$out"
sha256sum code/wireroute.cpp | tee "$out/source.sha256"
mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    code/wireroute.cpp code/validate.cpp -o "$out/wireroute"
ompi_info --param coll tuned --level 9 > "$out/mpi-options.txt"
cd "$out"
run_case() {
    local mode=$1 ranks=$2 log=$3 group=16
    local flags=(--nooversubscribe --bind-to core --map-by core
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
    case "$mode" in
        g8) group=8 ;;
        g32) group=32 ;;
        binomial) flags+=(--mca coll_tuned_reduce_algorithm 5 --mca coll_tuned_bcast_algorithm 6) ;;
        linear) flags+=(--mca coll_tuned_reduce_algorithm 1 --mca coll_tuned_bcast_algorithm 1) ;;
    esac
    printf 'Starting few %s n%s (%s)\n' "$mode" "$ranks" "$log"
    if timeout 180 mpirun "${flags[@]}" -x WR_REDUCE_GROUP="$group" -np "$ranks" ./wireroute \
        -f "$root/code/inputs/timeinput/few_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for repeat in 1 2 3; do run_case g16 1 "n1-r$repeat.log"; done
modes=(g16 g8 binomial g32 linear)
for repeat in 1 2 3 4 5; do
    for offset in 0 1 2 3 4; do
        mode=${modes[$(( (offset + repeat - 1) % 5 ))]}
        run_case "$mode" 128 "$mode-r$repeat.log"
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
serial=[result(f'n1-r{r}.log')[0] for r in (1,2,3)]
n1=median(serial)
print('n1_runs='+','.join(f'{t:.6f}' for t in serial))
print('mode n1_median below_table1_time n128_median speedup runs_sec costs')
for mode in ('g16','g8','binomial','g32','linear'):
    runs=[result(f'{mode}-r{r}.log') for r in (1,2,3,4,5)]
    med=median(t for t,_ in runs)
    print(mode,f'{n1:.6f}',n1<3.400,f'{med:.6f}',f'{n1/med:.2f}x',
          ','.join(f'{t:.6f}' for t,_ in runs),','.join(str(c) for _,c in runs))
PY
printf 'Full logs: %s\n' "$out"
