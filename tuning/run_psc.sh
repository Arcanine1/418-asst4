#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || ! $1 =~ ^[0-9]+$ || $1 -lt 1 ]]; then
    printf >&2 'Usage: bash tuning/run_psc.sh RANKS -f INPUT [-b 16 -i 5 -p 0.1]\n'
    exit 2
fi
ranks=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
input_path=
wire_count=
batch=1
route_args=("$@")
arg_count=$#
for ((arg_index=0; arg_index<arg_count; arg_index++)); do
    case ${route_args[$arg_index]} in
        -f)
            if ((arg_index + 1 < arg_count)); then
                arg_index=$((arg_index + 1)); input_path=${route_args[$arg_index]}
            fi ;;
        -f?*) input_path=${route_args[$arg_index]#-f} ;;
        -b)
            if ((arg_index + 1 < arg_count)); then
                arg_index=$((arg_index + 1)); batch=${route_args[$arg_index]}
            fi ;;
        -b?*) batch=${route_args[$arg_index]#-b} ;;
        --) break ;;
    esac
done
if [[ -n $input_path && -r $input_path ]]; then
    if { read -r grid_header && read -r count_value wire_tail; } < "$input_path"; then
        if [[ $count_value =~ ^[0-9]+$ ]]; then wire_count=$count_value; fi
    fi
fi
group=${WR_REDUCE_GROUP:-16}
if [[ -z ${WR_REDUCE_GROUP:-} && -n $wire_count ]]; then
    if ((10#$wire_count <= 512)); then group=8; fi
fi
nodes=${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-unknown}}
transport=${WR_MPI_TRANSPORT:-auto}
if [[ $transport == auto ]]; then
    transport=native
    if [[ $nodes == 1 && $ranks == 128 && $batch == 16 && -n $wire_count ]]; then
        if ((10#$wire_count > 4096)); then transport=large; fi
    fi
fi
mkdir -p outputs
flags=(--nooversubscribe --bind-to core --map-by core
       --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
       --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
if [[ -n ${WR_REDUCE_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_reduce_algorithm "$WR_REDUCE_ALGORITHM"); fi
if [[ -n ${WR_BCAST_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_bcast_algorithm "$WR_BCAST_ALGORITHM"); fi
case "$transport" in
    native) ;;
    shm|fast|large)
        if [[ $nodes != 1 ]]; then
            printf >&2 'Shared-memory transport requires a known single-node Slurm allocation.\n'; exit 2
        fi
        flags+=(--mca pml ob1 --mca btl self,vader)
        if [[ $transport != shm ]]; then flags+=(--mca btl_vader_fbox_threshold 1 --mca btl_vader_fbox_max 128); fi
        if [[ $transport == large ]]; then flags+=(--mca btl_vader_fbox_size 8192); fi ;;
    *) printf >&2 'Unknown WR_MPI_TRANSPORT: %s\n' "$transport"; exit 2 ;;
esac
exec mpirun "${flags[@]}" -x WR_REDUCE_GROUP="$group" -np "$ranks" "${WR_EXECUTABLE:-$root/code/wireroute}" "$@"
