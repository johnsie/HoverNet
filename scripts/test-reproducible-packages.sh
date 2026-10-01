#!/usr/bin/env bash
# Builds each Debian package twice, a couple of seconds apart, from the binaries
# in BUILD_DIR and fails if the two builds are not byte-identical.
#
#   scripts/test-reproducible-packages.sh BUILD_DIR SOURCE_DIR WORK_DIR
#
# Runs in a scratch tree of symlinks so it never writes into a real dist/.
# Exits 77 (ctest "skipped") if dpkg-deb is not installed.
set -euo pipefail

build_dir=$(realpath "${1:?build directory required}")
source_dir=$(realpath "${2:?source directory required}")
work_dir=$(realpath -m "${3:?work directory required}")

command -v dpkg-deb >/dev/null 2>&1 || { echo "dpkg-deb not available; skipped"; exit 77; }
rm -rf "$work_dir"; mkdir -p "$work_dir"
export SOURCE_DATE_EPOCH=1700000000   # pinned, as CI would; outside git there is no commit time

mkdir -p "$work_dir/tracks"
cp "$source_dir"/NetTarget/Tracks/ClassicH.trk "$work_dir/tracks/Reproducible Test.trk"

for pass in 1 2; do
  tree="$work_dir/pass$pass"
  mkdir -p "$tree"
  ln -s "$source_dir/NetTarget" "$tree/NetTarget"
  ln -s "$source_dir/packaging" "$tree/packaging"
  ln -s "$source_dir/docs" "$tree/docs"
  ln -s "$source_dir/THIRD_PARTY_NOTICES.md" "$tree/THIRD_PARTY_NOTICES.md"
  ln -s "$source_dir/CHANGELOG.md" "$tree/CHANGELOG.md"
  (
    cd "$tree"
    export CI_PROJECT_DIR="$tree"
    packaging/debian/build-game-deb.sh 9.9.9 "$build_dir/HoverNetGame2Player" "$build_dir/ObjFac1.so" >/dev/null
    packaging/debian/build-raceserver-deb.sh 9.9.9 "$build_dir/RaceServer" >/dev/null
    packaging/debian/build-community-tracks-deb.sh 9.9.9 "$work_dir/tracks" "$tree/dist" >/dev/null
  )
  [[ $pass -eq 1 ]] && sleep 3   # a real gap: timestamps must not leak into the package
done

status=0
for deb in "$work_dir"/pass1/dist/*.deb; do
  name=$(basename "$deb")
  if cmp -s "$deb" "$work_dir/pass2/dist/$name"; then
    echo "reproducible: $name"
  else
    echo "NOT reproducible: $name" >&2
    status=1
  fi
done
[[ $(ls "$work_dir"/pass1/dist/*.deb | wc -l) -eq 3 ]] || { echo "expected three packages" >&2; status=1; }
# A pre-release tag must map to a Debian version that sorts BEFORE the final
# release (2.0.0~beta.1 < 2.0.0), or a beta would shadow the release on upgrade.
tree="$work_dir/prerelease"
mkdir -p "$tree"
for link in NetTarget packaging docs; do ln -s "$source_dir/$link" "$tree/$link"; done
ln -s "$source_dir/THIRD_PARTY_NOTICES.md" "$tree/THIRD_PARTY_NOTICES.md"
ln -s "$source_dir/CHANGELOG.md" "$tree/CHANGELOG.md"
(
  cd "$tree"
  export CI_PROJECT_DIR="$tree"
  packaging/debian/build-game-deb.sh v2.0.0-beta.1 "$build_dir/HoverNetGame2Player" "$build_dir/ObjFac1.so" >/dev/null
  packaging/debian/build-raceserver-deb.sh 2.0.0-beta.1 "$build_dir/RaceServer" >/dev/null
)
for deb in "$tree"/dist/hovernet-game_*.deb "$tree"/dist/hovernet-raceserver_*.deb; do
  version=$(dpkg-deb -f "$deb" Version)
  if [[ $version == "2.0.0~beta.1" ]] && dpkg --compare-versions "$version" lt 2.0.0 \
     && dpkg --compare-versions "$version" gt 1.9.9; then
    echo "pre-release version ok: $(basename "$deb") -> $version"
  else
    echo "BAD pre-release version in $(basename "$deb"): $version" >&2
    status=1
  fi
done
exit $status
