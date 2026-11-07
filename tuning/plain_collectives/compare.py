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
    source = (ROOT / 'code/wireroute.cpp').read_text()
    (out / 'source.sha256').write_text(hashlib.sha256(source.encode()).hexdigest() + '\n')
    binaries = {}
    for name, text in variants(source).items():
        build = out / name
        build.mkdir()
        for filename in ('Makefile', 'wireroute.h', 'validate.cpp'):
            shutil.copy2(ROOT / 'code' / filename, build / filename)
        (build / 'wireroute.cpp').write_text(text)
        with (build / 'build.log').open('w') as log:
            subprocess.run(['make', '-C', str(build), 'wireroute'], stdout=log, stderr=subprocess.STDOUT, check=True)
        binaries[name] = build / 'wireroute'
    inputs = out / 'inputs'
    inputs.mkdir()
    for name in INPUT_REFERENCES:
        shutil.copy2(ROOT / 'code/inputs/timeinput' / (name + '_wires.txt'), inputs)
    work = Path(tempfile.mkdtemp(prefix='wr-collectives-'))
    (work / 'outputs').mkdir()
    records = []
    print('Logs:', out, flush=True)
    subprocess.run(['mpirun', '--version'], stdout=(out / 'mpi-version.txt').open('w'), check=True)
    for name in INPUT_REFERENCES:
        for ranks, repeats in ((1, 3), (128, 7)):
            for repeat in range(repeats):
                names = list(binaries)
                names = names[repeat % 3:] + names[:repeat % 3]
                for variant in names:
                    log = out / f'{name}-{variant}-n{ranks}-r{repeat + 1}.log'
                    command = ['mpirun', '-np', str(ranks), str(binaries[variant]), '-f',
                               str(inputs / (name + '_wires.txt')), '-b', '16', '-i', '5', '-p', '0.1']
                    with log.open('w') as target:
                        subprocess.run(['timeout', '--kill-after=5s', '180', *command], cwd=work,
                                       stdout=target, stderr=subprocess.STDOUT, check=True)
                    text = log.read_text()
                    if 'Validate Passed' not in text:
                        raise RuntimeError('Validation failed: ' + str(log))
                    seconds = float(re.search(r'Computation time \(sec\):\s*([\d.]+)', text)[1])
                    cost = int(re.search(r'Total cost:\s*(\d+)', text)[1])
                    records.append(dict(input=name, variant=variant, ranks=ranks, repeat=repeat+1,
                                        seconds=seconds, cost=cost, command=command))
                    (out / 'runs.json').write_text(json.dumps(records, indent=2))
                    print(log.name, seconds, cost, 'VALID', flush=True)
    lines = ['input variant n1_median n128_median speedup cost_pass parallel_runs']
    for name, ref in INPUT_REFERENCES.items():
        for variant in binaries:
            rows = [r for r in records if r['input'] == name and r['variant'] == variant]
            serial = statistics.median(r['seconds'] for r in rows if r['ranks'] == 1)
            parallel = [r['seconds'] for r in rows if r['ranks'] == 128]
            median = statistics.median(parallel)
            passed = all(abs(r['cost']/ref - 1) <= .05 for r in rows)
            lines.append(f'{name} {variant} {serial:.6f} {median:.6f} {serial/median:.2f}x {passed} ' +
                         ','.join(f'{v:.6f}' for v in parallel))
    summary = '\n'.join(lines)
    (out / 'summary.txt').write_text(summary + '\n')
    print(summary)
    print('Full logs:', out)


if __name__ == '__main__':
    main()
