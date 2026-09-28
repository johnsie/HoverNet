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

The same list is available in-game through **Controls** on the main menu and
the Escape menu, including while browsing the online lobby.

## Display settings

The Settings screen supports windowed/fullscreen mode, master volume, and common
window resolutions from 1024x768 through 2560x1440. Resolution and fullscreen
changes preview immediately; Cancel restores the previous display state. Saved
window dimensions are restored on the next launch, and the window can also be
resized directly while windowed.

UI scale is adjustable from 75% to 150% in the same screen and applies to every
ImGui-based menu. Like resolution and volume, it previews live and rolls back on Cancel.

## Bounded Runs

The player accepts deterministic flags for automated verification:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/linux/HoverNetGame2Player --autoplay --fire --jump --select-weapon --frames 300
```

`--frames N` exits after `N` frames. The flags `--autoplay`, `--fire`, `--jump`, and `--select-weapon` hold their matching controls during the bounded run.
`--controls-reference` opens the Controls screen directly for bounded UI
verification.
