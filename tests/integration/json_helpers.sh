#!/usr/bin/env bash

# Python's standard library is available on supported Linux hosts and keeps the
# integration scripts runnable when jq is not installed.  Expressions are
# fixed literals in repository-owned scripts, never caller input.
json_get() {
  local key=$1
  python3 -c 'import json, sys; value = json.load(sys.stdin)[sys.argv[1]]; print(value)' "$key"
}

json_check() {
  local expression=$1
  python3 -c 'import json, sys; data = json.load(sys.stdin); scope = {"data": data, "isinstance": isinstance, "int": int, "str": str}; assert eval(sys.argv[1], scope, scope)' "$expression"
}
