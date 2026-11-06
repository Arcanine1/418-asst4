# Plain MPI PSC check

Copy psc_check.sh into tuning/plain_mpi/ in your PSC checkout, along with the
current code files. In a single-node 128-core interactive allocation, run:

    bash tuning/plain_mpi/psc_check.sh

The script freezes and builds the current source, then measures each of the
three inputs with three B1 serial runs, three B16 serial runs, and five B16
128-rank runs. It uses only mpirun -np N, with no custom MPI runtime flags.
It validates every run and reports serial bounds, B16 endpoint speedups,
and costs against the agreed 5% tolerance.

This is an endpoint check; it does not measure the full B4/B16 curves.
It does not create or modify handin.tar. It must not be run on an 8-core GHC
machine. The separate GHC report collector can continue on GHC while this
check runs on a PSC node.
