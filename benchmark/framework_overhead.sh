#!/usr/bin/env bash
set -euo pipefail

base_url=${BASE_URL:-http://152.136.20.86/}
duration=${DURATION_SECONDS:-15}
timeout=${WRK_TIMEOUT_SECONDS:-2}
concurrencies=${BENCHMARK_CONCURRENCIES:-"10 20 50 100"}
command -v wrk >/dev/null || { echo 'wrk is required' >&2; exit 2; }

run_case() {
  local label=$1
  local path=$2
  shift 2
  local concurrency
  for concurrency in $concurrencies; do
    echo "${label}, concurrency=${concurrency}"
    wrk -t 2 -c "$concurrency" -d "${duration}s" --timeout "${timeout}s" --latency "$@" "$base_url$path"
  done
}

# The ten warm-up requests prime the static-file and route dispatch paths.
for request in $(seq 1 10); do curl -fsS "$base_url/api/scenes" >/dev/null; done

# /api/scenes is a direct static Router callback; / is the reusable static-file
# handler; /api/scenes/1 exercises a dynamic parameter route.  All application
# routes run through CORS and access logging, and the final case sends Origin to
# exercise the CORS response path explicitly.
run_case 'direct callback' '/api/scenes'
run_case 'static file' '/'
run_case 'dynamic route' '/api/scenes/1'
run_case 'CORS plus access log' '/api/scenes/1' -H 'Origin: https://benchmark.invalid'
