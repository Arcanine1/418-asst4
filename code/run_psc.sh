#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || ! $1 =~ ^[0-9]+$ || $1 -lt 1 ]]; then
    printf >&2 'Usage: bash code/run_psc.sh RANKS -f INPUT [-b 16 -i 5 -p 0.1]\n'
    exit 2
fi
ranks=$1
shift
code_dir=$(cd "$(dirname "$0")" && pwd)
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
mkdir -p outputs
flags=(--nooversubscribe --bind-to core --map-by core
       --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
       --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
nodes=${SLURM_JOB_NUM_NODES:-${SLURM_NNODES:-unknown}}
if [[ $nodes == 1 && $ranks == 128 && $batch == 16 && -n $wire_count ]]; then
    if ((10#$wire_count > 4096)); then
        flags+=(--mca pml ob1 --mca btl self,vader
                --mca btl_vader_fbox_threshold 1 --mca btl_vader_fbox_max 128
                --mca btl_vader_fbox_size 8192)
    fi
fi
exec mpirun "${flags[@]}" -np "$ranks" "$code_dir/wireroute" "$@"
