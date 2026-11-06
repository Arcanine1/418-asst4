# Wire routing tuning status

Goal: match the handout's approximate 128-rank B16 speedup curves (few 76x,
medium 100x, abundant 89x), while keeping one-rank time below the PSC Table 1
figures (3.400, 11.134, 26.779 seconds), validating occupancy, and tracking cost.

The main source is now the qualified iteration 33 candidate: 8-bit occupancy,
adaptive work assignment, grouped proposal reduction at high ranks for the
small inputs, and an abundant-specific inlining helper. Its default reduction
group size is 16. `tuning/run_psc.sh` selects group 8 for few and group 16 for
medium, and supplies the qualified MPI settings without recompiling the source.

Best recent candidate (iteration 30, grouped proposal reduction):

| Input | Group | One-rank seconds | 128-rank median seconds | Speedup |
| --- | ---: | ---: | ---: | ---: |
| few | 8 | 3.033723 | 0.038698 | 78.39x |
| medium | 16 | 10.371377 | 0.105364 | 98.43x |

These rows came from separately compiled group settings. Iteration 31 selects
the group at runtime in one executable and takes three serial measurements,
so group comparisons share a measured serial denominator. It tests few groups
4/8/16 and medium groups 8/16/32/64. GHC validation passed with runtime groups
2/4/8 and both inputs.

The candidate evaluates all assigned paths, reduces the best proposal per wire,
and applies routes at iteration boundaries. Timing rates are carried in the
same reduction. Spatial wire ordering is computed once from fixed endpoints;
no path costs are memoized. Hierarchical reduction uses group reduce, leader
allreduce, and group broadcast.

The 8-bit representation passed transient overflow checks at 128 ranks in
iteration 20: peaks were 2 for few, 4 for medium, and 7 for abundant. New routing
strategies still require validation; final occupancy alone does not prove an
absence of transient wrap.

Iteration 31 used one runtime-configured executable: few group 16 reached
76.14x (3.006882 / 0.039491), and medium group 16 reached 99.53x
(10.909966 / 0.109619). Group 32 had large outliers; group 16 was chosen for
consolidation.

Iteration 32 defaults to group 16 and makes per-rank partition logging opt-in
with WR_PARTITION_LOGS. It has a separate WR_CHECK_OCCUPANCY build. Both builds
passed GHC validation on all three inputs; checked transient peaks were 2, 5,
and 7. The PSC script compiles with code/Makefile's C++20 flags, measures actual
B1 serial time, three B16 serial runs, all intermediate B16 ranks, five 128-rank
runs for few/medium and three for abundant, plus checked 128-rank runs.

Iteration 32 PSC qualification: few 78.14x, medium 99.85x, abundant 93.94x.
All validated; transient peaks 2/4/7. Abundant failed the serial bound:
B1 n1=27.730840 >26.779 seconds (B16 median n1=27.763371). Medium B1=10.553263
passed. Thus qualification is not yet complete.

Iteration 33 extracts the unchanged ordinary task kernel into an abundant-only
helper marked GNU flatten/noinline, allowing traversal helpers to inline inside
it without flattening the full main function. GHC B1 abundant n1 improved from
19.241718 to 15.966925 seconds; B16 n8 improved from 2.791369 to 2.329771.
Few/medium and all checked builds validated on GHC; transient peaks 2/5/7.
Its PSC script repeats full qualification using C++20, since compiler effects on
serial and parallel timing must be measured again in the consolidated binary.

Iteration 33 PSC qualification: B1 times 3.095960/10.999064/21.221017 all passed
Table 1. B16 speedups were 75.47x/100.47x/89.21x; checked transient peaks were
2/5/7. Medium and abundant reached their approximate graph endpoints. Few is
about 0.7% short of 76x, within its observed run spread, but has not strictly
exceeded it in this consolidated executable. Full results and launch commands
are in RUNNING.md.

The iteration 33 source was copied exactly to local and GHC code/wireroute.cpp,
built with the Makefile, and its launcher validated. Iteration 34 held this
source fixed and tested runtime group sizes 8/16/32 and small-group binomial or
linear reduce/broadcast. With five repeats per mode and a shared median of
three serial runs, few group 8 reached 79.16x (3.146751 / 0.039750), passing the
approximate 76x endpoint. Group 16 binomial reached 78.14x with less run spread;
this is documented as an alternative. The default launcher chooses group 8 for
up to 512 wires and group 16 otherwise, preserving explicit overrides.

