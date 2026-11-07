#!/usr/bin/env bash
set -euo pipefail
campaign=$1
job_name=$2
exec python3 "$campaign/tools/collect.py" --campaign "$campaign" --job "$job_name"
