#include "../Util/RecordFile.h"
#include "../Util/FuzzyLogic.h"
#include "../Util/WorldCoordinates.h"

#include <cstdio>
#include <cstring>

int main()
{
    const char* fixturePath = "NetTarget/Tracks/ClassicH.trk";
    const char expectedTitle[] = "\x08\rHoverRace track file, (c)GrokkSoft 1997\n\x1a";

    MR_RecordFile fixture;
    if (!fixture.OpenForRead(fixturePath) || fixture.GetNbRecords() != 4 ||
        fixture.GetNbRecordsMax() != 4 || std::strcmp(fixture.GetFileTitle(), expectedTitle) != 0) {
        std::fprintf(stderr, "Unable to read the ClassicH record table\n");
        return 1;
    }

    fixture.SelectRecord(0);
    const ULONGLONG firstRecordLength = fixture.GetLength();
    int magicNumber = 0;
    if (fixture.Read(&magicNumber, sizeof(magicNumber)) != sizeof(magicNumber) ||
        firstRecordLength == 0 || magicNumber != 0x000142b9) {
        std::fprintf(stderr, "ClassicH first record was not read correctly\n");
        return 1;
    }
    fixture.Close();

    const char* outputPath = "build/linux/record-file-smoke.dat";
    MR_RecordFile output;
    if (!output.CreateForWrite(outputPath, 2, "Linux record test") ||
        !output.BeginANewRecord()) {
        std::fprintf(stderr, "Unable to create a record file\n");
        return 1;
    }
    const DWORD firstValue = 0x12345678;
    output.Write(&firstValue, sizeof(firstValue));
    output.BeginANewRecord();
    MR_3DCoordinate secondValue(100, -200, 300);
    {
        CArchive archive(&output, CArchive::store);
        secondValue.Serialize(archive);
    }
    output.Close();

    MR_RecordFile roundTrip;
    DWORD readValue = 0;
    MR_3DCoordinate readCoordinate;
    if (!roundTrip.OpenForRead(outputPath) || roundTrip.GetNbRecords() != 2 ||
        std::strcmp(roundTrip.GetFileTitle(), "Linux record test") != 0) {
        std::fprintf(stderr, "Unable to reopen generated record file\n");
        return 1;
    }
    roundTrip.SelectRecord(0);
    if (roundTrip.Read(&readValue, sizeof(readValue)) != sizeof(readValue) || readValue != firstValue) {
        std::fprintf(stderr, "First record did not round-trip\n");
        return 1;
    }
    roundTrip.SelectRecord(1);
    {
        CArchive archive(&roundTrip, CArchive::load);
        readCoordinate.Serialize(archive);
    }
    if (readCoordinate != secondValue) {
        std::fprintf(stderr, "Serialized coordinate did not round-trip\n");
        return 1;
    }

    MR_InitFuzzyModule();
    MR_ProbTable probabilities;
    probabilities.AddProb(1);
    probabilities.AddProb(3);
    probabilities.AddProb(2);
    for (int sample = 0; sample < 100; ++sample) {
        if (probabilities.GetVal() < 0 || probabilities.GetVal() > 2) {
            std::fprintf(stderr, "Fuzzy probability selection was out of range\n");
            return 1;
        }
    }

    std::puts("RecordFile smoke test passed");
    return 0;
}