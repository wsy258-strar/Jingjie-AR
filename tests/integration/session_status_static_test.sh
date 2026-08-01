#!/usr/bin/env bash
set -euo pipefail

source_file=src/db/SessionDAO.cpp
grep -Fq 'INSERT INTO sessions (session_token, user_id, scene_id, status)' "$source_file"
grep -Fq 'VALUES (?, ?, ?, 1)' "$source_file"
grep -Fq "UPDATE sessions SET scene_id = '', status = 0 WHERE id = ?" "$source_file"
