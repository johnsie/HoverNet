// WireFormat.h : Fixed-width, alignment-safe wire encoding helpers.

#pragma once

#include <cstdint>

namespace HoverNetWire
{
   inline void WriteI16LE(std::uint8_t* pDestination, std::int16_t pValue)
   {
      const std::uint16_t lValue = static_cast<std::uint16_t>(pValue);
      pDestination[0] = static_cast<std::uint8_t>(lValue & 0xff);
      pDestination[1] = static_cast<std::uint8_t>((lValue >> 8) & 0xff);
   }

   inline std::int16_t ReadI16LE(const std::uint8_t* pSource)
   {
      const std::uint16_t lValue = static_cast<std::uint16_t>(pSource[0]) |
         (static_cast<std::uint16_t>(pSource[1]) << 8);
      return static_cast<std::int16_t>(lValue);
   }

   inline void WriteI32LE(std::uint8_t* pDestination, std::int32_t pValue)
   {
      const std::uint32_t lValue = static_cast<std::uint32_t>(pValue);
      pDestination[0] = static_cast<std::uint8_t>(lValue & 0xff);
      pDestination[1] = static_cast<std::uint8_t>((lValue >> 8) & 0xff);
      pDestination[2] = static_cast<std::uint8_t>((lValue >> 16) & 0xff);
      pDestination[3] = static_cast<std::uint8_t>((lValue >> 24) & 0xff);
   }

   inline std::int32_t ReadI32LE(const std::uint8_t* pSource)
   {
      const std::uint32_t lValue = static_cast<std::uint32_t>(pSource[0]) |
         (static_cast<std::uint32_t>(pSource[1]) << 8) |
         (static_cast<std::uint32_t>(pSource[2]) << 16) |
         (static_cast<std::uint32_t>(pSource[3]) << 24);
      return static_cast<std::int32_t>(lValue);
   }
}
