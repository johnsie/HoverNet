// ResourceLibBuilder.cpp
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.
//

#include "StdAfx.h"

#include <boost/foreach.hpp>

#include "../../engine/Parcel/ClassicRecordFile.h"
#include "../../engine/Parcel/ClassicObjStream.h"

#include "ResourceLibBuilder.h"

using namespace HoverRace::Parcel;

namespace {
	template<class T>
	void WriteRes(ObjStream &os, T &res)
	{
		MR_UInt32 num = res.size();
		os << num;
		BOOST_FOREACH(const typename T::value_type &ent, res) {
			MR_Int32 key = ent.first;
			os << key;
			ent.second->Serialize(os);
		}
	}
}

namespace HoverRace {
namespace ResourceCompiler {

ResourceLibBuilder::ResourceLibBuilder()
{
	// Notting to do
}

ResourceLibBuilder::~ResourceLibBuilder()
{
	// Notting to do
}

void ResourceLibBuilder::AddBitmap(ObjFacTools::ResBitmap *pBitmap)
{
	bitmaps.insert(bitmaps_t::value_type(pBitmap->GetResourceId(), pBitmap));
}

void ResourceLibBuilder::AddActor(ObjFacTools::ResActor *pActor)
{
	actors.insert(actors_t::value_type(pActor->GetResourceId(), pActor));
}

void ResourceLibBuilder::AddSprite(ObjFacTools::ResSprite *pSprite)
{
	sprites.insert(sprites_t::value_type(pSprite->GetResourceId(), pSprite));
}

void ResourceLibBuilder::AddSound(ObjFacTools::ResShortSound *pSound)
{
	shortSounds.insert(shortSounds_t::value_type(pSound->GetResourceId(), pSound));
}

void ResourceLibBuilder::AddSound(ObjFacTools::ResContinuousSound *pSound)
{
	continuousSounds.insert(continuousSounds_t::value_type(pSound->GetResourceId(), pSound));
}

/**
 * This is the function that is called when the file is being written.
 * It creates the output file, then writes (in this order):
 * - bitmaps
 * - actors (meshes)
 * - sprites
 * - sounds
 *
 * @param filename Filename of output file
 * @return BOOL indicating the success of the operation
 */
bool ResourceLibBuilder::Export(const HoverRace::Util::OS::path_t &filename)
{
	bool lReturnValue = true;

	ClassicRecordFile lFile;

	lReturnValue = lFile.CreateForWrite(filename, 1, "\x8\rFireball object factory resource file, (c)GrokkSoft 1996\n\x1a");

	if(!lReturnValue) {
		fprintf(stderr, "%s: %s.\n", _("ERROR"), _("unable to create output file"));
	}
	else {
		lFile.BeginANewRecord();

		{
			// Write the magic number
			int lMagicNumber = FILE_MAGIC;

			ObjStreamPtr archivePtr(lFile.StreamOut());
			ObjStream &lArchive = *archivePtr;

			lArchive << lMagicNumber;

			WriteRes(lArchive, bitmaps);
			WriteRes(lArchive, actors);
			WriteRes(lArchive, sprites);
			WriteRes(lArchive, shortSounds);
			WriteRes(lArchive, continuousSounds);
		}
	}

	return lReturnValue;
}

}  // namespace ResourceCompiler
}  // namespace HoverRace
