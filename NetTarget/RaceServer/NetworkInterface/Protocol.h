// Protocol.h : Versioned RaceServer handshake shared by client and server.

#pragma once

#include <cstddef>
#include <cstdint>

namespace HoverNetProtocol
{
    constexpr int MessageType = 59;
    constexpr std::uint16_t Major = 2;
    constexpr std::uint16_t Minor = 0;
    constexpr std::uint16_t MaxPayload = 255;
    constexpr std::size_t HelloSize = 14;
    constexpr std::size_t ReplyPrefixSize = 11;

    constexpr std::uint8_t Accepted = 0;
    constexpr std::uint8_t IncompatibleVersion = 1;
    constexpr std::uint8_t MalformedHello = 2;
    constexpr std::uint8_t NegotiationRequired = 3;

    inline void WriteU16(std::uint8_t* pDestination, std::uint16_t pValue)
    {
        pDestination[0] = static_cast<std::uint8_t>((pValue >> 8) & 0xff);
        pDestination[1] = static_cast<std::uint8_t>(pValue & 0xff);
    }

    inline std::uint16_t ReadU16(const std::uint8_t* pSource)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(pSource[0]) << 8) | pSource[1]);
    }

    inline void WriteU32(std::uint8_t* pDestination, std::uint32_t pValue)
    {
        pDestination[0] = static_cast<std::uint8_t>((pValue >> 24) & 0xff);
        pDestination[1] = static_cast<std::uint8_t>((pValue >> 16) & 0xff);
        pDestination[2] = static_cast<std::uint8_t>((pValue >> 8) & 0xff);
        pDestination[3] = static_cast<std::uint8_t>(pValue & 0xff);
    }

    inline std::uint32_t ReadU32(const std::uint8_t* pSource)
    {
        return (static_cast<std::uint32_t>(pSource[0]) << 24) |
            (static_cast<std::uint32_t>(pSource[1]) << 16) |
            (static_cast<std::uint32_t>(pSource[2]) << 8) |
            static_cast<std::uint32_t>(pSource[3]);
    }

    // Existing lobby/gameplay identifiers are four-byte little-endian values.
    // Keep that deployed wire format, but encode it explicitly instead of
    // depending on sizeof(int), host byte order, or alignment.
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
