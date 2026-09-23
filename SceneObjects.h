#ifndef SCENEOBJECTS_H
#define SCENEOBJECTS_H

// ============================================================================
//  SceneObjects.h  -  Every object in the garage, built from the unit cube.
// ============================================================================
//
//  HOW TO READ THIS FILE
//  ---------------------
//  There are two levels of function here.
//
//  LEVEL 1 - the drawing helpers:
//        Scene::drawCube(...)      draws one transformed copy of the unit cube
//        Scene::drawCylinder(...)  draws one transformed copy of the cylinder
//        Scene::place(...)         builds a model matrix from pos/size/rotation
//
//  LEVEL 2 - the object functions:
//        Scene::drawCar(...), Scene::drawGarage(...), Scene::drawRobotArm(...)
//        Each one is just a list of Level-1 calls with comments.
//
//  HIERARCHY
//  ---------
//  Every object function takes a `parent` matrix. Everything the function
//  draws is multiplied by that parent. So moving the whole car is a matter of
//  changing one matrix, not editing thirty positions:
//
//      Mat4 carTransform = translate(0, liftTop, -1) * rotateY(0);
//      Scene::drawCar(carTransform);
//
//  ANIMATION READINESS
//  -------------------
//  The functions that will move later already take their animation value as a
//  parameter. In this laboratory version we simply pass constants:
//
//      drawGarageDoor(parent, 0.0f);      // 0 = closed, 6.2 = fully open
//      drawHydraulicLift(parent, 0.0f);   // 0 = fully lowered, 4.0 = top
//      drawRobotArm(parent, -40.0f, -26.0f);
//      drawCeilingFan(parent, 0.0f);
//
//  Later those constants become variables driven by time. No geometry changes.
// ============================================================================

#include "Math3D.h"

namespace Scene
{
    // ---- setup ------------------------------------------------------------
    // Remembers the shader program and caches its uniform locations.
    void init(unsigned int shaderProgram);

    // ---- Level 1: drawing helpers -----------------------------------------

    // Builds a model matrix:  translate(pos) * rotate(rotDeg) * scale(size)
    // Read right to left: SCALE the unit cube first, then ROTATE it, then
    // TRANSLATE it into position.
    Mat4 place(const Vec3& pos, const Vec3& size, const Vec3& rotDeg);

    // Convenience overload for parts that need no rotation.
    Mat4 place(const Vec3& pos, const Vec3& size);

    // Optional metallic/roughness values feed the lightweight material shader.
    // Existing call-sites keep the same behaviour through the defaults.
    void drawCube(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                  float metallic = 0.0f, float roughness = 0.65f);
    void drawCylinder(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                      float metallic = 0.0f, float roughness = 0.65f);

    // The same call on the 48-segment mesh. Use it whenever the camera can see
    // that the shape is round; drawCylinder stays for bolts, pins and casters.
    void drawCylS(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                  float metallic = 0.0f, float roughness = 0.65f);
    void drawSphere(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                    float metallic = 0.0f, float roughness = 0.65f);

    // Teaching/presentation mode: reveal existing primitive draw calls progressively.
    void beginTeachingDraw(int visibleLimit, int highlightFrom = -1);
    void endTeachingDraw();
    int  teachingDrawCount();

    // ---- imperfection helpers (SECTION 1b) --------------------------------
    // Deterministic pseudo-random from an instance index, so repeated objects
    // vary but never flicker.
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
    // `bodyColor` is the only difference between the five cars in the scene.
    // One model, five paint jobs - no extra geometry of any kind.
    void drawCar(const Mat4& parent, const Vec3& bodyColor);

    // `radius` and `width` can differ front to rear on the angular pickup.
    void drawWheel(const Mat4& parent, const Vec3& localPos, float radius, float width, float outward);

    // Painted floor markings + charging bollard for one parking bay.
    void drawParkingBay(const Mat4& parent);
    void drawRobotArm(const Mat4& parent, float baseYaw, float joint1Angle, float joint2Angle,
        float wristPitch, float wristRoll, int toolType);
    void drawRobotToolStation(const Mat4& parent, int selectedTool);
    void drawScanner(const Mat4& parent);
    void drawCeilingFan(const Mat4& parent, float fanAngle);
    void drawLamp(const Mat4& parent);
    void drawNeonSign(const Mat4& parent);
    void drawToolCabinet(const Mat4& parent);
    void drawShelf(const Mat4& parent);
    void drawProps(const Mat4& parent);

    // ---- the daylit exterior seen through the open door --------------------
    void drawExterior(const Mat4& parent);

    // ---- SECTION 18: the repeated equipment library -------------------------
    // Every one of these appears at least three times in the scene. Each is
    // written once and called many times with a different parent matrix; the
    // `index` argument feeds the hash so no two instances are identical.
    void drawOilDrum(const Mat4& parent, int index);
    void drawToolChest(const Mat4& parent, int index);
    void drawRackingBay(const Mat4& parent, int levels, int index);
    void drawPartsBin(const Mat4& parent, int index);
    void drawFireExtinguisher(const Mat4& parent);
    void drawExtractionHose(const Mat4& parent, int index);
    void drawCeilingFixture(const Mat4& parent, float dropLen);
    void drawDrainPanStack(const Mat4& parent, int count, int index);
    void drawJerryCan(const Mat4& parent, int index);
    void drawWorkStool(const Mat4& parent, int index);
    void drawBoxStack(const Mat4& parent, int count, int index);
    void drawWorkbench(const Mat4& parent, int index);
    void drawSupportColumn(const Mat4& parent, float height, float neonSide);
    void drawFloorDrain(const Mat4& parent);
    void drawSprinkler(const Mat4& parent);
    void drawWheelChock(const Mat4& parent, int index);
    void drawConduitRun(const Mat4& parent, float length);
    void drawPipeRun(const Mat4& parent, float length, int index);

    // ---- the whole scene ---------------------------------------------------
    void drawScene(float doorOffset,
        float liftHeight,
        float robotBaseYaw,
        float joint1Angle,
        float joint2Angle,
        float wristPitch,
        float wristRoll,
        int robotTool,
        float fanAngle);

    // Height of the top surface of the lift runways for a given lift height.
    // The car is placed on exactly this value, so raising the lift later will
    // carry the car with it automatically.
    float liftTopY(float liftHeight);
}

#endif // SCENEOBJECTS_H
