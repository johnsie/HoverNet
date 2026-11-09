# Linux Game2 Player

## Build

Requirements: CMake, a C++14 compiler, and SDL2 development files.

```bash
cmake -S . -B build/linux -DBUILD_TESTING=ON
cmake --build build/linux --target HoverNetGame2Player --parallel 2
```

Run from the repository root so the player can load `NetTarget/ObjFac1.dat` and `ObjFac1.so`:

```bash
./build/linux/HoverNetGame2Player
```

## Controls

| Input | Action |
| --- | --- |
| Left Shift / Right Shift | Accelerate |
| Left / Right | Steer; select hovercraft during the countdown |
| Up | Jump |
| Down | Brake / reverse |
| Left Ctrl or Right Ctrl | Fire missile |
| Tab | Select weapon |
| F3 | Following camera |
| F4 | Cockpit camera |
| F5 | Player list |
| F6 | More messages |
| Insert / Delete | Zoom in / out |
| Page Up / Page Down | Scroll display |
| + / - | Decrease / increase display margin |
| Home | Reset camera |
| Escape | Open the in-race menu; leave an online race or quit |

Standard SDL-compatible controllers are supported during races: use the left
stick to steer, right trigger to accelerate, left trigger to brake or reverse,
A to jump, X to fire, Y to select a weapon, and Start to open the pause menu.
Menus support the D-pad, A to confirm, and B to go back where applicable.

The main menu has four choices: **Play Offline**, **Race Online**, **Leaderboards** and **Garage**, and a **Settings** button. Settings opens a hub with
Game settings, Controls, Watch a replay and **Tutorial**. The tutorial is a one-lap practice race on the first track with a coach: eight lessons
(accelerate, steer, brake, jump, fire, read the HUD, finish a lap), each finished by what you actually do, shown in the HUD font over the race. Finishing it
stores `tutorial_complete` beside the other per-user settings; until then the main menu points new players to it. `HoverNetGame2Player --play --tutorial`
starts it directly. The lesson logic is `NetTarget/LinuxClient/Tutorial.cpp` (test `HoverNetTutorial`).

The keyboard and controller bindings are available in-game through **Controls**
on the main menu and the Escape menu, including while browsing the online lobby.
Select any gameplay keyboard binding on that screen and press a new key to
change it immediately. Jump, Fire, and Select Weapon controller buttons can be
changed the same way. Steering, accelerate, and brake can each use any standard
stick or trigger axis; their direction can be inverted independently and the
shared analog deadzone is adjustable. Escape cancels keyboard capture, and
**Reset all control defaults** restores the original layout. Bindings are stored
in `$XDG_CONFIG_HOME/hovernet/keyboard_bindings` and `controller_bindings` (or
the equivalent files under `~/.config/hovernet/`). Existing three-button
controller files remain valid and are upgraded when next saved. The Controls
screen reports whether SDL recognizes a connected controller, keeps Start
reserved for pause, and Escape cancels either keyboard or controller capture
before it can close the screen. Assigning a key or controller button already
used by another gameplay action swaps the two bindings, so one input never
silently triggers multiple actions.

## Display settings

The Settings screen supports windowed/fullscreen mode, master volume, a separate
mute control, and common window resolutions from 1024x768 through 2560x1440.
Muting preserves the chosen volume for later unmuting; both controls preview live
and Cancel restores the previous audio state. Resolution and fullscreen changes
preview immediately; Cancel restores the previous display state. Saved
window dimensions are restored on the next launch, and the window can also be
resized directly while windowed.

UI scale is adjustable from 75% to 150% in the same screen and applies to every
ImGui-based menu. Like resolution and volume, it previews live and rolls back on
Cancel. **Large HUD text** is a separate accessibility option: it keeps race
status at the bitmap font's native size and splits dense information across
shorter lines so it is not automatically shrunk to fit. **Reduced motion**
freezes the main menu's moving speed streaks while preserving its racing scene
and all navigation cues. **High contrast menus** use blacker surfaces, white
primary text, brighter secondary text and borders, and stronger focus/selection
states throughout the ImGui interface. **Restore defaults** previews the
built-in identity, server, audio, display, and accessibility defaults together;
those changes are only persisted by Save, while Cancel restores the exact state
from before Settings was opened.

## Bounded Runs

The player accepts deterministic flags for automated verification:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/linux/HoverNetGame2Player --autoplay --fire --jump --select-weapon --frames 300
```

`--frames N` exits after `N` frames. The flags `--autoplay`, `--fire`, `--jump`, and `--select-weapon` hold their matching controls during the bounded run.
`--controls-reference` opens the Controls screen directly for bounded UI
verification.
