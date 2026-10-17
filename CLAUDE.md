# Project rules

Rules for anyone (people or AI assistants) working in this repository.

1. **Do not use Docker, or any other container tooling, for any part of this project.** No Dockerfiles, compose files or container
   images; no building, testing, packaging or deploying inside containers. Build and run natively (CMake, the Windows runner, the
   Emscripten toolchain in `.tools/`) and deploy servers as ordinary services (see `docs/operators.md`).
2. Never publish a recorded fact that has not been checked: say what was run, on which platform, and what was not.
3. Behaviour changes that alter the simulation must bump the rules version (`NetTarget/Authority/ReplayFormat.h`) and regenerate the
   golden files in one reviewed change (`docs/determinism.md`).
4. Do not push, tag or release without being asked.
