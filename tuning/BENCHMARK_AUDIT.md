# Handout reference benchmark audit

Scope: the reference tables and speedup graphs in Section 9 of
`../asst4_handout.pdf`. The user accepts costs within +/-5% of each reference.
This does not establish completion of Section 7's cache-miss, total-program
timing, or multi-node report experiments, which have not been audited here.

## Current candidate: GHC passed, final PSC audit pending

Source SHA-256:
`7f2d398c35377980877d7ee63fec9de381e7276a977df7fd47a821f02ae810f0`.
Launcher SHA-256:
`b09e374597fb51b79a8735320583bf76f9574f10d4c59f8f1f589ad40c5f45a9`.

Endpoint experiments on this source measured B16 speedups of 106.22x for few
and 124.85x for medium (iteration 38), and 92.31x for abundant (iteration 40,
`large` transport). Costs passed +/-5%; the few/medium serial bounds passed.
Abundant's fresh B1 median will be checked by the final audit. The abundant
transport selection is limited to a known single-node allocation, 128 ranks,
B16, and more than 4096 wires. Other transport defaults are unchanged.

`bash tuning/iteration41/benchmark_audit.sh ghc|psc` checks both hashes and
measures all relevant tables and curves through the production launcher.
The GHC audit completed at
`iteration41/ghc-local-20261106-144145/` on GHC. A copy of its full summary is
saved locally as `iteration41/ghc-summary.txt`. Results:

| Reference | Result |
| --- | --- |
| Table 1 GHC B1 serial times | 2.054808 / 7.121776 / 14.927966 s, all below 2.543 / 8.485 / 20.709 s |
| Table 1 GHC costs | All individual costs within 5% |
| Table 2 GHC costs | All 12 entries pass; largest absolute individual deviation 0.400% |
| Figure 2(a), GHC B4 | Every measured point exceeds its plotted reference; eight-rank endpoints 6.60x / 7.01x / 6.74x |
| Figure 3(a), GHC B16 | Every measured point exceeds its plotted reference; eight-rank endpoints 6.74x / 7.05x / 6.75x |
| Checked transient occupancy | Peaks 2 / 4 / 7; all validation passed |

Input order is few / medium / abundant. Graph comparisons were checked against
rendered handout pages 8 and 9. The final PSC full-curve audit is pending. The
older complete audit below is retained as historical evidence, not proof that
the new executable passes every PSC point.

## Historical audit: iteration 35

Source SHA-256:
`61a291578a5baa6df959f80236b12b86fbc1be624beeddcc67eb9bbf5b8b4ffd`.
Compiler and launch settings are in `iteration35/benchmark_audit.sh` and
`run_psc.sh`. All benchmark runs must validate before they are summarized.

| Reference | Status | Evidence |
| --- | --- | --- |
| Table 1 GHC B1 serial times and costs | Pass | Median times 2.105443 / 7.308403 / 16.004268 s; limits 2.543 / 8.485 / 20.709 s. All run costs within 5%. |
| Table 1 PSC B1 serial times and costs | Pass | Fresh audit times 3.045172 / 10.754365 / 20.993312 s, all below 3.400 / 11.134 / 26.779 s. Costs pass 5% tolerance. |
| Table 2 GHC medium costs, B1/B4/B16, 1/2/4/8 ranks | Pass | All 12 entries measured. Largest absolute deviation across individual runs was below 0.37%. |
| Figure 2(a), GHC B4 curves | Pass | Full curves measured; 8-rank speedups 6.67x / 7.06x / 6.80x exceed the plotted endpoints (approximately 4.3x / 5.2x / 4.7x). Intermediate points also exceed the plotted curves. |
| Figure 2(b), PSC B4 curves | Pass | Full curves measured; 128-rank speedups 54.34x / 87.58x / 85.93x exceed approximate 51x / 73x / 64x endpoints. Intermediate points exceed the plotted curves. |
| Figure 3(a), GHC B16 curves | Pass | Full curves measured; 8-rank speedups 6.50x / 7.00x / 6.78x exceed the plotted endpoints (approximately 5.2x / 5.75x / 5.7x). Intermediate points also exceed the plotted curves. |
| Figure 3(b), PSC B16 curves | Few passes; medium/abundant slightly short in the fresh audit | Full curves measured. 128-rank speedups 77.27x / 99.31x / 86.40x vs approximate 76x / 100x / 89x endpoints. Intermediate points exceed the plotted curves. Earlier successful endpoints do not establish that this fresh audit strictly clears every endpoint. |

Input order above is few / medium / abundant. Speedups use the measured serial
time for the same batch size. Graph values are read from plotted curves, not
presented by the handout as exact numerical thresholds.

GHC evidence: the completed runner printed its full summary. The summary and
raw logs on GHC are under `tuning/iteration35/ghc-local-20261105-164005/`
(`summary.txt` contains the table and full-curve measurements).

PSC audit command (completed):

```bash
bash tuning/iteration35/benchmark_audit.sh psc
```

Copy both `iteration35/benchmark_audit.sh` and `iteration35/summarize.py` to
PSC before running it. It compiles the final `code/wireroute.cpp`, records its
checksum, checks Table 1 again, measures both full PSC curves, and records the
B1/B4/B16 sensitivity point at 32 ranks. Every cost is checked against the 5%
tolerance; the graph comparisons use the printed full curves.

Every reported fresh-audit cost passed the agreed 5% band. Largest observed
PSC cost excess among the printed graph rows was about 2.49% (medium B16).
The remaining parallel-time reductions to the approximate B16 endpoints are
about 0.7% for medium and 2.9% for abundant. Read existing timing zones with
`python3 tuning/iteration36/read_timings.py`; no benchmark rerun is needed.
