// MainCharacterHeadless.cpp
//
// The hovercraft's presentation seam for programs with no screen or speakers: the server, the replay and
// determinism tools, and the tests. MainCharacterView.cpp is the real implementation; a program links exactly one
// of the two (linking both is a duplicate-symbol error, which is the point). See docs/adr/0001-simulation-core.md.

#include "StdAfx.h"

#include "MainCharacter.h"

void MR_MainCharacter::ReleaseRenderer()
{
}

void MR_MainCharacter::QueueSound( eSoundEvent, BOOL, BOOL )
{
}

void MR_MainCharacter::AddRenderer()
{
}

void MR_MainCharacter::Render( MR_3DViewPort*, MR_SimulationTime )
{
}

void MR_MainCharacter::PlayInternalSounds()
{
}

void MR_MainCharacter::PlayExternalSounds( int, int )
{
}
