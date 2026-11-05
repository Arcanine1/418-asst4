# Granularity with the 8-bit occupancy and medium collective

Run from a 128-core PSC interactive allocation:

```bash
bash tuning/iteration24/psc_tune.sh
```

The script builds isolated executables, validates every run, measures each
variant at one rank, and reports median computation time from three 128-rank
runs. It does not edit `code/wireroute.cpp`.

| Variant | Tasks multiplier | Splitting floor | Collective for few |
| --- | ---: | --- | --- |
| tpp2 | 2 | Existing 10% floor | No |
| tpp4 | 4 | Existing 10% floor | No |
| tpp8 | 8 | Existing 10% floor | No |
| tpp16 | 16 | Existing 10% floor | No |
| few16 | 16 | Removed | No |
| few16collect | 16 | Removed | Yes |
| few32collect | 32 | Removed | Yes |

Few runs tpp4, tpp8, few16, few16collect, and few32collect. Medium runs
tpp2, tpp4, tpp8, and tpp16, all using the current medium collective path.
The one-rank routing and candidate search remain the same across variants.

GHC validation: all four original task multipliers passed at eight ranks.
The finest uncapped collective case also passed at eight ranks, with its
multiplier increased to produce the splitting target expected at 128 ranks.

Reason for the experiment: iteration 22 measured medium route maxima of
roughly 18–21 ms versus rank averages near 14 ms per iteration. Interleaving
spatial blocks (iteration 23) produced only a small PSC improvement and was
not promoted. This experiment tests whether finer partial tasks reduce the
imbalance now that medium can exchange proposals with one collective.
