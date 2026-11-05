#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration33/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\nBuilding with the code/Makefile compiler flags\n' "$out"
for build in normal checked; do
    extra=()
    if [[ $build == checked ]]; then extra=(-DWR_CHECK_OCCUPANCY); fi
    mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas "${extra[@]}" \
        tuning/iteration33/wireroute.abundant.cpp code/validate.cpp -o "$out/wireroute.$build"
done
mpirun --version > "$out/mpi-version.txt"
cd "$out"
run_case() {
    local input=$1 ranks=$2 batch=$3 build=$4 log=$5
    printf 'Starting %s n%s B%s %s (%s)\n' "$input" "$ranks" "$batch" "$build" "$log"
    if timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 \
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3 \
        -x WR_REDUCE_GROUP=16 -np "$ranks" "./wireroute.$build" \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
for input in few medium abundant; do
    run_case "$input" 1 1 normal "$input-n1-b1.log"
    for repeat in 1 2 3; do
        run_case "$input" 1 16 normal "$input-n1-r$repeat.log"
    done
    for ranks in 2 4 8 16 32 64; do
        run_case "$input" "$ranks" 16 normal "$input-n$ranks-r1.log"
    done
    repeats=5
    if [[ $input == abundant ]]; then repeats=3; fi
    for ((repeat=1; repeat<=repeats; repeat++)); do
        run_case "$input" 128 16 normal "$input-n128-r$repeat.log"
    done
    run_case "$input" 128 16 checked "$input-checked.log"
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median
def result(path):
    text=Path(path).read_text()
    return (float(re.search(r'Computation time \(sec\): ([\d.]+)',text).group(1)),
            int(re.search(r'Total cost: (\d+)',text).group(1)))
limits={'few':3.400,'medium':11.134,'abundant':26.779}
for name in ('few','medium','abundant'):
    b1,_=result(f'{name}-n1-b1.log')
    serial=[result(f'{name}-n1-r{r}.log')[0] for r in (1,2,3)]
    n1=median(serial)
    print(f'{name} B1_n1={b1:.6f} below_Table1={b1<limits[name]} B16_n1_median={n1:.6f}')
    print('  B16_n1_runs='+','.join(f'{t:.6f}' for t in serial))
    print('  ranks median_sec speedup runs_sec costs')
    for ranks in (1,2,4,8,16,32,64,128):
        runs=[result(p) for p in sorted(Path('.').glob(f'{name}-n{ranks}-r*.log'))]
        med=median(t for t,_ in runs)
        print(' ',ranks,f'{med:.6f}',f'{n1/med:.2f}x',','.join(f'{t:.6f}' for t,_ in runs),
              ','.join(str(c) for _,c in runs))
    check=Path(f'{name}-checked.log').read_text()
    peak=int(re.search(r'Observed transient max occupancy: (\d+)',check).group(1))
    print('  checked_transient_max=',peak)
PY
printf 'Full logs: %s\n' "$out"
