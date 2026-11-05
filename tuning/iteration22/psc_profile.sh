#!/usr/bin/env bash
set -euo pipefail
: "${SLURM_JOB_ID:?Run inside a PSC interactive compute allocation}"
cd "$(dirname "$0")/../.."
root=$PWD
out="$root/tuning/iteration22/psc-$SLURM_JOB_ID-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out/outputs"
mpic++ -Wall -O3 -std=c++17 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
    tuning/iteration22/wireroute.profile.cpp code/validate.cpp -o "$out/wireroute.profile"
cd "$out"
for input in few medium; do
    for repeat in 1 2 3; do
        log="$input-r$repeat.log"
        printf 'Starting %s n128 profile repeat %s\n' "$input" "$repeat"
        timeout 180 mpirun --nooversubscribe --bind-to core --map-by core \
            -x WR_ITER_TIMINGS=1 -np 128 ./wireroute.profile \
            -f "$root/code/inputs/timeinput/${input}_wires.txt" -b 16 -i 5 -p 0.1 > "$log" 2>&1
        grep -q 'Validate Passed' "$log"
        printf 'Finished %s repeat %s\n' "$input" "$repeat"
    done
done
python3 - <<'PY' | tee summary.txt
import re
from pathlib import Path
from statistics import median, mean

for name in ('few','medium'):
    runs=[]
    for repeat in (1,2,3):
        log=Path(f'{name}-r{repeat}.log').read_text()
        time=float(re.search(r'Computation time \(sec\): ([\d.]+)',log).group(1))
        cost=int(re.search(r'Total cost: (\d+)',log).group(1))
        rows={}
        for line in log.splitlines():
            if line.startswith('ITIMING '):
                fields=dict(re.findall(r'(\w+)=([\d.]+)',line))
                rows.setdefault(int(fields['iteration']),[]).append(fields)
        assert all(len(rows[i])==128 for i in range(5)),(name,repeat)
        runs.append((time,cost,rows))
    print(f'{name} runs_sec='+','.join(f'{x[0]:.6f}' for x in runs),
          f'median_sec={median(x[0] for x in runs):.6f}',
          'costs='+','.join(str(x[1]) for x in runs))
    for iteration in range(5):
        def peak(field,run):
            return max(float(row[field]) for row in run[2][iteration])
        def avg(field,run):
            return mean(float(row[field]) for row in run[2][iteration])
        print(f'  iter={iteration}',
              f'route_max_med={median(peak("route",r) for r in runs):.6f}',
              f'route_avg_med={median(avg("route",r) for r in runs):.6f}',
              f'sync_max_med={median(peak("sync",r) for r in runs):.6f}',
              f'stall_max_med={median(peak("stall",r) for r in runs):.6f}',
              f'adapt_max_med={median(peak("adapt",r) for r in runs):.6f}')
PY
printf 'Full logs: %s\n' "$out"
