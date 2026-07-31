#!/usr/bin/env bash
set -euo pipefail

source_root=${1:?usage: provision_krpano.sh /path/to/pano/html/assets/krp}
test -f "$source_root/1.19-pr10/player_krp_v2.js"
test -f "$source_root/player_offline.xml"
install -d WebApps/ARServer/www/assets/krp/runtime
install -m 0644 "$source_root/1.19-pr10/player_krp_v2.js" \
  WebApps/ARServer/www/assets/krp/runtime/player_krp_v2.js
install -m 0644 "$source_root/player_offline.xml" \
  WebApps/ARServer/www/assets/krp/runtime/player_offline.xml
