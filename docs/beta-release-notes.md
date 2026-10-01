## HoverNet 2.0 public beta

This is a **beta** of HoverNet 2.0. It is feature-complete for 2.0 and open for
testing, but it can still have bugs, and we want to hear about them. It is
published as a *pre-release* and is not marked as the latest release.

### What's in it
- One shared client for **Windows (x64)** and **Linux**: consistent menus, settings,
  pause menu, lobby and post-race screens; remappable keyboard and controller
  controls; display and audio options; an improved HUD; an in-game tutorial; and
  accessibility options (UI scale, high contrast, large HUD text, reduced motion).
- **Online races** through the HoverNet lobby, with a hardened race server
  (versioned protocol, rate limits, input validation).
- **Seven official tracks** and about a thousand **community tracks** from the classic
  HoverRace library. Community tracks appear below the official ones and download
  on demand (a few hundred KB each) when you pick one or join a race that uses one.
- The **legacy 32-bit Windows client** is still included
  (`HoverNet-*-win32-setup.exe`) as a compatibility fallback.

### Installing
- **Windows:** run `HoverNet-*-windows-x64-setup.exe` (the 32-bit installer is for
  the legacy client). Verify downloads with `SHA256SUMS`.
- **Linux (Debian/Ubuntu):** `sudo apt install ./hovernet-game_*.deb`, then run
  `hovernet`.
- **Running your own server:** `sudo apt install ./hovernet-raceserver_*.deb`, or the
  `hovernet-raceserver_*_win32.zip` on Windows. In the game, *Settings* lets you
  point the lobby at your server.

### Please test
Start with the checklist in the
[beta test plan](https://github.com/johnsie/HoverNet/blob/main/docs/beta-test-plan.md):
a clean install, a local race on a few tracks, joining an online race (especially
between Windows and Linux), picking a community track, changing settings and
controls, and uninstalling.

### Known limitations
- **Community tracks vary a lot.** They were made by many people over many years and
  are used exactly as published. Some start you facing a wall, some have no finish
  line (battle, tag and sports arenas, marked *free play*), and a few are odd or
  unfinished. Their licensing was never recorded.
- Online play relies on the race server being reachable. Multiplayer movement is
  relayed between players, not server-authoritative (planned for a later release).
- The race server's 24-hour soak test has not yet been run for this build.
- Community track downloads need `curl` (or `wget`) on Linux and an internet
  connection; Windows 10 and later include `curl.exe`.

### Reporting problems
Open an issue at <https://github.com/johnsie/HoverNet/issues> and include: the
HoverNet version (shown in the window title), your operating system, what you
did, what you expected, and the track name if it matters. For crashes, say whether
it happened online or in a local race.

### Upgrading and going back
Your settings are kept when you install over an older version and when you
uninstall. To go back to an earlier release, install that release over this one; see
[Releasing](https://github.com/johnsie/HoverNet/blob/main/docs/releasing.md).
