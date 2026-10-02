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
    // Runtime-only visual values used by the simple automatic service demo.
    // The automation itself lives in Main.cpp; SceneObjects only renders these
    // values, which keeps the project easy to explain in a viva.
    struct ServiceVisualState
    {
        //  The arm is bolted beside the vehicle, so it can only ever point at
        //  the flank nearest to it - no amount of reach lets a shoulder-mounted
        //  arm aim INWARD at the far side.  The lift therefore carries an
        //  alignment turntable, and the vehicle is rotated to present each
        //  flank in turn.  0 = as driven on, 180 = turned about.
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

    // ---- SECTION 0c: SCENE SWITCHES ---------------------------------------
    //  Everything in here is something the operator can physically switch on
    //  or off in a real workshop, which is why it lives in one struct that the
    //  keyboard writes and the scene reads.  Main.cpp owns the values; the
    //  renderer only obeys them.
    struct SceneSwitches
    {
        bool  ceilingLights  = true;   // the overhead fluorescent bays
        bool  neonSign       = true;   // the SMART AUTO SERVICE sign
        bool  vehicleLights  = true;   // front + rear light bars (a DRL: on by default)
        bool  hazardBeacon   = false;  // flashing amber bay beacon
        float fanSpeed       = 1.0f;   // 0 = stopped .. 2 = fast
        float carDrive       = 0.0f;   // 0 = on the lift, 1 = out on the apron
        float wheelSpin      = 0.0f;   // degrees, rolls with carDrive

        // A vehicle crossing the road outside, left to right.  Its wheel spin
        // is derived from the distance it has covered, exactly as the serviced
        // car's is, so it reads as driving rather than sliding.
        bool  trafficOn      = true;
        float trafficX       = -16.0f; // world X of the passing vehicle
        float trafficSpin    = 0.0f;   // degrees
    };

    void setSwitches(const SceneSwitches& s);
    const SceneSwitches& switches();

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
                  float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);
    void drawCylinder(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                      float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);

    // The same call on the 48-segment mesh. Use it whenever the camera can see
    // that the shape is round; drawCylinder stays for bolts, pins and casters.
    void drawCylS(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                  float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);
    void drawSphere(const Mat4& model, const Vec3& color, float emissive = 0.0f,
                    float metallic = 0.0f, float roughness = 0.65f, int materialType = -1);

    // ---- surface finish (SECTION 0b) --------------------------------------
    //  Material = what it is made of.  Finish = how it has been worked.
    //  Everything drawn between setSurfaceFinish() and clearSurfaceFinish()
    //  is shaded with the given finish:
    //
    //      polish      0 = as manufactured, 1 = machine polished
    //      clearCoat   < 0 keeps the material default
    //      anisotropy  < 0 keeps the material default
    //
    //  Polishing lowers micro-roughness, adds a smooth specular top layer and
    //  reduces grain directionality, so a polished panel is measurably - and
    //  visibly - shinier than the untouched panel beside it.
    //  `materialType` (>= 0) makes every draw inside the block use that
    //  material unless the call site names one of its own.  It is how the
    //  robot arm is made uniformly matte powder-coat without editing forty
    //  individual draw calls.
    void  setSurfaceFinish(float polish, float clearCoat = -1.0f, float anisotropy = -1.0f,
                           int materialType = -1);
    void  clearSurfaceFinish();

    // Mirrors the F6 material-map switch into the sampled-texture path.
    void  setTexturesEnabled(bool on);

    //  Road grime lying on the surface, 0..1.  It is a shaded film, not
    //  geometry: it darkens the albedo, roughens it and - the part that makes
    //  it read as dirt rather than grey paint - drops the metallic term,
    //  because dust is a dielectric and kills the reflection underneath.
    void  setSurfaceDirt(float dirt);
    float currentPolish();

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
    //  `sidePolish` (0..1) polishes ONLY the side body panels, which is what
    //  the robot's rotary polisher actually works on.  Roof, hood, tailgate,
    //  cladding and bumpers keep their factory finish, so the before/after
    //  contrast is visible on the same vehicle in the same frame.
    void drawCar(const Mat4& parent, const Vec3& bodyColor, float sidePolish = 0.0f,
                 float wheelSpinDeg = 0.0f, bool lightsOn = false,
                 float dirtAmount = 0.0f);

    // `radius` and `width` can differ front to rear on the angular pickup.
    //  `spinDeg` rolls the wheel about its axle.  It is driven from the
    //  distance the vehicle has travelled, so the wheels turn at exactly the
    //  rate the ground speed demands instead of free-spinning.
    void drawWheel(const Mat4& parent, const Vec3& localPos, float radius, float width,
                   float outward, float spinDeg = 0.0f);

    // Painted floor markings + charging bollard for one parking bay.
    void drawParkingBay(const Mat4& parent);
    void drawRobotArm(const Mat4& parent, float baseYaw, float joint1Angle, float joint2Angle,
        float wristPitch, float wristRoll, int toolType,
        float gripperOpen = 0.0f, bool spongeAttached = false, float polisherAngle = 0.0f, float driverAngle = 0.0f);
    // Top of a dock's connector, in the tool station's own frame.  The service
    // sequence solves its approach poses against this, so the rack and the
    // robot can never disagree about where a tool actually sits.
    //  Tools are drawn on the rack at the SAME scale the robot draws them, so
    //  a head does not change size the instant it is picked up.  At 0.62 the
    //  exchange was visibly a swap between two different objects.
    const float kToolDockTopY = 0.975f;
    const float kToolRackScale = 1.0f;

    // Where each tool lives on the open table (2 x 2 layout).
    Vec3 toolDockPosition(int toolType);

    void drawRobotToolStation(const Mat4& parent, int selectedTool, bool spongeOnTable = true);
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
    // ---- SECTION 1d: CURVE-BASED GEOMETRY ---------------------------------
    //
    //  Everything else in this project is a transformed cube, cylinder or
    //  sphere.  These two build geometry from a PARAMETRIC CURVE instead.
    //
    //  A cubic Bezier is defined by four control points:
    //
    //      B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3,  t in [0,1]
    //
    //  and its tangent, which is what orients the tube, is the derivative:
    //
    //      B'(t) = 3(1-t)^2 (P1-P0) + 6(1-t)t (P2-P1) + 3t^2 (P3-P2)
    //
    //  The curve passes through P0 and P3 and is pulled toward P1 and P2
    //  without touching them, which is exactly how a hose hangs: fixed at both
    //  ends, sagging under its own weight in between.

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
        float fanAngle,
        const ServiceVisualState& service);

    // Height of the top surface of the lift runways for a given lift height.
    // The car is placed on exactly this value, so raising the lift later will
    // carry the car with it automatically.
    float liftTopY(float liftHeight);
}

#endif // SCENEOBJECTS_H
