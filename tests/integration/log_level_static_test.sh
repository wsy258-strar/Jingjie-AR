#!/usr/bin/env bash
set -euo pipefail

source_file="src/net/EPollPoller.cpp"

rg -Fq 'LOG_DEBUG<<"fd total count:"<<channels_.size();' "$source_file"
rg -Fq 'LOG_DEBUG<<"events happend"<<numEvents;' "$source_file"
! rg -Fq 'LOG_INFO<<"fd total count:"<<channels_.size();' "$source_file"
! rg -Fq 'LOG_INFO<<"events happend"<<numEvents;' "$source_file"
