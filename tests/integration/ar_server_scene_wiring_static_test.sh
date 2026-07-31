#!/usr/bin/env bash
set -euo pipefail

require_contains() {
  local file="$1"
  local text="$2"
  if ! grep -Fq "$text" "$file"; then
    printf 'missing scene wiring in %s: %s\n' "$file" "$text" >&2
    return 1
  fi
}

require_absent() {
  local file="$1"
  local text="$2"
  if grep -Fq "$text" "$file"; then
    printf 'obsolete scene wiring remains in %s: %s\n' "$file" "$text" >&2
    return 1
  fi
}

require_contains WebApps/ARServer/include/ARServer.h 'class SceneHandlers;'
require_contains WebApps/ARServer/include/ARServer.h 'SceneHandlers* scenes'
require_contains WebApps/ARServer/src/ARServer.cpp 'SceneHandlers* scenes'
require_contains WebApps/ARServer/src/ARServer.cpp 'scenes->list(request, response);'
require_contains WebApps/ARServer/src/ARServer.cpp 'scenes->get(request, response);'
require_absent WebApps/ARServer/src/ARServer.cpp 'interactions->detail'

require_contains WebApps/ARServer/src/main.cpp '#include <catalog/ExhibitionCatalog.h>'
require_contains WebApps/ARServer/src/main.cpp \
  'ar::ExhibitionCatalog::load(config.exhibitionConfig, config.staticRoot,'
require_contains WebApps/ARServer/src/main.cpp 'ar::SceneHandlers sceneHandlers(catalog.get());'

ar_server_cmake="$(
  awk '
    /^add_executable\(ar_server$/ { inside = 1 }
    inside { print }
    inside && /^endif\(\)$/ { exit }
  ' CMakeLists.txt
)"
if ! grep -Fq 'WebApps/ARServer/src/catalog/ExhibitionCatalog.cpp' <<<"$ar_server_cmake"; then
  printf 'ar_server target does not compile ExhibitionCatalog.cpp\n' >&2
  exit 1
fi
if ! grep -Fq 'target_include_directories(ar_server PRIVATE' <<<"$ar_server_cmake" ||
   ! grep -Fq '${CMAKE_SOURCE_DIR}/third_party' <<<"$ar_server_cmake"; then
  printf 'ar_server target does not include vendored JSON headers\n' >&2
  exit 1
fi
