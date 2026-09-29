#include "../Game2/Observer.h"
#include "../Util/DllObjectFactory.h"

#include <cstdio>
#include <string>

int main()
{
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_Observer* observer = MR_Observer::New();
    if (observer == nullptr) {
        std::fprintf(stderr, "Could not create the Game2 observer\n");
        return 1;
    }

    if (std::string(MR_Observer::GetFuelWarningForPercent(0)) != "FUEL EMPTY" ||
        std::string(MR_Observer::GetFuelWarningForPercent(19)) != "LOW FUEL" ||
        std::string(MR_Observer::GetFuelWarningForPercent(20)) != "") {
        std::fprintf(stderr, "Fuel warning accessibility thresholds are incorrect\n");
        observer->Delete();
        return 1;
    }

    observer->Delete();
    std::puts("Observer smoke test passed");
    return 0;
}