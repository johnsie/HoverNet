#include "CraftStore.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

namespace HoverNetCraft
{
namespace
{
void MakeDir(const std::string& dir)
{
#ifdef _WIN32
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
}

bool ReplaceFile(const std::string& from, const std::string& to)
{
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}
}

std::string CraftStore::PathFor(int index) const { return mDir + "/design" + std::to_string(index) + ".hcd"; }

bool CraftStore::Load(int index, Authority::CraftDesign& design) const
{
    if (index < 0 || index >= kSavedDesigns) return false;
    std::ifstream in(PathFor(index), std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0 || static_cast<std::uintmax_t>(size) > Authority::kMaxDesignBytes) return false;
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in) return false;
    return Authority::DecodeCraftDesign(bytes.data(), bytes.size(), design);
}

bool CraftStore::Save(int index, const Authority::CraftDesign& design) const
{
    if (index < 0 || index >= kSavedDesigns) return false;
    MakeDir(mDir);
    const std::vector<std::uint8_t> bytes = Authority::EncodeCraftDesign(Authority::SanitiseCraftDesign(design));
    const std::string temp = PathFor(index) + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    return ReplaceFile(temp, PathFor(index));   // a damaged half-written design never replaces a good one
}

bool CraftStore::Delete(int index) const
{
    if (index < 0 || index >= kSavedDesigns) return false;
    if (Active() == index) SetActive(-1);
    return std::remove(PathFor(index).c_str()) == 0;
}

int CraftStore::Active() const
{
    std::ifstream in(mDir + "/active");
    int index = -1;
    if (!(in >> index) || index < 0 || index >= kSavedDesigns) return -1;
    Authority::CraftDesign design;
    return Load(index, design) ? index : -1;
}

bool CraftStore::SetActive(int index) const
{
    MakeDir(mDir);
    std::ofstream out(mDir + "/active", std::ios::trunc);
    out << index << '\n';
    return static_cast<bool>(out);
}

bool CraftStore::LoadActive(Authority::CraftDesign& design) const
{
    const int index = Active();
    return index >= 0 && Load(index, design) && !design.IsPlain();
}

std::vector<std::uint8_t> CraftStore::ActiveBytes() const
{
    Authority::CraftDesign design;
    return LoadActive(design) ? Authority::EncodeCraftDesign(design) : std::vector<std::uint8_t>();
}
}
