// ReplayStoreDir.h -- the replays a server keeps in its replay folder (--replay-dir), offered to every player through the lobby.
//
// The newest `keep` replays are kept (older ones are removed together with their results table, after each new replay is saved), so the folder
// cannot grow without bound. A replay is named by the first 8 bytes of the SHA-256 of its file (what `HoverNetReplay id` prints), the list
// is built from the files' own headers and the results table beside them, and a client can only ever ask for a file by that number: no file
// name from the network is ever used. Files that do not decode are ignored (and never sent).
#ifndef REPLAY_STORE_DIR_H
#define REPLAY_STORE_DIR_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "LobbyServer.h"

namespace Authority
{
// One player's line of a race's results table.
struct ReplayRun { std::uint8_t slot; std::int32_t finishMs; std::int32_t bestLapMs; bool finished; std::string name; int craft; bool valid; };   // craft: 0 to 4, -1 when the table does not say

class DirReplayStore : public ReplayStore
{
public:
    DirReplayStore(const std::string& directory, std::size_t keep) : mDir(directory), mKeep(keep) {}

    std::vector<Protocol3::ReplayInfo> List(const std::string& track, int max) override;
    bool Read(std::uint64_t id, std::vector<std::uint8_t>& bytes) override;
    Protocol3::RecordList Records(const std::string& track, int laps) override;
    Protocol3::WinnerList Winners(int max) override;
    // Removes the oldest replays (and their results tables) beyond `keep`. Returns how many were removed.
    int Prune();
    std::size_t Count();

private:
    struct Item
    {
        std::string name;
        std::int64_t size = 0, mtime = 0;
        Protocol3::ReplayInfo info;
        std::vector<ReplayRun> runs;      // from the results table beside the replay
    };
    void Refresh();
    std::vector<const Item*> Sorted() const;

    std::string mDir;
    std::size_t mKeep;
    std::map<std::string, Item> mItems;   // by file name
};
}

#endif
