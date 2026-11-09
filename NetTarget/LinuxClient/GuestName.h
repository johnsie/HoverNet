// A player who has not signed in with Outiva races as a guest, and other players are told so: " (nonreg)" is added after the name they chose. The whole name still
// has to fit the protocol's 32 bytes, so a long name is cut to make room for the tag, never the other way round.
#pragma once

#include <string>

namespace HoverNetGuest
{
inline const char* Tag() { return " (nonreg)"; }

inline std::string Tagged(const std::string& pName, bool pGuest)
{
    if (!pGuest || pName.empty()) return pName;
    const std::string tag = Tag();
    if (pName.size() >= tag.size() && pName.compare(pName.size() - tag.size(), tag.size(), tag) == 0) return pName.size() <= 32 ? pName : pName.substr(0, 32);   // already tagged
    std::string base = pName;
    if (base.size() + tag.size() > 32) base = base.substr(0, 32 - tag.size());
    while (!base.empty() && base.back() == ' ') base.pop_back();
    return base + tag;
}
}  // namespace HoverNetGuest
