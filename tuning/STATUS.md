# Wire routing tuning status

Goal: match the handout's approximate 128-rank B16 speedup curves (few 76x,
medium 100x, abundant 89x), while keeping one-rank time below the PSC Table 1
figures (3.400, 11.134, 26.779 seconds), validating occupancy, and tracking cost.

The main source currently contains 8-bit occupancy, adaptive work assignment,
and a fixed-size allgather plus task multiplier 16 for medium at high rank
counts. Subsequent reduction experiments are isolated in `tuning/`.

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

Pending: iteration 32 PSC qualification, final source selection and promotion,
and any changes required by those measurements. The task is not yet complete.
