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
allgather/allreduce settings, and the default reduction group size of 16 used
in qualification. Running plain `mpirun` with different collective settings can
produce different times.

## PSC qualification, iteration 33

| Input | B1 one-rank time | Table 1 PSC time | B16 one-rank median | B16 128-rank median | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| few | 3.095960 | 3.400 | 3.053560 | 0.040458 | 75.47x |
| medium | 10.999064 | 11.134 | 10.996299 | 0.109444 | 100.47x |
| abundant | 21.221017 | 26.779 | 21.368682 | 0.239531 | 89.21x |

All entries are computation seconds. Serial B16 medians use three runs; 128
medians use five runs for few/medium and three for abundant. All validated.
The separate checked builds found transient occupancy maxima of 2, 5, and 7.
Final 128-rank costs were 29,240–29,256; 272,253–273,615; and
1,549,432–1,550,344 respectively. The handout is `../asst4_handout.pdf`.

Medium and abundant reach the approximate B16 graph endpoints. Few is roughly
0.7% short of the approximate 76x endpoint, within its observed run spread;
iteration 34 tests only runtime settings for that remaining margin. This is
not a claim that few strictly exceeds 76x in this qualification.

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

`WR_REDUCE_GROUP` overrides group size through the launcher. The optional
launcher variables `WR_REDUCE_ALGORITHM` and `WR_BCAST_ALGORITHM` support the
remaining runtime experiment; the qualified defaults leave these unset.
