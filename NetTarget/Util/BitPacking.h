// BitPacking.h


#ifndef BIT_PACKING_H
#define BIT_PACKING_H


#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_UTIL
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

#include "MR_Types.h"

class MR_BitPack
{

   protected:
      // MR_MainCharacterState's deployed wire representation is 20 bytes.
      // Keeping the storage here avoids the former one-byte base array being
      // accessed past its bounds into a derived-class member.
      MR_UInt8 mData[20];

   public:
      void Clear( int pSize );
      void Set( int pOffset, int pLen, int pPrecision, MR_Int32 pValue );

      MR_Int32  Get( int pOffset, int pLen, int pPrecision )const;
      MR_UInt32 Getu( int pOffset, int pLen, int pPrecision )const;
};

// Inlined because good performances are needed
inline MR_Int32 MR_BitPack::Get( int pOffset, int pLen, int pPrecision )const
{
   MR_UInt32 lValue = Getu( pOffset, pLen, 0 );
   if( pLen < 32 && (lValue & (MR_UInt32(1) << (pLen-1))) != 0 )
   {
      lValue |= ~((MR_UInt32(1) << pLen)-1);
   }
   return static_cast<MR_Int32>(lValue) * (MR_Int32(1) << pPrecision);
}
   

inline MR_UInt32 MR_BitPack::Getu( int pOffset, int pLen, int pPrecision )const
{
   MR_UInt32 lReturnValue = 0;
   for( int lBit = 0; lBit < pLen; ++lBit )
   {
      const int lSourceBit = pOffset + lBit;
      if( (mData[lSourceBit/8] & (MR_UInt8(1) << (lSourceBit%8))) != 0 )
      {
         lReturnValue |= MR_UInt32(1) << lBit;
      }
   }
   return lReturnValue << pPrecision;
}

inline void MR_BitPack::Clear( int pSize )
{
   memset( mData, 0, pSize );
}

inline void MR_BitPack::Set( int pOffset, int pLen, int pPrecision, MR_Int32 pValue )
{

   const MR_UInt32 lValue = static_cast<MR_UInt32>(pValue >> pPrecision);
   for( int lBit = 0; lBit < pLen; ++lBit )
   {
      if( (lValue & (MR_UInt32(1) << lBit)) != 0 )
      {
         const int lDestinationBit = pOffset + lBit;
         mData[lDestinationBit/8] |= MR_UInt8(1) << (lDestinationBit%8);
      }
   }
}



#undef MR_DllDeclare

#endif
