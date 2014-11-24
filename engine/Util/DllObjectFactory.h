// DllObjectFactory.h
//
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

//
// This file constais fonctions for creating objects from
// two integer numbers. One of this number identify the
// dll containing the object to create and the other contain
// factory class
//
//

#pragma once

#include "MR_Types.h"

#if defined(_WIN32) && defined(HR_ENGINE_SHARED)
#	ifdef MR_ENGINE
#		define MR_DllDeclare   __declspec( dllexport )
#	else
#		define MR_DllDeclare   __declspec( dllimport )
#	endif
#else
#	define MR_DllDeclare
#endif

namespace HoverRace {
	namespace Parcel {
		class ObjStream;
	}
	namespace ObjFacTools {
		class ResourceLib;
	}
}

namespace HoverRace {
namespace Util {

/// Unique identifier for a Factory Object.
class ObjectFromFactoryId
{
	public:
		MR_UInt16 mDllId;
		MR_UInt16 mClassId;

		void Serialize(HoverRace::Parcel::ObjStream &pArchive);

		MR_DllDeclare int operator==(const ObjectFromFactoryId &pId) const;
};

/// Base class for object created with a Dll Factory
class MR_DllDeclare ObjectFromFactory
{
	private:
		ObjectFromFactoryId mId;

	public:
		ObjectFromFactory(const ObjectFromFactoryId &pId);
		virtual ~ObjectFromFactory();

		const ObjectFromFactoryId &GetTypeId() const;

		// Serialisation functions
		//
		// Warning this module do not support multiple references to objects
		// or looped structures
		static void SerializePtr(HoverRace::Parcel::ObjStream &pArchive, ObjectFromFactory * &pPtr);
		virtual void Serialize(HoverRace::Parcel::ObjStream &pArchive);

};

namespace DllObjectFactory
{
	typedef ObjectFromFactory* (*getObject_t) (MR_UInt16);

	MR_DllDeclare void Init();					  ///< Must be called at the begining of the program
	MR_DllDeclare void Clean(BOOL pOnlyDynamic);  ///< Must be called at the end of the program

	MR_DllDeclare void IncrementReferenceCount(int pDllId);
	MR_DllDeclare void DecrementReferenceCount(int pDllId);

	/// Fast Object Creation function
	MR_DllDeclare ObjectFromFactory *CreateObject(const ObjectFromFactoryId &pId);

	MR_DllDeclare ObjFacTools::ResourceLib &GetResourceLib(int dllId = 1);

	// Local Dll
	MR_DllDeclare void RegisterLocalDll(int pDLLId, getObject_t pFunc);

}

}  // namespace Util
}  // namespace HoverRace

#undef MR_DllDeclare
