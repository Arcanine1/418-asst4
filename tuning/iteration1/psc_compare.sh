#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run this inside a PSC compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration1/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
# Compile both versions with exactly the same flags and MPI toolchain.
for version in before after; do
    mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
        "tuning/iteration1/wireroute.$version.cpp" code/validate.cpp -o "$out/wireroute.$version"
done
cd "$out"
{
    hostname
    date
    mpirun --version
    lscpu
    sha256sum "$root/tuning/iteration1/"wireroute.{before,after}.cpp
} > environment.txt 2>&1
# One comparison at batch size 4 first; expand after checking the 128-rank result.
for input in few medium abundant; do
    for n in 1 32 64 128; do
        for version in before after; do
            log="$version-$input-n$n-b4.log"
            timeout 180 mpirun --nooversubscribe --bind-to core --map-by core -np "$n" \
                "./wireroute.$version" -f "$root/code/inputs/timeinput/${input}_wires.txt" \
                -b 4 -i 5 -p 0.1 > "$log" 2>&1
            grep -q 'Validate Passed' "$log"
            echo "$log"
            grep -E 'Initialization time|Computation time|Total cost|Validate|^(Max|Average) .*time' "$log"
        done
    done
done | tee summary.txt
printf 'Full logs: %s\n' "$out"
