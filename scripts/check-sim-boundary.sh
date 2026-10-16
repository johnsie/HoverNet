#!/usr/bin/env bash
# The simulation must not depend on video, audio or resource code (docs/adr/0001-simulation-core.md). Linking is
# enforced by the headless tools linking only HoverNetSim; this checks the headers too: no simulation source or header
# may include anything from VideoServices, ObjFacTools, ColorTools or the renderer/sound headers. Run from the repo root.
set -uo pipefail
files=(NetTarget/Model/*.h NetTarget/Model/*.cpp \
       NetTarget/MainCharacter/MainCharacter.h NetTarget/MainCharacter/MainCharacter.cpp \
       NetTarget/MainCharacter/MainCharacterHeadless.cpp)
bad=$(grep -nE '#include *["<][^">]*(VideoServices|ObjFacTools|ColorTools|ObjFac1|MainCharacterRenderer|SoundServer|3DViewport|Game2/)' "${files[@]}" || true)
if [ -n "$bad" ]; then echo "simulation boundary violated:"; echo "$bad"; exit 1; fi
echo "simulation boundary: ${#files[@]} files include nothing from video, audio or resources"
