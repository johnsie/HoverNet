# Menu flow: modern and classic

*Added 17 November 2026.* The game has two menu flows. **Modern** is the default; **Classic** is the menus as they were. Choose in Settings ("Modern menus"), or set
`HOVERNET_MENU_STYLE=classic` (or `modern`), which overrides the setting. The setting is the file `menu_style` in the config folder (`classic` or `modern`).

| Step | Modern | Classic |
| --- | --- | --- |
| Main menu | A **Continue** card (the last local race: track, laps, weapons, your best lap) with one big RACE button; RACE ONLINE; CHOOSE A TRACK; Leaderboards, Garage, Tutorial, Settings, Quit | RACE ONLINE; PRACTICE OFFLINE; the same four doors; Quit |
| Choosing a race | **One race screen** (`racehub.rml`): the track as a card (plan with the pads in colour, briefing, your best), your own craft turning, laps and weapons as chips, CHANGE TRACK (the track browser), EDIT IN GARAGE (and back), one RACE button, over the live 3D track | The Local Race Setup form (track list, laps slider, weapons box, Start Race) |
| After an offline race | RACE AGAIN (first), CHOOSE ANOTHER TRACK, MAIN MENU; F2 = race again | MAIN MENU; F2 = main menu |
| After an online race | RETURN TO LOBBY, MAIN MENU (unchanged) | The same |
| Online lobby | Unchanged so far (see the roadmap: a waiting-room lobby and "Race now" are not built) | The server lobby |

Both flows use the same race loading, track browser, Garage, downloader and server code, so a fix lands in both. Only the screens that choose between them differ
(`RunRaceScreen` against `RunLocalRaceSetup`, and the Continue card and Race again). Bounded test runs on the desktop use the older ImGui screens, which are the classic ones.
`scripts/test-modern-flow.sh` (ctest `HoverNetModernFlow`) checks the modern flow; `scripts/test-rml-menu.sh` checks the classic one.
