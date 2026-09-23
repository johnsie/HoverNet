#include "../Game2/Observer.h"
#include "../Util/DllObjectFactory.h"

#include <cstdio>

int main()
{
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_Observer* observer = MR_Observer::New();
    if (observer == nullptr) {
        std::fprintf(stderr, "Could not create the Game2 observer\n");
        return 1;
    }

    observer->Delete();
    std::puts("Observer smoke test passed");
    return 0;
}