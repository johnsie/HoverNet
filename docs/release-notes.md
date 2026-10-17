## HoverNet 2.2

### What's new in 2.2
- **One way into online play.** **ONLINE MULTIPLAYER** (main menu, pause menu, results screen, and in the browser) connects to the HoverNet server by itself: you type nothing but, once, your name. The lobby has the classic layout (game list, race details with the track map, actions, who is in the lobby, and lobby chat), with private races, one-player time trials and watching. The race list refreshes by itself. If the HoverNet server is not available, the classic lobby opens instead. A friend's or a club's server is chosen in **Settings** (*HoverNet server*). See [Playing on a HoverNet server](playing-on-servers.md).
- **Finishing no longer takes the race away.** You are told to press **Enter** for your results; keep driving and chatting as long as you like.
- **Chat** in the lobby and in races on the new servers, during and after the race.
- **Spectator camera:** **[** and **]** (and Left/Right when watching a live race) switch between the craft.
- **Fixed:** the web client no longer freezes when opening the online lobby (2.1.0 and 2.1.1 shipped fixes for the browser build).
- **For server owners:** the `hovernet-server` package (deployed from the GitLab tag pipeline), graceful restarts that let races finish, the browser path over WebSocket, replay verification and leaderboards (`HoverNetReplay`), an Ed25519 signing tool, and an operator guide. The old `hovernet-raceserver` is unchanged.
- The Linux packages carry the licence text, as the HoverRace licence requires.

### Known limits of this release
- No one has clicked through the new screen on a real phone or on Windows by hand, and nobody has installed the Windows installer by hand
  (the CI build builds and validates it, including the licence files).
- The browser reaches the new servers through a TLS proxy in front of them; the proxy was tried with a test proxy, not the production web server.
- Nothing is signed yet: there is no project signing key.

## HoverNet 2.0

HoverNet 2.0 is out of beta and is now the current release. Thank you to everyone
who tried the beta.

### &#128172; Your feedback is still welcome
**You can keep posting GitHub issues and giving feedback.** Found a bug, hit something
confusing, or have an idea? Open an issue at
<https://github.com/johnsie/HoverNet/issues>. It takes a minute and it genuinely
shapes what we fix next. Please include the HoverNet version (shown in the window
title), your operating system, what you did, and the track name if it matters.

### What's in 2.0
- One shared client for **Windows (x64)** and **Linux**: consistent menus, settings,
  pause menu, lobby and post-race screens; remappable keyboard and controller
  controls; display and audio options; an improved HUD; an in-game tutorial; and
  accessibility options (UI scale, high contrast, large HUD text, reduced motion).
- **Online races** through the HoverNet lobby with a hardened race server. Windows and
  Linux players can race together. The redesigned lobby shows you and everyone else
  who is online, a full race summary, and a scrolling chat.
- **Seven official tracks** and about a thousand **community tracks** from the classic
  HoverRace library, downloaded on demand when you pick one or join a race that uses one.
- The **legacy 32-bit Windows client** is still included
  (`HoverNet-*-win32-setup.exe`) as a compatibility fallback.

### Installing
- **Windows:** run `HoverNet-*-windows-x64-setup.exe`. Verify downloads with `SHA256SUMS`.
- **Linux (Debian/Ubuntu):** `sudo apt install ./hovernet-game_*.deb`, then run `hovernet`.
- **Running your own server:** `sudo apt install ./hovernet-raceserver_*.deb`, or the
  `hovernet-raceserver_*_win32.zip` on Windows. In the game, *Settings* lets you point
  the lobby at your server.

### Upgrading from the beta
Install over it. Your settings and saved choices are kept. A beta package sorts
*before* this release, so `apt` treats 2.0 as an upgrade.

### Known limitations
- **Community tracks vary a lot.** They were made by many people over many years and
  are used exactly as published: some start you facing a wall, some have no finish line
  (battle, tag and sports arenas, marked *free play*), and a few are odd or unfinished.
  Their licensing was never recorded.
- Online movement is relayed between players, not server-authoritative (planned for a
  later release).
- The race server's **24-hour soak test is still running** for this build; results will
  be published when it finishes.
- Community track downloads need `curl` (or `wget`) on Linux and an internet connection;
  Windows 10 and later include `curl.exe`.

### Going back
Your settings are kept when you update or uninstall. To return to an earlier release,
install that release over this one; see
[Releasing](https://github.com/johnsie/HoverNet/blob/main/docs/releasing.md).
