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

## Bounded Runs

The player accepts deterministic flags for automated verification:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/linux/HoverNetGame2Player --autoplay --fire --jump --select-weapon --frames 300
```

`--frames N` exits after `N` frames. The flags `--autoplay`, `--fire`, `--jump`, and `--select-weapon` hold their matching controls during the bounded run.
