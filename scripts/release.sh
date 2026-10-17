#!/usr/bin/env bash
# Releases a version from this machine, following the project's release rules (CLAUDE.md, docs/releasing.md): the full local test suite, a
# green GitHub Actions run for exactly that commit, a green GitLab pipeline (you confirm it), main only by fast-forward, and a new
# annotated tag pushed to both remotes. Pushing the tag DEPLOYS TO PRODUCTION (web client, race server, 3.0 server) through GitLab.
#
#   scripts/release.sh 2.1.0                      # releases the current commit (HEAD) as v2.1.0
#   scripts/release.sh 2.1.0 --commit f5baa35     # or a named commit
#   scripts/release.sh 2.1.0 --dry-run            # checks everything and prints what it would do, changes nothing
# Options: --skip-tests (you ran the full ctest yourself just now)   --no-wait (do not wait for GitHub Actions: you checked it)
#
# It stops at the first problem. It never moves or reuses a tag, never force-pushes, and asks you to type the version before the final step.
set -euo pipefail

version="${1:-}"; shift || true
commit=""; dry=no; skip_tests=no; no_wait=no
while [ $# -gt 0 ]; do
  case "$1" in
    --commit) commit="${2:?}"; shift 2 ;;
    --dry-run) dry=yes; shift ;;
    --skip-tests) skip_tests=yes; shift ;;
    --no-wait) no_wait=yes; shift ;;
    -h|--help) sed -n 2,14p "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 X.Y.Z [--commit SHA] [--dry-run] [--skip-tests] [--no-wait]" >&2; exit 2; }
tag="v$version"
cd "$(dirname "$0")/.."
run() { echo "+ $*"; [ "$dry" = yes ] || "$@"; }
die() { echo "STOP: $*" >&2; exit 1; }

branch="$(git rev-parse --abbrev-ref HEAD)"
sha="$(git rev-parse "${commit:-HEAD}^{commit}")"
short="${sha:0:8}"
echo "Releasing $tag from commit $short (branch $branch)"

echo "-- checks"
git diff --quiet && git diff --cached --quiet || die "tracked files have uncommitted changes: commit or stash them first"
grep -q "^## ${version} " CHANGELOG.md || die "CHANGELOG.md has no '## ${version}' heading: add it (and move Unreleased under it) first"
git merge-base --is-ancestor "$sha" HEAD || die "$short is not part of the current branch"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "tag $tag already exists locally (never reuse a tag)"
for r in origin gitlab; do
  git ls-remote --tags "$r" "refs/tags/$tag" | grep -q . && die "tag $tag already exists on $r (never reuse a tag)"
done
git fetch -q origin main 2>/dev/null || true; git fetch -q gitlab main 2>/dev/null || true
for r in origin gitlab; do
  if git rev-parse -q --verify "refs/remotes/$r/main" >/dev/null; then
    git merge-base --is-ancestor "refs/remotes/$r/main" "$sha" || die "$r/main has commits that $short does not contain: main cannot be fast-forwarded to it (merge or rebase first)"
  fi
done
echo "ok: changelog, tag unused on both remotes, main can be fast-forwarded"

if [ "$skip_tests" = no ]; then
  echo "-- the full local test suite (a few minutes; the machine should be otherwise idle)"
  [ -d build ] || die "no build directory: build first (cmake --build build)"
  if [ "$dry" = yes ]; then echo "+ ctest --test-dir build -j2 --output-on-failure"; else ctest --test-dir build -j2 --output-on-failure || die "the local test suite failed"; fi
fi

echo "-- pushing the commit to both remotes so CI can check it (this deploys nothing)"
run git push origin "$sha:refs/heads/$branch"
run git push gitlab "$sha:refs/heads/$branch"

if [ "$no_wait" = no ] && [ "$dry" = no ]; then
  echo "-- waiting for GitHub Actions on $short (up to 90 minutes)"
  for _ in $(seq 1 90); do
    json="$(curl -s -m 20 "https://api.github.com/repos/johnsie/HoverNet/actions/runs?head_sha=$sha" || true)"
    state="$(printf '%s' "$json" | python3 -c "
import sys,json
try: d=json.load(sys.stdin)
except Exception: print('unknown'); raise SystemExit
runs=d.get('workflow_runs',[])
if not runs: print('none')
elif any(r['status']!='completed' for r in runs): print('running')
elif all(r['conclusion']=='success' for r in runs): print('success')
else: print('failed')" )"
    echo "   $(date +%T) GitHub Actions: $state"
    [ "$state" = success ] && break
    [ "$state" = failed ] && die "GitHub Actions failed for $short: https://github.com/johnsie/HoverNet/actions"
    sleep 60
  done
  [ "$state" = success ] || die "GitHub Actions did not finish green for $short: check https://github.com/johnsie/HoverNet/actions"
elif [ "$dry" = yes ]; then
  echo "+ (would wait for GitHub Actions to be green for $sha)"
fi

if [ "$dry" = no ]; then
  echo "-- GitLab: open https://gitlab.outiva.com/devteam/hovernet/-/pipelines and look at the pipeline for $short"
  read -r -p "Is it green (build, tests, web smoke test)? [y/N] " ans; [[ "$ans" =~ ^[Yy] ]] || die "not confirmed"
  echo
  echo "FINAL STEP. Pushing $tag will deploy to PRODUCTION: the web client (hovernet.outiva.com), the 2.0 race server, and the 3.0 server."
  read -r -p "Type the version ($version) to continue: " typed; [ "$typed" = "$version" ] || die "not confirmed"
fi

echo "-- main (fast-forward only) and the tag"
run git checkout main
run git merge --ff-only "$sha"
run git push origin main
run git push gitlab main
run git tag -a "$tag" -m "HoverNet $version" "$sha"
run git push origin "$tag"
run git push gitlab "$tag"
run git checkout "$branch"
echo
echo "Done. Watch: https://github.com/johnsie/HoverNet/actions  and  https://gitlab.outiva.com/devteam/hovernet/-/pipelines"
echo "GitHub builds the installers and publishes the release only when every job passes. After the GitLab deploy, open https://hovernet.outiva.com and check the version in the main menu."
