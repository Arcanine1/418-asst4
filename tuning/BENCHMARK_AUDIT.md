# Handout reference benchmark audit

Scope: the reference tables and speedup graphs in Section 9 of
`../asst4_handout.pdf`. The user accepts costs within +/-5% of each reference.
This does not establish completion of Section 7's cache-miss, total-program
timing, or multi-node report experiments, which have not been audited here.

Source SHA-256:
`61a291578a5baa6df959f80236b12b86fbc1be624beeddcc67eb9bbf5b8b4ffd`.
Compiler and launch settings are in `iteration35/benchmark_audit.sh` and
`run_psc.sh`. All benchmark runs must validate before they are summarized.

| Reference | Status | Evidence |
| --- | --- | --- |
| Table 1 GHC B1 serial times and costs | Pass | Median times 2.105443 / 7.308403 / 16.004268 s; limits 2.543 / 8.485 / 20.709 s. All run costs within 5%. |
| Table 1 PSC B1 serial times and costs | Pass in iteration 33; fresh audit pending | Times 3.095960 / 10.999064 / 21.221017 s, all below 3.400 / 11.134 / 26.779 s. |
| Table 2 GHC medium costs, B1/B4/B16, 1/2/4/8 ranks | Pass | All 12 entries measured. Largest absolute deviation across individual runs was below 0.37%. |
| Figure 2(a), GHC B4 curves | Pass | Full curves measured; 8-rank speedups 6.67x / 7.06x / 6.80x exceed the plotted endpoints (approximately 4.3x / 5.2x / 4.7x). Intermediate points also exceed the plotted curves. |
| Figure 2(b), PSC B4 curves | Pending | Final-source full audit requires PSC output. Earlier-source measurements do not establish this result. |
| Figure 3(a), GHC B16 curves | Pass | Full curves measured; 8-rank speedups 6.50x / 7.00x / 6.78x exceed the plotted endpoints (approximately 5.2x / 5.75x / 5.7x). Intermediate points also exceed the plotted curves. |
| Figure 3(b), PSC B16 curves | Qualified endpoint results; fresh full audit pending | Final-source 128-rank median speedups 79.16x / 100.47x / 89.21x. Earlier full qualification and the group-8 few endpoint are recorded in RUNNING.md. |

Input order above is few / medium / abundant. Speedups use the measured serial
time for the same batch size. Graph values are read from plotted curves, not
presented by the handout as exact numerical thresholds.

GHC evidence: the completed runner printed its full summary. The summary and
raw logs on GHC are under `tuning/iteration35/ghc-local-20261105-164005/`
(`summary.txt` contains the table and full-curve measurements).

Pending PSC command (128-core allocation):

```bash
bash tuning/iteration35/benchmark_audit.sh psc
```

Copy both `iteration35/benchmark_audit.sh` and `iteration35/summarize.py` to
PSC before running it. It compiles the final `code/wireroute.cpp`, records its
checksum, checks Table 1 again, measures both full PSC curves, and records the
B1/B4/B16 sensitivity point at 32 ranks. Every cost is checked against the 5%
tolerance; the graph comparisons use the printed full curves.
