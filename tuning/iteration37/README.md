# Placement-only test

This experiment keeps `code/wireroute.cpp` fixed. It compares 128-rank core,
socket, and NUMA mapping while preserving core binding, group 16, and the
qualified MPI algorithm settings. One-rank runs always use core mapping, so
the numerator is shared across alternatives and is not tuned to raise speedup.
Any eventual placement policy should likewise keep the one-rank configuration
fixed. Probe failures for optional topology objects are reported and skipped.

Each input has three serial runs and five parallel runs per placement, with
rotating launch order. Hardware and binding reports are saved. Every run must
validate; the summary checks all parallel costs against the user's 5% band.

Reason: the final-source PSC audit measured abundant's route maximum near
0.185 s out of 0.241 s computation, receive maxima around 0.040–0.042 s, and
sync maxima around 0.031–0.034 s. Independent rank maxima cannot be summed.
About 7 ms needs to be saved to reach the approximate B16 89x endpoint using
that audit's serial baseline. Medium needs about 0.75 ms to reach 100x and is
included as a control/candidate; it is much more routing-bound.

Run in a 128-core PSC allocation:

```bash
bash tuning/iteration37/psc_placement.sh
```

No source or production launcher change is made by this experiment.
