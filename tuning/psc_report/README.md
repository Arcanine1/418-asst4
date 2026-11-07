# PSC report data for Q3(a)-Q3(d)

## One command inside an interactive allocation

Copy the entire tuning/psc_report directory and the current code to PSC.
From the repository root inside your allocation, run:

```bash
bash tuning/psc_report/run_interactive.sh
```

The wrapper freezes source, detects your allocated layout, builds, collects,
and writes CSV summaries. It also generates plots when matplotlib is available.
With 1 node / 128 tasks it collects Q3(a)/(c). Run the same command in the
matching 1/2-node, 2/8/64-tasks-per-node allocations to accumulate Q3(d).
It cannot obtain additional nodes or change your allocation. Results persist
in results/interactive-report; completed cases are skipped on subsequent runs.
The wrapper reports uncollected layouts explicitly. For Q3(b), add
--ghc-dir PATH_TO_GHC_RESULTS containing timing_summary.csv and manifest.json.
Use --out NEW_DIRECTORY if you change the source or want a fresh dataset.
Expired allocations stop collection; rerun in a matching fresh allocation.
Resumed runs record their individual Slurm job IDs and may use a different node.

All application runs use only `mpirun -np N` plus the program's normal flags.
No collective, transport, mapping, binding, or other custom MPI runtime
settings are supplied. These are measurement scripts outside the submission.
They do not change the C++ program or build handin.tar. No GHC access is needed.

Copy this whole directory and the current code files to the same paths in
your PSC checkout. Load the assignment's usual compiler/MPI modules before
submitting; the jobs inherit that environment. No module versions are forced.

## Preview and submit

To run only the Q3(c) batch-size study at 128 ranks on one node, submit one
job with `--suite q3c128`. It measures all integer batch sizes 1 through 16
for all three inputs, with three repeats (144 routing runs). The output
`q3c_batches.csv` and its plots contain these 128-rank measurements.

```bash
bash tuning/psc_report/submit.sh --submit --suite q3c128 \
  --account cis260304p --out tuning/psc_report/results/q3c-128
```

After the job completes:

```bash
python3 tuning/psc_report/results/q3c-128/tools/summarize.py \
  tuning/psc_report/results/q3c-128 --plots
```

From the repository root on PSC:

```bash
bash tuning/psc_report/submit.sh --dry-run --account cis260304p
bash tuning/psc_report/submit.sh --submit --account cis260304p
```

Omit --account if your PSC default account is already correct. The first
command creates nothing. The second freezes the source, inputs, and collection
tools, prints the campaign directory, and queues seven Slurm jobs. The default
partition is RM and each job has a one-hour time limit. RM uses whole nodes;
these are real compute allocations, not seven local shell processes.

| Section | Data collected |
| --- | --- |
| 3(a) | Total and computation speedup curves at 1,2,4,8,16,32,64,128 ranks for B=1,4,16, all three inputs |
| 3(b) | Common-rank PSC/GHC comparison after importing the plain-MPI Q2 timing CSV and manifest |
| 3(c) | Total/computation time and cost at 32 ranks for every integer batch size from 1 through 16 |
| 3(d) | One node versus two nodes, using 2,8,64 ranks per node; B16, all three inputs |

There are three repeats of every configuration: 333 application runs in the
single-node job and nine in each of the six layout jobs (387 total). Layout
jobs request precisely their intended nodes/tasks per node. Before measuring,
each job runs `mpirun -np N hostname` and checks the observed host counts.
A two-node test fails if default MPI placement packs ranks onto one node;
it never silently relabels that run or adds placement overrides.

Q3(d)'s comparisons use P total ranks on one node and 2P total ranks on two
nodes. It is not a fixed-total-rank one-versus-two-node comparison.

To collect only one part, add --suite single or --suite multi. To reduce the
batch samples for Q3(c), use --sensitivity-batches 1 4 16. The default samples
all 16 batch sizes. Source changes require a new campaign; all jobs in one
campaign use its frozen source even if you edit the workspace afterward.

## Use an existing single-node interactive allocation

In a fresh allocation with 128 cores and sufficient remaining time:

```bash
bash tuning/psc_report/submit.sh --prepare-only --suite single --out tuning/psc_report/results/interactive
python3 tuning/psc_report/results/interactive/tools/collect.py \
  --campaign tuning/psc_report/results/interactive --job single
```

The output directory must not already exist. This collects 3(a)/3(c) without
submitting a batch job. Use the six matching batch allocations for 3(d);
a generic two-node allocation with 128 tasks per node may otherwise pack a
small MPI run onto one node. The hostname check prevents that mistake.

## Tables and plots

After all planned jobs finish, run the command printed by the submitter, or:

```bash
python3 tuning/psc_report/summarize.py CAMPAIGN_DIRECTORY --plots
```

For 3(b), copy both `timing_summary.csv` and `manifest.json` from the completed
plain-MPI GHC Q2 result directory into a folder available here, then run:

```bash
python3 tuning/psc_report/summarize.py CAMPAIGN_DIRECTORY \
  --ghc-dir PATH_TO_GHC_RESULT_DIRECTORY --plots
```

GHC and PSC source hashes must match, and old custom-launch GHC data are
rejected. The comparison figures use the common rank counts 1,2,4,8;
the full PSC curves through 128 ranks are in the separate 3(a) figures.
If --ghc-dir is omitted, 3(b) is explicitly reported as pending.

Outputs include runs.csv, summary.csv, q3a_speedups.csv,
q3b_ghc_comparison.csv, q3c_batches.csv, q3d_nodes.csv, summary.txt, and
standalone PNG/PDF figures. Every repeat also retains its raw log, command,
validation result, cost, job ID, and timing. Job metadata records actual
host placement, executable hashes, and compiler/MPI versions.

The collector/summarizer use Python's standard library. Plotting requires
matplotlib. You can copy the entire campaign to your own computer and render
there without running MPI or Slurm. Use --allow-partial to inspect an
incomplete campaign; otherwise missing planned runs stop summarization.

## Timing and storage definitions

Total time is launcher-to-exit wall time, including initialization, validation,
output, and MPI teardown. Computation time comes from the program's printed
computation interval. Speedups use the same input/batch one-rank median.
Statistics include medians and min/max across repeats, plus the agreed 5%
cost band. Cache counters are not part of Q3.

All nodes must see the same working path, so output grids use a reused
work/outputs directory on the shared PSC filesystem. Each job retains only
its latest grid pair, not one copy per repeat. One- and two-node PSC jobs use
the same filesystem convention. GHC Q2 totals may include local /tmp I/O;
state this difference when discussing total-time comparisons. Computation
timings exclude output I/O.

## Resume

Once interrupted/failed jobs have exited, submit only unfinished jobs:

```bash
bash tuning/psc_report/submit.sh --submit --resume CAMPAIGN_DIRECTORY --account cis260304p
```

Completed jobs are skipped. Rerun jobs reuse their saved executable and skip
completed individual measurements. Do not queue duplicate copies of a job
while its earlier instance is still running. Monitor the printed Slurm IDs
with the normal squeue command.

Reference documentation:
- https://www.psc.edu/resources/bridges-2/user-guide/
- https://docs.open-mpi.org/en/main/launching-apps/slurm.html

Prepared and checked locally; actual PSC measurements and placement
verification must be performed on PSC. No GHC jobs are launched by these tools.
Local checks exercised all seven allocation plans, 43 mock application runs,
placement rejection, resume behavior, CSV exports, and PNG/PDF rendering.
Mock data are not benchmark measurements.
