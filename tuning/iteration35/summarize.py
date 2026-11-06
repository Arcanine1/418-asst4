#!/usr/bin/env python3
"""Audit the final executable against the handout's reference section."""
import re
import sys
from pathlib import Path
from statistics import median

directory=Path(sys.argv[1])
platform=sys.argv[2]
if platform not in ('ghc','psc'):
    raise SystemExit('Platform must be ghc or psc')
rows={}
for path in directory.glob('*.log'):
    match=re.fullmatch(r'(few|medium|abundant)-b(1|4|16)-n(\d+)-r\d+\.log',path.name)
    if not match:
        continue
    text=path.read_text()
    if 'Validate Passed' not in text:
        raise SystemExit(f'Validation missing: {path}')
    time=float(re.search(r'Computation time \(sec\): ([\d.]+)',text).group(1))
    cost=int(re.search(r'Total cost: (\d+)',text).group(1))
    name,batch,n=match.groups()
    rows.setdefault((name,int(batch),int(n)),[]).append((time,cost))

def results(name,batch,n):
    key=(name,batch,n)
    if key not in rows:
        raise SystemExit(f'Missing measurements: {key}')
    values=rows[key]
    return median(t for t,_ in values),median(c for _,c in values),values

serial_ref={'ghc':{'few':2.543,'medium':8.485,'abundant':20.709},
            'psc':{'few':3.400,'medium':11.134,'abundant':26.779}}
cost_ref={'few':29240,'medium':267589,'abundant':1539198}
print(f'Platform: {platform}; cost tolerance: +/-5%; source: {(directory/"source.sha256").read_text().strip()}')
print('TABLE 1: input B1_n1_median reference_sec time_pass cost_min cost_max cost_pass')
for name in ('few','medium','abundant'):
    time,cost,values=results(name,1,1)
    limit=serial_ref[platform][name]
    costs=[c for _,c in values]
    print(name,f'{time:.6f}',f'{limit:.3f}',time<=limit,min(costs),max(costs),
          all(abs(c/cost_ref[name]-1)<=0.05 for c in costs))

if platform=='ghc':
    table={1:(267179,267261,267517,268317),4:(267641,267547,268053,267959),
           16:(267587,267555,267639,268051)}
    print('TABLE 2: batch ranks cost_median reference min_delta_pct max_delta_pct all_within_5pct')
    for batch in (1,4,16):
        for n,ref in zip((1,2,4,8),table[batch]):
            _,cost,values=results('medium',batch,n)
            deltas=[100*(c/ref-1) for _,c in values]
            print(batch,n,f'{cost:.0f}',ref,f'{min(deltas):+.3f}',f'{max(deltas):+.3f}',
                  all(abs(d)<=5 for d in deltas))

rank_counts=(1,2,4,8) if platform=='ghc' else (1,2,4,8,16,32,64,128)
print('FIGURES 2/3: batch input ranks computation_median speedup cost_min cost_max within_Table1_cost_5pct')
for batch in (4,16):
    for name in ('few','medium','abundant'):
        serial,_,_=results(name,batch,1)
        for n in rank_counts:
            time,cost,values=results(name,batch,n)
            costs=[c for _,c in values]
            print(batch,name,n,f'{time:.6f}',f'{serial/time:.2f}x',min(costs),max(costs),
                  all(abs(c/cost_ref[name]-1)<=0.05 for c in costs))
print('Graph references are plotted curves, not exact numerical pass/fail tables; compare the printed full curves.')
