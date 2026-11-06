# Final reference audit

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
