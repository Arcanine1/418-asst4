# Compiler inlining and reusable send storage

The production source remains the audited iteration 33 implementation. These
are isolated experiments based on its exact source (SHA-256 61a291...b4ffd).

* `baseline`: unchanged audited implementation.
* `reduce`: adds GNU flatten/noinline to the reduction routing function, so its
  candidate and cell traversal helpers can inline into that hot function.
* `receive`: experimental pure received-update helper, rejected after GHC showed
  a routing regression despite a small receive-time saving.
* `combined`: reduction plus receive helper; also rejected from the PSC set.
* `pool`: reduction inlining plus reusable send slots. Each slot owns a fixed
  array of four MPI requests. Slots are held at stable addresses in a deque
  that never removes slots while MPI can reference them. Cleanup returns an
  index to the free list only after MPI_Testall reports every send complete.
  Finalization waits for every active slot before clearing storage. Only the
  transmitted message bytes are copied. Candidate search, message counts,
  route update ordering, and routing arithmetic are unchanged.

GHC validation passed all B16 inputs for every variant. The pool additionally
passed message-heavy medium/abundant B1 runs at eight ranks. Serial B1 checks
passed GHC bounds. Reduction inlining improved GHC's medium reduction path from
about 0.97 s to 0.78 s. Pool abundant runs were slower than the control on GHC;
the PSC experiment therefore reports both raw times and each binary's own
serial baseline rather than assuming a gain.

Run on PSC:

```bash
bash tuning/iteration38/psc_compare.sh
```

Copy baseline/reduce/pool sources and psc_compare.sh. Few and medium compare all
three, while abundant compares baseline/pool. Each has a B1 serial-bound check,
three B16 serial runs, and five B16 128-rank runs with rotating variant order.
The script validates every run, checks the 5% cost tolerance, prints timing
zones, and runs a separate pool build that detects 8-bit underflow/overflow.
Expected runtime is roughly 7–9 minutes, depending on node speed.

GHC quota recovery: eight untracked generated occupancy grids were removed from
iteration18–25/ghc-test/outputs/occ_output.txt, freeing 64 MiB. Source, logs, and
wire-output files were retained; occupancy grids can be regenerated.
