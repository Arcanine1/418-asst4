# Plain-MPI proposal exchange experiment

Copy this directory and the current code directory to PSC. In the usual
single-node, 128-core interactive allocation, run from the repository root:

```bash
bash tuning/plain_collectives/psc_compare.sh
```

The script freezes source and inputs, builds three separate executables using
the supplied Makefile, and runs few and medium at B16, p=0.1, five iterations.
It measures three serial repetitions and seven 128-rank repetitions for each
variant, rotating variant order. Every run must validate. Costs are checked
against the agreed 5% band. Raw logs, generated sources, commands, and medians
remain in the printed psc-* directory.

Variants:

- baseline: current group Reduce / leader Allreduce / group Bcast.
- allreduce: a single ordinary MPI_Allreduce across all ranks.
- doubling: explicit pair exchanges and lexicographic minimum combination,
  with seven rounds at 128 ranks; non-power-of-two counts use MPI_Allreduce.

All routing launches use only mpirun -np N and program arguments. There are
no runtime tuning settings or affinity changes. Production C++ and handin.tar
are not edited. Abundant uses another routing path and is not changed here.

The minimum operation preserves cost/index tie breaking, random moves with
cost -1, and the per-rank routing rates appended to proposals. Adaptive work
assignments can still differ because measured routing rates change.

Local checks covered source generation, shell/Python syntax, and simulated
minimum exchanges at powers of two through 128 ranks. Actual MPI compilation,
validation, and timing require PSC; no speedup improvement is assumed yet.

Judge changes by parallel time as well as each variant's measured speedup.
The last plain-MPI baselines imply approximate 128-rank targets of 0.02790 s
for few (76x), 0.08065 s for medium (100x), and 0.24138 s for abundant (89x).
Those targets must be recalculated when the serial baseline changes.

## Work estimate correction

The current nonrandom work estimate multiplies candidate count by horizontal
length only, leaving vertical length outside the multiplication. To compare
the original with candidates times the full weighted length, run:

```bash
python3 tuning/plain_collectives/work_compare.py
```

This uses the same measurement protocol for baseline, work_fixed, and
work_fixed_allreduce. Only the latter also replaces the grouped exchange with
one MPI_Allreduce. Production source remains unchanged until results support
promotion. Neither variant moves work across timing boundaries.

## Vertical-work calibration after the correction

```bash
python3 tuning/plain_collectives/weight_compare.py
```

Requires the corrected workload formula in production source. Compares the
current vertical-work factor 4 with factors 2 and 8 on few, medium, and abundant.
Each variant receives three serial and seven parallel repetitions, rotating
variant order. All run at B16 and 128 ranks for the parallel measurements.
Only the estimated work used for splitting/assignment changes; route scoring
and communication algorithms stay the same. Scheduling may change routing
results, so every run validates and the summary checks costs within 5%.
The production source is not edited. This is a calibration experiment, not
a claim that either alternative is faster. Use a full B1/B4/B16 audit before
accepting a winning variant for submission.

Weight 2 improved all three measured parallel medians and was promoted to
production. With that source, refine the estimate with factors 1, 2, and 3:

```bash
python3 tuning/plain_collectives/weight_fine.py
```

Copy the current code/wireroute.cpp and this directory first. The earlier
weight_compare.py intentionally requires a factor-4 source; weight_fine.py
requires factor 2. Both fail instead of silently testing mislabeled variants.
