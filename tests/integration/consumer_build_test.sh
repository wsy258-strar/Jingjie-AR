#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(mktemp -d /tmp/http-framework-package.XXXXXX)
prefix=$(mktemp -d /tmp/http-framework-prefix.XXXXXX)
consumer_build=$(mktemp -d /tmp/http-framework-consumer.XXXXXX)
trap 'rm -rf "$build_dir" "$prefix" "$consumer_build"' EXIT

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug \
  -DHTTP_FRAMEWORK_WITH_MYSQL=OFF -DHTTP_FRAMEWORK_WITH_REDIS=OFF
cmake --build "$build_dir" --target http_framework -j2
cmake --install "$build_dir" --prefix "$prefix"
cmake -S "$root/tests/consumer" -B "$consumer_build" -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$consumer_build" -j2
port=${CONSUMER_PORT:-28081}
CONSUMER_PORT="$port" "$consumer_build/consumer" >"$consumer_build/consumer.log" 2>&1 &
consumer_pid=$!
trap 'kill "$consumer_pid" 2>/dev/null || true; rm -rf "$build_dir" "$prefix" "$consumer_build"' EXIT
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if body=$(curl -fsS "http://127.0.0.1:$port/health" 2>/dev/null); then
        test "$body" = ok
        echo "PASS: external consumer"
        exit 0
    fi
    sleep 0.1
done
cat "$consumer_build/consumer.log" >&2
exit 1