The measured median endpoints now meet the user's target: 79.16x/100.47x/89.21x,
with all Table 1 serial bounds passed. Results come from the stated runs, not
a claim that every run will achieve those numbers. Source SHA-256 remains
61a291578a5baa6df959f80236b12b86fbc1be624beeddcc67eb9bbf5b8b4ffd.
No further benchmark work is needed for the requested endpoint targets.

The user subsequently expanded the audit to every reference graph and table,
accepting +/-5% routing cost. Iteration 35's final-source GHC audit completed:
Table 1 times 2.105443/7.308403/16.004268 s all passed, every Table 2 entry
passed with largest absolute individual-run cost deviation below 0.37%, and
all intermediate and endpoint points exceeded both GHC reference curves.
Raw summary: GHC tuning/iteration35/ghc-local-20261105-164005/summary.txt.
The PSC audit subsequently completed on the same final source. Table 1 times
3.045172/10.754365/20.993312 passed, and every printed cost passed 5%. The full
B4 curves passed: 128-rank endpoints 54.34x/87.58x/85.93x. B16 intermediate
points passed, but its 128-rank endpoints were 77.27x/99.31x/86.40x. Medium is
about 0.7% below ~100x and abundant about 2.9% below ~89x in this fresh audit.
Earlier successful endpoint results do not justify an all-benchmarks claim.
See BENCHMARK_AUDIT.md. Next: inspect existing B16 128-rank timing zones before
further tuning. GHC auth expired during staging of iteration36/read_timings.py;
that reader is local-only so far. An inline PSC grep can retrieve the same
needed timing data without transfer or rerunning jobs. No source change yet.

The user provided the timing zones: medium route max ~0.099 s out of 0.109 s,
average ~0.085 s; abundant route max ~0.185 s out of 0.241 s, average ~0.169 s,
receive max ~0.040–0.042 s and sync max ~0.031–0.034 s. Independent rank maxima
must not be added. GHC SSH was restored. Iteration 37 tests only 128-rank
placement (core/socket/numa) with a shared three-run serial core-mapping median,
five parallel repeats per placement and rotating order. Both inputs validate
on GHC for all placements. It is staged on GHC. No code or production launcher
change has been made. Pending: PSC placement results to target ~0.75 ms medium
and ~7 ms abundant parallel-time gaps; costs must stay within 5%.

Iteration 37 PSC placement changes did not help: medium core/socket/numa
98.75x/99.27x/98.44x; abundant 86.45x/86.29x/85.84x. Keep core mapping.

Iteration 38 is isolated from the main source. `reduce` adds flatten/noinline
to route_with_reduction; GHC medium reduction time improved from 0.969851 to
0.775913 s. A received-update helper (`receive`/`combined`) reduced receive time
slightly but increased abundant routing time, so it is excluded from PSC.
`pool` combines reduction inlining with fixed four-request arrays and stable
reusable send slots, freed for reuse only after MPI_Testall completion. GHC
validation passed all B16 inputs and medium/abundant B1 message-heavy stress
runs; a checked build passed with peaks 2/5/7. Pool abundant was slower than
the control on GHC (paired 2.345/2.529 vs 2.191/2.213 s), so no win is claimed.
GHC B1 serial checks passed for all variants. Baseline/reduce/pool times were
few 2.040887/1.982265/2.067616, medium 7.138657/7.211515/7.473452, abundant
14.915436/15.117399/16.015438 seconds. Compiler/heap-layout effects on serial
and parallel timings therefore must be measured for each executable.

GHC quota was full; eight untracked, regenerable occupancy grids from old
iteration18–25 ghc-test outputs were removed (64 MiB). Source, logs, and wire
outputs were retained. A server interruption and SSH resets delayed pool
testing; its tests have now completed. Main source is still SHA-256 61a291...
b4ffd. Iteration38/psc_compare.sh and baseline/reduce/pool sources are staged
on GHC; script control flow/parser verified with mock MPI. Pending: PSC data.
