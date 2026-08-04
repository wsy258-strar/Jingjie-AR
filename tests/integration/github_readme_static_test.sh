#!/usr/bin/env bash
set -euo pipefail

readme=README.md
grep -Fq '# 境界AR（Jingjie-AR）' "$readme"
grep -Fq '自研 C++11 HTTP 服务框架' "$readme"
grep -Fq 'A-Frame 360° 全景协作浏览应用' "$readme"
grep -Fq 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release' "$readme"
grep -Fq 'cmake --build build --target ar_server -j2' "$readme"
grep -Fq '不要提交 `.env.arserver`' "$readme"
! grep -Fq 'tests/' "$readme"
! grep -Fq '反向代理' "$readme"
! grep -Fq 'systemctl' "$readme"
! grep -Fq 'certbot' "$readme"
git check-ignore -q docs/sql/jingjie_ar_scene_interactions.sql
git check-ignore -q tests/integration/jingjie_ar_api_test.sh
git check-ignore -q 'docs/Ubuntu-云服务器部署指南（本地不提交）.md'
test -f sql/jingjie_ar_schema.sql
grep -Fq 'sql/jingjie_ar_schema.sql' "$readme"
! grep -Fq 'jingjie_ar_scene_interactions.sql' "$readme"
