#!/usr/bin/env bash
set -euo pipefail

schema=sql/jingjie_ar_schema.sql
test -f "$schema"
for table in users sessions scene_likes scene_comments; do
  grep -Fq "CREATE TABLE IF NOT EXISTS $table" "$schema"
done
for table in exhibition_statistics artwork_likes artwork_comments; do
  grep -Fq "CREATE TABLE IF NOT EXISTS $table" "$schema"
done
grep -Fq 'UNIQUE KEY uq_users_username (username)' "$schema"
grep -Fq 'UNIQUE KEY uq_sessions_token (session_token)' "$schema"
users_definition="$(sed -n '/CREATE TABLE IF NOT EXISTS users (/,/) ENGINE=InnoDB/p' "$schema")"
grep -Fq 'id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT' <<<"$users_definition"
grep -Fq 'PRIMARY KEY (artwork_id, user_id)' "$schema"
grep -Fq 'KEY idx_artwork_comments_page (artwork_id, id)' "$schema"
for table in artwork_likes artwork_comments; do
  definition="$(sed -n "/CREATE TABLE IF NOT EXISTS $table (/,/) ENGINE=InnoDB/p" "$schema")"
  grep -Fq 'user_id BIGINT UNSIGNED NOT NULL' <<<"$definition"
  grep -Fq 'FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE' <<<"$definition"
done
grep -Fq 'sql/jingjie_ar_schema.sql' README.md
! test -e sql/jingjie_ar_scene_interactions.sql
