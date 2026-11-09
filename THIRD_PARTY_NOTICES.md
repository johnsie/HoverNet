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
| [RmlUi](https://github.com/mikke89/RmlUi) (`NetTarget/ThirdParty/rmlui`: `Include`, `Source/Core` and the SDL backend files; licence in `LICENSE.txt`; downloaded as the 5.1 release tarball, SHA-256 `0d28177118f0777e42864b2b7ddfc2937e81eb0dc4c52fc034c71a0c93516626`, not compared with a published value) | 5.1 | MIT | the prototype main menu (HTML/CSS-style layout, transitions and animation) | compiled in only when built with `-DHOVERNET_RMLUI=ON`; not in the packages yet. Changed by HoverNet (2026-11-09, marked in the files): the SDL backend decodes images with stb_image instead of SDL_image; the font engine loads glyphs without hinting |
| [FreeType](https://freetype.org) (`NetTarget/ThirdParty/freetype`: `include`, `src`, `builds`, `CMakeLists.txt`, licences in `docs/`; the 2.13.2 tarball, SHA-256 `1ac27e16c134a7f2ccea177faba19801131116fd682efc1f5737037c5db224b5`, which matches the published value) | 2.13.2 | FreeType Licence (FTL, BSD-style with a credit clause) or GPL 2.0, at your choice (the FTL is used) | text rendering for RmlUi | compiled in only with RmlUi, as above. Credit: "Portions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved." |
| [stb_image](https://github.com/nothings/stb) (`NetTarget/ThirdParty/stb/stb_image.h`, one file, SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`) | 2.30 | public domain (Unlicense) or MIT, at your choice; the file carries both texts | decoding the PNG images in the texture pack (`ObjFac1.textures`) | compiled into the client and the object plug-in |
| [stb_vorbis](https://github.com/nothings/stb) (`NetTarget/ThirdParty/stb/stb_vorbis.c`, one file, SHA-256 `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`) | 1.22 | public domain (Unlicense) or MIT, at your choice; the file carries both texts | decoding the Ogg Vorbis music | compiled into the client |
| Music: *Hard Left* (Dan Knoflicek), *Asphalt* (Sygil), *Fever Stadium* with its *Climax* version (MintoDog) and *Hyper Ultra-Racing* (cynicmusic), `NetTarget/Music/` | n/a | CC-BY 3.0 (Hard Left, chosen from its CC-BY / CC-BY-SA / GPL options), GPL 3.0 (Asphalt) and CC0 (the other three), all from OpenGameArt.org | the menu and race music | data files shipped beside the game; credits and what the licences ask are in `NetTarget/Music/CREDITS.md`, the GPL text in `NetTarget/Music/GPL-3.0.txt` |
| `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h` (stb, bundled with ImGui) | as bundled | public domain / MIT | font and text editing for ImGui | compiled into the client |
| [Inter](https://github.com/rsms/inter) | variable font from Google Fonts | SIL Open Font License 1.1; see `NetTarget/LinuxClient/assets/Inter-OFL.txt` | menu and interface text | yes |
| [Oxanium](https://github.com/sevmeyer/oxanium) | variable font from Google Fonts | SIL Open Font License 1.1; see `NetTarget/LinuxClient/assets/Oxanium-OFL.txt` | interface headings | yes |
| OpenAL API headers (`OpenAL/AL`) | 1.1-era | GNU LGPL (headers state the Library GPL) | Win32 legacy client audio API shape; the implementations (`alstub*.cpp`) are HoverNet's own | legacy Win32 client only |
| Ed25519 and SHA-512 (`NetTarget/Authority/Ed25519.*`) | algorithms from RFC 8032 and FIPS 180-4; the field and curve arithmetic follows the structure and constants of [TweetNaCl](https://tweetnacl.cr.yp.to/) (public domain), re-typed and checked against the RFC vectors and an independent reference | public domain (TweetNaCl) / HoverNet's own code | signing catalogues and release files | compiled into the tools |
| Menu artwork `menu-hovercraft.bmp` | n/a | origin not recorded; rights/provenance need confirmation | main menu | yes |

## Libraries linked at build or run time (not in the repository)

| Component | Licence | Platform | How it arrives |
| --- | --- | --- | --- |
| [SDL2](https://www.libsdl.org/) | zlib | Linux, Windows client | Linux: `libsdl2-2.0-0` (a package dependency). Windows: `SDL2.dll` is shipped in the installer |
| GNU C library, `libstdc++`, `libgcc` | LGPL with runtime exceptions | Linux | `libc6`, `libstdc++6`, `libgcc-s1` (package dependencies) |
| Microsoft Visual C++ 2015-2022 Redistributable (x64) | Microsoft redistribution terms | Windows x64 | shipped inside the x64 installer and installed on demand |
| Microsoft MFC | Microsoft Visual Studio terms | legacy Win32 client | statically/dynamically linked at build |
| `curl` (or `wget`; `curl.exe` on Windows 10+) | curl licence (MIT-style) / GPL | Linux, Windows | **not** linked: run as a separate program to download tracks. Recommended by the game package |

## AI-enhanced textures

`ObjFac1.textures` (a pack read by `NetTarget/ObjFacTools/TexturePack.cpp`) holds improved versions of the game's own textures. Each image is **derived from a
texture of the original HoverRace art** (`ObjFac1.dat`; GrokkSoft HoverRace SourceCode License v1.0, see above: its conditions apply to these derived images
and travel with them in every package) by running it through the **Real-ESRGAN** `realesr-animevideov3` super-resolution network (4x, tiled so the textures
still repeat seamlessly). Real-ESRGAN is BSD-3-Clause (https://github.com/xinntao/Real-ESRGAN; the weights are from its v0.2.5.0 release, file SHA-256
`b8a8376811077954d82ca3fcf476f1ac3da3e8a68a4f4d71363008000a18b75d`); the network is run by `tools/texture-ai/srvgg.py`, HoverNet's own numpy
re-implementation of its inference, which reads the weights without executing any code from the file. The network's code and weights are not shipped, only the images
it produced. The images are regenerated by `tools/texture-ai/enhance_all.py` and `build_pack.py` (`docs/texture-pipeline.md`).

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
