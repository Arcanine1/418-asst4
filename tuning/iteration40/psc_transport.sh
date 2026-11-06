#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
: "${SLURM_JOB_ID:?Run in a PSC interactive allocation with 128 cores}"
if [[ ${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-1}} != 1 ]]; then
    printf >&2 'This shared-memory transport experiment requires a single-node allocation.\n'; exit 2
fi
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration40/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Logs: %s\n' "$out"
sha256sum code/wireroute.cpp | tee "$out/source.sha256"
read -r source_hash rest < "$out/source.sha256"
if [[ $source_hash != 7f2d398c35377980877d7ee63fec9de381e7276a977df7fd47a821f02ae810f0 ]]; then
    printf >&2 'Please copy the promoted code/wireroute.cpp from GHC before this test.\n'; exit 1
fi
ompi_info --param btl vader --level 9 > "$out/shared-memory-options.txt" 2>&1
ompi_info --parsable --param pml all > "$out/pml-components.txt"
for parameter in btl_vader_fbox_threshold btl_vader_fbox_max btl_vader_fbox_size; do
    if ! grep -q "$parameter" "$out/shared-memory-options.txt"; then
        printf >&2 'MPI option %s is unavailable; see shared-memory-options.txt.\n' "$parameter"; exit 1
    fi
done
if ! grep -q 'mca:pml:ob1:' "$out/pml-components.txt"; then
    printf >&2 'The ob1 MPI component is unavailable; see pml-components.txt.\n'; exit 1
fi
for build in normal checked; do
    compiler_flags=(-Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas)
    if [[ $build == checked ]]; then compiler_flags+=(-DWR_CHECK_OCCUPANCY); fi
    mpic++ "${compiler_flags[@]}" code/wireroute.cpp code/validate.cpp -o "$out/wireroute.$build"
done
mpirun --version > "$out/mpi-version.txt"
cd "$out"
run_case() {
    local mode=$1 ranks=$2 batch=$3 build=$4 log=$5
    local flags=(--nooversubscribe --bind-to core --map-by core
        --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
        --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
    if [[ $mode != native ]]; then flags+=(--mca pml ob1 --mca btl self,vader); fi
    if [[ $mode == fast || $mode == large ]]; then
        flags+=(--mca btl_vader_fbox_threshold 1 --mca btl_vader_fbox_max 128)
    fi
    if [[ $mode == large ]]; then flags+=(--mca btl_vader_fbox_size 8192); fi
    printf 'Starting abundant %s n%s B%s %s\n' "$mode" "$ranks" "$batch" "$build"
    if timeout 180 mpirun "${flags[@]}" -x WR_REDUCE_GROUP=16 -np "$ranks" "./wireroute.$build" \
        -f "$root/code/inputs/timeinput/abundant_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; return 1; fi
    else
        local rc=$?; tail -n 30 "$log" >&2; return "$rc"
    fi
}
# Keep the one-rank policy fixed; only the 128-rank transport changes.
run_case native 1 1 normal n1-b1.log
for repeat in 1 2 3; do run_case native 1 16 normal "n1-r$repeat.log"; done
modes=(native shm fast large)
for repeat in 1 2 3 4 5; do
    for offset in 0 1 2 3; do
        mode=${modes[$(( (offset + repeat - 1) % 4 ))]}
        run_case "$mode" 128 16 normal "$mode-r$repeat.log"
    done
done
for mode in "${modes[@]}"; do run_case "$mode" 128 16 checked "$mode-checked.log"; done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median
def result(path):
    text=Path(path).read_text()
    def number(label):
        return float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1))
    return number('Computation time (sec):'),int(number('Total cost:')),text
b1,_,_=result('n1-b1.log')
serial=[result(f'n1-r{r}.log')[0] for r in (1,2,3)]
n1=median(serial)
print(f'abundant B1_n1={b1:.6f} below_Table1={b1<26.779} B16_serial_runs='+','.join(f'{t:.6f}' for t in serial))
print('mode n1_median n128_median speedup costs_within_5pct runs_sec costs')
for mode in ('native','shm','fast','large'):
    runs=[result(f'{mode}-r{r}.log') for r in (1,2,3,4,5)]
    med=median(t for t,_,_ in runs)
    print(mode,f'{n1:.6f}',f'{med:.6f}',f'{n1/med:.2f}x',all(abs(c/1539198-1)<=.05 for _,c,_ in runs),
          ','.join(f'{t:.6f}' for t,_,_ in runs),','.join(str(c) for _,c,_ in runs))
    metrics=[]
    for label in ('Max route time:','Average route time:','Max receive time:','Max sync time:'):
        metrics.append(median(float(re.search(re.escape(label)+r'\s*([\d.]+)',text).group(1)) for _,_,text in runs))
    print('  route_max route_avg receive_max sync_max:',*(f'{v:.6f}' for v in metrics))
    text=Path(f'{mode}-checked.log').read_text()
    print('  checked_transient_max='+re.search(r'Observed transient max occupancy: (\d+)',text).group(1))
PY
printf 'Full logs: %s\n' "$out"
