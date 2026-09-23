#include "../Model/ShapeCollisions.h"

#include <cstdio>

class TestCylinder : public MR_CylinderShape
{
public:
    TestCylinder(MR_Int32 axisX, MR_Int32 axisY, MR_Int32 radius, MR_Int32 zMin, MR_Int32 zMax) :
        mAxisX(axisX), mAxisY(axisY), mRadius(radius), mZMin(zMin), mZMax(zMax) {}

    MR_Int32 AxisX() const override { return mAxisX; }
    MR_Int32 AxisY() const override { return mAxisY; }
    MR_Int32 RayLen() const override { return mRadius; }
    MR_Int32 ZMin() const override { return mZMin; }
    MR_Int32 ZMax() const override { return mZMax; }

private:
    MR_Int32 mAxisX;
    MR_Int32 mAxisY;
    MR_Int32 mRadius;
    MR_Int32 mZMin;
    MR_Int32 mZMax;
};

int main()
{
    TestCylinder player(0, 0, 10, 0, 20);
    TestCylinder nearbyObstacle(15, 0, 10, 0, 20);
    TestCylinder distantObstacle(25, 0, 10, 0, 20);
    TestCylinder elevatedObstacle(15, 0, 10, 20, 40);
    MR_ContactSpec contact;

    if (!MR_DetectActorContact(&player, &nearbyObstacle, contact) ||
        contact.mZMin != 0 || contact.mZMax != 20 ||
        MR_DetectActorContact(&player, &distantObstacle, contact) ||
        MR_DetectActorContact(&player, &elevatedObstacle, contact)) {
        std::fprintf(stderr, "Cylinder collision detection failed\n");
        return 1;
    }

    std::puts("Model collision smoke test passed");
    return 0;
}