#!/usr/bin/env python3
"""Summarize existing iteration 29 logs; does not launch MPI jobs."""
import re
import sys
from pathlib import Path
from statistics import median

if len(sys.argv) > 1:
    directory = Path(sys.argv[1])
else:
    candidates = sorted(Path(__file__).resolve().parent.glob('psc-*'), key=lambda p: p.stat().st_mtime)
    if not candidates:
        raise SystemExit('No results found. Pass the PSC result directory as an argument.')
    directory = candidates[-1]

groups = {}
for path in sorted(directory.glob('*-r*.log')):
    match = re.fullmatch(r'(few|medium)-(baseline|piggyback)-r\d+\.log', path.name)
    if not match:
        continue
    text = path.read_text()
    if 'Validate Passed' not in text:
        raise SystemExit(f'Validation missing: {path}')
    def number(label):
        found = re.search(re.escape(label) + r'\s*([\d.]+)', text)
        if not found:
            raise SystemExit(f'Missing {label}: {path}')
        return float(found.group(1))
    row = {'file': path.name, 'compute': number('Computation time (sec):')}
    for field, label in (
        ('route_max', 'Max route time:'), ('route_avg', 'Average route time:'),
        ('sync_max', 'Max sync time:'), ('split_max', 'Max split time:'),
        ('stall_max', 'Max stall time:'),
    ):
        row[field] = number(label)
    groups.setdefault(match.groups(), []).append(row)

if not groups:
    raise SystemExit(f'No matching iteration 29 run logs in {directory}')
print(f'Logs: {directory}')
print('Medians in seconds; rank maxima are separate measurements and cannot be added.')
print('input variant runs compute route_max route_avg sync_max split_max stall_max')
for (name, variant), rows in sorted(groups.items()):
    fields = ('compute', 'route_max', 'route_avg', 'sync_max', 'split_max', 'stall_max')
    print(name, variant, len(rows), *(f'{median(r[f] for r in rows):.6f}' for f in fields))
    representative = sorted(rows, key=lambda r: r['compute'])[len(rows)//2]
    print('  median-time log:', representative['file'])
