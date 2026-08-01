#!/usr/bin/env bash
set -euo pipefail

binary=$1
if [ -z "${AR_TEST_MYSQL_DATABASE:-}" ]; then
  echo 'SKIP: set AR_TEST_MYSQL_DATABASE to an isolated database to run session status tests'
  exit 77
fi

MYSQL_DATABASE="$AR_TEST_MYSQL_DATABASE" "$binary"
