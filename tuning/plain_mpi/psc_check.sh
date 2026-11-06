#!/usr/bin/env bash
set -Eeuo pipefail
out=not-created
trap 'rc=$?; printf >&2 "Check failed at line %s (status %s). Logs: %s\n" "$LINENO" "$rc" "$out"; exit "$rc"' ERR
root=$(cd "$(dirname "$0")/../.." && pwd)
: "${SLURM_JOB_ID:?Run in a PSC single-node allocation with 128 cores}"
if [[ ${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-unknown}} != 1 ]]; then
    printf >&2 'Use a known single-node allocation with 128 cores.\n'
    exit 2
fi
out="$root/tuning/plain_mpi/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/source/inputs"
printf 'Plain mpirun endpoint check. Logs: %s\n' "$out"
python3 - <<'PY'
import os
overrides=[key for key in os.environ if key.startswith(("OMPI_MCA_","OPAL_MCA_","PMIX_MCA_","PRTE_MCA_"))]
if overrides:
    raise SystemExit("Unset custom MPI environment settings first: "+", ".join(sorted(overrides)))
PY
cp "$root/code/wireroute.cpp" "$root/code/wireroute.h" "$root/code/validate.cpp" "$root/code/Makefile" "$out/source/"
for input in few medium abundant; do
    cp "$root/code/inputs/timeinput/${input}_wires.txt" "$out/source/inputs/"
done
sha256sum "$out/source/wireroute.cpp" | tee "$out/source.sha256"
mpirun --version > "$out/mpi-version.txt"
make -C "$out/source" wireroute > "$out/build.log" 2>&1
work=$(mktemp -d /tmp/wireroute-plain-psc.XXXXXX)
mkdir "$work/outputs"
cd "$work"
for input in few medium abundant; do
    for config in 1:1:3 16:1:3 16:128:5; do
        IFS=: read -r batch ranks repeats <<< "$config"
        for ((repeat=1; repeat<=repeats; repeat++)); do
            log="$out/$input-b$batch-n$ranks-r$repeat.log"
            printf 'Run %s B%s n%s repeat %s\n' "$input" "$batch" "$ranks" "$repeat"
            timeout --kill-after=5s 180 mpirun -np "$ranks" "$out/source/wireroute" \
                -f "$out/source/inputs/${input}_wires.txt" -b "$batch" -i 5 -p 0.1 > "$log" 2>&1
            if ! grep -q 'Validate Passed' "$log"; then
                tail -30 "$log" >&2
                exit 1
            fi
        done
    done
done
cd "$out"
python3 - <<'PY' | tee summary.txt
from pathlib import Path
from statistics import median
import re
def measurements(name,batch,ranks):
    values=[]
    for path in Path(".").glob(f"{name}-b{batch}-n{ranks}-r*.log"):
        text=path.read_text()
        if "Validate Passed" not in text:
            raise SystemExit(f"Validation missing: {path}")
        seconds=float(re.search(r"Computation time \(sec\): ([\d.]+)",text).group(1))
        cost=int(re.search(r"Total cost: (\d+)",text).group(1))
        values.append((seconds,cost))
    return values
print("Launch policy: plain mpirun -np N; no custom MPI runtime settings.")
print("input B1_n1_median B1_limit_pass B16_n1_median B16_n128_median speedup reference_approx costs_within_5pct parallel_runs")
for name,limit,cost_ref,target in (("few",3.400,29240,76),("medium",11.134,267589,100),("abundant",26.779,1539198,89)):
    b1=measurements(name,1,1)
    serial=measurements(name,16,1)
    parallel=measurements(name,16,128)
    assert len(b1)==3 and len(serial)==3 and len(parallel)==5
    b1_time=median(t for t,_ in b1)
    n1=median(t for t,_ in serial)
    n128=median(t for t,_ in parallel)
    cost_pass=all(abs(c/cost_ref-1)<=.05 for _,c in b1+serial+parallel)
    print(name,f"{b1_time:.6f}",b1_time<=limit,f"{n1:.6f}",f"{n128:.6f}",f"{n1/n128:.2f}x",f"~{target}x",
          cost_pass,",".join(f"{t:.6f}" for t,_ in parallel))
print("Endpoint rebaseline only: full B4/B16 curves are not requalified by this check.")
PY
printf 'Logs: %s\nScratch outputs: %s\n' "$out" "$work"
