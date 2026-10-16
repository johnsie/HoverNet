// StateHash.h
//
// The one definition of "the state of a race" for hashing (docs/specs/simulation-state.md). Every element of the
// level contributes its own logic state through MR_Element::HashLogicState, so the hash covers exactly what the
// simulation depends on. Replays, the determinism and netcode tools, the server and the tests all use it.
#ifndef STATE_HASH_H
#define STATE_HASH_H

#include <cstdint>
#include <cstring>

class MR_Level;

// FNV-1a, 64 bit. Floating-point values are hashed by exact bit pattern: one differing bit is a divergence.
class MR_StateHasher
{
public:
    MR_StateHasher() : mHash( 1469598103934665603ULL ) {}

    void Int( std::int64_t pValue )
    {
        const std::uint64_t lValue = static_cast<std::uint64_t>( pValue );
        for( int lByte = 0; lByte < 8; ++lByte ) {
            mHash ^= ( lValue >> ( lByte * 8 ) ) & 0xFF;
            mHash *= 1099511628211ULL;
        }
    }
    void Double( double pValue )
    {
        std::uint64_t lBits = 0;
        std::memcpy( &lBits, &pValue, sizeof( lBits ) );
        Int( static_cast<std::int64_t>( lBits ) );
    }
    std::uint64_t Value() const { return mHash; }

private:
    std::uint64_t mHash;
};

// Hash of the whole simulation state: the simulation time, then every free element of the level in list order (its
// type, room, position, orientation and its own logic state).
std::uint64_t MR_HashLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime );

#endif
