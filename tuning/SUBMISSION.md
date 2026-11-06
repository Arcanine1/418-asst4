# Clean submission

Submit `handin.tar` from the repository root. The same archive is on GHC at
`~/private/15418/asst4/handin.tar`.

The archive contains exactly:

- `code/Makefile`
- `code/README.md`
- `code/run_psc.sh`
- `code/wireroute.h`
- `code/validate.cpp`
- `code/wireroute.cpp`

To regenerate it, run `make handin.tar` at the repository root. The packaging
rule excludes macOS resource-fork metadata.

Source SHA-256:
`b60524c1737aa8f9ede5e0107e8b57cf421917b519b2ec1d989941d770c8cef5`

Launcher SHA-256:
`f0a9ceaafede74233d835f92a09d70c018e7e911d4b59b579f13f07ba8692033`

Archive SHA-256 at delivery:
`dda400b79d1f88ac308f9735eb143d7ab8d4107f31808a0cb350e7a077bbfd07`

The source has no experimental build macros, custom environment controls,
partition/rank logging, or phase-timing reports. Routing timers remain because
the adaptive partitioning algorithm uses them. The final occupancy validator,
normal run timing/cost output, optimized kernels, and routing logic remain.
The former allgather-update path was unreachable under the qualified
reduction conditions and was removed. Reduction groups are selected internally
(8 for up to 512 wires, 16 otherwise). The launcher preserves the qualified
MPI policy, including the abundant single-node B16/128-rank transport settings.

GHC verification used a fresh archive extraction at `/tmp/wr-submit.ML6pc4`:

- Build succeeded with the supplied Makefile and no compiler warnings.
- All three benchmark inputs validated at B1/n1, B16/n1, B4/n8, and B16/n8.
- Every benchmark cost was within 5% of its Table 1 reference.
- B1 serial times were 2.076875 / 7.143478 / 15.030620 seconds, below the GHC
  Table 1 limits 2.543 / 8.485 / 20.709 seconds.
- Group sizes 8 and 16 validated on small synthetic inputs at 64 oversubscribed
  ranks. These runs check correctness, not parallel performance.
- Separate launcher checks covered eight rank/input/batch/node configurations.
- Archive members were checked byte-for-byte against the final workspace files.

The clean source has not been rebenchmarked at 128 physical PSC cores.
Iteration 41's successful full-curve results describe the pre-cleanup build;
see `BENCHMARK_AUDIT.md`. No new performance numbers are inferred from the
cleanup. Section 7 report experiments remain separate.
