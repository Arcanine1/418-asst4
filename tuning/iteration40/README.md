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
GHC, where eight ranks do not exercise the 32-peer limit. Performance at 128
ranks remains unproven. The production launcher is unchanged by this script.

The source hash must match the promoted reduction-inlining version, otherwise
the script asks for the correct source. One B1 serial run checks Table 1; three
B16 native serial runs supply a shared numerator. Five parallel repetitions
per mode use rotating order. Every run validates and its cost is checked
against the user's 5% band. Separate checked runs detect transient counter
overflow. Expected runtime is about 3–4 minutes.

The explicit shared-memory modes are deliberately single-node only and must
not be carried into a multi-node experiment without a suitable network
transport. No source logic, candidate search, or routing arithmetic changes.
