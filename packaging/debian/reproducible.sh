# Sourced by the Debian package build scripts so that building the same commit
# twice produces byte-identical packages.
#
# SOURCE_DATE_EPOCH is the reproducible-builds.org convention: if set (CI can pin
# it) it is used; otherwise the time of the checked-out commit; otherwise "now"
# (a source tarball with no git history cannot be reproduced anyway). dpkg-deb
# clamps every file time in the package to it.

if [[ -z ${SOURCE_DATE_EPOCH:-} ]]; then
  SOURCE_DATE_EPOCH=$(git log -1 --format=%ct 2>/dev/null || date +%s)
fi
export SOURCE_DATE_EPOCH

# Gives every file and directory under $1 the pinned time, so nothing depends on
# when the package was staged.
normalize_package_tree() {
  find "$1" -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +
}
