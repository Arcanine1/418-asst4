# Uniform splitting comparison

Copy the edited code/wireroute.cpp, compare.py, and split_compare.py to the
same paths on PSC. In your single-node 128-core allocation, from the repo root:

```bash
python3 tuning/plain_collectives/split_compare.py
```

The script builds frozen source copies for serial_exception (the prior code)
and uniform_split (your edited code). The only difference is whether one rank
is exempted from splitting wires. It uses plain mpirun -np N for all runs.

For each input and variant it measures three B1 serial runs, three B16 serial
runs, and seven B16 128-rank runs, alternating variant order (78 runs total).
Every run must validate; costs and B1 limits are checked in the final summary.

The speedup column uses each variant's own B16 serial median. The
common_baseline_speedup column uses the serial_exception B16 serial median
for both variants, exposing whether parallel execution actually improved.
Splitting may change serial update order and routing cost. Higher measured
speedup alone is not evidence of faster parallel execution.

Source generation was checked locally: the baseline matched the previous Git
version exactly and the candidate matched the edited working file exactly.
MPI compilation and execution require PSC. This checks endpoints only; full
curves still require the report/audit scripts. No handin archive is built.
