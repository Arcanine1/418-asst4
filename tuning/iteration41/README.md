# Final reference audit

Completed on GHC and PSC: all Section 9 tables and reference curves pass with
the agreed +/-5% cost tolerance. Saved summaries are `ghc-summary.txt` and
`psc-summary.txt`. The latter records the user's pasted PSC output through
the curve rows. No further benchmark run is required for qualification.

These scripts pin the pre-cleanup source and launcher hashes for historical
reproducibility. Submission cleanup changed those hashes, so the current
source deliberately fails their version guard. See `../SUBMISSION.md` for
the clean archive and its separate GHC verification.

Run from the repository root:

```bash
bash tuning/iteration41/benchmark_audit.sh ghc
# On PSC, in a known single-node 128-core Slurm allocation:
bash tuning/iteration41/benchmark_audit.sh psc
```

Copy `code/wireroute.cpp`, `tuning/run_psc.sh`, this directory's
`benchmark_audit.sh`, and `summarize.py` together. The runner verifies source
and launcher hashes before compiling. Invoke shell scripts with `bash`;
invoke the summary parser with `python3`.

The runner builds normal and occupancy-checked executables, checks validation,
and measures Table 1 plus the complete B4/B16 curves. GHC also measures all
Table 2 entries. Each serial measurement has three runs; PSC B16 at 128 ranks
has five. Performance uses normal builds; checked builds separately detect
transient occupancy wrap. The parser prints costs against the agreed +/-5%
tolerance. Graph references must be compared with the printed curves.

Every MPI launch uses `tuning/run_psc.sh`. This includes the selected `large`
shared-memory policy for abundant B16 at 128 ranks on one node. No other
transport settings are changed. Logs, hashes, MPI version, executables, and
`summary.txt` are stored in the timestamped output directory printed at start.

To re-summarize existing logs without starting MPI jobs:

```bash
python3 tuning/iteration41/summarize.py tuning/iteration41/psc-JOB-TIMESTAMP psc
```
