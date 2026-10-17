# Project rules

Rules for anyone (people or AI assistants) working in this repository.

1. **Do not use Docker, or any other container tooling, for any part of this project.** No Dockerfiles, compose files or container
   images; no building, testing, packaging or deploying inside containers. Build and run natively (CMake, the Windows runner, the
   Emscripten toolchain in `.tools/`) and deploy servers as ordinary services (see `docs/operators.md`).
2. Never publish a recorded fact that has not been checked: say what was run, on which platform, and what was not.
3. Behaviour changes that alter the simulation must bump the rules version (`NetTarget/Authority/ReplayFormat.h`) and regenerate the
   golden files in one reviewed change (`docs/determinism.md`).
4. Do not push, tag or release without being asked.
5. Source derived from HoverRace (files with the GrokkSoft licence header, and re-implementations of its code such as
   `NetTarget/ObjFac1/SimElements.*`) must carry a modification notice with the date of the change when HoverNet changes them
   (format in `docs/licence-compliance.md`), and the licence conditions travel with binary distributions. Do not add third-party code
   without recording its licence in `THIRD_PARTY_NOTICES.md`.
6. Releases: only from `main`, only after the full local test suite, the Windows runner checks and the GitHub Actions run for that commit
   have passed; never reuse or move a pushed tag (`docs/releasing.md`). Keep working after a release.
