#!/usr/bin/env python3
"""Prepare, collect, and summarize Q3 in the current interactive allocation."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys

from common import HERE, ROOT, digest, load_campaign, make_jobs, prepare_campaign, require_active_allocation


def allocation_job(env):
    nodes = int(env.get('SLURM_JOB_NUM_NODES', env.get('SLURM_NNODES', '0')))
    slots = env.get('SLURM_TASKS_PER_NODE', env.get('SLURM_NTASKS_PER_NODE', ''))
    counts = []
    for item in slots.split(',') if slots else []:
        match = re.fullmatch(r'(\d+)(?:\(x(\d+)\))?', item.strip())
        if not match:
            raise ValueError('Unrecognized Slurm task layout: ' + slots)
        counts.extend([int(match[1])] * int(match[2] or 1))
    if len(counts) == 1 and nodes > 1:
        counts *= nodes
    if not counts and nodes and int(env.get('SLURM_NTASKS', '0')) % nodes == 0:
        counts = [int(env.get('SLURM_NTASKS', '0')) // nodes] * nodes
    if len(counts) != nodes or len(set(counts)) != 1:
        raise ValueError('Need an allocation with equal task counts per node.')
    per_node = counts[0]
    if nodes == 1 and per_node == 128:
        return 'single'
    if nodes in (1, 2) and per_node in (2, 8, 64):
        return f'layout-n{nodes}-p{per_node}'
    raise ValueError('Use 1 node with 128 tasks for Q3(a)/(c), or 1/2 nodes with 2/8/64 tasks per node for Q3(d).')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=HERE / 'results/interactive-report')
    parser.add_argument('--ghc-dir', type=Path)
    args = parser.parse_args()
    require_active_allocation()
    job_name = allocation_job(os.environ)
    if args.out.exists():
        campaign, manifest = load_campaign(args.out)
        for filename in ('wireroute.cpp', 'wireroute.h', 'validate.cpp', 'Makefile'):
            if digest(ROOT / 'code' / filename) != manifest['hashes']['source/' + filename]:
                raise SystemExit('Source changed since collection began. Use --out with a new directory.')
    else:
        config = dict(inputs=['few', 'medium', 'abundant'], repeats=3,
                      sensitivity_batches=list(range(1, 17)), SA_prob=.1, SA_iters=5)
        campaign, manifest = prepare_campaign(args.out, config, make_jobs('all'))
    print(f'Results: {campaign}\nCurrent allocation collects: {job_name}', flush=True)
    if not (campaign / 'jobs' / job_name / 'complete.json').exists():
        subprocess.run([sys.executable, str(campaign / 'tools/collect.py'), '--campaign', str(campaign),
                        '--job', job_name], check=True)
    else:
        print('This layout is already complete; retaining its measurements.', flush=True)
    command = [sys.executable, str(campaign / 'tools/summarize.py'), str(campaign), '--allow-partial']
    if args.ghc_dir:
        command += ['--ghc-dir', str(args.ghc_dir.resolve())]
    if importlib.util.find_spec('matplotlib'):
        command += ['--plots']
    subprocess.run(command, check=True)
    pending = [j['name'] for j in manifest['jobs'] if not (campaign / 'jobs' / j['name'] / 'complete.json').exists()]
    print('Remaining layouts: ' + (', '.join(pending) or 'none'), flush=True)
    if not args.ghc_dir:
        print('Q3(b) remains pending GHC timing data (--ghc-dir).', flush=True)
    print('CSV summaries saved. Plots are generated automatically when matplotlib is available.', flush=True)


if __name__ == '__main__':
    main()
