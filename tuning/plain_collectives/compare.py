#!/usr/bin/env python3
"""Compare proposal exchanges with plain MPI and frozen source copies."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INPUT_REFERENCES = {'few': 29240, 'medium': 267589}
RUN_CONFIGS = ((16, 1, 3), (16, 128, 7))
B1_LIMITS = {'few': 3.400, 'medium': 11.134, 'abundant': 26.779}
START = '    // Find the winning route for each wire and distribute all ranks\' rates.'
END = '    // Every rank applies the same winners in the same order.'
ALLREDUCE = '''    MPI_Allreduce(MPI_IN_PLACE, proposals.data(), static_cast<int>(proposals.size()),
                  MPI_2INT, MPI_MINLOC, MPI_COMM_WORLD);

'''
DOUBLING = '''    // Each round combines disjoint rank sets, doubling their size.
    // Fall back to MPI for non-power-of-two communicator sizes.
    if ((run.nproc & (run.nproc - 1)) != 0) {
        MPI_Allreduce(MPI_IN_PLACE, proposals.data(), static_cast<int>(proposals.size()),
                      MPI_2INT, MPI_MINLOC, MPI_COMM_WORLD);
    } else {
        std::vector<RouteProposal> incoming(proposals.size());
        for (int distance = 1; distance < run.nproc; distance *= 2) {
            const int partner = run.pid ^ distance;
            MPI_Sendrecv(proposals.data(), static_cast<int>(proposals.size()), MPI_2INT, partner, 271,
                         incoming.data(), static_cast<int>(incoming.size()), MPI_2INT, partner, 271,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (size_t i = 0; i < proposals.size(); ++i) {
                if (incoming[i].cost < proposals[i].cost ||
                    (incoming[i].cost == proposals[i].cost && incoming[i].index < proposals[i].index)) {
                    proposals[i] = incoming[i];
                }
            }
        }
    }

'''


def variants(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError('Source changed: inspect the reduction block before generating experiments.')
    left, rest = source.split(START)
    _, right = rest.split(END)
    return {'baseline': source, 'allreduce': left + ALLREDUCE + END + right,
            'doubling': left + DOUBLING + END + right}


def main():
    if not os.environ.get('SLURM_JOB_ID') or os.environ.get(
            'SLURM_JOB_NUM_NODES', os.environ.get('SLURM_NNODES')) != '1':
        raise SystemExit('Run in a single-node PSC allocation with 128 cores.')
    overrides = [k for k in os.environ if k.startswith(('OMPI_MCA_', 'OPAL_MCA_', 'PMIX_MCA_', 'PRTE_MCA_'))]
    if overrides:
        raise SystemExit('Unset custom runtime settings: ' + ', '.join(overrides))
    out = Path(tempfile.mkdtemp(prefix='psc-' + os.environ['SLURM_JOB_ID'] + '-', dir=Path(__file__).parent))
    print('Logs:', out, flush=True)
    source = (ROOT / 'code/wireroute.cpp').read_text()
    (out / 'source.sha256').write_text(hashlib.sha256(source.encode()).hexdigest() + '\n')
    binaries = {}
    for name, text in variants(source).items():
        build = out / name
        build.mkdir()
        for filename in ('Makefile', 'wireroute.h', 'validate.cpp'):
            shutil.copy2(ROOT / 'code' / filename, build / filename)
        (build / 'wireroute.cpp').write_text(text)
        print(f'Building {name}; log: {build / "build.log"} (180-second timeout)', flush=True)
        with (build / 'build.log').open('w') as log:
            subprocess.run(['timeout', '--kill-after=5s', '180', 'make', '-C', str(build), 'wireroute'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        print(f'Built {name}', flush=True)
        binaries[name] = build / 'wireroute'
    inputs = out / 'inputs'
    inputs.mkdir()
    for name in INPUT_REFERENCES:
        shutil.copy2(ROOT / 'code/inputs/timeinput' / (name + '_wires.txt'), inputs)
    work = Path(tempfile.mkdtemp(prefix='wr-collectives-'))
    (work / 'outputs').mkdir()
    records = []
    print('Recording MPI version (30-second timeout)', flush=True)
    with (out / 'mpi-version.txt').open('w') as version_log:
        subprocess.run(['timeout', '--kill-after=5s', '30', 'mpirun', '--version'],
                       stdout=version_log, stderr=subprocess.STDOUT, check=True)
    for name in INPUT_REFERENCES:
        for batch, ranks, repeats in RUN_CONFIGS:
            for repeat in range(repeats):
                names = list(binaries)
                offset = repeat % len(names)
                names = names[offset:] + names[:offset]
                for variant in names:
                    log = out / f'{name}-{variant}-b{batch}-n{ranks}-r{repeat + 1}.log'
                    command = ['mpirun', '-np', str(ranks), str(binaries[variant]), '-f',
                               str(inputs / (name + '_wires.txt')), '-b', str(batch), '-i', '5', '-p', '0.1']
                    print(f'Running {log.name} (180-second timeout)', flush=True)
                    with log.open('w') as target:
                        subprocess.run(['timeout', '--kill-after=5s', '180', *command], cwd=work,
                                       stdout=target, stderr=subprocess.STDOUT, check=True)
                    text = log.read_text()
                    if 'Validate Passed' not in text:
                        raise RuntimeError('Validation failed: ' + str(log))
                    seconds = float(re.search(r'Computation time \(sec\):\s*([\d.]+)', text)[1])
                    cost = int(re.search(r'Total cost:\s*(\d+)', text)[1])
                    records.append(dict(input=name, variant=variant, batch=batch, ranks=ranks, repeat=repeat+1,
                                        seconds=seconds, cost=cost, command=command))
                    (out / 'runs.json').write_text(json.dumps(records, indent=2))
                    print(log.name, seconds, cost, 'VALID', flush=True)
    lines = ['input variant B1_n1 B1_limit_pass B16_n1 n128_median speedup common_baseline_speedup cost_pass parallel_runs']
    for name, ref in INPUT_REFERENCES.items():
        baseline = statistics.median(r['seconds'] for r in records if r['input'] == name and
                                     r['variant'] == next(iter(binaries)) and r['batch'] == 16 and r['ranks'] == 1)
        for variant in binaries:
            rows = [r for r in records if r['input'] == name and r['variant'] == variant]
            b1 = [r['seconds'] for r in rows if r['batch'] == 1 and r['ranks'] == 1]
            b1_time = f'{statistics.median(b1):.6f}' if b1 else 'NA'
            b1_pass = statistics.median(b1) <= B1_LIMITS[name] if b1 else 'NA'
            serial = statistics.median(r['seconds'] for r in rows if r['ranks'] == 1 and r['batch'] == 16)
            parallel = [r['seconds'] for r in rows if r['ranks'] == 128 and r['batch'] == 16]
            median = statistics.median(parallel)
            passed = all(abs(r['cost']/ref - 1) <= .05 for r in rows)
            lines.append(f'{name} {variant} {b1_time} {b1_pass} {serial:.6f} {median:.6f} '
                         f'{serial/median:.2f}x {baseline/median:.2f}x {passed} ' +
                         ','.join(f'{v:.6f}' for v in parallel))
    summary = '\n'.join(lines)
    (out / 'summary.txt').write_text(summary + '\n')
    print(summary)
    print('Full logs:', out)


if __name__ == '__main__':
    main()
