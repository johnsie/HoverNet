# HoverNet

HoverNet is a modernization of the original HoverRace codebase. The repository contains the native Windows client, an SDL2 Linux client, and the authoritative multiplayer race server.

Multiplayer clients connect to `outiva.com:9600` by default. Linux can override this with `--lobby host:port`; Windows can set `HOVERNET_LOBBY=host:port`.

## Linux build

Install a C++ compiler, CMake, and the SDL2 development package, then run:

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
```

The primary outputs are:

- `build/linux/HoverNetGame2Player` — game client
- `build/linux/RaceServer` — multiplayer lobby and race server
- `build/linux/HoverNetLobby` — command-line lobby client

Run the game from the repository root so it can locate its tracks and resource library:

```bash
./build/linux/HoverNetGame2Player
```

## Windows build

The Windows client targets Win32 with the Visual Studio 2022 `v143` toolset, MFC, and SDL2 installed under `C:\SDL2`:

```powershell
msbuild NetTarget\ObjFac1\ObjFac1.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
msbuild NetTarget\Game2\Game2.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

The runnable client and its libraries are written to `Release\`. CI creates an Inno Setup installer from that directory.

## Packaging and delivery

- GitHub Actions builds both clients, the race server, Windows setup EXE, and Linux DEBs.
- GitLab CI provides the existing protected production deployment path.
- Linux packaging scripts live under `packaging/debian/`.
- The Windows installer definition is `packaging/windows/HoverNet.iss`.

## Documentation

- [Linux client and controls](docs/linux-client.md)
- [GitHub Actions](docs/github-actions.md)
- [GitLab CI and deployment](docs/gitlab-ci.md)
- [Track compiler format](docs/track-format.md)
- [Race server](NetTarget/RaceServer/README.md)
- [Resource decompiler](NetTarget/ResourceDecompiler/README.md)
