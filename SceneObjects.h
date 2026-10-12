#ifndef SCENEOBJECTS_H
#define SCENEOBJECTS_H

#include "Math3D.h"
#include <vector>

namespace Scene
{
    struct ServiceVisualState
    {
        float carYaw = 0.0f;

        float dirtAmount = 1.0f;       // 1 = dirty patch visible, 0 = clean
        float polishAmount = 0.0f;     // 0 = normal stainless, 1 = polished
        float scanProgress = -1.0f;    // -1 = scanner beam hidden, 0..1 = sweep
        float shineProgress = -1.0f;   // -1 = hidden, 0..1 = final highlight sweep
        float warningPulse = 0.0f;     // 0..1 amber warning beacon intensity
        float completeGlow = 0.0f;     // 0..1 green service-complete indicator
        float gripperOpen = 0.0f;      // 0 = closed, 1 = open
        float polisherAngle = 0.0f;    // visible rotation marker angle in degrees
        float driverAngle = 0.0f;      // impact-driver socket rotation in degrees
        float lugProgress[5] = { 0,0,0,0,0 }; // 0 = loose/protruding, 1 = seated
        float wheelServiceGlow = 0.0f; // green confirmation ring after tightening
        bool spongeOnTable = true;
        bool spongeAttached = false;
    };

    struct SceneSwitches
    {
        bool  ceilingLights  = true;   // the overhead fluorescent bays
        bool  neonSign       = true;   // the SMART AUTO SERVICE sign
        bool  vehicleLights  = true;   // front + rear light bars (a DRL: on by default)
        bool  hazardBeacon   = false;  // flashing amber bay beacon
        float carDrive       = 1.0f;
        float wheelSpin      = 0.0f;   // degrees, rolls with carDrive
    };

    void setSwitches(const SceneSwitches& s);
    const SceneSwitches& switches();

    // ---- setup ------------------------------------------------------------
    // Remembers the shader program and caches its uniform locations.
    void init(unsigned int shaderProgram);

    // ---- Level 1: drawing helpers -----------------------------------------

    Mat4 place(const Vec3& pos, const Vec3& size, const Vec3& rotDeg);

    // Convenience overload for parts that need no rotation.
    Mat4 place(const Vec3& pos, const Vec3& size);

    // Optional metallic/roughness values feed the lightweight material shader.
    // Existing call-sites keep the same behaviour through the defaults.
    void drawCube(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                  float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);
    void drawCylinder(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                      float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);

    // The same call on the 48-segment mesh. Use it whenever the camera can see
    // that the shape is round; drawCylinder stays for bolts, pins and casters.
    void drawCylS(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                  float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);
    void drawSphere(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                    float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);

    void  setSurfaceFinish(float polish, float clearCoat = -1.0f, float anisotropy = -1.0f,
                           int materialType = -1);
    void  clearSurfaceFinish();

    // Mirrors the F6 material-map switch into the sampled-texture path.
    void  setTexturesEnabled(bool on);

    void  setSurfaceDirt(float dirt);
    float currentPolish();

    // Teaching/presentation mode: reveal existing primitive draw calls progressively.
    void beginTeachingDraw(int visibleLimit, int highlightFrom = -1);
    void endTeachingDraw();
    int  teachingDrawCount();

    float hash01(int index);
    float jitter(int index, float amount);
    Vec3  tintVary(const Vec3& c, int index, float percent);
    Mat4  messy(int index, float posAmt, float yawAmt, float scaleAmt);

    // ---- surface detail helpers (SECTION 1c) ------------------------------
    // A panel with a dark recess behind it and a lit chamfer along its top edge.
    void drawPanel(const Mat4& parent, const Vec3& pos, const Vec3& size,
                   const Vec3& color, const Vec3& rotDeg = Vec3(0.0f, 0.0f, 0.0f));
    // A dark patch on the floor under an object - cheap ambient occlusion.
    void drawContactPatch(const Mat4& parent, const Vec3& centre, float sizeX, float sizeZ);

    // ---- Level 2: the scene objects ---------------------------------------

    void drawGarage(const Mat4& parent);
    void drawGarageDoor(const Mat4& parent, float doorOffset);
    void drawHydraulicLift(const Mat4& parent, float liftHeight);

