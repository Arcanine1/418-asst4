#!/usr/bin/env bash
set -euo pipefail
module load gcc/13.3.1-p20240614 openmpi/5.0.8-gcc13.3.1
campaign=$1
job_name=$2
exec python3 "$campaign/tools/collect.py" --campaign "$campaign" --job "$job_name"
