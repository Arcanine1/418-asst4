# Qualified wire routing implementation

`code/wireroute.cpp` is the iteration 33 candidate, copied without modification.
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
path does not use grouped reduction. Running plain `mpirun` with different
collective settings can produce different times.

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
`BENCHMARK_AUDIT.md`. The completed GHC audit passes Table 1, Table 2 within
the user's +/-5% cost tolerance, and both complete GHC curves. Final-source PSC
B4 measurements are pending; the B16 endpoints alone do not establish that
every reference benchmark passes.

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
