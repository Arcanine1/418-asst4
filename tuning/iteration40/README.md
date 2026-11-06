# Runtime-only abundant transport test

Copy the promoted GHC `code/wireroute.cpp` and `psc_transport.sh` to the same
PSC paths, then run in a single-node 128-core allocation:

```bash
bash tuning/iteration40/psc_transport.sh
```

This compiles one unchanged executable, with the qualified C++20 flags, and
compares four parallel-only transport policies:

| Mode | Policy |
| --- | --- |
| native | Qualified core binding and MPI collective flags, default transport |
| shm | Explicit ob1 PML with self,vader shared-memory transport |
| fast | shm plus fast-channel threshold 1 and maximum 128 peers |
| large | fast plus 8192-byte per-peer buffers |

The installed GHC metadata reports defaults of 16 sends before activating a
fast channel, maximum 32 peers, and 4096 bytes per peer. The script checks the
PSC options rather than assuming those controls exist. All modes validated on
GHC, where eight ranks do not exercise the 32-peer limit.

PSC results on the promoted source, using a shared B16 serial median of
21.126558 seconds:

| Mode | 128-rank median seconds | Speedup |
| --- | ---: | ---: |
| native | 0.241558 | 87.46x |
| shm | 0.231644 | 91.20x |
| fast | 0.226841 | 93.13x |
| large | 0.228868 | 92.31x |

All costs were within 5%, and every checked transient maximum was 7. `fast`
had two large timing outliers (0.574177 and 0.431477 seconds). `large` had all
five runs between 0.227601 and 0.229628 seconds, so it was selected for the
production launcher. That selection applies only to B16, 128 ranks, more
than 4096 wires, and a known single-node Slurm allocation. Iteration 41 audits
the complete final source/launcher combination.

The source hash must match the promoted reduction-inlining version, otherwise
the script asks for the correct source. One B1 serial run checks Table 1; three
B16 native serial runs supply a shared numerator. Five parallel repetitions
per mode use rotating order. Every run validates and its cost is checked
against the user's 5% band. Separate checked runs detect transient counter
overflow. Expected runtime is about 3–4 minutes.

The explicit shared-memory modes are deliberately single-node only and must
not be carried into a multi-node experiment without a suitable network
transport. No source logic, candidate search, or routing arithmetic changes.
