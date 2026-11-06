#!/usr/bin/env python3
"""Read 128-rank timing zones from the existing final audit; launch no jobs."""
import re
import sys
from pathlib import Path
from statistics import median

if len(sys.argv)>1:
    directory=Path(sys.argv[1])
else:
    base=Path(__file__).resolve().parent.parent/'iteration35'
    candidates=sorted(base.glob('psc-*'),key=lambda p:p.stat().st_mtime)
    if not candidates:
        raise SystemExit('No PSC audit found. Pass its results directory as an argument.')
    directory=candidates[-1]

print('Logs:',directory)
print('Medians in seconds; independent rank maxima cannot be added.')
print('batch input runs compute route_max route_avg sync_max send_max receive_max split_max stall_max')
for batch in (4,16):
    for name in ('few','medium','abundant'):
        rows=[]
        for path in sorted(directory.glob(f'{name}-b{batch}-n128-r*.log')):
            text=path.read_text()
            if 'Validate Passed' not in text:
                raise SystemExit(f'Validation missing: {path}')
            labels=('Computation time (sec):','Max route time:','Average route time:',
                    'Max sync time:','Max send time:','Max receive time:',
                    'Max split time:','Max stall time:')
            values=[]
            for label in labels:
                found=re.search(re.escape(label)+r'\s*([\d.]+)',text)
                if not found:
                    raise SystemExit(f'Missing {label}: {path}')
                values.append(float(found.group(1)))
            rows.append(values)
        if not rows:
            raise SystemExit(f'Missing 128-rank logs for {name} B{batch}')
        print(batch,name,len(rows),*(f'{median(row[i] for row in rows):.6f}' for i in range(len(rows[0]))))
