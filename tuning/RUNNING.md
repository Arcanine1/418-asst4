# Clean final implementation

The submission archive is `handin.tar` at the repository root. It contains
only the final C++ sources/header, build Makefile, PSC launcher, and README.
See `SUBMISSION.md` for hashes and verification details.

Build and run from the repository root:

```bash
make -C code
bash code/run_psc.sh 128 -f code/inputs/timeinput/medium_wires.txt -b 16 -i 5 -p 0.1
```

Run in an allocation with enough cores. The launcher preserves the caller's
working directory and creates `outputs/` there. It supplies the qualified
core binding and MPI collective settings. For B16, 128 ranks, and more than
4096 wires on a known single-node Slurm allocation, it also selects the tested
shared-memory transport settings. Other cases retain native MPI transport.
The existing `bash tuning/run_psc.sh ...` command forwards to this launcher.

The source selects reduction group sizes internally: 8 for inputs with up to
512 wires, 16 otherwise. Experimental build switches, environment overrides,
partition logging, rank timing reports, and occupancy-check builds have been
removed. Final routing validation, initialization/computation timing, and
occupancy/cost output remain. Routing timers used by adaptive scheduling remain
part of the algorithm. The private occupancy grid uses 8-bit cell counts.

To regenerate the submission archive:

```bash
make handin.tar
```

A fresh extraction of the clean archive built and passed 14 GHC checks,
including the three benchmark inputs and both reduction-group sizes. GHC
serial times and costs remained within the handout limits. The 128-core PSC
timings have not been remeasured after cleanup. The full passing pre-cleanup
benchmark evidence is preserved in `BENCHMARK_AUDIT.md` and `iteration41/`.
The older version-pinned audit scripts intentionally require their historical
source/launcher hashes.
