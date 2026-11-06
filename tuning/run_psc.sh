#!/usr/bin/env bash
set -euo pipefail
exec bash "$(cd "$(dirname "$0")/../code" && pwd)/run_psc.sh" "$@"
