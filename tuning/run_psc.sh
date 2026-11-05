#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || ! $1 =~ ^[0-9]+$ || $1 -lt 1 ]]; then
    printf >&2 'Usage: bash tuning/run_psc.sh RANKS -f INPUT [-b 16 -i 5 -p 0.1]\n'
    exit 2
fi
ranks=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p outputs
flags=(--nooversubscribe --bind-to core --map-by core
       --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
       --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
if [[ -n ${WR_REDUCE_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_reduce_algorithm "$WR_REDUCE_ALGORITHM"); fi
if [[ -n ${WR_BCAST_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_bcast_algorithm "$WR_BCAST_ALGORITHM"); fi
exec mpirun "${flags[@]}" -x WR_REDUCE_GROUP="${WR_REDUCE_GROUP:-16}" -np "$ranks" "$root/code/wireroute" "$@"
