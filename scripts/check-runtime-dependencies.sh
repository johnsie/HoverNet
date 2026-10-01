#!/usr/bin/env bash
# Verifies the runtime dependency inventory for the shipped Linux binaries.
#
#   scripts/check-runtime-dependencies.sh BUILD_DIR SOURCE_DIR
#
# For RaceServer (server package) and HoverNetGame2Player + ObjFac1.so (game
# package): every shared library each links against must be listed in
# packaging/debian/runtime-dependencies.tsv, and its Debian package must be named
# in that package's Depends (libgcc-s1 may instead come through libstdc++6).
# Exits 77 (ctest "skipped") if readelf is missing.
set -euo pipefail

build_dir=$(realpath "${1:?build directory required}")
source_dir=$(realpath "${2:?source directory required}")
command -v readelf >/dev/null 2>&1 || { echo "readelf not available; skipped"; exit 77; }
# A sanitizer build links libasan/libubsan; that is not what ships, so skip it.
if readelf -d "$build_dir/RaceServer" 2>/dev/null | grep -qE 'lib(asan|ubsan)'; then
  echo "sanitizer build; the shipped dependency set is checked on the normal build"; exit 77
fi
map="$source_dir/packaging/debian/runtime-dependencies.tsv"
status=0

check() {
  local control=$1; shift
  local depends
  depends=$(grep -E '^Depends:' "$source_dir/packaging/debian/$control" | head -n1)
  for binary in "$@"; do
    [[ -f "$build_dir/$binary" ]] || { echo "missing build output $binary" >&2; status=1; continue; }
    while IFS= read -r soname; do
      package=$(awk -F'\t' -v s="$soname" '!/^#/ && $1 == s { print $2 }' "$map")
      if [[ -z $package ]]; then
        echo "UNDOCUMENTED dependency: $binary needs $soname (add it to runtime-dependencies.tsv and the package's Depends)" >&2
        status=1
      elif [[ $depends != *"$package"* ]] && ! { [[ $package == libgcc-s1 ]] && [[ $depends == *libstdc++6* ]]; }; then
        echo "MISSING from $control Depends: $package (needed by $binary for $soname)" >&2
        status=1
      fi
    done < <(readelf -d "$build_dir/$binary" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
  done
}

check control RaceServer
check game-control HoverNetGame2Player ObjFac1.so
[[ $status -eq 0 ]] && echo "runtime dependencies documented and declared"
exit $status