    // Floor-mounted approach ramp at the front of the main lift.  Bolted to
    // the slab, so it does not rise with the platform.
    void drawLiftApproachRamp(const Mat4& parent);

    // How far the vehicle travels between the apron and the lift, in metres.
    // Main.cpp needs it to roll the wheels by the distance actually covered.
    float carDriveDistance();
    // Rolling radius of the drawn wheel, so the spin matches the distance covered.
    float carWheelRadius();
    Mat4 serviceCarMatrix(float liftHeight, float carYaw);
    // Vehicle-local position of a headlight point light (side -1 left, +1 right).
    Vec3 headlightLocalPosition(int side);
    void drawCar(const Mat4& parent, const Vec3& bodyColor, float sidePolish = 0.0f,
                 float wheelSpinDeg = 0.0f, bool lightsOn = false,
                 float dirtAmount = 0.0f);

    void drawWheel(const Mat4& parent, const Vec3& localPos, float radius, float width,
                   float outward, float spinDeg = 0.0f);

    // Painted floor markings + charging bollard for one parking bay.
    void drawParkingBay(const Mat4& parent);
    void drawRobotArm(const Mat4& parent, float baseYaw, float joint1Angle, float joint2Angle,
        float wristPitch, float wristRoll, int toolType,
        float gripperOpen = 0.0f, bool spongeAttached = false, float polisherAngle = 0.0f, float driverAngle = 0.0f);
    const float kToolDockTopY = 0.975f;
    const float kToolRackScale = 1.0f;

    // Where each tool lives on the open table (2 x 2 layout).
    Vec3 toolDockPosition(int toolType);

    void drawRobotToolStation(const Mat4& parent, int selectedTool, bool spongeOnTable = true);
    void drawScanner(const Mat4& parent);
    void drawNeonSign(const Mat4& parent);

    // ---- the daylit exterior seen through the open door --------------------
    void drawExterior(const Mat4& parent);

    void drawOilDrum(const Mat4& parent, int index);
    void drawRackingBay(const Mat4& parent, int levels, int index);
    void drawFireExtinguisher(const Mat4& parent);

    // One tube segment: a unit cylinder rotated so its axis lies along
    // (to - from).  Used by the sweep below, and useful on its own.
    void drawTubeSegment(const Mat4& parent, const Vec3& from, const Vec3& to,
                         float radius, const Vec3& color,
                         float metallic = 0.0f, float roughness = 0.75f,
                         int materialType = -1);

    // Sweeps a tube of the given radius along a cubic Bezier.
    void drawBezierHose(const Mat4& parent,
                        const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                        float radius, int segments, const Vec3& color,
                        int materialType = -1);

    void drawLiftHydraulicHose(const Mat4& parent, int index);
    void drawCeilingFixture(const Mat4& parent, float dropLen);
    void drawWorkStool(const Mat4& parent, int index);
    void drawBoxStack(const Mat4& parent, int count, int index);
    void drawWorkbench(const Mat4& parent, int index);
    void drawSupportColumn(const Mat4& parent, float height);
    void drawWheelChock(const Mat4& parent, int index);

    // ---- the whole scene ---------------------------------------------------
    void drawScene(float doorOffset,
        float liftHeight,
        float robotBaseYaw,
        float joint1Angle,
        float joint2Angle,
        float wristPitch,
        float wristRoll,
        int robotTool,
        const ServiceVisualState& service);

    float liftTopY(float liftHeight);

    struct RtItem
    {
        int type = 0;
        Mat4 m;
        Vec3 a, b, c;
        Vec3 color;
        bool emissive = false;
    };

    void recordInspectionGeometry(std::vector<RtItem>& out, float liftHeight,
                                  float robotBaseYaw, float joint1Angle, float joint2Angle,
                                  float wristPitch, float wristRoll, int robotTool,
                                  const ServiceVisualState& service);

    // The monitor: housing drawn with the scene, screen quad drawn by Main.cpp.
    void drawInspectionMonitorHousing(const Mat4& parent);
    // How many items the last recording took from the lift / car / robot (for the report).
    void lastRecordCounts(int& lift, int& car, int& robot);
    Mat4 inspectionMonitorMatrix();
}

#endif // SCENEOBJECTS_H
