# Final wire routing candidate

`code/wireroute.cpp` now includes the PSC-proven iteration 38 reduction-kernel
inlining change (SHA-256
`7f2d398c35377980877d7ee63fec9de381e7276a977df7fd47a821f02ae810f0`).
That version measured 106.22x for few and 124.85x for medium at 128 B16 ranks,
with passing serial bounds and costs. The send-buffer pool and receive-inlining
candidates were not promoted. Iteration 40 reached 92.31x for abundant with
the `large` transport: 21.126558 s serial / 0.228868 s parallel. All five
parallel runs were between 0.227601 and 0.229628 s, and every cost passed the
agreed +/-5% band. Its checked transient occupancy maximum was 7.
The complete historical audit below refers to the earlier source, not a claim
that this newer executable has passed every full curve yet.

Build on PSC with the supplied Makefile and use the tested MPI settings:

```bash
cd code
make
bash ../tuning/run_psc.sh 128 -f inputs/timeinput/medium_wires.txt -b 16 -i 5 -p 0.1
```

The launcher preserves the caller's working directory and creates `outputs/`
there. Change the rank count or input filename as needed. Run in an allocation
with enough cores. `run_psc.sh` supplies core binding, recursive-doubling
allgather/allreduce settings, and selects group 8 for inputs with up to 512 wires
(including few) and group 16 for larger inputs (including medium). The abundant
path does not use grouped reduction. For B16, 128 ranks, and more than 4096
wires in a known single-node Slurm allocation, the launcher also selects
`ob1`, `self,vader`, fast-channel threshold 1, maximum 128, and size 8192.
Other cases retain native MPI transport. `WR_MPI_TRANSPORT=native` overrides
that selection. Running plain `mpirun` with different MPI settings can produce
different times.

## Final audit

Copy the current `code/wireroute.cpp`, `tuning/run_psc.sh`,
`tuning/iteration41/benchmark_audit.sh`, and
`tuning/iteration41/summarize.py` from GHC to PSC. In a single-node allocation
with 128 cores, run:

```bash
bash tuning/iteration41/benchmark_audit.sh psc
```

This compiles normal and overflow-checked builds, measures Table 1 and both
full speedup curves, and checks every cost against the 5% band. The GHC
counterpart adds all Table 2 entries. The audit checks source hash
`7f2d398c35377980877d7ee63fec9de381e7276a977df7fd47a821f02ae810f0`
and launcher hash
`b09e374597fb51b79a8735320583bf76f9574f10d4c59f8f1f589ad40c5f45a9`.
It uses the production launcher for every run. The final GHC audit passed
both tables and all points on both curves; see `iteration41/ghc-summary.txt`.
The final PSC full-curve audit is pending. The historical results below use
earlier source versions.

## PSC qualification, iterations 33 and 34

| Input | B1 one-rank time | Table 1 PSC time | B16 one-rank median | B16 128-rank median | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| few | 3.095960 | 3.400 | 3.146751 | 0.039750 | 79.16x |
| medium | 10.999064 | 11.134 | 10.996299 | 0.109444 | 100.47x |
| abundant | 21.221017 | 26.779 | 21.368682 | 0.239531 | 89.21x |

All entries are computation seconds. Few's B16 results come from iteration 34,
using the same promoted source with runtime group 8; medium/abundant and all
B1 measurements come from iteration 33 with runtime group 16. Serial B16
medians use three runs; 128 medians use five runs for few/medium and three for
abundant. All validated.
The separate checked builds found transient occupancy maxima of 2, 5, and 7.
Final 128-rank costs were 29,240–29,296; 272,253–273,615; and
1,549,432–1,550,344 respectively. The handout is `../asst4_handout.pdf`.

All three measured median speedups exceed the approximate B16 graph endpoints
of 76x/100x/89x. These are medians, not guarantees for every run. Few group 8
had one 0.122254-second outlier among five runs. The tested binomial alternative
reached 78.14x with all five runs between 0.039343 and 0.042876 seconds:

```bash
WR_REDUCE_GROUP=16 WR_REDUCE_ALGORITHM=5 WR_BCAST_ALGORITHM=6 \
  bash ../tuning/run_psc.sh 128 -f inputs/timeinput/few_wires.txt -b 16 -i 5 -p 0.1
```

For the subsequent audit of **every** reference table and graph, see
`BENCHMARK_AUDIT.md`. Both serial-time columns, the GHC cost table within the
user's +/-5% tolerance, both GHC curves, and the PSC B4 curve pass. The fresh PSC
B16 audit reached 77.27x/99.31x/86.40x at 128 ranks. Thus the previous successful
endpoint runs do not establish a strict pass for every endpoint in the fresh
audit: medium is about 0.7% short and abundant about 2.9% short. All reported
costs remain within 5%. Further work is focused on these small parallel-time
gaps, using existing log timing zones before another performance experiment.

## Implementation

The implementation evaluates all assigned candidate routes. It uses an 8-bit
private occupancy grid, spatial task ordering, and adaptive rank weights.
Spatial order depends only on fixed endpoints and is computed once.

At 64+ ranks, B16, and up to 4096 wires, ranks make proposals and combine them
with grouped reduce, leader allreduce, and group broadcast. Routing rates are
carried in the same reduction; accepted routes are applied at iteration
boundaries. Other cases use asynchronous batches forwarded through a tree.
Abundant has a separate task helper with compiler inlining attributes; it uses
the same candidate enumeration, tie breaking, and batch update semantics.

The 8-bit grid requires counts to stay within 0–255. For a checked build:

```bash
mpic++ -Wall -O3 -std=c++20 -m64 -Icode -fopenmp -Wno-unknown-pragmas \
  -DWR_CHECK_OCCUPANCY code/wireroute.cpp code/validate.cpp -o /tmp/wireroute.checked
```

That build aborts before a counter wraps and reports the maximum transient
count. Use the ordinary build for performance measurements. Partition logging
is opt-in through `WR_PARTITION_LOGS`; it adds timed output work.

`WR_REDUCE_GROUP` overrides group size through the launcher. Optional launcher
variables `WR_REDUCE_ALGORITHM` and `WR_BCAST_ALGORITHM` select the tested
few-input alternative above; the qualified defaults leave these unset.
