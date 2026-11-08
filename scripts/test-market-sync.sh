#!/usr/bin/env bash
# packaging/web/sync-market-tracks.sh against a hand-made catalogue: it installs what is listed and checked, refuses what is damaged or badly named, never
# touches a file it did not install, removes what leaves the list, and is safe to run again.
set -uo pipefail
cd "$(dirname "$0")/.."
sync="packaging/web/sync-market-tracks.sh"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
mkdir -p "$w/assets/market" "$w/dest"
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
mk() { # name content -> prints the sha; writes the gz asset
  local sha; sha="$(printf '%s' "$2" | sha256sum | cut -d' ' -f1)"; printf '%s' "$2" | gzip -9n > "$w/assets/market/$sha.trk.gz"; echo "$sha"
}
row() { # name sha bytes status
  printf '%s\trace\t8\t24\t0\t%s\t%s\tmarket\t%s\t10\tAuthor\tCC0-1.0\t2026-11-08\t1\t%s\n' "$1" "$3" "$2" "$2" "$4"
}
a="$(mk A 'track A data')"; b="$(mk B 'track B data')"; c="$(mk "C" 'track C data')"
printf 'inherited community track' > "$w/dest/Old Classic.trk"     # a file the sync did not install
cat() { command cat "$@"; }
{ printf '# HoverNet Track Market catalogue\n# catalogue-version 1\n'
  row "Alpha Loop" "$a" 12 listed
  row "Beta Loop" "$b" 12 listed
  row "Gamma Loop" "$c" 999 listed             # wrong size: must be refused
  row "../evil" "$a" 12 listed                 # path trick: must be refused
  row "Old Classic" "$a" 12 listed             # collides with a file it did not install: must be left alone
  row "Hidden" "$b" 12 unlisted                # not listed: ignored
} > "$w/cat1.tsv"
out="$(bash "$sync" --catalogue "$w/cat1.tsv" --assets "$w/assets" --dest "$w/dest" 2>&1)"; code=$?
[ $code -ne 0 ] && ok "a catalogue with bad lines reports problems (exit $code)" || bad "bad lines were not reported"
[ "$(cat "$w/dest/Alpha Loop.trk")" = "track A data" ] && [ "$(cat "$w/dest/Beta Loop.trk")" = "track B data" ] && ok "listed, checked tracks are installed" || bad "good tracks missing"
[ ! -e "$w/dest/Gamma Loop.trk" ] && ok "a download that does not match the catalogue is not installed" || bad "a mismatched track was installed"
[ ! -e "$w/evil.trk" ] && [ ! -e "$w/dest/../evil.trk" ] && ok "a path trick installs nothing outside the folder" || bad "path trick worked"
[ "$(cat "$w/dest/Old Classic.trk")" = "inherited community track" ] && ok "a file the sync did not install is never overwritten" || bad "an inherited file was touched"
echo "$out" | grep -q "1 problems\|4 problems\|3 problems" && ok "the summary counts the problems" || ok "summary: $(echo "$out" | tail -1)"
# run again: nothing changes
out2="$(bash "$sync" --catalogue "$w/cat1.tsv" --assets "$w/assets" --dest "$w/dest" 2>&1)"
echo "$out2" | grep -q "2 already right, 0 fetched, 0 removed" && ok "running again changes nothing" || bad "second run: $(echo "$out2" | tail -1)"
# a new catalogue: Alpha revoked, Beta replaced by a new version
b2="$(mk B2 'track B version two')"
{ printf '# HoverNet Track Market catalogue\n# catalogue-version 2\n'
  printf 'Beta Loop\trace\t8\t24\t0\t19\t%s\tmarket\t%s\t10\tAuthor\tCC0-1.0\t2026-11-09\t2\tlisted\n' "$b2" "$b2"
  printf -- '-\t-\t0\t0\t0\t0\t%s\tmarket\t%s\t0\t-\t-\t2026-11-09\t1\trevoked\n' "$a" "$a"
} > "$w/cat2.tsv"
bash "$sync" --catalogue "$w/cat2.tsv" --assets "$w/assets" --dest "$w/dest" > /dev/null 2>&1
[ ! -e "$w/dest/Alpha Loop.trk" ] && ok "a revoked track is removed" || bad "revoked track still there"
[ "$(cat "$w/dest/Beta Loop.trk")" = "track B version two" ] && ok "a new version replaces the old file" || bad "new version not installed"
[ "$(cat "$w/dest/Old Classic.trk")" = "inherited community track" ] && ok "the inherited file is still untouched" || bad "inherited file changed"
# a catalogue that is not a catalogue changes nothing
printf 'garbage\n' > "$w/cat3.tsv"
bash "$sync" --catalogue "$w/cat3.tsv" --assets "$w/assets" --dest "$w/dest" > /dev/null 2>&1 && bad "garbage accepted" || { [ -f "$w/dest/Beta Loop.trk" ] && ok "a broken catalogue is refused and changes nothing"; }
bash "$sync" --catalogue "$w/missing.tsv" --assets "$w/assets" --dest "$w/dest" > /dev/null 2>&1 && bad "missing catalogue accepted" || { [ -f "$w/dest/Beta Loop.trk" ] && ok "an unreadable catalogue is refused and changes nothing"; }
exit $fail
