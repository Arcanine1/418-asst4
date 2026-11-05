#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration11/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration11/wireroute.profile.cpp code/validate.cpp -o "$out/wireroute.profile"
cd "$out"
for input in few medium abundant; do
    timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        -np 1 ./wireroute.profile \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$input-n1.log" 2>&1
    grep -q 'Validate Passed' "$input-n1.log"
    log="$input.log"
    timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
        -x WR_ITER_TIMINGS=1 -np 128 ./wireroute.profile \
        -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1
    grep -q 'Validate Passed' "$log"
    printf '%s\n' "$input n1"
    grep -E 'Computation time|Total cost|Validate' "$input-n1.log"
    printf '%s\n' "$input n128"
    grep -E 'Computation time|Total cost|Validate' "$log"
    python3 - "$log" <<'PY'
import re, statistics, sys
rows={}
for line in open(sys.argv[1]):
    if not line.startswith('ITIMING '):
        continue
    fields=dict(re.findall(r'(\w+)=([\d.]+)', line))
    rows.setdefault(int(fields['iteration']), []).append(fields)
for iteration, data in sorted(rows.items()):
    assert len(data)==128, (iteration, len(data))
    route=sorted(data, key=lambda x: float(x['route']), reverse=True)
    sync=sorted(data, key=lambda x: float(x['sync']), reverse=True)
    avg_route=statistics.mean(float(x['route']) for x in data)
    avg_work=statistics.mean(float(x['work']) for x in data)
    print(f"iter={iteration} route_max={float(route[0]['route']):.6f} route_avg={avg_route:.6f} "
          f"sync_max={float(sync[0]['sync']):.6f} "
          f"work_max/avg={max(float(x['work']) for x in data)/avg_work:.2f}")
    print('  slow-route ranks:', ', '.join(f"{x['rank']}:{x['route']}s/work={x['work']}" for x in route[:3]))
    print('  slow-sync ranks:', ', '.join(f"{x['rank']}:{x['sync']}s" for x in sync[:3]))
PY
done | tee summary.txt
python3 - <<'PY' | tee -a summary.txt
import re
from pathlib import Path
for name in ('few', 'medium', 'abundant'):
    times=[]
    for suffix in ('n1', ''):
        log=Path(f'{name}-{suffix}.log' if suffix else f'{name}.log').read_text()
        times.append(float(re.search(r'Computation time \(sec\): ([\d.]+)', log).group(1)))
    print(f'{name} speedup={times[0]/times[1]:.2f}x (n1={times[0]:.6f}s, n128={times[1]:.6f}s)')
PY
printf 'Full logs: %s\n' "$out"
