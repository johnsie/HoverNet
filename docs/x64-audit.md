# Windows x64 readiness audit

The 2.0 roadmap makes native x64 the supported Windows target and requires that
wire and content compatibility not depend on pointer size. This records what was
audited on 1 October 2026, how, what was fixed, and what is not covered.

## Method

Linux x86-64 is an LP64 platform with 64-bit pointers, like Windows x64 (LLP64),
so most 64-bit mistakes show up there. Everything the shared SDL2/ImGui client,
RaceServer and tools build from is compiled on Linux with `-Wall -Wextra
-Wformat=2 -Wcast-align` and under ASan/UBSan, and then exercised by the test
suite and by the 1,009-track community library.

## Pointer / integer casts

In C++ a cast from a pointer to a smaller integer is a compile *error*, so none
exists in code that builds on Linux. The only truncating pointer cast found by
searching is in the legacy MFC client (`Game2/InternetRoom.cpp`,
`SendMessage(..., (long)mBanner.GetImage(...))`), which is not part of the shared
client or any x64 target and goes away with the Win32 client.

## Saved and shared file formats (tracks, resources)

`CArchive` writes `sizeof(T)` raw bytes, so any field whose size differs between
Win32, Windows x64 and Linux would silently fork the format. The archive now
rejects such types **at compile time**: pointers, `long`, `unsigned long`,
`wchar_t`, `long double`, `size_t` and `ptrdiff_t` cannot be serialized (use
`std::int32_t`/`MR_Int32` etc.). The whole tree compiles with the guard, and all
1,009 community tracks plus the seven bundled tracks load identically (the
regenerated manifest is byte-identical).

## Network format

Every RaceServer message is encoded field by field with explicit width and byte
order (`Util/WireFormat.h`, `Protocol.h`); nothing sends a native struct. See
[the network protocol](2.0-network-protocol.md). The server and client are
exercised against each other on every test run, including all tracks.

## Defects found and fixed

| Where | Problem | Fix |
| --- | --- | --- |
| `Model/ShapeCollisions.cpp` `MR_GetFeatureForceLongitude` | loop condition read an uninitialised variable, so the force direction was sometimes never computed | loop over every wall; 64-bit cross product |
| `ObjFacTools/ResBitmap.h` | `MR_ResBitmap` is derived from (`MR_ResBitmapBuilder`) and deleted through the base without a virtual destructor | virtual destructor |
| `Model/Level.cpp` (5 sites), `MainCharacter.cpp` (2) | object pointers and an enum accessed through a type-punned `int*`/`T*&` alias (strict-aliasing UB) | typed helper / explicit conversion |
| `ResourceCompiler/TextParsing.cpp`, `MazeCompiler/main.cpp` | `sscanf` with an array address, `printf("%s")` given a `CString` object | correct arguments, bounded `%99s` |
| `Model/MazeElement.cpp` | `memset` of a class object | assignment |
| collision and map maths | 32-bit overflow on large arenas (see [Community tracks](community-tracks.md)) | 64-bit intermediates |

## Not covered

- **MSVC itself.** No MSVC warnings (`C4311`/`C4312`/`C4302`) were run; this audit is
  by LP64 proxy. The Windows x64 CI build compiles the shared client; enabling `/W3`
  or `/W4` there and reviewing pointer-truncation warnings is the remaining step.
- **Third-party binaries and installers.** The x64 installer bundles x64 `SDL2.dll`
  and the x64 VC++ redistributable; the audit did not re-inspect the installer
  script beyond what the existing x64 package validation checks.
- **Legacy Win32 client.** It is MFC-only and is being retired; its code was not
  audited for 64-bit safety.
- **Runtime x64 behaviour.** Behaviour on a real Windows x64 machine is covered only
  by the existing installed-client acceptance steps in CI.
