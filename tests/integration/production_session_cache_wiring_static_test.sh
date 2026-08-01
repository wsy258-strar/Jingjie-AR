#!/usr/bin/env bash
set -euo pipefail

main_file="WebApps/ARServer/src/main.cpp"

require_contains() {
  local text="$1"
  if ! grep -Fq "$text" "$main_file"; then
    printf 'production session cache wiring missing from %s: %s\n' "$main_file" "$text" >&2
    return 1
  fi
}

require_contains '#include <services/CachedSessionStore.h>'
require_contains '#include <cache/SessionCache.h>'
require_contains 'new SessionCache(redisPool.get())'
require_contains 'new ar::SessionCacheAdapter(sessionCache.get())'
require_contains 'new TaskWorkerPool(config.cacheWorkers, 128)'
require_contains 'new ar::CachedSessionStore(sessionStore.get(),'
require_contains 'effectiveSessionStore = cachedSessionStore.get();'
require_contains 'ar::SessionService sessionService(effectiveSessionStore);'

cache_worker_shutdown_line="$(grep -nF 'sessionCacheWorkers->shutdown();' "$main_file" | cut -d: -f1 || true)"
db_worker_reset_line="$(grep -nF 'dbWorkers.reset();' "$main_file" | cut -d: -f1 || true)"
cache_worker_reset_line="$(grep -nF 'sessionCacheWorkers.reset();' "$main_file" | cut -d: -f1 || true)"
if [[ -z "$cache_worker_shutdown_line" || -z "$db_worker_reset_line" ||
      -z "$cache_worker_reset_line" ||
      "$cache_worker_shutdown_line" -ge "$db_worker_reset_line" ||
      "$db_worker_reset_line" -ge "$cache_worker_reset_line" ]]; then
  printf 'session cache workers must stop before DB drain and remain alive until it completes\n' >&2
  exit 1
fi
