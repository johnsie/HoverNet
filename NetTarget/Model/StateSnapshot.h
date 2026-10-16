// StateSnapshot.h
//
// Versioned snapshots of a race (docs/specs/simulation-state.md): the complete simulation state of a level as bytes, and
// the inverse. Needed for mid-race joins, spectators, replay seeking and server-side lag compensation.
//
// A snapshot holds every free element of the level in list order. Elements are of three kinds:
//   permanent  placed by the track (power-ups, bumper gates, fuel zones...); restored in place, identified by index;
//   external   created by the program (the craft); restored in place, identified by their position in the list the
//              caller passes (the same list, in the same order, must be passed when restoring);
//   transient  created during the race (missiles, mines); destroyed and re-created from their class id on restore.
// The element state is written by the same field list that feeds the state hash (MR_Element::VisitLogicState), so a
// snapshot restores what the hash covers. Restoring on any platform gives the state saved on any other.
#ifndef STATE_SNAPSHOT_H
#define STATE_SNAPSHOT_H

#include <cstdint>
#include <string>
#include <vector>

class MR_Level;
class MR_FreeElement;

const std::uint32_t kSnapshotVersion = 1;

// Saves the state of pLevel at pSimulationTime. pExternal are the free elements the program owns (the craft).
std::vector<std::uint8_t> MR_SaveLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime,
                                             const std::vector<MR_FreeElement*>& pExternal, std::uint32_t pRules );

// Restores a snapshot into a level of the same track that already contains the same external elements. Returns false and
// sets pError if the data is damaged, from another version or rules version, or does not fit the level; after a failure
// the level may be partly restored and must be discarded. On success pSimulationTime receives the saved time.
bool MR_RestoreLevelState( MR_Level* pLevel, const std::uint8_t* pData, std::size_t pSize,
                           const std::vector<MR_FreeElement*>& pExternal, std::uint32_t pRules,
                           std::int64_t& pSimulationTime, std::string& pError );

#endif
