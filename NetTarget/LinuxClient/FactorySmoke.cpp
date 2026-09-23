#include "../Util/DllObjectFactory.h"

#include <cstdio>

class TestFactoryObject : public MR_ObjectFromFactory
{
public:
    explicit TestFactoryObject(const MR_ObjectFromFactoryId& id) : MR_ObjectFromFactory(id) {}
};

MR_ObjectFromFactory* CreateTestObject(MR_UInt16 classId)
{
    const MR_ObjectFromFactoryId id = {1, classId};
    return new TestFactoryObject(id);
}

int main()
{
    const char* archivePath = "build/linux/factory-id-smoke.dat";
    MR_ObjectFromFactoryId writtenId = {1, 42};
    MR_ObjectFromFactoryId readId = {0, 0};

    CFile output;
    if (!output.Open(archivePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
        std::fprintf(stderr, "Unable to create factory ID archive\n");
        return 1;
    }
    {
        CArchive archive(&output, CArchive::store);
        writtenId.Serialize(archive);
    }
    output.Close();

    CFile input;
    if (!input.Open(archivePath, CFile::modeRead | CFile::typeBinary)) {
        std::fprintf(stderr, "Unable to reopen factory ID archive\n");
        return 1;
    }
    {
        CArchive archive(&input, CArchive::load);
        readId.Serialize(archive);
    }
    input.Close();
    if (!(readId == writtenId)) {
        std::fprintf(stderr, "Factory ID did not round-trip\n");
        return 1;
    }

    MR_DllObjectFactory::Init();
    MR_DllObjectFactory::RegisterLocalDll(1, CreateTestObject);
    MR_ObjectFromFactory* object = MR_DllObjectFactory::CreateObject(writtenId);
    if (object == nullptr || !(object->GetTypeId() == writtenId)) {
        std::fprintf(stderr, "Local factory did not create the expected object\n");
        return 1;
    }
    delete object;
    MR_DllObjectFactory::Clean(FALSE);

    std::puts("Object factory smoke test passed");
    return 0;
}