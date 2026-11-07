#!/usr/bin/env bash
set -euo pipefail

if [[ -z ${SLURM_JOB_ID:-} ]]; then
    printf >&2 'Run this script inside a PSC Slurm allocation.\n'
    exit 2
fi
nodes=${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-}}
if [[ $nodes != 2 ]]; then
    printf >&2 'This measurement requires exactly two allocated nodes.\n'
    exit 2
fi

module load gcc/13.3.1-p20240614 openmpi/5.0.8-gcc13.3.1
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/tuning/psc_report/remeasure-abundant-2n2p-$SLURM_JOB_ID"}
mkdir -p "$out/source/inputs" "$out/work/outputs"
for file in wireroute.cpp wireroute.h validate.cpp Makefile; do
    cp "$root/code/$file" "$out/source/$file"
done
cp "$root/code/inputs/timeinput/abundant_wires.txt" "$out/source/inputs/"
sha256sum "$out/source/wireroute.cpp" | tee "$out/source.sha256"
make -C "$out/source" wireroute > "$out/build.log" 2>&1

cd "$out/work"
mpirun -np 4 python3 -c 'import os,socket; print(os.environ.get("OMPI_COMM_WORLD_RANK"), socket.gethostname())' \
    > "$out/placement.log"
python3 - "$out/placement.log" <<'PY'
import collections, sys
hosts = collections.Counter(line.split()[-1] for line in open(sys.argv[1]) if line.split())
if len(hosts) != 2 or sorted(hosts.values()) != [2, 2]:
    raise SystemExit("Expected exactly 2 MPI ranks on each of 2 nodes; observed " + repr(dict(hosts)))
print("Verified placement:", dict(hosts))
PY

python3 - "$out" <<'PY'
import json, pathlib, re, statistics, subprocess, sys, time
out = pathlib.Path(sys.argv[1])
samples = []
for repeat in range(1, 4):
    log = out / f"abundant-b16-n4-r{repeat}.log"
    command = ["mpirun", "-np", "4", str(out / "source/wireroute"), "-f",
               str(out / "source/inputs/abundant_wires.txt"), "-b", "16", "-i", "5", "-p", "0.1"]
    print(f"Run {repeat}/3: {log}", flush=True)
    start = time.perf_counter()
    with log.open("w") as stream:
        subprocess.run(["timeout", "600", *command], cwd=out / "work", stdout=stream,
                       stderr=subprocess.STDOUT, check=True)
    total = time.perf_counter() - start
    text = log.read_text()
    if "Validate Passed" not in text:
        raise SystemExit(f"Validation failed; inspect {log}")
    computation = float(re.search(r"Computation time \(sec\):\s*([\d.]+)", text)[1])
    cost = int(re.search(r"Total cost:\s*(\d+)", text)[1])
    samples.append(dict(run=repeat, total_sec=total, computation_sec=computation, cost=cost))
    (out / "runs.json").write_text(json.dumps(samples, indent=2) + "\n")
    print(f"  total={total:.6f}s computation={computation:.6f}s cost={cost} VALID", flush=True)
print("MEDIAN total_sec={:.6f} computation_sec={:.6f}".format(
    statistics.median(s["total_sec"] for s in samples),
    statistics.median(s["computation_sec"] for s in samples)))
print("Logs:", out)
PY
