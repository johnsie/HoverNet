# HoverNet 2.0 beta test plan

The roadmap calls for a two-week public beta, then a release-candidate freeze. This
is the plan for running it.

## Entry criteria (before announcing)

- [ ] CI is green on GitLab and GitHub for the beta tag, and the GitHub release is
      published as a **pre-release** with all packages and `SHA256SUMS`.
- [ ] A working lobby server is deployed and reachable at the default address
      (`outiva.com`) running this build, or the announcement says to use a named
      server. (The release process never deploys the server; see
      [Releasing](releasing.md).)
- [ ] `scripts/test-cross-platform-race.sh` passes (Windows x64 <-> Linux, both
      directions).
- [ ] The community track downloads are published (`community-tracks-1..3`) and one
      track downloads from a clean machine.
- [ ] The 24-hour server soak (`scripts/run-raceserver-soak.sh`) is running, or a
      date for it is set (it is a 2.0 release criterion).

## Scope: what testers should cover

| Area | What to check |
| --- | --- |
| Install | clean install, install over an older version, uninstall (settings kept), Windows x64 and Linux `.deb`; legacy Win32 installer still works |
| Local play | start a race on each official track; finish a race; pause menu; post-race screen; quit |
| Controls | keyboard, remapping, controller, persisted after restart |
| Settings | display mode/resolution, volume, UI scale, high contrast, large HUD text, reduced motion |
| Online | connect to the lobby, host, join, chat, start, finish; Windows <-> Linux in one race; reconnect after the server restarts |
| Community tracks | the selector (search, sections), download & start, download & join, "download all", offline behaviour, a free-play track |
| Own server | install the server package, host a race against it |
| Accessibility | tutorial reachable in game; text readable at larger UI scale |

## Severity and triage

- **P0:** crash, hang, data loss, or remotely triggerable server failure. Fix during
  the beta; ships in the next beta build.
- **P1:** a core flow broken (cannot install, cannot start a race, cannot join).
  Fix during the beta.
- **P2:** wrong or confusing behaviour with a workaround. Fix if cheap, otherwise
  document as a known issue.
- **P3:** polish and wishlist. Defer to 2.1.

Reports arrive as GitHub issues. Label them `beta` and one of the severities above;
ask for version, OS, steps, and the track name.

## Rules during the beta

- Only P0 and P1 fixes go to `main` for a beta build (`v2.0.0-beta.N`). New features
  wait for 2.1.
- Every beta build repeats the entry checks that apply to what changed.
- Keep the 32-bit Windows client building and installing: it ships in 2.0 as the
  compatibility fallback (decision of 1 October 2026; revisit for 2.1).

## Exit criteria (release-candidate freeze)

All of the roadmap's 2.0 release criteria:

- [ ] Windows and Linux can host and join each other reliably.
- [ ] Supported Windows binaries are native x64 and do not depend on 32-bit DLLs.
- [ ] All supported race flows have automated coverage or a documented manual test.
- [ ] No known crash, hang, data-loss or remotely exploitable defects (no open P0).
- [ ] The server survives the 24-hour soak test.
- [ ] Clean installations contain everything needed to play.
- [ ] Settings and error messages do not require source-tree knowledge.
- [ ] Every bundled track completes correctly in local and online play.
- [ ] Release and rollback procedures have both been exercised.

Plus: no open P1 for two consecutive beta builds, and the production-server
migration rehearsal done. Then tag `v2.0.0-rc.1`, freeze, and release `v2.0.0`.

## Record results

Keep a short log per beta build in the release notes: what changed, which entry
checks were run, and the count of reports by severity.
