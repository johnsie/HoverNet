// StateHash.h
//
// The single definition of "the state of a race" (docs/specs/simulation-state.md). Every element lists the fields its
// behaviour depends on once, in MR_Element::VisitLogicState, and that one list serves three jobs through a visitor:
//
//   MR_StateHasher  folds the fields into a 64-bit hash (replays, determinism tools, the server);
//   MR_StateWriter  writes them to bytes (snapshots);
//   MR_StateReader  reads them back (restoring a snapshot).
//
// Because the three share the field list, a snapshot restores exactly what the hash covers: a field that matters to the
// simulation but is missing from the list shows up as a hash that differs after a restore (the snapshot property test).
#ifndef STATE_HASH_H
#define STATE_HASH_H

#include <cstdint>
#include <cstring>
#include <vector>

class MR_Level;

class MR_StateVisitor
{
public:
    virtual ~MR_StateVisitor() {}

    // True while reading a snapshot back: the visited fields are overwritten.
    virtual bool IsLoading() const { return false; }

    // Any integer-like field (int, unsigned, BOOL, enum, angle, time).
    template <class T> void Int( T& pValue )
    {
        std::int64_t lValue = static_cast<std::int64_t>( pValue );
        VisitInt64( lValue );
        pValue = static_cast<T>( lValue );
    }
    void Double( double& pValue ) { VisitDouble( pValue ); }

protected:
    virtual void VisitInt64( std::int64_t& pValue ) = 0;
    virtual void VisitDouble( double& pValue ) = 0;
};

// FNV-1a, 64 bit. Floating-point values are hashed by exact bit pattern: one differing bit is a divergence.
class MR_StateHasher : public MR_StateVisitor
{
public:
    MR_StateHasher() : mHash( 1469598103934665603ULL ) {}
    std::uint64_t Value() const { return mHash; }

protected:
    void VisitInt64( std::int64_t& pValue ) override
    {
        const std::uint64_t lValue = static_cast<std::uint64_t>( pValue );
        for( int lByte = 0; lByte < 8; ++lByte ) {
            mHash ^= ( lValue >> ( lByte * 8 ) ) & 0xFF;
            mHash *= 1099511628211ULL;
        }
    }
    void VisitDouble( double& pValue ) override
    {
        std::uint64_t lBits = 0;
        std::memcpy( &lBits, &pValue, sizeof( lBits ) );
        std::int64_t lAsInt = static_cast<std::int64_t>( lBits );
        VisitInt64( lAsInt );
    }

private:
    std::uint64_t mHash;
};

// Little-endian, fixed width: eight bytes per value, independent of the platform.
class MR_StateWriter : public MR_StateVisitor
{
public:
    std::vector<std::uint8_t> mBytes;

protected:
    void VisitInt64( std::int64_t& pValue ) override
    {
        const std::uint64_t lValue = static_cast<std::uint64_t>( pValue );
        for( int lByte = 0; lByte < 8; ++lByte ) mBytes.push_back( static_cast<std::uint8_t>( lValue >> ( lByte * 8 ) ) );
    }
    void VisitDouble( double& pValue ) override
    {
        std::uint64_t lBits = 0;
        std::memcpy( &lBits, &pValue, sizeof( lBits ) );
        std::int64_t lAsInt = static_cast<std::int64_t>( lBits );
        VisitInt64( lAsInt );
    }
};

// Never reads past the end: once the data runs out it stops changing values and reports failure through Ok().
class MR_StateReader : public MR_StateVisitor
{
public:
    MR_StateReader( const std::uint8_t* pData, std::size_t pSize ) : mData( pData ), mSize( pSize ), mPos( 0 ), mOk( true ) {}
    bool IsLoading() const override { return true; }
    bool Ok() const { return mOk; }
    std::size_t Position() const { return mPos; }
    std::size_t Remaining() const { return mSize - mPos; }

protected:
    void VisitInt64( std::int64_t& pValue ) override
    {
        if( !mOk || mSize - mPos < 8 ) {
            mOk = false;
            return;
        }
        std::uint64_t lValue = 0;
        for( int lByte = 0; lByte < 8; ++lByte ) lValue |= static_cast<std::uint64_t>( mData[ mPos + lByte ] ) << ( lByte * 8 );
        mPos += 8;
        pValue = static_cast<std::int64_t>( lValue );
    }
    void VisitDouble( double& pValue ) override
    {
        std::int64_t lAsInt = 0;
        VisitInt64( lAsInt );
        if( !mOk ) return;
        const std::uint64_t lBits = static_cast<std::uint64_t>( lAsInt );
        std::memcpy( &pValue, &lBits, sizeof( pValue ) );
    }

private:
    const std::uint8_t* mData;
    std::size_t mSize;
    std::size_t mPos;
    bool mOk;
};

// Hash of the whole simulation state: the simulation time, then every free element of the level in list order (its
// type, room, position, orientation and its own logic state).
std::uint64_t MR_HashLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime );

#endif
