#!/usr/bin/env bash
set -Eeuo pipefail
trap 'rc=$?; printf >&2 "Failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "${out:-not created}"; exit "$rc"' ERR
platform=${1:-}
case "$platform" in
    ghc) max_ranks=8; ranks=(1 2 4 8) ;;
    psc)
        : "${SLURM_JOB_ID:?Run in a PSC allocation with 128 cores}"
        if [[ ${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-unknown}} != 1 ]]; then
            printf >&2 'The reference audit requires a known single-node Slurm allocation.\n'; exit 2
        fi
        max_ranks=128; ranks=(1 2 4 8 16 32 64 128) ;;
    *) printf >&2 'Usage: bash tuning/iteration41/benchmark_audit.sh ghc|psc\n'; exit 2 ;;
esac
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration41/$platform-${SLURM_JOB_ID:-local}-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
printf 'Audit platform: %s\nLogs: %s\n' "$platform" "$out"
sha256sum code/wireroute.cpp > "$out/source.sha256"
sha256sum tuning/run_psc.sh > "$out/launcher.sha256"
read -r code_hash rest < "$out/source.sha256"
read -r launcher_hash rest < "$out/launcher.sha256"
if [[ $code_hash != 7f2d398c35377980877d7ee63fec9de381e7276a977df7fd47a821f02ae810f0 ||
      $launcher_hash != b09e374597fb51b79a8735320583bf76f9574f10d4c59f8f1f589ad40c5f45a9 ]]; then
    printf >&2 'Copy the final code/wireroute.cpp and tuning/run_psc.sh from GHC before this audit.\n'; exit 1
fi
unset WR_REDUCE_GROUP WR_MPI_TRANSPORT WR_REDUCE_ALGORITHM WR_BCAST_ALGORITHM WR_PARTITION_LOGS
for build in normal checked; do
    compiler_flags=(-Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas)
    if [[ $build == checked ]]; then compiler_flags+=(-DWR_CHECK_OCCUPANCY); fi
    mpic++ "${compiler_flags[@]}" code/wireroute.cpp code/validate.cpp -o "$out/wireroute.$build"
done
mpirun --version > "$out/mpi-version.txt"
cd "$out"
for input in few medium abundant; do
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
            if [[ $platform == psc && $batch == 16 && $n == 128 ]]; then repeats=5; fi
            for ((repeat=1; repeat<=repeats; repeat++)); do
                log="$input-b$batch-n$n-r$repeat.log"
                printf 'Starting %s B%s n%s repeat %s\n' "$input" "$batch" "$n" "$repeat"
                if WR_EXECUTABLE="$out/wireroute.normal" timeout 180 bash "$root/tuning/run_psc.sh" "$n" \
                    -f "$root/code/inputs/timeinput/${input}_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1; then
                    if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; exit 1; fi
                else
                    rc=$?; tail -n 30 "$log" >&2; exit "$rc"
                fi
            done
        done
    done
    printf 'Starting %s checked B16 n%s\n' "$input" "$max_ranks"
    log="$input-checked.log"
    if WR_EXECUTABLE="$out/wireroute.checked" timeout 180 bash "$root/tuning/run_psc.sh" "$max_ranks" \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1; then
        if ! grep -q 'Validate Passed' "$log"; then tail -n 30 "$log" >&2; exit 1; fi
    else
        rc=$?; tail -n 30 "$log" >&2; exit "$rc"
    fi
done
python3 "$root/tuning/iteration41/summarize.py" "$out" "$platform" | tee summary.txt
