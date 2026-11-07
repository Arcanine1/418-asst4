#!/usr/bin/env python3
"""Collect Q2 timing and per-rank cache misses with plain MPI on GHC."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
from statistics import median
import subprocess
import sys
import tempfile
from datetime import datetime

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tuning/psc_report'))
from common import plain_environment, read_run, run_logged


def write_csv(path, rows):
    if not rows:
        return
    with path.open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--skip-perf', action='store_true', help='Timing only; Q2(b) remains incomplete.')
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('repeats must be positive')
    env = plain_environment()
    for command in ('make', 'mpic++', 'mpirun', 'timeout') + (() if args.skip_perf else ('perf',)):
        if not shutil.which(command):
            raise SystemExit('Missing command: ' + command)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'source'
    source.mkdir()
    for name in ('wireroute.cpp', 'wireroute.h', 'validate.cpp', 'Makefile'):
        shutil.copy2(ROOT / 'code' / name, source / name)
    (source / 'inputs').mkdir()
    for name in ('few', 'medium', 'abundant'):
        shutil.copy2(ROOT / 'code/inputs/timeinput' / (name + '_wires.txt'), source / 'inputs')
    hashes = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in source.rglob('*') if p.is_file()}
    work = Path(tempfile.mkdtemp(prefix='wr-q2-'))
    (work / 'outputs').mkdir()
    manifest = dict(launch_policy='plain-mpirun', mpi_flags=[], source_hashes=hashes,
                    repeats=args.repeats, created=datetime.now().isoformat(),
                    total_time_definition='Launcher-to-exit wall time including validation and output.',
                    output_filesystem=str(work), perf_scope='Whole rank process including MPI initialization and output; B16')
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print('Results:', out, flush=True)
    run_logged(['make', '-C', str(source), 'wireroute'], work, out / 'build.log', 300, env)
    # One perf instance per MPI rank, with an independent counter output file.
    wrapper = out / 'perf_rank.sh'
    wrapper.write_text('#!/usr/bin/env bash\nset -euo pipefail\n'
                       'rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-}}\n'
                       ': "${rank:?MPI rank environment missing}"\n'
                       'directory=$1\nshift\n'
                       'exec perf stat -x, -e cache-misses -o "$directory/rank-$rank.csv" -- "$@"\n')
    wrapper.chmod(0o755)
    if not args.skip_perf:
        run_logged(['perf', 'stat', '-e', 'cache-misses', '--', 'true'], work,
                   out / 'perf-preflight.log', 30, env)
    cases = [(name, batch, ranks) for name in ('few', 'medium', 'abundant')
             for batch in (1, 4, 16) for ranks in (1, 2, 4, 8)]
    timing, perf_rows, rank_rows = [], [], []
    for repeat in range(1, args.repeats + 1):
        for name, batch, ranks in cases if repeat % 2 else reversed(cases):
            stem = f'{name}-b{batch}-n{ranks}-r{repeat}'
            command = [str(source / 'wireroute'), '-f', str(source / 'inputs' / (name + '_wires.txt')),
                       '-b', str(batch), '-i', '5', '-p', '0.1']
            print('Timing', stem, flush=True)
            log = out / (stem + '.log')
            total = run_logged(['mpirun', '-np', str(ranks), *command], work, log, 600, env)
            row = dict(input=name, batch=batch, ranks=ranks, repeat=repeat, total_sec=total,
                       **read_run(log, ranks))
            timing.append(row)
            write_csv(out / 'timing_runs.csv', timing)
            if batch == 16 and not args.skip_perf:
                directory = out / (stem + '-perf')
                directory.mkdir()
                print('Cache misses', stem, flush=True)
                perf_log = directory / 'program.log'
                run_logged(['mpirun', '-np', str(ranks), str(wrapper), str(directory), *command],
                           work, perf_log, 600, env)
                read_run(perf_log, ranks)
                counts = []
                for rank in range(ranks):
                    with (directory / f'rank-{rank}.csv').open() as data:
                        entries = [r for r in csv.reader(data) if len(r) >= 3 and r[2].strip() == 'cache-misses']
                    if len(entries) != 1 or not entries[0][0].strip().isdigit():
                        raise RuntimeError(f'Counter unavailable for {stem}, rank {rank}; inspect perf output.')
                    count = int(entries[0][0].strip())
                    counts.append(count)
                    rank_rows.append(dict(input=name, batch=batch, ranks=ranks, repeat=repeat, rank=rank, cache_misses=count))
                perf_rows.append(dict(input=name, batch=batch, ranks=ranks, repeat=repeat,
                                      total_cache_misses=sum(counts), mean_cache_misses=sum(counts)/ranks))
                write_csv(out / 'cache_rank_runs.csv', rank_rows)
                write_csv(out / 'cache_runs.csv', perf_rows)
    summary = []
    for name, batch, ranks in cases:
        values = [r for r in timing if (r['input'], r['batch'], r['ranks']) == (name, batch, ranks)]
        base = [r for r in timing if (r['input'], r['batch'], r['ranks']) == (name, batch, 1)]
        compute = median(r['computation_sec'] for r in values)
        total = median(r['total_sec'] for r in values)
        summary.append(dict(input=name, batch=batch, ranks=ranks, runs=len(values),
                            computation_sec_median=compute, total_sec_median=total,
                            computation_speedup=median(r['computation_sec'] for r in base)/compute,
                            total_speedup=median(r['total_sec'] for r in base)/total,
                            cost_median=median(r['cost'] for r in values),
                            cost_min=min(r['cost'] for r in values), cost_max=max(r['cost'] for r in values)))
    write_csv(out / 'timing_summary.csv', summary)
    cache_summary = []
    for name in ('few', 'medium', 'abundant'):
        for ranks in (1, 2, 4, 8):
            values = [r for r in perf_rows if r['input'] == name and r['ranks'] == ranks]
            if values:
                cache_summary.append(dict(input=name, batch=16, ranks=ranks, runs=len(values),
                    total_cache_misses_median=median(r['total_cache_misses'] for r in values),
                    mean_cache_misses_median=median(r['mean_cache_misses'] for r in values)))
    write_csv(out / 'cache_summary.csv', cache_summary)
    print('Completed:', out, flush=True)


if __name__ == '__main__':
    main()
