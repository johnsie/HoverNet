// Mine.h
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

#pragma once

#include "../ObjFacTools/FreeElementBase.h"
#include "../Model/RaceEffects.h"

namespace HoverRace {
namespace ObjFac1 {

class Mine : public ObjFacTools::FreeElementBase, protected Model::CylinderShape
{
	typedef ObjFacTools::FreeElementBase SUPER;
	protected:

		// Shape interface
		MR_Int32 ZMin() const;
		MR_Int32 ZMax() const;
		MR_Int32 AxisX() const;
		MR_Int32 AxisY() const;
		MR_Int32 RayLen() const;

	private:

		BOOL mOnGround;
		MR_LostOfControl mEffect;
		MR_ContactEffectList mEffectList;

	public:
		Mine(const Util::ObjectFromFactoryId & pId, ObjFacTools::ResourceLib* resourceLib);
		~Mine();

	protected:

		// ContactEffectShapeInterface
		const MR_ContactEffectList *GetEffectList();
		const Model::ShapeInterface *GetGivingContactEffectShape();
		const Model::ShapeInterface *GetReceivingContactEffectShape();

		int Simulate(MR_SimulationTime pTimeSlice, Model::Level * pLevel, int pRoom);

		void Render(VideoServices::Viewport3D * pDest, MR_SimulationTime pTime);

		// void  ApplyEffect( const MR_ContactEffect* pEffect,  MR_SimulationTime pTime, MR_SimulationTime pDuration, BOOL pValidDirection, MR_Int32 pZMin, MR_Int32 pZMax, MR_Angle pHorizontalDirection );

		// Network state
		Model::ElementNetState GetNetState() const;
		void SetNetState(int pDataLen, const MR_UInt8 * pData);

		BOOL AssignPermNumber(int pNumber);
};

}  // namespace ObjFac1
}  // namespace HoverRace
