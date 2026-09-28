// ServerConfig.cpp : Configuration implementation

#include "stdafx.h"
#include "ServerConfig.h"

#include <fstream>
#include <sstream>

namespace
{
bool ExtractValue(const std::string& pXml, const char* pTag, std::string& pValue)
{
    const std::string lOpen = std::string("<") + pTag + ">";
    const std::string lClose = std::string("</") + pTag + ">";
    const std::size_t lStart = pXml.find(lOpen);
    if (lStart == std::string::npos || pXml.find(lOpen, lStart + 1) != std::string::npos) return false;
    const std::size_t lValueStart = lStart + lOpen.size();
    const std::size_t lEnd = pXml.find(lClose, lValueStart);
    if (lEnd == std::string::npos) return false;
    pValue = pXml.substr(lValueStart, lEnd - lValueStart);
    const std::size_t lFirst = pValue.find_first_not_of(" \t\r\n");
    const std::size_t lLast = pValue.find_last_not_of(" \t\r\n");
    if (lFirst == std::string::npos) return false;
    pValue = pValue.substr(lFirst, lLast - lFirst + 1);
    return true;
}

bool ExtractSection(const std::string& pXml, const char* pTag, std::string& pValue)
{
    const std::string lOpen = std::string("<") + pTag + ">";
    const std::string lClose = std::string("</") + pTag + ">";
    const std::size_t lStart = pXml.find(lOpen);
    const std::size_t lEnd = pXml.find(lClose);
    if (lStart == std::string::npos || lEnd == std::string::npos || lEnd <= lStart ||
        pXml.find(lOpen, lStart + 1) != std::string::npos) return false;
    pValue = pXml.substr(lStart + lOpen.size(), lEnd - lStart - lOpen.size());
    return true;
}

bool ParseInt(const std::string& pXml, const char* pTag, int pMinimum,
              int pMaximum, int& pValue)
{
    std::string lText;
    if (!ExtractValue(pXml, pTag, lText)) return false;
    char* lEnd = nullptr;
    errno = 0;
    const long lValue = strtol(lText.c_str(), &lEnd, 10);
    if (errno != 0 || lEnd == lText.c_str() || *lEnd != '\0' ||
        lValue < pMinimum || lValue > pMaximum) return false;
    pValue = static_cast<int>(lValue);
    return true;
}

bool ParseBool(const std::string& pXml, const char* pTag, BOOL& pValue)
{
    std::string lText;
    if (!ExtractValue(pXml, pTag, lText)) return false;
    if (lText == "true" || lText == "1") { pValue = TRUE; return true; }
    if (lText == "false" || lText == "0") { pValue = FALSE; return true; }
    return false;
}

int ParseLogLevel(const std::string& pText)
{
    if (pText == "DEBUG") return MR_LOG_DEBUG;
    if (pText == "INFO") return MR_LOG_INFO;
    if (pText == "WARN") return MR_LOG_WARN;
    if (pText == "ERROR") return MR_LOG_ERROR;
    return -1;
}
}

MR_ServerConfig::MR_ServerConfig()
    : mPort(9600),
      mMaxConnections(100),
      mTcpNoDelay(TRUE),
      mSendBufferSize(8192),
      mRecvBufferSize(8192),
      mMaxConcurrentRaces(50),
      mMaxPlayersPerRace(8),
      mIdleRaceTimeoutSec(300),
      mPlayerDisconnectTimeoutSec(30),
      mLogLevel(1),  // MR_LOG_INFO
      mInternetRoomPort(80)
{
    snprintf(mLogFile, sizeof(mLogFile), "%s", "raceserver.log");
    snprintf(mInternetRoomHost, sizeof(mInternetRoomHost), "%s", "localhost");
}

