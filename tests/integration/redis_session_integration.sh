#!/usr/bin/env bash
set -euo pipefail

redis_cli="${REDIS_CLI:-redis-cli}"
test_binary="$1"
redis_port="${REDIS_PORT:-6379}"
pattern='http_session:{integration-*}'

while IFS= read -r key; do
    [ -z "$key" ] || "$redis_cli" -p "$redis_port" DEL "$key" >/dev/null
done < <("$redis_cli" -p "$redis_port" --scan --pattern "$pattern")

key='http_session:{integration-probe}'
"$test_binary" save
ttl="$("$redis_cli" -p "$redis_port" TTL "$key")"
[ "$ttl" -ge 1790 ]
[ "$ttl" -le 1800 ]
"$test_binary" load
"$test_binary" remove
[ "$("$redis_cli" -p "$redis_port" EXISTS "$key")" = "0" ]
