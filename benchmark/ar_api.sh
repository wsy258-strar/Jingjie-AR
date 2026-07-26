#!/usr/bin/env bash
set -euo pipefail

base_url=${BASE_URL:-http://127.0.0.1:8080}
token=${SESSION_TOKEN:?set SESSION_TOKEN to a test token}
scene=${SCENE_ID:-1}
duration=${DURATION_SECONDS:-15}
timeout=${WRK_TIMEOUT_SECONDS:-2}
concurrencies=${BENCHMARK_CONCURRENCIES:-"10 100 500"}
command -v wrk >/dev/null || { echo 'wrk is required' >&2; exit 2; }

for request in $(seq 1 10); do curl -fsS "$base_url/api/session?token=$token" >/dev/null; done

for concurrency in $concurrencies; do
  echo "cached session read, concurrency=${concurrency}"
  wrk -t 2 -c "$concurrency" -d "${duration}s" --timeout "${timeout}s" --latency "$base_url/api/session?token=$token"
  echo "scene member polling, concurrency=${concurrency}"
  wrk -t 2 -c "$concurrency" -d "${duration}s" --timeout "${timeout}s" --latency "$base_url/api/scenes/$scene/members?token=$token"
done