BOOL MR_ServerConfig::LoadFromFile(const char* filename)
{
    if (filename == nullptr || *filename == '\0') return FALSE;
    std::ifstream lInput(filename, std::ios::binary);
    if (!lInput) return FALSE;
    std::ostringstream lBuffer;
    lBuffer << lInput.rdbuf();
    const std::string lXml = lBuffer.str();
    if ((!lInput.good() && !lInput.eof()) || lXml.size() > 1024 * 1024) return FALSE;

    std::string lNetwork, lRaces, lLogging, lInternet;
    if (!ExtractSection(lXml, "network", lNetwork) ||
        !ExtractSection(lXml, "races", lRaces) ||
        !ExtractSection(lXml, "logging", lLogging) ||
        !ExtractSection(lXml, "internetroom", lInternet)) return FALSE;

    MR_ServerConfig lParsed;
    int lPort = 0;
    int lInternetPort = 0;
    std::string lLogLevel, lLogFile, lInternetHost;
    if (!ParseInt(lNetwork, "port", 1025, 65535, lPort) ||
        !ParseInt(lNetwork, "max_connections", 1, 10000, lParsed.mMaxConnections) ||
        !ParseBool(lNetwork, "tcp_nodelay", lParsed.mTcpNoDelay) ||
        !ParseInt(lNetwork, "send_buffer_size", 1024, 1048576, lParsed.mSendBufferSize) ||
        !ParseInt(lNetwork, "receive_buffer_size", 1024, 1048576, lParsed.mRecvBufferSize) ||
        !ParseInt(lRaces, "max_concurrent", 1, 1000, lParsed.mMaxConcurrentRaces) ||
        !ParseInt(lRaces, "max_players_per_race", 1, 8, lParsed.mMaxPlayersPerRace) ||
        !ParseInt(lRaces, "idle_race_timeout_sec", 1, 86400, lParsed.mIdleRaceTimeoutSec) ||
        !ParseInt(lRaces, "player_disconnect_timeout_sec", 1, 3600, lParsed.mPlayerDisconnectTimeoutSec) ||
        !ExtractValue(lLogging, "level", lLogLevel) ||
        !ExtractValue(lLogging, "file", lLogFile) || lLogFile.size() >= sizeof(lParsed.mLogFile) ||
        !ExtractValue(lInternet, "host", lInternetHost) || lInternetHost.size() >= sizeof(lParsed.mInternetRoomHost) ||
        !ParseInt(lInternet, "port", 1, 65535, lInternetPort)) return FALSE;

    lParsed.mLogLevel = ParseLogLevel(lLogLevel);
    if (lParsed.mLogLevel < 0) return FALSE;
    lParsed.mPort = static_cast<unsigned>(lPort);
    lParsed.mInternetRoomPort = static_cast<unsigned>(lInternetPort);
    snprintf(lParsed.mLogFile, sizeof(lParsed.mLogFile), "%s", lLogFile.c_str());
    snprintf(lParsed.mInternetRoomHost, sizeof(lParsed.mInternetRoomHost), "%s", lInternetHost.c_str());
    *this = lParsed;
    return TRUE;
}

BOOL MR_ServerConfig::SaveToFile(const char* filename)
{
    if (filename == nullptr || *filename == '\0' || mLogLevel < MR_LOG_DEBUG || mLogLevel > MR_LOG_ERROR) return FALSE;
    static const char* const lLevels[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::ofstream lOutput(filename, std::ios::binary | std::ios::trunc);
    if (!lOutput) return FALSE;
    lOutput << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<raceserver>\n"
            << "  <network>\n    <port>" << mPort << "</port>\n"
            << "    <max_connections>" << mMaxConnections << "</max_connections>\n"
            << "    <tcp_nodelay>" << (mTcpNoDelay ? "true" : "false") << "</tcp_nodelay>\n"
            << "    <send_buffer_size>" << mSendBufferSize << "</send_buffer_size>\n"
            << "    <receive_buffer_size>" << mRecvBufferSize << "</receive_buffer_size>\n  </network>\n"
            << "  <races>\n    <max_concurrent>" << mMaxConcurrentRaces << "</max_concurrent>\n"
            << "    <max_players_per_race>" << mMaxPlayersPerRace << "</max_players_per_race>\n"
            << "    <idle_race_timeout_sec>" << mIdleRaceTimeoutSec << "</idle_race_timeout_sec>\n"
            << "    <player_disconnect_timeout_sec>" << mPlayerDisconnectTimeoutSec << "</player_disconnect_timeout_sec>\n  </races>\n"
            << "  <logging>\n    <level>" << lLevels[mLogLevel] << "</level>\n"
            << "    <file>" << mLogFile << "</file>\n  </logging>\n"
            << "  <internetroom>\n    <host>" << mInternetRoomHost << "</host>\n"
            << "    <port>" << mInternetRoomPort << "</port>\n  </internetroom>\n</raceserver>\n";
    return lOutput.good() ? TRUE : FALSE;
}
