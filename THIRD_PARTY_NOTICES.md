# Third-party components and licences

What HoverNet contains or links, where it is used, and under which licence. This
is an inventory, not legal advice.

## Known gap: the HoverRace licence text is not in this repository

About 160 source files carry a header saying they are *"Licensed under GrokkSoft
HoverRace SourceCode License v1.0 ... A copy of the license should have been
attached to the package"*, but no copy of that licence is in this repository, and
there is no top-level `LICENSE`. Until the project owner adds the licence text (or
re-licenses the code), the terms for the original HoverRace source, its game
resources and its bundled tracks are not documented here. The same is true of the
legacy `GrokkSoft.url` / `Registration.url` shortcuts under `NetTarget/`.

## Source included in the repository

| Component | Version | Licence | Used for | In the packages |
| --- | --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) (`NetTarget/ThirdParty/imgui`, licence in `LICENSE.txt`) | 1.90.9 | MIT | in-game menus, lobby, settings | compiled into the client |
| `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h` (stb, bundled with ImGui) | as bundled | public domain / MIT | font and text editing for ImGui | compiled into the client |
| ImGui's built-in default font (ProggyClean) | as bundled | MIT-style, see ImGui's `LICENSE.txt` | menu text | compiled into the client |
| OpenAL API headers (`OpenAL/AL`) | 1.1-era | GNU LGPL (headers state the Library GPL) | Win32 legacy client audio API shape; the implementations (`alstub*.cpp`) are HoverNet's own | legacy Win32 client only |
| Original HoverRace code, resources (`ObjFac1.dat`, `NetTarget/Sounds`, `NetTarget/bitmaps`) and the seven bundled tracks | 1.x | **see the known gap above** | the game itself | yes |
| Menu artwork `menu-hovercraft.bmp` | n/a | origin not recorded | main menu | yes |

## Libraries linked at build or run time (not in the repository)

| Component | Licence | Platform | How it arrives |
| --- | --- | --- | --- |
| [SDL2](https://www.libsdl.org/) | zlib | Linux, Windows client | Linux: `libsdl2-2.0-0` (a package dependency). Windows: `SDL2.dll` is shipped in the installer |
| GNU C library, `libstdc++`, `libgcc` | LGPL with runtime exceptions | Linux | `libc6`, `libstdc++6`, `libgcc-s1` (package dependencies) |
| Microsoft Visual C++ 2015-2022 Redistributable (x64) | Microsoft redistribution terms | Windows x64 | shipped inside the x64 installer and installed on demand |
| Microsoft MFC | Microsoft Visual Studio terms | legacy Win32 client | statically/dynamically linked at build |
| `curl` (or `wget`; `curl.exe` on Windows 10+) | curl licence (MIT-style) / GPL | Linux, Windows | **not** linked: run as a separate program to download tracks. Recommended by the game package |

## Build and release tools (not shipped)

CMake, vcpkg, GCC/Clang, MSVC, Inno Setup 6 (the installer builder; its runtime
stub is embedded in the installers it creates), GitLab Runner, GitHub Actions,
`gh`, `dpkg-deb`, Python 3 (scripts and tests only).

## Community tracks

About a thousand community-made tracks are downloadable on demand (not part of the
game packages). They were mirrored from the old OpenHover site; **no licence or
authorship was recorded**, so redistribution rights are unknown. See
[Community tracks](docs/community-tracks.md).

## Keeping this accurate

`scripts/check-runtime-dependencies.sh` (the `HoverNetRuntimeDependencies` test)
fails if a shipped Linux binary links a shared library that is not listed in
`packaging/debian/runtime-dependencies.tsv`, or if the package providing it is
missing from the package's `Depends`. Update this file and that list together
whenever a dependency is added.
