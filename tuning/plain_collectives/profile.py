#!/usr/bin/env python3
"""Profile temporary variants under plain MPI; leave production source intact."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from compare import ROOT, variants, END


def instrument(source):
    source = source.replace('struct RouteProposal {', 'double exchange_seconds = 0.0;\nstruct RouteProposal {', 1)
    marker = '    // Only this rank writes this slot;'
    # Time the whole exchange, including its allocation and local combine work.
    start = source.index('    if (update_weights) {', source.index('void route_with_reduction'))
    start = source.index('\n    }', start) + len('\n    }')
    source = source[:start] + '\n    const double exchange_start = MPI_Wtime();\n' + source[start:]
    source = source.replace(END, '    exchange_seconds += MPI_Wtime() - exchange_start;\n' + END, 1)
    source = source.replace('    double route_time = 0.0;', '    double route_time = 0.0;\n    double partition_seconds = 0.0;', 1)
    source = source.replace('        wire_work_units.clear();', '        const double partition_start = MPI_Wtime();\n        wire_work_units.clear();', 1)
    source = source.replace('        if (run.reduce_mode) {', '        partition_seconds += MPI_Wtime() - partition_start;\n        if (run.reduce_mode) {', 1)
    marker = '    if (run.pid == 0) {\n        std::vector<std::vector<int>> occupancy_for_output'
    report = '''    double local_times[3] = {route_time, exchange_seconds, partition_seconds};
    std::vector<double> rank_times(3 * run.nproc);
    MPI_Gather(local_times, 3, MPI_DOUBLE, rank_times.data(), 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (run.pid == 0) {
        for (int rank = 0; rank < run.nproc; ++rank)
            std::cout << "PROFILE " << rank << " " << rank_times[3*rank] << " "
                      << rank_times[3*rank+1] << " " << rank_times[3*rank+2] << "\\n";
    }

'''
    if source.count(marker) != 1:
        raise ValueError('Output section changed')
    return source.replace(marker, report + marker, 1)


def main():
    if not os.environ.get('SLURM_JOB_ID') or os.environ.get('SLURM_JOB_NUM_NODES', os.environ.get('SLURM_NNODES')) != '1':
        raise SystemExit('Use the single-node PSC allocation with 128 cores.')
    if any(k.startswith(('OMPI_MCA_', 'OPAL_MCA_', 'PMIX_MCA_', 'PRTE_MCA_')) for k in os.environ):
        raise SystemExit('Unset custom MPI runtime settings first.')
    out = Path(tempfile.mkdtemp(prefix='psc-profile-', dir=Path(__file__).resolve().parent))
    probe = out / 'placement.py'
    probe.write_text('import os,json,socket\nprint(json.dumps(dict(rank=os.environ.get("OMPI_COMM_WORLD_RANK"),host=socket.gethostname(),cpus=sorted(os.sched_getaffinity(0)))))\n')
    with (out / 'placement.jsonl').open('w') as log:
        subprocess.run(['timeout', '60', 'mpirun', '-np', '128', 'python3', str(probe)], stdout=log, check=True)
    work = Path(tempfile.mkdtemp(prefix='wr-profile-'))
    (work / 'outputs').mkdir()
    inputs = out / 'inputs'
    inputs.mkdir()
    for name in ('few', 'medium'):
        shutil.copy2(ROOT / 'code/inputs/timeinput' / (name + '_wires.txt'), inputs)
    binaries = {}
    for variant, source in variants((ROOT / 'code/wireroute.cpp').read_text()).items():
        build = out / variant
        build.mkdir()
        for filename in ('Makefile', 'validate.cpp', 'wireroute.h'):
            shutil.copy2(ROOT / 'code' / filename, build / filename)
        (build / 'wireroute.cpp').write_text(instrument(source))
        with (build / 'build.log').open('w') as log:
            subprocess.run(['make', '-C', str(build)], stdout=log, stderr=subprocess.STDOUT, check=True)
        binaries[variant] = build / 'wireroute'
    print('rank columns: rank route_seconds exchange_seconds partition_seconds', flush=True)
    for name in ('few', 'medium'):
        for repeat in range(3):
            names = list(binaries)
            for variant in names[repeat:] + names[:repeat]:
                log = out / f'{name}-{variant}-r{repeat+1}.log'
                with log.open('w') as target:
                    subprocess.run(['timeout', '180', 'mpirun', '-np', '128', str(binaries[variant]),
                                    '-f', str(inputs / (name + '_wires.txt')), '-b', '16', '-i', '5', '-p', '0.1'],
                                   cwd=work, stdout=target, stderr=subprocess.STDOUT, check=True)
                lines = log.read_text().splitlines()
                if not any('Validate Passed' in line for line in lines):
                    raise RuntimeError('Validation failed: ' + str(log))
                rows = [[float(x) for x in line.split()[1:]] for line in lines if line.startswith('PROFILE ')]
                assert len(rows) == 128
                print(log.name, next(line for line in lines if line.startswith('Computation time')), flush=True)
                for col, label in enumerate(('route', 'exchange', 'partition'), 1):
                    print(f'  {label}: max={max(r[col] for r in rows):.6f} avg={sum(r[col] for r in rows)/128:.6f}', flush=True)
                print('  slow routing ranks:', sorted(rows, key=lambda r: r[1], reverse=True)[:5], flush=True)
    print('Full logs and placement:', out)
    print('Rank maxima are separate measurements; do not add them.')


if __name__ == '__main__':
    main()
