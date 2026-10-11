#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cnrf_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/cnrf-tests.XXXXXX")
trap 'rm -rf "$cnrf_test_dir"' EXIT
for source in tests/test_*.cpp; do
  binary="$cnrf_test_dir/$(basename "$source" .cpp)"
  if [ "${source#tests/test_proxy_}" != "$source" ]; then
    c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -pthread \
      -DUSE_ECODAN_CNRF_PROXY -I tests/stubs -I components/ecodan \
      "$source" components/ecodan/cnrf_proxy.cpp -o "$binary"
  else
    c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
      -I components/ecodan "$source" -o "$binary"
  fi
  "$binary"
done
