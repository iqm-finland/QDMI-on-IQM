#!/usr/bin/env bash

set -u

ctest "$@"
failed=$?
if [[ "$failed" -eq 0 ]]; then
  exit 0
fi

for attempt in 1 2 3; do
  sleep 2
  ctest "$@" --rerun-failed && exit 0
done

exit "$failed"
