# Third-party components and licences

What HoverNet contains or links, where it is used, and under which licence. This
is an inventory, not legal advice.

## HoverRace-derived code and resources

HoverNet is derived from the original HoverRace source code. The repository now
includes the **GrokkSoft HoverRace SourceCode License v1.0 (November 29, 2008)**
as the top-level [`LICENSE`](LICENSE). Source files that identify themselves as
licensed under that licence remain subject to its terms.

The GrokkSoft licence permits redistribution and use in source and binary forms,
with or without modification, subject to its conditions. Among those conditions:

- source redistributions must retain the copyright notice, licence conditions and
  disclaimer;
- binary redistributions must reproduce the copyright notice, licence conditions
  and disclaimer in the documentation and/or other materials supplied with the
  distribution;
- Richard Langlois and GrokkSoft inc. may not be used to endorse or promote a
  derived product without prior written permission;
- the software or its derivatives may not be used for commercial activities
  without prior written permission from the copyright holders; and
- modified files must carry prominent notices stating that they were changed and
  the date of the change.

The complete licence text in [`LICENSE`](LICENSE), rather than this summary,
governs the HoverRace-derived portions of the project.

### Licensing boundaries

The top-level `LICENSE` documents the licence inherited from HoverRace. It should
**not** be read as a claim that every independently authored or third-party file in
this repository was originally published by GrokkSoft under that licence.

Original HoverRace code and original HoverRace resources included with HoverNet,
including `ObjFac1.dat`, `NetTarget/Sounds`, `NetTarget/bitmaps`, and the seven
bundled legacy tracks, are treated by this project as HoverRace-derived material.
This inventory does not independently establish the copyright provenance of every
individual resource; where a resource has uncertain provenance it is called out
below.

New HoverNet code and other material may contain its own copyright or licence
notices. Third-party components retain their respective licences as listed in this
file. Nothing in HoverNet's top-level `LICENSE` overrides those third-party terms.

## Source included in the repository

| Component | Version | Licence | Used for | In the packages |
| --- | --- | --- | --- | --- |
| Original HoverRace-derived source and resources (`ObjFac1.dat`, `NetTarget/Sounds`, `NetTarget/bitmaps`) and the seven bundled legacy tracks | 1.x | GrokkSoft HoverRace SourceCode License v1.0; see [`LICENSE`](LICENSE) and provenance note above | the game itself | yes |
| [Dear ImGui](https://github.com/ocornut/imgui) (`NetTarget/ThirdParty/imgui`, licence in `LICENSE.txt`) | 1.90.9 | MIT | in-game menus, lobby, settings | compiled into the client |
| `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h` (stb, bundled with ImGui) | as bundled | public domain / MIT | font and text editing for ImGui | compiled into the client |
| ImGui's built-in default font (ProggyClean) | as bundled | MIT-style, see ImGui's `LICENSE.txt` | menu text | compiled into the client |
| OpenAL API headers (`OpenAL/AL`) | 1.1-era | GNU LGPL (headers state the Library GPL) | Win32 legacy client audio API shape; the implementations (`alstub*.cpp`) are HoverNet's own | legacy Win32 client only |
| Menu artwork `menu-hovercraft.bmp` | n/a | origin not recorded; rights/provenance need confirmation | main menu | yes |

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

Their availability from a historical community archive should not be interpreted
as a grant of redistribution rights. Keep them separate from release packages
unless their licensing/provenance is established.

## Distribution and commercial use

The GrokkSoft HoverRace SourceCode License v1.0 expressly requires prior written
permission from the copyright holders for commercial activities involving the
software or its derivatives. Distribution through a commercial platform should
therefore be reviewed against that restriction and any written permission obtained
from the copyright holders before release. This file does not assert that such
permission has been obtained.

## Modified-file requirement

The GrokkSoft licence requires modified files to carry prominent notices stating
that they were changed and the date of the change. Because HoverNet has extensively
modified the historical codebase, release preparation should include an audit of
HoverRace-derived source files to verify compliance with this requirement. The
presence of the top-level `LICENSE` alone does not satisfy that separate condition.

## Keeping this accurate

`scripts/check-runtime-dependencies.sh` (the `HoverNetRuntimeDependencies` test)
fails if a shipped Linux binary links a shared library that is not listed in
`packaging/debian/runtime-dependencies.tsv`, or if the package providing it is
missing from the package's `Depends`. Update this file and that list together
whenever a dependency is added.

When adding code, artwork, audio, tracks, fonts or other assets, record their
origin and licence here (or in a linked licence file) so that release packages can
be audited without reconstructing provenance later.
