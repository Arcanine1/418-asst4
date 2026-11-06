#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || ! $1 =~ ^[0-9]+$ || $1 -lt 1 ]]; then
    printf >&2 'Usage: bash tuning/run_psc.sh RANKS -f INPUT [-b 16 -i 5 -p 0.1]\n'
    exit 2
fi
ranks=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
group=${WR_REDUCE_GROUP:-16}
if [[ -z ${WR_REDUCE_GROUP:-} ]]; then
    input_path=
    route_args=("$@")
    for ((arg_index=0; arg_index<${#route_args[@]}; arg_index++)); do
        case ${route_args[$arg_index]} in
            -f)
                if ((arg_index + 1 < ${#route_args[@]})); then input_path=${route_args[$((arg_index + 1))]}; fi ;;
            -f?*) input_path=${route_args[$arg_index]#-f} ;;
        esac
    done
    if [[ -n $input_path && -r $input_path ]]; then
        if { read -r grid_header && read -r wire_count wire_tail; } < "$input_path"; then
            if [[ $wire_count =~ ^[0-9]+$ ]] && ((10#$wire_count <= 512)); then group=8; fi
        fi
    fi
fi
mkdir -p outputs
flags=(--nooversubscribe --bind-to core --map-by core
       --mca coll_tuned_priority 100 --mca coll_tuned_use_dynamic_rules 1
       --mca coll_tuned_allgather_algorithm 3 --mca coll_tuned_allreduce_algorithm 3)
if [[ -n ${WR_REDUCE_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_reduce_algorithm "$WR_REDUCE_ALGORITHM"); fi
if [[ -n ${WR_BCAST_ALGORITHM:-} ]]; then flags+=(--mca coll_tuned_bcast_algorithm "$WR_BCAST_ALGORITHM"); fi
exec mpirun "${flags[@]}" -x WR_REDUCE_GROUP="$group" -np "$ranks" "$root/code/wireroute" "$@"
