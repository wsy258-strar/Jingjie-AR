#!/usr/bin/env bash
set -euo pipefail

binary=$1
port=${2:-16379}
work=$(mktemp -d /tmp/ar-presence-redis.XXXXXX)
cleanup() {
  redis-cli -p "$port" shutdown nosave >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT
redis-server --port "$port" --save '' --appendonly no --dir "$work" --daemonize yes
for _ in $(seq 1 20); do redis-cli -p "$port" ping >/dev/null 2>&1 && break; sleep 0.1; done
"$binary" "$port"
echo 'PASS: Redis presence store'
