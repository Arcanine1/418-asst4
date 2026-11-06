#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
platform=${1:-}
case "$platform" in
    ghc) max_ranks=8; ranks=(1 2 4 8) ;;
    psc) : "${SLURM_JOB_ID:?Run in a PSC allocation with 128 cores}"; max_ranks=128; ranks=(1 2 4 8 16 32 64 128) ;;
    *) printf >&2 'Usage: bash tuning/iteration35/benchmark_audit.sh ghc|psc\n'; exit 2 ;;
esac
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration35/$platform-${SLURM_JOB_ID:-local}-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Audit platform: %s\nLogs: %s\n' "$platform" "$out"
sha256sum code/wireroute.cpp > "$out/source.sha256"
mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    code/wireroute.cpp code/validate.cpp -o "$out/wireroute"
mpirun --version > "$out/mpi-version.txt"
cd "$out"
for input in few medium abundant; do
    group=16
    if [[ $input == few ]]; then group=8; fi
    for batch in 1 4 16; do
        cases=("${ranks[@]}")
        if [[ $batch == 1 ]]; then
            cases=(1)
            if [[ $platform == ghc && $input == medium ]]; then cases=(1 2 4 8); fi
            if [[ $platform == psc ]]; then cases=(1 32); fi
        fi
        for n in "${cases[@]}"; do
            repeats=1
            if [[ $n == 1 || $n == "$max_ranks" ]]; then repeats=3; fi
            for ((repeat=1; repeat<=repeats; repeat++)); do
                log="$input-b$batch-n$n-r$repeat.log"
                printf 'Starting %s B%s n%s repeat %s\n' "$input" "$batch" "$n" "$repeat"
                if timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
                    --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1 \
                    --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3 \
                    -x WR_REDUCE_GROUP="$group" -np "$n" ./wireroute \
                    -f "$root/code/inputs/timeinput/${input}_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1; then
                    if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; exit 1; fi
                else
                    rc=$?; tail -n 30 "$log" >&2; exit "$rc"
                fi
            done
        done
    done
done
python3 "$root/tuning/iteration35/summarize.py" "$out" "$platform" | tee summary.txt
