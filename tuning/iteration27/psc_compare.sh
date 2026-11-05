#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration27/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpirun --version > "$out/mpi-version.txt"
ompi_info --param coll tuned --level 9 > "$out/mpi-tuned-options.txt"
grep -q '3:"recursive_doubling"' "$out/mpi-tuned-options.txt"
grep -q '2:"bruck"' "$out/mpi-tuned-options.txt"
for binary in baseline gather reduce; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration27/wireroute.$binary.cpp" code/validate.cpp -o "$out/wireroute.$binary"
done
cd "$out"
run_case() {
    local input=$1 mode=$2 ranks=$3 log=$4 binary
    local mpi_flags=(--nooversubscribe --bind-to core --map-by core)
    case "$mode" in
        baseline) binary=baseline ;;
        gather-default) binary=gather ;;
        reduce-default) binary=reduce ;;
        gather-doubling)
            binary=gather
            mpi_flags+=(--mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allgather_algorithm 3) ;;
        gather-bruck)
            binary=gather
            mpi_flags+=(--mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allgather_algorithm 2) ;;
        reduce-doubling)
            binary=reduce
            mpi_flags+=(--mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3) ;;
    esac
    printf 'Starting %s %s n%s\n' "$input" "$mode" "$ranks"
    timeout 180 mpirun "${mpi_flags[@]}" -np "$ranks" "./wireroute.$binary" \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1
    grep -q 'Validate Passed' "$log"
}
modes=(baseline gather-default gather-doubling gather-bruck reduce-default reduce-doubling)
for input in few medium; do
    for mode in baseline gather-default reduce-default; do
        run_case "$input" "$mode" 1 "$input-$mode-n1.log"
    done
    for repeat in 1 2 3; do
        for offset in 0 1 2 3 4 5; do
            mode=${modes[$(( (offset + repeat - 1) % 6 ))]}
            run_case "$input" "$mode" 128 "$input-$mode-r$repeat.log"
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
print('input mode n1_sec median_n128_sec speedup runs_sec costs')
for name in ('few','medium'):
    for mode in ('baseline','gather-default','gather-doubling','gather-bruck','reduce-default','reduce-doubling'):
        serial='baseline' if mode=='baseline' else ('gather-default' if mode.startswith('gather') else 'reduce-default')
        n1,_=result(f'{name}-{serial}-n1.log')
        runs=[result(f'{name}-{mode}-r{r}.log') for r in (1,2,3)]
        med=median(t for t,_ in runs)
        print(name,mode,f'{n1:.6f}',f'{med:.6f}',f'{n1/med:.2f}x',
              ','.join(f'{t:.6f}' for t,_ in runs),','.join(str(c) for _,c in runs))
PY
printf 'Full logs: %s\n' "$out"
