# Q2 GHC collection

Copy code/, tuning/ghc_report/, and tuning/psc_report/ to GHC. The collector
uses shared parsing helpers from psc_report/common.py. On an idle eight-core
GHC machine, from the repository root:

```bash
python3 tuning/ghc_report/collect.py --out tuning/ghc_report/results/final
```

Choose a fresh output path. The script freezes source and inputs, builds with
the supplied Makefile, and launches only plain mpirun -np N. Defaults:

- Q2(a): total/computation timings at 1,2,4,8 ranks for B1,B4,B16.
- Q2(b): separate perf measurements at B16 and 1,2,4,8 ranks, with one
  cache-misses counter per MPI rank. Each run exports both the sum and the
  arithmetic mean across ranks. Summary rows are medians across repeats.
- Q2(c): B1,B4,B16 time/cost comparison at eight ranks, available in the
  timing summary (filter ranks=8).

All three inputs receive three repeats: 108 timing runs plus 36 perf runs.
Performance counters cover the whole process, including MPI initialization,
validation and output, not only the routing interval. Total elapsed timing
includes launcher overhead and local temporary-file output. Counter runs are
separate from speedup measurements. The perf preflight fails if counters are
unavailable; --skip-perf permits timing collection but leaves Q2(b) incomplete.

Outputs: manifest.json, timing_runs.csv, timing_summary.csv, cache_rank_runs.csv,
cache_runs.csv, cache_summary.csv, raw logs and frozen source. For Q3(b), copy
timing_summary.csv and manifest.json together to PSC and pass their containing
directory as --ghc-dir to the Q3 summarizer. Both platforms must use the same
source hash. Do not run other benchmarks simultaneously on the GHC machine.

Local mock checks exercised the configuration matrix, counter parsing,
sum/mean aggregation and CSV summaries. Real MPI/perf validation must occur
on GHC. This collector does not build handin.tar or change production code.
