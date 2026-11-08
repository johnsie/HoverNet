// Opens a HoverCad project (.TR) so it can be edited in the new editor and saved as a .track.txt (docs/roadmap-track-editor.md, Phase 1; the replacement for HoverCad).
//
// A .TR file is an MFC archive of HoverCad's own objects (nodes, anchored walls, polygons, items) written by HoverCad/HoverCadDoc.cpp. This reads that archive and then
// does what HoverCad's own Compile did to make the compiler's text (CHoverCadDoc::GenerateOutputFile): polygons become rooms and features in the order they were made,
// each numbered by its place in the list; items become starts and elements; walls shared by two rooms become connections. The numbers HoverCad wrote for textures
// (its "serial ids") are mapped to the game's surface ids with HoverCad's own tables (HoverCad/HoverTypes.cpp).
//
// The format knowledge here is from HoverCad's source, which carries the GrokkSoft licence: this file is a re-implementation of that behaviour and carries the notice
// docs/licence-compliance.md asks for. It is checked against the real HoverCad projects shipped in NetTarget/mazes (see scripts/test-hovercad-import.sh).
#pragma once

#include "TrackSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace HoverNet
{
// Reads a .TR file's bytes into the model. `notes` gets one plain-language line for everything that was not carried over as it was (a start HoverCad had no room
// for, a feature outside every room, a background picture re-pointed at the game's own folder). Returns false with `error` when the file is not a HoverCad project.
bool ImportHoverCad(const std::vector<std::uint8_t>& bytes, TrackModel& model, std::string& error, std::vector<std::string>& notes);

// True when the bytes look like a HoverCad project (cheap check: an object count, then the archive's new-class tag and the class name HCNode).
bool LooksLikeHoverCad(const std::vector<std::uint8_t>& bytes);
}  // namespace HoverNet
