#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "Math3D.h"
#include "Camera.h"
#include "Shader.h"
#include "Primitives.h"
#include "SceneObjects.h"

static int   gWidth = 1280;
static int   gHeight = 720;
static const char* kTitle = "SmartGarage AUTO SERVICE v6 - Cybertruck Realism / PBR Rendering";

static Camera gCamera;
static bool   gMouseCaptured = true;
static bool   gFirstMouse = true;
static double gLastX = 0.0;
static double gLastY = 0.0;
// True for offline captures: the frame must come from the command line alone,
// so stray mouse or keyboard input must not move the camera.
static bool   gInputLocked = false;
// Scripted demonstration recorder (--demo); see SCRIPTED DEMONSTRATION below.
static bool   gDemoActive = false;
static int    gRtScale = 1;            // --rtscale: inspection-view resolution multiplier
static bool   gDemoHeld[GLFW_KEY_LAST + 1] = { false };
// A held key, either pressed on the keyboard or held by the demo script.
static bool keyDown(GLFWwindow* window, int key)
{
    return gDemoHeld[key] || (!gInputLocked && glfwGetKey(window, key) == GLFW_PRESS);
}

//  Scene / display toggles
static bool  gWireframe = false;

// Rendering feature toggles.  Cook-Torrance PBR is the photoreal default; Gouraud remains
// available as an explicit coursework comparison mode.
static int   gShadingMode = 0; // 0 Cook-Torrance PBR, 1 Gouraud (per vertex), 2 Phong (per fragment)
static const char* kShadingNames[3] = { "Cook-Torrance PBR (per fragment)",
                                        "Gouraud (Phong model per vertex)",
                                        "Phong (Phong model per fragment)" };
static bool  gTexturesEnabled = true;
static bool  gNormalMapsEnabled = true;
static bool  gShadowsEnabled = true;
static bool  gReflectionsEnabled = true;
static bool  gBloomEnabled = true;
static float gExposure = 0.74f;

static bool  gOrthographic = false;
static float gOrthoHalfHeight = 4.0f;   // vertical half-extent, world units

static bool  gMsaaEnabled = true;     // 4x multisampled HDR target
static float gIblIntensity = 0.72f;   // strength of the environment probe
static float gBloomStrength = 0.14f;
static float gVignette = 0.30f;
static float gGrain = 0.006f;

enum LightingPreset { LIGHT_WORKSHOP = 0, LIGHT_STUDIO, LIGHT_NIGHT, LIGHT_PRESET_COUNT };
static LightingPreset gLightingPreset = LIGHT_WORKSHOP;
// ---- ray-traced underbody inspection monitor (state; code further down) ----
static bool  gRtEnabled = true;
static bool  gRtLarge = false;          // key 9: enlarged inspection view
static bool  gRtFull = false;           // capture only: overlay fills the frame
static bool  gRtShadows = true;         // key -: inspection-light shadow rays
static int   gRtPreset = 3;             // key =: camera preset; default follows the robot-side work area
static float gRtYaw = 0.0f;             // Insert / Delete: orbit the camera
static int   gRtLightIdx = 2;           // key PageDown: light preset; default inspection lamp is on robot side
static bool  gRtLightCustom = false;    // numpad moves the light freely
static Vec3  gRtLight(-2.6f, 0.60f, -6.0f);
static void  rtApplySize();
static bool gNight = false;   // key 0: constant daylight outside <-> night
static const char* kLightingPresetNames[LIGHT_PRESET_COUNT] =
    { "Workshop", "Studio (3-point product rig)", "Night" };

// Showcase mode orbits the camera around the serviced vehicle at a fixed
// radius and elevation.  It exists so the model can be presented hands-free.
static bool  gShowcase = false;
static float gShowcaseAngle = 0.0f;
static float gShowcaseRadius = 9.4f;
static float gShowcaseHeight = 2.55f;
static float gShowcaseSpeed = 11.0f;   // degrees per second

// Rolling frame-time statistics for the on-screen/console render report.
static float gFrameMsAccum = 0.0f;
static int   gFrameMsCount = 0;
static float gFrameMsAvg = 0.0f;
static bool  gDoorOpen = false;   // the vehicle is outside; the building is shut
static bool  gLiftRaised = false;

enum TeachingObject {
    TEACH_SCENE = 0, TEACH_CAR, TEACH_LIFT, TEACH_ROBOT,
    TEACH_CUBE, TEACH_CYLINDER, TEACH_SMOOTH_CYLINDER, TEACH_SPHERE
};
static TeachingObject gTeachObject = TEACH_SCENE;
static int gTeachStage = 0;
static bool gTeachNoHighlight = false;   // capture-only: draw the stage without the amber highlight

static const int kAllRemaining = 100000;
static const int kCarLimits[]   = { 1, 2, 4, 5, 6, 7, 13, 25, 33, kAllRemaining };
static const char* kCarStages[] = {
    "stainless side skin - one folded flank with the daylight opening cut out",
    "hood, angled front fascia, tailgate and underbody floor pan",
    "single-pane windscreen, side glazing, panoramic roof + vault cover",
    "flush black belt-line seals and B-pillar blackout",
    "sealed five-facet 3D wheel-arch cladding",
    "full 3D protruding front + rear bumper blocks",
    "light channels, front fascia, mirrors, front white / rear red lens bars",
    "rocker cladding, door shut-lines, flush handles, charge-port door",
    "bed-rail tie-downs riding the tapering vault rail",
    "four all-terrain wheels with tread blocks, aero covers + brake discs"
};

static const int kLiftLimits[]   = { 40, 81, 122, 124, 132, kAllRemaining };
static const char* kLiftStages[] = {
    "base rails + anchors", "left scissor stack + hydraulic ram",
    "right scissor stack + hydraulic ram", "platform cross beams",
    "left runway", "right runway"
};

static const int kRobotLimits[]   = { 13, 15, 16, 17, 21, 27, 31, 35, 38, kAllRemaining };
static const char* kRobotStages[] = {
    "base plate + four anchor bolts", "pedestal column + status band", "turntable disc",
    "shoulder yoke", "shoulder joint (barrel, flanges, sphere)", "lower arm + bands + cable conduit",
    "elbow joint (barrel, flanges, sphere)", "forearm + band + conduit", "wrist sphere + block + coupler",
    "quick-change tool head (scanner)"
};

//  ANIMATION PARAMETERS - constants for this stage, variables later.
static float gDoorOffset = 0.0f;    // 4.9 = fully open, 0.0 = closed (the start state)
static float gDoorTarget = 0.0f;    // where gDoorOffset is currently easing towards
static float gLiftHeight = 0.0f;    // 0.0 = lowered, 4.0 = fully raised
static float gLiftTarget = 0.0f;    // where gLiftHeight is currently easing towards
static float gRobotBaseYaw = 60.0f;   // parked sideways; TOP round turntable only, square pedestal fixed
static float gJoint1Angle = -44.0f;   // shoulder pitch
static float gJoint2Angle = -30.0f;   // elbow pitch
static float gWristPitch = 74.0f;     // end-effector pitch (parks the head level)
static float gWristRoll = 0.0f;       // end-effector roll
static int   gRobotTool = 0;           // 0 scanner, 1 driver, 2 gripper, 3 polisher

static Scene::SceneSwitches gSwitches;

// Arrival animation: 1 = waiting outside on the apron, 0 = parked on the lift.
// The scene opens with the vehicle outside, so the target starts at 1 too.
static float gCarDriveTarget = 1.0f;
static const float kCarDriveSpeed = 0.092f;  // fraction of the run per second (~11 s end to end)
static bool  gSafetyStop = false;       // true for a frame when collision prevention blocks motion

static const float kDoorSpeed = 0.80f;   
static const float kLiftSpeed = 0.55f;     
static const float kArmSpeed = 35.0f;      
static const float kBaseYawSpeed = 45.0f;  
static const float kWristSpeed = 55.0f;   

enum ServiceState
{
    SERVICE_IDLE = 0,
    SERVICE_DOOR_OPEN_IN,   // the vehicle is outside; open up for it
    SERVICE_DRIVE_IN,       // it drives in and up the ramp onto the lift
    SERVICE_DOOR_CLOSE,
    SERVICE_LIFT_UP,
    SERVICE_SCAN,
    SERVICE_GET_SPONGE,
    SERVICE_CLEAN,
    SERVICE_RETURN_SPONGE,
    SERVICE_GET_POLISHER,
    SERVICE_POLISH,
    SERVICE_GET_DRIVER,
    SERVICE_WHEEL_TIGHTEN,
    SERVICE_RETURN_DRIVER,
    SERVICE_ROBOT_HOME,
    SERVICE_LIFT_DOWN,
    SERVICE_DOOR_OPEN,
    SERVICE_DRIVE_OUT,      // the serviced vehicle reverses out onto the apron
    SERVICE_COMPLETE
};

struct RobotPose
{
    float yaw;
    float joint1;
    float joint2;
    float wristPitch;
    float wristRoll;
};

static ServiceState gServiceState = SERVICE_IDLE;
static int   gServicePhase = 0;
static float gServiceTimer = 0.0f;
static Scene::ServiceVisualState gServiceVisual;

// Inspection coverage, 0..100, reported in the status line while scanning.
static int gScanPercent = 0;

static const RobotPose POSE_HOME          = {  60.0f, -44.0f,  -30.0f, 74.0f, 0.0f };
// Compact retract pose.  The older "safe" pose actually stretched across the
// vehicle.  This one folds the arm back toward its pedestal before any yaw.
static const RobotPose POSE_SAFE_HIGH     = {   0.0f,  30.0f, -140.0f, 60.0f, 0.0f };
static const RobotPose POSE_DOCK_HOVER[4] = {
    {  177.61f,  -15.56f,  -118.46f,  134.03f, 0.0f },   // scanner
    {  176.14f,    5.36f,  -137.92f,  132.56f, 0.0f },   // impact driver
    { -167.96f,  -15.51f,  -116.58f,  132.09f, 0.0f },   // gripper
    { -160.95f,    0.55f,  -137.92f,  137.37f, 0.0f },   // polisher
};
static const RobotPose POSE_DOCK_SEAT[4] = {
    {  177.61f,  -23.40f,  -121.56f,  144.96f, 0.0f },   // scanner
    {  176.14f,   -5.44f,  -142.91f,  148.34f, 0.0f },   // impact driver
    { -167.96f,  -22.97f,  -119.88f,  142.85f, 0.0f },   // gripper
    { -160.95f,  -10.85f,  -142.10f,  152.95f, 0.0f },   // polisher
};
static const RobotPose POSE_RACK_SAFE    = { -173.95f,  -4.68f, -127.77f, 132.45f, 0.0f };
static const RobotPose POSE_SPONGE_HOVER = { -173.95f,  -6.42f, -128.85f, 135.27f, 0.0f };
static const RobotPose POSE_SPONGE_GRASP = { -173.95f, -15.67f, -132.61f, 148.28f, 0.0f };

static const RobotPose PATH_LINE_HIGH[4] = {
    {    8.87f,   7.62f,  -132.90f,  215.27f, 0.0f },
    {    0.56f,   8.22f,  -133.39f,  215.17f, 0.0f },
    {   -7.76f,   7.75f,  -133.01f,  215.26f, 0.0f },
    {  -15.77f,   6.28f,  -131.77f,  215.50f, 0.0f },
};
static const RobotPose PATH_LINE_MID[4] = {
    {    8.87f,   6.03f,  -134.26f,  218.24f, 0.0f },
    {    0.56f,   6.65f,  -134.77f,  218.12f, 0.0f },
    {   -7.76f,   6.18f,  -134.38f,  218.21f, 0.0f },
    {  -15.77f,   4.70f,  -133.11f,  218.41f, 0.0f },
};
static const RobotPose PATH_LINE_LOW[4] = {
    {    8.87f,   4.33f,  -135.56f,  221.24f, 0.0f },
    {    0.56f,   4.90f,  -136.08f,  221.18f, 0.0f },
    {   -7.76f,   4.45f,  -135.68f,  221.23f, 0.0f },
    {  -15.77f,   3.03f,  -134.39f,  221.36f, 0.0f },
};
static const RobotPose PATH_CLEAN_HIGH[4] = {
    {    8.87f,  10.78f,  -135.36f,  214.59f, 0.0f },
    {    0.56f,  11.43f,  -135.84f,  214.41f, 0.0f },
    {   -7.76f,  10.93f,  -135.47f,  214.55f, 0.0f },
    {  -15.77f,   9.35f,  -134.28f,  214.93f, 0.0f },
};
static const RobotPose PATH_CLEAN_MID[4] = {
    {    8.87f,   9.15f,  -136.79f,  217.64f, 0.0f },
    {    0.56f,   9.78f,  -137.27f,  217.50f, 0.0f },
    {   -7.76f,   9.30f,  -136.90f,  217.60f, 0.0f },
    {  -15.77f,   7.75f,  -135.68f,  217.93f, 0.0f },
};
static const RobotPose PATH_CLEAN_LOW[4] = {
    {    8.87f,   7.35f,  -138.15f,  220.80f, 0.0f },
    {    0.56f,   7.98f,  -138.65f,  220.67f, 0.0f },
    {   -7.76f,   7.50f,  -138.26f,  220.76f, 0.0f },
    {  -15.77f,   6.00f,  -137.01f,  221.01f, 0.0f },
};

static const RobotPose PATH_SCAN_FRONT[3] = {
    {   26.06f,  -11.60f,   -95.96f,  131.55f, 0.0f },
    {   21.91f,   -9.51f,   -96.94f,  130.45f, 0.0f },
    {   17.50f,   -7.30f,   -97.18f,  128.48f, 0.0f },
};
static const RobotPose PATH_SCAN_WIND[3] = {
    {   12.47f,   -3.31f,  -100.83f,  142.13f, 0.0f },
    {    5.80f,   -0.74f,   -99.97f,  138.71f, 0.0f },
    {   -0.66f,   -1.08f,   -98.59f,  137.66f, 0.0f },
};
static const RobotPose PATH_SCAN_ROOF[3] = {
    {   -4.47f,   -6.43f,   -89.24f,  103.67f, 0.0f },
    {   -6.70f,   -6.68f,   -89.20f,  103.88f, 0.0f },
    {   -8.90f,   -7.01f,   -89.07f,  104.08f, 0.0f },
};
static const RobotPose PATH_SCAN_UPPER[3] = {
    {   12.69f,    9.89f,  -135.39f,  215.51f, 0.0f },
    {   -3.44f,   11.04f,  -136.26f,  215.22f, 0.0f },
    {  -18.59f,    8.43f,  -134.26f,  215.83f, 0.0f },
};
static const RobotPose PATH_SCAN_LOWER[3] = {
    {   12.69f,    5.37f,  -138.81f,  223.44f, 0.0f },
    {   -3.44f,    6.47f,  -139.73f,  223.26f, 0.0f },
    {  -18.59f,    3.99f,  -137.60f,  223.61f, 0.0f },
};
static const RobotPose PATH_SCAN_REAR[3] = {
    {  -27.72f,    6.25f,  -134.63f,  218.37f, 0.0f },
    {  -34.05f,    2.70f,  -131.47f,  218.77f, 0.0f },
    {  -39.04f,   -3.03f,  -125.72f,  218.75f, 0.0f },
};

static const RobotPose POSE_SCAN_TOP_SAFE  = {   -5.59f,   -6.87f,   -78.44f,   91.31f, 0.0f };
static const RobotPose POSE_SCAN_SIDE_SAFE = {   -6.66f,   24.28f,  -143.00f,  208.72f, 0.0f };

static const RobotPose POSE_SCAN_OUT       = {   -7.70f,   34.79f,  -129.51f,  156.72f, 0.0f };

static const RobotPose POSE_PLATFORM_UP = { 0.00f, 0.00f, -90.00f, 180.00f, 0.0f };  // yaw: unchanged
static const float kPlatformPullBack = 25.0f;   // shoulder lean, degrees
static const RobotPose POSE_SIDE_RETRACT   = {   -3.55f,   10.90f,  -139.01f,  218.11f, 0.0f };

static const float kDriverTipReach = 1.15f;   // wrist -> socket tip, metres
static const float kModelToolReach = 0.95f;   // what the safety model assumes

static const RobotPose POSE_WHEEL_SAFE    = {  24.50f, 30.00f, -140.00f, 200.00f, 0.0f };
static const RobotPose POSE_LUG_1         = {  24.91f,  5.05f, -139.01f, 223.96f, 0.0f };
static const RobotPose POSE_LUG_2         = {  23.17f,  4.53f, -140.31f, 225.79f, 0.0f };
static const RobotPose POSE_LUG_3         = {  23.84f,  2.20f, -141.19f, 228.99f, 0.0f };
static const RobotPose POSE_LUG_4         = {  25.96f,  1.38f, -140.41f, 229.03f, 0.0f };
static const RobotPose POSE_LUG_5         = {  26.60f,  3.15f, -139.08f, 225.93f, 0.0f };

static const float kServiceLiftTarget = 1.10f;

static const float kJoint1Min = -110.0f, kJoint1Max = 55.0f;
static const float kJoint2Min = -150.0f, kJoint2Max = 120.0f;
static const float kBaseYawMin = -180.0f, kBaseYawMax = 180.0f;
static const float kWristPitchMin = -270.0f, kWristPitchMax = 270.0f;
static const float kWristRollMin = -180.0f, kWristRollMax = 180.0f;

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static float moveToward(float current, float target, float maxDelta)
{
    float diff = target - current;
    if (diff > maxDelta)  return current + maxDelta;
    if (diff < -maxDelta) return current - maxDelta;
    return target;
}

static void setTeachingMode(TeachingObject obj); // forward declaration

static float shortestAngleDelta(float from, float to);   // defined below
static float wrapAngle180(float a);                     // defined below

// Drive the turntable toward a target angle, taking the shortest way round,
// at a rate that reads as a powered plate rather than a snap.
static bool moveCarYawToward(float target, float dt, float degPerSec = 34.0f)
{
    const float d = shortestAngleDelta(gServiceVisual.carYaw, target);
    const float step = degPerSec * dt;
    if (std::fabs(d) <= step)
    {
        gServiceVisual.carYaw = wrapAngle180(target);
        return true;
    }
    gServiceVisual.carYaw += (d > 0.0f) ? step : -step;
    return false;
}

static void updateCarDrive(float dt)
{
    const float before = gSwitches.carDrive;
    gSwitches.carDrive = moveToward(gSwitches.carDrive, gCarDriveTarget, kCarDriveSpeed * dt);

    const float travelled = (gSwitches.carDrive - before) * Scene::carDriveDistance();

    const float wheelRadius = Scene::carWheelRadius();
    gSwitches.wheelSpin += (travelled / wheelRadius) * 180.0f / PI_F;
    if (gSwitches.wheelSpin > 360.0f)  gSwitches.wheelSpin = std::fmod(gSwitches.wheelSpin, 360.0f);
    if (gSwitches.wheelSpin < -360.0f) gSwitches.wheelSpin = std::fmod(gSwitches.wheelSpin, 360.0f);
}

static bool serviceRunning()
{
    return gServiceState != SERVICE_IDLE && gServiceState != SERVICE_COMPLETE;
}

static const char* serviceStatusText()
{
    switch (gServiceState)
    {
    case SERVICE_IDLE:           return "READY - ENTER starts automatic surface service";
    case SERVICE_DOOR_OPEN_IN:   return "SERVICE STARTED - opening the door for the vehicle";
    case SERVICE_DRIVE_IN:       return "vehicle driving in and up the lift ramp";
    case SERVICE_DOOR_CLOSE:     return "vehicle in position - closing garage door";
    case SERVICE_LIFT_UP:        return "raising vehicle to service height";
    case SERVICE_SCAN:
    {
        if (gServicePhase >= 23) return "SURFACE CONTAMINATION DETECTED on left flank";
        static char buf[96];
        const char* region = "approaching";
        switch (gServicePhase)
        {
        case 1:  region = "front / hood";        break;
        case 3:  region = "windscreen";          break;
        case 5:  region = "roof";                break;
        case 9:  region = "near side, upper";    break;
        case 11: region = "near side, lower";    break;
        case 13: region = "near rear / bed";     break;
        case 15: region = "turning the vehicle"; break;
        case 16: region = "far side, upper";     break;
        case 18: region = "far side, lower";     break;
        case 20: region = "far rear / bed";      break;
        case 22: region = "returning to datum";  break;
        default: break;
        }
        std::snprintf(buf, sizeof(buf), "SCANNING VEHICLE: %d%%  (%s)", gScanPercent, region);
        return buf;
    }
    case SERVICE_GET_SPONGE:     return "changing to gripper and picking up cleaning sponge";
    case SERVICE_CLEAN:          return "CLEANING SIDE PANEL";
    case SERVICE_RETURN_SPONGE:  return "returning cleaning sponge";
    case SERVICE_GET_POLISHER:   return "changing to rotary polisher";
    case SERVICE_POLISH:         return "POLISHING CLEANED PANEL";
    case SERVICE_GET_DRIVER:     return "changing to impact driver for wheel fasteners";
    case SERVICE_WHEEL_TIGHTEN:  return "CHECKING / TIGHTENING FRONT WHEEL FASTENERS";
    case SERVICE_RETURN_DRIVER:  return "WHEEL FASTENERS SECURED - returning driver";
    case SERVICE_ROBOT_HOME:     return "service finished - robot returning HOME";
    case SERVICE_LIFT_DOWN:      return "SERVICE COMPLETE - lowering vehicle";
    case SERVICE_DOOR_OPEN:      return "SERVICE COMPLETE - opening garage door";
    case SERVICE_DRIVE_OUT:      return "SERVICE COMPLETE - vehicle leaving the garage";
    case SERVICE_COMPLETE:       return "SERVICE COMPLETE - ENTER runs again; R resets";
    default:                     return "automatic service";
    }
}

static float shortestAngleDelta(float from, float to);   // defined below

static bool robotPoseReached(const RobotPose& p)
{
    // Compared on the WRAPPED difference: 359 deg and 1 deg are two degrees
    // apart, not 358.
    return std::fabs(shortestAngleDelta(gRobotBaseYaw, p.yaw)) < 0.65f &&
           std::fabs(shortestAngleDelta(gJoint1Angle, p.joint1)) < 0.65f &&
           std::fabs(shortestAngleDelta(gJoint2Angle, p.joint2)) < 0.65f &&
           std::fabs(shortestAngleDelta(gWristPitch, p.wristPitch)) < 0.65f &&
           std::fabs(shortestAngleDelta(gWristRoll, p.wristRoll)) < 0.65f;
}

// Shortest signed angular difference, wrapped into [-180, +180].
static float shortestAngleDelta(float from, float to)
{
    float d = to - from;
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static float wrapAngle180(float a)
{
    while (a > 180.0f)  a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

struct PoseMove
{
    RobotPose from{};
    RobotPose to{};
    float elapsed = 0.0f;
    float duration = 0.0f;
    bool  active = false;
};
static PoseMove gMove;

static bool sameTarget(const RobotPose& a, const RobotPose& b)
{
    return std::fabs(a.yaw - b.yaw) < 1e-4f &&
           std::fabs(a.joint1 - b.joint1) < 1e-4f &&
           std::fabs(a.joint2 - b.joint2) < 1e-4f &&
           std::fabs(a.wristPitch - b.wristPitch) < 1e-4f &&
           std::fabs(a.wristRoll - b.wristRoll) < 1e-4f;
}


static void cancelRobotMove() { gMove.active = false; }

static const float kRobotSlewRate = 52.0f;   // degrees per second at scale 1.0

static bool moveRobotTowardPose(const RobotPose& p, float dt, float speedScale = 1.0f)
{
    if (!gMove.active || !sameTarget(gMove.to, p))
    {
        gMove.from.yaw = gRobotBaseYaw;
        gMove.from.joint1 = gJoint1Angle;
        gMove.from.joint2 = gJoint2Angle;
        gMove.from.wristPitch = gWristPitch;
        gMove.from.wristRoll = gWristRoll;
        gMove.to = p;
        gMove.elapsed = 0.0f;

        float widest = 0.0f;
        widest = std::fmax(widest, std::fabs(shortestAngleDelta(gMove.from.yaw, p.yaw)));
        widest = std::fmax(widest, std::fabs(shortestAngleDelta(gMove.from.joint1, p.joint1)));
        widest = std::fmax(widest, std::fabs(shortestAngleDelta(gMove.from.joint2, p.joint2)));
        widest = std::fmax(widest, std::fabs(shortestAngleDelta(gMove.from.wristPitch, p.wristPitch)));
        widest = std::fmax(widest, std::fabs(shortestAngleDelta(gMove.from.wristRoll, p.wristRoll)));

        const float rate = kRobotSlewRate * ((speedScale > 0.05f) ? speedScale : 0.05f);
        gMove.duration = std::fmax(0.22f, widest / rate);
        gMove.active = true;
    }

    gMove.elapsed += dt;
    float u = (gMove.duration > 0.0001f) ? (gMove.elapsed / gMove.duration) : 1.0f;
    if (u > 1.0f) u = 1.0f;
    const float e = u * u * (3.0f - 2.0f * u);          // ease in / ease out

    gRobotBaseYaw = gMove.from.yaw        + shortestAngleDelta(gMove.from.yaw,        p.yaw)        * e;
    gJoint1Angle  = gMove.from.joint1     + shortestAngleDelta(gMove.from.joint1,     p.joint1)     * e;
    gJoint2Angle  = gMove.from.joint2     + shortestAngleDelta(gMove.from.joint2,     p.joint2)     * e;
    gWristPitch   = gMove.from.wristPitch + shortestAngleDelta(gMove.from.wristPitch, p.wristPitch) * e;
    gWristRoll    = gMove.from.wristRoll  + shortestAngleDelta(gMove.from.wristRoll,  p.wristRoll)  * e;

    if (u >= 1.0f)
    {
        // Land exactly on the target, and fold the base back into [-180, 180]
        // so repeated shortest-path moves cannot wind it up over time.
        gRobotBaseYaw = wrapAngle180(p.yaw);
        gJoint1Angle = p.joint1;
        gJoint2Angle = p.joint2;
        gWristPitch = p.wristPitch;
        gWristRoll = p.wristRoll;
        gMove.active = false;
        return true;
    }
    return false;
}

static int gServiceWaypoint = 0;

static bool moveRobotAlongPath(const RobotPose* path, int count, float dt,
                               float speedScale = 1.0f)
{
    if (gServiceWaypoint >= count) return true;
    if (moveRobotTowardPose(path[gServiceWaypoint], dt, speedScale))
        ++gServiceWaypoint;
    return gServiceWaypoint >= count;
}

// How far along the current path we are, 0..1.  The dirt and polish effects
// are driven from this so they track the tool rather than a free-running timer.
static float pathProgress(int count)
{
    if (count <= 1) return 1.0f;
    const float p = static_cast<float>(gServiceWaypoint) / static_cast<float>(count - 1);
    return (p > 1.0f) ? 1.0f : p;
}

static RobotPose gPlatformFrom;      // the pose the ascent started from

static bool moveToPlatformSafe(float dt, float speedScale = 0.95f)
{
    if (gServiceWaypoint == 0)
    {
        // Capture where the arm is once, before the first stage starts
        // moving it, so the pull-back target does not chase the arm.
        if (!gMove.active)
        {
            gPlatformFrom.yaw = gRobotBaseYaw;
            gPlatformFrom.joint1 = gJoint1Angle;
            gPlatformFrom.joint2 = gJoint2Angle;
            gPlatformFrom.wristPitch = gWristPitch;
            gPlatformFrom.wristRoll = 0.0f;
        }
        const RobotPose pullBack = { gPlatformFrom.yaw, gPlatformFrom.joint1 + kPlatformPullBack,
                                     gPlatformFrom.joint2, gPlatformFrom.wristPitch, 0.0f };
        if (moveRobotTowardPose(pullBack, dt, speedScale)) gServiceWaypoint = 1;
        return false;
    }
    if (gServiceWaypoint == 1)
    {
        const RobotPose raised = { gPlatformFrom.yaw, POSE_PLATFORM_UP.joint1,
                                   POSE_PLATFORM_UP.joint2, POSE_PLATFORM_UP.wristPitch, 0.0f };
        return moveRobotTowardPose(raised, dt, speedScale);   // up, same yaw - done
    }
    return true;
}

// True when the arm is in the raised platform shape (at any yaw).
static bool armIsRaised()
{
    return std::fabs(gJoint1Angle - POSE_PLATFORM_UP.joint1) < 0.5f &&
           std::fabs(gJoint2Angle - POSE_PLATFORM_UP.joint2) < 0.5f &&
           std::fabs(shortestAngleDelta(gWristPitch, POSE_PLATFORM_UP.wristPitch)) < 0.5f;
}

static bool platformDescend(const RobotPose& to, int& stage, float dt, float speedScale = 0.90f)
{
    if (stage == 0)
    {
        // Swing round while still high.
        const RobotPose overhead = { to.yaw, POSE_PLATFORM_UP.joint1,
                                     POSE_PLATFORM_UP.joint2, POSE_PLATFORM_UP.wristPitch, 0.0f };
        if (moveRobotTowardPose(overhead, dt, speedScale)) stage = 1;
        return false;
    }
    if (stage == 1)
    {
        // Lower into the working shape, but still leaning back off the panel.
        const RobotPose standOff = { to.yaw, to.joint1 + kPlatformPullBack,
                                     to.joint2, to.wristPitch, 0.0f };
        if (moveRobotTowardPose(standOff, dt, speedScale)) stage = 2;
        return false;
    }
    return moveRobotTowardPose(to, dt, speedScale);   // lean in to the panel
}

static int gToolChangeFrom = 0;

static void nextServicePhase();   // defined just below

static bool runToolChange(int nextTool, float dt)
{
    switch (gServicePhase)
    {
    case 0:     // clear of the vehicle, over the rack
        gToolChangeFrom = (gRobotTool >= 0) ? gRobotTool : nextTool;
        if (gServiceWaypoint == 0 && armIsRaised())
        {
            const RobotPose overRack = { POSE_RACK_SAFE.yaw, POSE_PLATFORM_UP.joint1,
                                         POSE_PLATFORM_UP.joint2, POSE_PLATFORM_UP.wristPitch, 0.0f };
            if (moveRobotTowardPose(overRack, dt, 1.10f)) gServiceWaypoint = 1;
            break;
        }
        gServiceWaypoint = 1;
        if (moveRobotTowardPose(POSE_RACK_SAFE, dt, 1.25f)) nextServicePhase();
        break;
    case 1:     // line up over the dock this tool came from
        if (moveRobotTowardPose(POSE_DOCK_HOVER[gToolChangeFrom], dt, 0.85f)) nextServicePhase();
        break;
    case 2:     // straight down onto the dock, slowly
        if (moveRobotTowardPose(POSE_DOCK_SEAT[gToolChangeFrom], dt, 0.26f)) nextServicePhase();
        break;
    case 3:     // settle, then release
        if (gServiceTimer > 0.40f) { gRobotTool = -1; nextServicePhase(); }
        break;
    case 4:     // bare wrist lifts clear
        if (moveRobotTowardPose(POSE_DOCK_HOVER[gToolChangeFrom], dt, 0.45f)) nextServicePhase();
        break;
    case 5:     // hold: bare connector + both tools on the table
        if (gServiceTimer > 0.55f) nextServicePhase();
        break;
    case 6:     // across to the next dock, still bare
        if (moveRobotTowardPose(POSE_DOCK_HOVER[nextTool], dt, 0.85f)) nextServicePhase();
        break;
    case 7:     // down onto the new tool
        if (moveRobotTowardPose(POSE_DOCK_SEAT[nextTool], dt, 0.24f)) nextServicePhase();
        break;
    case 8:     // latch
        if (gServiceTimer > 0.40f) { gRobotTool = nextTool; nextServicePhase(); }
        break;
    case 9:     // lift the new tool off its dock
        if (moveRobotTowardPose(POSE_DOCK_HOVER[nextTool], dt, 0.45f)) nextServicePhase();
        break;
    default:
        return true;
    }
    return false;
}

static void nextServicePhase()
{
    ++gServicePhase;
    gServiceTimer = 0.0f;
    gServiceWaypoint = 0;
}

static void setServiceState(ServiceState state)
{
    gServiceState = state;
    gServicePhase = 0;
    gServiceTimer = 0.0f;
    gServiceWaypoint = 0;
    std::cout << "[Service] -> " << serviceStatusText() << std::endl;
}

static void resetServiceDemo()
{
    gServiceState = SERVICE_IDLE;
    gServicePhase = 0;
    gServiceTimer = 0.0f;
    gServiceWaypoint = 0;
    cancelRobotMove();
    gServiceVisual = Scene::ServiceVisualState();

    gDoorOpen = false;
    gDoorOffset = 0.0f;
    gDoorTarget = 0.0f;

    // The vehicle starts outside, on the apron, facing the door.
    gSwitches.carDrive = 1.0f;
    gCarDriveTarget = 1.0f;
    gSwitches.wheelSpin = 0.0f;

    gLiftRaised = false;
    gLiftHeight = 0.0f;
    gLiftTarget = 0.0f;

    gRobotBaseYaw = POSE_HOME.yaw;
    gJoint1Angle = POSE_HOME.joint1;
    gJoint2Angle = POSE_HOME.joint2;
    gWristPitch = POSE_HOME.wristPitch;
    gWristRoll = POSE_HOME.wristRoll;
    gRobotTool = 0;

    // Keep the diagnostic monitor aligned with the side the robot actually services.
    gRtPreset = 3;
    gRtYaw = 0.0f;
    gRtLightIdx = 2;
    gRtLightCustom = false;

    std::cout << "Automatic service reset. Press ENTER to start.\n";
}

static void startServiceDemo()
{
    if (gTeachObject != TEACH_SCENE)
        setTeachingMode(TEACH_SCENE);

    if (gServiceState == SERVICE_COMPLETE)
        resetServiceDemo();

    if (gServiceState != SERVICE_IDLE) return;

    gServiceVisual.dirtAmount = 1.0f;
    gServiceVisual.polishAmount = 0.0f;
    gServiceVisual.scanProgress = -1.0f;
    gServiceVisual.shineProgress = -1.0f;
    gServiceVisual.spongeOnTable = true;
    gServiceVisual.spongeAttached = false;
    gServiceVisual.gripperOpen = 0.0f;
    gServiceVisual.completeGlow = 0.0f;
    gServiceVisual.warningPulse = 0.0f;
    gServiceVisual.driverAngle = 0.0f;
    gServiceVisual.wheelServiceGlow = 0.0f;
    for (int i = 0; i < 5; ++i) gServiceVisual.lugProgress[i] = 0.0f;

    gRobotTool = 0;
    setServiceState(SERVICE_DOOR_OPEN_IN);
    std::cout << "Automatic surface inspection / cleaning / polishing service started.\n";
}

static void updateServiceAutomation(float dt)
{
    if (gServiceState == SERVICE_IDLE || gServiceState == SERVICE_COMPLETE)
    {
        if (gServiceState == SERVICE_COMPLETE)
            gServiceVisual.completeGlow = 1.0f;
        return;
    }

    gServiceTimer += dt;
    gServiceVisual.warningPulse = 0.0f;
    gServiceVisual.completeGlow = 0.0f;

    if (gRobotTool == 3 && (gServiceState == SERVICE_GET_POLISHER ||
                            gServiceState == SERVICE_POLISH ||
                            gServiceState == SERVICE_ROBOT_HOME))
    {
        gServiceVisual.polisherAngle += 620.0f * dt;
        if (gServiceVisual.polisherAngle > 360.0f)
            gServiceVisual.polisherAngle = std::fmod(gServiceVisual.polisherAngle, 360.0f);
    }

    if (gRobotTool == 1 && gServiceState == SERVICE_WHEEL_TIGHTEN)
    {
        gServiceVisual.driverAngle += 980.0f * dt;
        if (gServiceVisual.driverAngle > 360.0f)
            gServiceVisual.driverAngle = std::fmod(gServiceVisual.driverAngle, 360.0f);
    }

    switch (gServiceState)
    {
    case SERVICE_DOOR_OPEN_IN:
        gDoorOpen = true;
        gDoorTarget = 4.9f;
        gCarDriveTarget = 1.0f;
        if (gDoorOffset >= 4.80f && gServiceTimer > 1.30f)
            setServiceState(SERVICE_DRIVE_IN);
        break;

    case SERVICE_DRIVE_IN:
        gCarDriveTarget = 0.0f;
        if (gSwitches.carDrive <= 0.002f && gServiceTimer > 0.60f)
            setServiceState(SERVICE_DOOR_CLOSE);
        break;

    case SERVICE_DOOR_CLOSE:
        gDoorOpen = false;
        gDoorTarget = 0.0f;
        gServiceVisual.warningPulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(gServiceTimer * 9.0f));
        if (gDoorOffset <= 0.035f && gServiceTimer > 0.45f)
            setServiceState(SERVICE_LIFT_UP);
        break;

    case SERVICE_LIFT_UP:
        gLiftRaised = true;
        gLiftTarget = kServiceLiftTarget;
        if (std::fabs(gLiftHeight - kServiceLiftTarget) < 0.025f && gServiceTimer > 0.55f)
            setServiceState(SERVICE_SCAN);
        break;

    case SERVICE_SCAN:
    {
        gRobotTool = 0;
        switch (gServicePhase)
        {
        case 0:  if (moveRobotTowardPose(POSE_SCAN_TOP_SAFE, dt, 1.20f)) nextServicePhase(); break;

        case 1:  if (moveRobotAlongPath(PATH_SCAN_FRONT, 3, dt, 0.32f)) nextServicePhase();
                 gScanPercent = static_cast<int>(pathProgress(3) * 12.0f); break;
        case 2:  gScanPercent = 12;
                 if (moveRobotTowardPose(POSE_SCAN_TOP_SAFE, dt, 0.90f)) nextServicePhase(); break;

        case 3:  if (moveRobotAlongPath(PATH_SCAN_WIND, 3, dt, 0.30f)) nextServicePhase();
                 gScanPercent = 12 + static_cast<int>(pathProgress(3) * 12.0f); break;
        case 4:  gScanPercent = 24;
                 if (moveRobotTowardPose(POSE_SCAN_TOP_SAFE, dt, 0.90f)) nextServicePhase(); break;

        case 5:  if (moveRobotAlongPath(PATH_SCAN_ROOF, 3, dt, 0.30f)) nextServicePhase();
                 gScanPercent = 24 + static_cast<int>(pathProgress(3) * 12.0f); break;
        case 6:  gScanPercent = 36;
                 if (moveRobotTowardPose(POSE_SCAN_TOP_SAFE, dt, 0.90f)) nextServicePhase(); break;

        // Out, then down: moving clear of the vehicle before descending.
        case 7:  if (moveRobotTowardPose(POSE_SCAN_OUT, dt, 1.05f)) nextServicePhase(); break;
        case 8:  if (moveRobotTowardPose(POSE_SCAN_SIDE_SAFE, dt, 0.95f)) nextServicePhase(); break;

        // ---- near flank -------------------------------------------------
        case 9:  gServiceVisual.scanProgress = pathProgress(3);
                 if (moveRobotAlongPath(PATH_SCAN_UPPER, 3, dt, 0.34f)) nextServicePhase();
                 gScanPercent = 36 + static_cast<int>(pathProgress(3) * 10.0f); break;
        case 10: gServiceVisual.scanProgress = -1.0f;
                 if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.80f)) nextServicePhase(); break;
        case 11: gServiceVisual.scanProgress = 1.0f - pathProgress(3);
                 if (moveRobotAlongPath(PATH_SCAN_LOWER, 3, dt, 0.34f)) nextServicePhase();
                 gScanPercent = 46 + static_cast<int>(pathProgress(3) * 10.0f); break;
        case 12: gServiceVisual.scanProgress = -1.0f;
                 if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.80f)) nextServicePhase(); break;
        case 13: if (moveRobotAlongPath(PATH_SCAN_REAR, 3, dt, 0.36f)) nextServicePhase();
                 gScanPercent = 56 + static_cast<int>(pathProgress(3) * 8.0f); break;

        // ---- turn the vehicle about ---------------------------------------
        case 14: gScanPercent = 64;
                 // Up and out of the way before the plate moves - see POSE_PLATFORM_UP.
                 if (moveToPlatformSafe(dt, 0.95f)) nextServicePhase(); break;
        case 15: // the arm is raised clear; only now may the turntable run,
                 // and only once it has stopped does the arm come back down.
                 if (gServiceWaypoint == 0)
                 {
                     if (moveCarYawToward(180.0f, dt)) gServiceWaypoint = 1;
                 }
                 else
                 {
                     int stage = gServiceWaypoint - 1;
                     const bool down = platformDescend(PATH_SCAN_UPPER[0], stage, dt);
                     gServiceWaypoint = stage + 1;
                     if (down) nextServicePhase();
                 }
                 break;

        // ---- far flank, same poses, opposite side of the vehicle ----------
        case 16: gServiceVisual.scanProgress = pathProgress(3);
                 if (moveRobotAlongPath(PATH_SCAN_UPPER, 3, dt, 0.34f)) nextServicePhase();
                 gScanPercent = 64 + static_cast<int>(pathProgress(3) * 12.0f); break;
        case 17: gServiceVisual.scanProgress = -1.0f;
                 if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.80f)) nextServicePhase(); break;
        case 18: gServiceVisual.scanProgress = 1.0f - pathProgress(3);
                 if (moveRobotAlongPath(PATH_SCAN_LOWER, 3, dt, 0.34f)) nextServicePhase();
                 gScanPercent = 76 + static_cast<int>(pathProgress(3) * 12.0f); break;
        case 19: gServiceVisual.scanProgress = -1.0f;
                 if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.80f)) nextServicePhase(); break;
        case 20: if (moveRobotAlongPath(PATH_SCAN_REAR, 3, dt, 0.36f)) nextServicePhase();
                 gScanPercent = 88 + static_cast<int>(pathProgress(3) * 12.0f); break;

        // ---- back to the delivered orientation ----------------------------
        case 21: gScanPercent = 100;
                 if (moveToPlatformSafe(dt, 0.95f)) nextServicePhase(); break;
        case 22: if (moveCarYawToward(0.0f, dt)) nextServicePhase(); break;
        case 23: if (gServiceTimer > 1.20f) nextServicePhase(); break;
        default: setServiceState(SERVICE_GET_SPONGE); break;
        }
        break;
    }

    case SERVICE_GET_SPONGE:
        
        if (gServicePhase <= 9) { runToolChange(2, dt); break; }
        if (gServicePhase == 10)
        {
            gServiceVisual.gripperOpen = moveToward(gServiceVisual.gripperOpen, 1.0f, dt * 2.4f);
            if (moveRobotTowardPose(POSE_SPONGE_HOVER, dt, 0.90f)) nextServicePhase();
        }
        else if (gServicePhase == 11)
        {
            gServiceVisual.gripperOpen = 1.0f;
            if (moveRobotTowardPose(POSE_SPONGE_GRASP, dt, 0.28f)) nextServicePhase();
        }
        else if (gServicePhase == 12)
        {
            gServiceVisual.gripperOpen = moveToward(gServiceVisual.gripperOpen, 0.0f, dt * 1.8f);
            if (gServiceVisual.gripperOpen <= 0.04f && gServiceTimer > 0.45f)
            {
                gServiceVisual.spongeOnTable = false;
                gServiceVisual.spongeAttached = true;
                nextServicePhase();
            }
        }
        else if (gServicePhase == 13)
        {
            if (moveRobotTowardPose(POSE_SPONGE_HOVER, dt, 0.50f)) nextServicePhase();
        }
        else
        {
            setServiceState(SERVICE_CLEAN);
        }
        break;

    case SERVICE_CLEAN:
        if (gServicePhase == 0)
        {
            if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 1.20f)) nextServicePhase();
        }
        else if (gServicePhase == 1)
        {
            if (moveRobotTowardPose(PATH_CLEAN_HIGH[0], dt, 0.75f)) nextServicePhase();
        }
        else if (gServicePhase == 2)
        {
            // (kept so the band phases below keep their numbering)
            nextServicePhase();
        }
        else if (gServicePhase == 3)
        {
            const bool done = moveRobotAlongPath(PATH_CLEAN_HIGH, 4, dt, 0.38f);
            gServiceVisual.dirtAmount = 1.0f - 0.35f * pathProgress(4);
            if (done) { gServiceVisual.dirtAmount = 0.65f; nextServicePhase(); }
        }
        else if (gServicePhase == 4)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_CLEAN_MID[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 5)
        {
            const bool done = moveRobotAlongPath(PATH_CLEAN_MID, 4, dt, 0.38f);
            gServiceVisual.dirtAmount = 0.65f - 0.35f * pathProgress(4);
            if (done) { gServiceVisual.dirtAmount = 0.30f; nextServicePhase(); }
        }
        else if (gServicePhase == 6)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_CLEAN_LOW[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 7)
        {
            const bool done = moveRobotAlongPath(PATH_CLEAN_LOW, 4, dt, 0.38f);
            gServiceVisual.dirtAmount = 0.30f - 0.30f * pathProgress(4);
            if (done) { gServiceVisual.dirtAmount = 0.0f; nextServicePhase(); }
        }
        else if (gServicePhase == 8)
        {
            if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 1.00f)) nextServicePhase();
        }
        else
        {
            setServiceState(SERVICE_RETURN_SPONGE);
        }
        break;

    case SERVICE_RETURN_SPONGE:
      
        if (gServicePhase == 0)
        {
            if (moveRobotTowardPose(POSE_SPONGE_HOVER, dt, 1.10f)) nextServicePhase();
        }
        else if (gServicePhase == 1)
        {
            if (moveRobotTowardPose(POSE_SPONGE_GRASP, dt, 0.28f)) nextServicePhase();
        }
        else if (gServicePhase == 2)
        {
            gServiceVisual.gripperOpen = moveToward(gServiceVisual.gripperOpen, 1.0f, dt * 1.8f);
            if (gServiceVisual.gripperOpen >= 0.96f && gServiceTimer > 0.40f)
            {
                gServiceVisual.spongeAttached = false;
                gServiceVisual.spongeOnTable = true;
                nextServicePhase();
            }
        }
        else if (gServicePhase == 3)
        {
            if (moveRobotTowardPose(POSE_SPONGE_HOVER, dt, 0.50f)) nextServicePhase();
        }
        else
        {
            setServiceState(SERVICE_GET_POLISHER);
        }
        break;

    case SERVICE_GET_POLISHER:
        gServiceVisual.gripperOpen = moveToward(gServiceVisual.gripperOpen, 0.0f, dt * 2.0f);
        if (gServicePhase <= 9) { runToolChange(3, dt); break; }
        setServiceState(SERVICE_POLISH);
        break;

    case SERVICE_POLISH:
        if (gServicePhase == 0)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 1.20f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_LINE_HIGH[0], dt, 0.75f)) nextServicePhase();
        }
        else if (gServicePhase == 1)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_HIGH, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.17f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 0.17f; nextServicePhase(); }
        }
        else if (gServicePhase == 2)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_LINE_MID[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 3)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_MID, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.17f + 0.17f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 0.34f; nextServicePhase(); }
        }
        else if (gServicePhase == 4)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_LINE_LOW[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 5)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_LOW, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.34f + 0.16f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 0.50f; nextServicePhase(); }
        }
        // ---- turn about and do the far flank ------------------------------
        else if (gServicePhase == 6)
        {
            // Fold clear of the vehicle's rotation sweep before the plate runs.
            if (moveToPlatformSafe(dt, 1.00f)) nextServicePhase();
        }
        else if (gServicePhase == 7)
        {
            if (moveCarYawToward(180.0f, dt)) nextServicePhase();
        }
        else if (gServicePhase == 8)
        {
            // Down from POSE_PLATFORM_UP in stages, now the plate has stopped.
            int stage = gServiceWaypoint;
            const bool down = platformDescend(PATH_LINE_HIGH[0], stage, dt, 0.80f);
            gServiceWaypoint = stage;
            if (down) nextServicePhase();
        }
        else if (gServicePhase == 9)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_HIGH, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.50f + 0.17f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 0.67f; nextServicePhase(); }
        }
        else if (gServicePhase == 10)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_LINE_MID[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 11)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_MID, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.67f + 0.17f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 0.84f; nextServicePhase(); }
        }
        else if (gServicePhase == 12)
        {
            if (gServiceWaypoint == 0)
            {
                if (moveRobotTowardPose(POSE_SIDE_RETRACT, dt, 0.85f)) ++gServiceWaypoint;
            }
            else if (moveRobotTowardPose(PATH_LINE_LOW[0], dt, 0.55f)) nextServicePhase();
        }
        else if (gServicePhase == 13)
        {
            const bool done = moveRobotAlongPath(PATH_LINE_LOW, 4, dt, 0.36f);
            gServiceVisual.polishAmount = 0.84f + 0.16f * pathProgress(4);
            if (done) { gServiceVisual.polishAmount = 1.0f; nextServicePhase(); }
        }
        else if (gServicePhase == 14)
        {
            if (moveToPlatformSafe(dt, 1.00f)) nextServicePhase();
        }
        else if (gServicePhase == 15)
        {
            if (moveCarYawToward(0.0f, dt)) nextServicePhase();
        }
        else if (gServicePhase == 16)
        {
            // Let the finished vehicle be looked at for a moment.
            if (gServiceTimer > 1.60f) nextServicePhase();
        }
        else
        {
            setServiceState(SERVICE_GET_DRIVER);
        }
        break;

    case SERVICE_GET_DRIVER:
        if (gServicePhase <= 9) { runToolChange(1, dt); break; }
        if (gServicePhase == 10)
        {
            if (moveRobotTowardPose(POSE_WHEEL_SAFE, dt, 1.10f)) nextServicePhase();
        }
        else
        {
            setServiceState(SERVICE_WHEEL_TIGHTEN);
        }
        break;

    case SERVICE_WHEEL_TIGHTEN:
    {
        // Star-pattern order: 1 -> 3 -> 5 -> 2 -> 4.  Each fastener has an
        // approach phase, a visible tightening phase, and a retract phase.
        static const int order[5] = { 0, 2, 4, 1, 3 };
        static const RobotPose lugPoses[5] = { POSE_LUG_1, POSE_LUG_2, POSE_LUG_3, POSE_LUG_4, POSE_LUG_5 };
        const int lugStep = gServicePhase / 3;
        const int sub = gServicePhase % 3;

        if (lugStep < 5)
        {
            const int lugIndex = order[lugStep];
            if (sub == 0)
            {
                if (moveRobotTowardPose(lugPoses[lugIndex], dt, 0.58f)) nextServicePhase();
            }
            else if (sub == 1)
            {
                gServiceVisual.lugProgress[lugIndex] = moveToward(
                    gServiceVisual.lugProgress[lugIndex], 1.0f, dt * 1.35f);
                if (gServiceVisual.lugProgress[lugIndex] >= 0.999f && gServiceTimer > 0.55f)
                {
                    gServiceVisual.lugProgress[lugIndex] = 1.0f;
                    nextServicePhase();
                }
            }
            else
            {
                // Small retract to the common safe wheel pose before the next nut.
                if (moveRobotTowardPose(POSE_WHEEL_SAFE, dt, 0.62f)) nextServicePhase();
            }
        }
        else
        {
            gServiceVisual.wheelServiceGlow = moveToward(gServiceVisual.wheelServiceGlow, 1.0f, dt * 2.0f);
            if (gServiceVisual.wheelServiceGlow >= 0.999f && gServiceTimer > 0.75f)
                setServiceState(SERVICE_RETURN_DRIVER);
        }
        break;
    }

    case SERVICE_RETURN_DRIVER:
        gServiceVisual.wheelServiceGlow = 1.0f;
        // Last exchange of the run: the driver goes back and the scanner is
        // picked up again, so the cell is left ready for the next vehicle.
        if (gServicePhase <= 9) { runToolChange(0, dt); break; }
        setServiceState(SERVICE_ROBOT_HOME);
        break;

    case SERVICE_ROBOT_HOME:
        // Driver has already been docked. Fold the robot first, rotate the
        // compact arm toward its parked heading, then settle into HOME.
        if (gServicePhase == 0)
        {
            if (moveRobotTowardPose(POSE_SAFE_HIGH, dt, 0.78f)) nextServicePhase();
        }
        else if (gServicePhase == 1)
        {
            RobotPose p = POSE_SAFE_HIGH; p.yaw = 60.0f;
            if (moveRobotTowardPose(p, dt, 0.82f)) nextServicePhase();
        }
        else if (gServicePhase == 2)
        {
            if (moveRobotTowardPose(POSE_HOME, dt, 0.82f)) nextServicePhase();
        }
        else if (gServiceTimer > 0.45f)
        {
            gServiceVisual.completeGlow = 1.0f;
            setServiceState(SERVICE_LIFT_DOWN);
        }
        break;

    case SERVICE_LIFT_DOWN:
        gServiceVisual.completeGlow = 1.0f;
        gLiftRaised = false;
        gLiftTarget = 0.0f;
        if (gLiftHeight <= 0.025f && gServiceTimer > 0.45f)
            setServiceState(SERVICE_DOOR_OPEN);
        break;

    case SERVICE_DOOR_OPEN:
        gServiceVisual.completeGlow = 1.0f;
        gDoorOpen = true;
        gDoorTarget = 4.9f;
        gServiceVisual.warningPulse = 0.28f + 0.52f * (0.5f + 0.5f * std::sin(gServiceTimer * 8.0f));
        if (gDoorOffset >= 4.865f && gServiceTimer > 0.45f)
            setServiceState(SERVICE_DRIVE_OUT);
        break;

    case SERVICE_DRIVE_OUT:
        gServiceVisual.completeGlow = 1.0f;
        gCarDriveTarget = 1.0f;
        if (gSwitches.carDrive >= 0.998f && gServiceTimer > 0.60f)
            setServiceState(SERVICE_COMPLETE);
        break;

    default:
        break;
    }
}

struct SafetyAABB
{
    Vec3 mn;
    Vec3 mx;
};

// Shortest distance between two line segments, for arm self-collision.
static float segmentSegmentDistance(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1)
{
    const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    float s, t;
    if (a < 1e-8f && e < 1e-8f) return length(r);
    if (a < 1e-8f) { s = 0.0f; t = clampf(f / e, 0.0f, 1.0f); }
    else
    {
        const float c = dot(d1, r);
        if (e < 1e-8f) { t = 0.0f; s = clampf(-c / a, 0.0f, 1.0f); }
        else
        {
            const float b = dot(d1, d2), den = a * e - b * b;
            s = (den > 1e-8f) ? clampf((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)      { t = 0.0f; s = clampf(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = clampf((b - c) / a, 0.0f, 1.0f); }
        }
    }
    return length((p0 + d1 * s) - (q0 + d2 * t));
}

static bool segmentHitsAABB(const Vec3& a, const Vec3& b, const SafetyAABB& box, float radius)
{
    Vec3 mn = box.mn - Vec3(radius);
    Vec3 mx = box.mx + Vec3(radius);
    Vec3 d = b - a;
    float tMin = 0.0f, tMax = 1.0f;

    const float av[3] = { a.x, a.y, a.z };
    const float dv[3] = { d.x, d.y, d.z };
    const float lov[3] = { mn.x, mn.y, mn.z };
    const float hiv[3] = { mx.x, mx.y, mx.z };

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(dv[axis]) < 1e-6f)
        {
            if (av[axis] < lov[axis] || av[axis] > hiv[axis]) return false;
        }
        else
        {
            float inv = 1.0f / dv[axis];
            float t1 = (lov[axis] - av[axis]) * inv;
            float t2 = (hiv[axis] - av[axis]) * inv;
            if (t1 > t2) { float temp = t1; t1 = t2; t2 = temp; }
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return false;
        }
    }
    return true;
}

static Vec3 yawRobotPoint(float localX, float y, float localZ, float yawDeg)
{
    const float baseX = -4.60f;
    const float baseZ = -2.20f;
    float a = radians(yawDeg);
    float c = std::cos(a), sn = std::sin(a);
    return Vec3(baseX + c * localX + sn * localZ,
        y,
        baseZ - sn * localX + c * localZ);
}

static float currentToolReach()
{
    switch (gRobotTool)
    {
    case 1:  return 1.150f;  // impact driver, socket tip
    case 2:  return gServiceVisual.spongeAttached ? 1.149f : 1.030f;  // gripper, or sponge seam
    case 3:  return 0.932f;  // polisher, pad face + rotation marker
    default: return 0.930f;  // scanner, lens face
    }
}

static void robotSafetyPoints(float baseYaw, float joint1Deg, float joint2Deg, float wristPitchDeg,
    Vec3& shoulder, Vec3& elbow, Vec3& wrist, Vec3& toolEnd, float toolReachOverride = -1.0f)
{
    const float shoulderY = 1.05f;
    const float L1 = 4.50f;
    const float L2 = 4.00f;
    const float toolReach = (toolReachOverride > 0.0f) ? toolReachOverride : 0.95f;

    float a1 = radians(joint1Deg);
    float a12 = radians(joint1Deg + joint2Deg);
    float aTool = radians(joint1Deg + joint2Deg + wristPitchDeg);

    float ex = -L1 * std::sin(a1);
    float ey = shoulderY + L1 * std::cos(a1);
    float wx = ex - L2 * std::sin(a12);
    float wy = ey + L2 * std::cos(a12);

    float tx = wx + toolReach * std::sin(aTool);
    float ty = wy - toolReach * std::cos(aTool);

    shoulder = yawRobotPoint(0.0f, shoulderY, 0.0f, baseYaw);
    elbow = yawRobotPoint(ex, ey, 0.0f, baseYaw);
    wrist = yawRobotPoint(wx, wy, 0.0f, baseYaw);
    toolEnd = yawRobotPoint(tx, ty, 0.0f, baseYaw);
}

static bool robotPoseSafe(float baseYaw, float joint1, float joint2, float wristPitch, float /*wristRoll*/, float liftHeight, bool allowVehicleServiceContact)
{
    Vec3 s, e, w, t;
    // Test the tool that is actually fitted, not a nominal stub.
    robotSafetyPoints(baseYaw, joint1, joint2, wristPitch, s, e, w, t, currentToolReach());

    const Vec3 pts[4] = { s, e, w, t };
    for (int i = 0; i < 4; ++i)
    {
        if (pts[i].y < 0.18f || pts[i].y > 6.72f) return false;
        if (pts[i].x < -13.15f || pts[i].x > 13.15f) return false;
        if (pts[i].z < -12.20f || pts[i].z > 12.20f) return false;
    }

    {
        const float fold = std::fabs(shortestAngleDelta(0.0f, wristPitch));
        if (fold < 50.0f) return false;
        // ...and the tool must never swing back into the upper arm.
        if (segmentSegmentDistance(w, t, s, e) < 0.32f) return false;
    }

    const SafetyAABB pedestal = { Vec3(-5.47f, 0.0f, -3.07f), Vec3(-3.73f, 0.82f, -1.33f) };

    const SafetyAABB toolStation = { Vec3(-9.15f, 0.0f, -2.85f), Vec3(-6.65f, 0.86f, -0.85f) };

    float carY = Scene::liftTopY(liftHeight);
    const SafetyAABB centralLift = { Vec3(-1.62f, 0.0f, -4.72f), Vec3(1.62f, carY + 0.02f, 0.72f) };
    const float cy = std::fabs(std::cos(radians(gServiceVisual.carYaw)));
    const float sy = std::fabs(std::sin(radians(gServiceVisual.carYaw)));
    const float truckHalfX = cy * 1.34f + sy * 2.95f;
    const float truckHalfZ = sy * 1.34f + cy * 2.95f;
    const float truckMidZ  = -2.00f + 0.025f;
    const SafetyAABB centralTruck = { Vec3(-truckHalfX, carY - 0.02f, truckMidZ - truckHalfZ),
                                      Vec3( truckHalfX, carY + 1.92f, truckMidZ + truckHalfZ) };

    const SafetyAABB vehicleSolid = { Vec3(-1.03f, carY + 0.28f, -4.90f),
                                      Vec3( 1.03f, carY + 1.80f,  0.92f) };
    const SafetyAABB vehicleWheels = { Vec3(-1.24f, carY, -4.25f),
                                       Vec3( 1.24f, carY + 0.99f, 0.25f) };

    const SafetyAABB rearLiftA = { Vec3(-10.25f, 0.0f, 0.45f), Vec3(-6.95f, 3.62f, 5.95f) };
    const SafetyAABB rearLiftB = { Vec3(-10.25f, 0.0f, -9.35f), Vec3(-6.95f, 1.35f, -3.85f) };

    if (segmentHitsAABB(s, e, pedestal, 0.20f) ||
        segmentHitsAABB(e, w, pedestal, 0.18f) ||
        segmentHitsAABB(w, t, pedestal, 0.28f)) return false;

    if (gTeachObject == TEACH_SCENE)
    {
        // Fixed scenery the arm can physically reach.
        const SafetyAABB powerPack  = { Vec3(-4.81f, 0.0f, -4.17f), Vec3(-4.29f, 1.20f, -3.73f) };
        const SafetyAABB liftRamp   = { Vec3(-1.50f, 0.0f,  0.55f), Vec3( 1.50f, 1.12f,  5.30f) };
        const SafetyAABB fixtureA   = { Vec3(-7.21f, 6.30f, -4.55f), Vec3(-5.59f, 7.00f, -4.25f) };
        const SafetyAABB fixtureB   = { Vec3(-7.21f, 6.30f,  0.65f), Vec3(-5.59f, 7.00f,  0.95f) };
        const SafetyAABB hardBoxes[] = { toolStation, rearLiftA, rearLiftB, powerPack, liftRamp,
                                         fixtureA, fixtureB };
        for (int i = 0; i < static_cast<int>(sizeof(hardBoxes) / sizeof(hardBoxes[0])); ++i)
        {
            if (segmentHitsAABB(s, e, hardBoxes[i], 0.20f)) return false;
            if (segmentHitsAABB(e, w, hardBoxes[i], 0.18f)) return false;
            if (segmentHitsAABB(w, t, hardBoxes[i], 0.28f)) return false;
        }

        if (segmentHitsAABB(s, e, centralLift, 0.20f) ||
            segmentHitsAABB(e, w, centralLift, 0.18f) ||
            segmentHitsAABB(w, t, centralLift, 0.22f)) return false;

        // The turntable disc overhangs the lift frame: radius 2.10 m about the
        // lift axis, at platform height.
        {
            const Vec3 segA[3] = { s, e, w }, segB[3] = { e, w, t };
            const float rad[3] = { 0.20f, 0.18f, 0.22f };
            const float discTop = carY + 0.006f;
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i <= 12; ++i)
                {
                    const Vec3 p = segA[k] + (segB[k] - segA[k]) * (static_cast<float>(i) / 12.0f);
                    const float rr = std::sqrt(p.x * p.x + (p.z + 2.0f) * (p.z + 2.0f));
                    if (rr < 2.10f + rad[k] && p.y < discTop + rad[k] && p.y > discTop - 0.17f - rad[k])
                        return false;
                }
        }

        // The upper arm is never allowed near the vehicle at all.
        if (segmentHitsAABB(s, e, centralTruck, 0.20f)) return false;

        if (!allowVehicleServiceContact)
        {
            // Transit: keep the whole arm out of the approach volume.
            if (segmentHitsAABB(e, w, centralTruck, 0.18f) ||
                segmentHitsAABB(w, t, centralTruck, 0.28f)) return false;
        }
        else
        {
            if (segmentHitsAABB(e, w, centralTruck, 0.18f)) return false;
            if (segmentHitsAABB(w, t, vehicleSolid, 0.010f)) return false;
            if (segmentHitsAABB(w, t, vehicleWheels, 0.010f)) return false;
        }
    }

    return true;
}

static bool liftPositionSafe(float candidateHeight)
{
    if (candidateHeight < 0.0f || candidateHeight > 3.80f) return false;
    if (gTeachObject != TEACH_SCENE) return true;
    return robotPoseSafe(gRobotBaseYaw, gJoint1Angle, gJoint2Angle,
        gWristPitch, gWristRoll, candidateHeight, false);
}

static void resetCamera()
{
    gCamera.position = Vec3(4.60f, 2.90f, 10.20f);
    gCamera.yaw = -104.0f;
    gCamera.pitch = -8.0f;
    gCamera.fov = 60.0f;
}

static int teachingStageCount()
{
    if (gTeachObject == TEACH_CAR)   return static_cast<int>(sizeof(kCarLimits) / sizeof(kCarLimits[0]));
    if (gTeachObject == TEACH_LIFT)  return static_cast<int>(sizeof(kLiftLimits) / sizeof(kLiftLimits[0]));
    if (gTeachObject == TEACH_ROBOT) return static_cast<int>(sizeof(kRobotLimits) / sizeof(kRobotLimits[0]));
    return 0;
}

static int teachingLimit()
{
    if (gTeachObject == TEACH_CAR)   return kCarLimits[gTeachStage];
    if (gTeachObject == TEACH_LIFT)  return kLiftLimits[gTeachStage];
    if (gTeachObject == TEACH_ROBOT) return kRobotLimits[gTeachStage];
    return -1;
}

static int teachingHighlightFrom()
{
    if (gTeachStage <= 0) return 1;
    if (gTeachObject == TEACH_CAR)   return kCarLimits[gTeachStage - 1] + 1;
    if (gTeachObject == TEACH_LIFT)  return kLiftLimits[gTeachStage - 1] + 1;
    if (gTeachObject == TEACH_ROBOT) return kRobotLimits[gTeachStage - 1] + 1;
    return -1;
}

static const char* teachingStageName()
{
    if (gTeachObject == TEACH_CAR)   return kCarStages[gTeachStage];
    if (gTeachObject == TEACH_LIFT)  return kLiftStages[gTeachStage];
    if (gTeachObject == TEACH_ROBOT) return kRobotStages[gTeachStage];
    return "full scene";
}

static const char* teachingObjectName()
{
    if (gTeachObject == TEACH_CAR) return "CAR";
    if (gTeachObject == TEACH_LIFT) return "SCISSOR LIFT";
    if (gTeachObject == TEACH_ROBOT) return "ROBOT ARM";
    if (gTeachObject == TEACH_CUBE) return "UNIT CUBE";
    if (gTeachObject == TEACH_CYLINDER) return "28-SEGMENT CYLINDER";
    if (gTeachObject == TEACH_SMOOTH_CYLINDER) return "48-SEGMENT CYLINDER";
    if (gTeachObject == TEACH_SPHERE) return "UV SPHERE";
    return "FULL SCENE";
}

static bool isProgressiveTeachingObject()
{
    return gTeachObject == TEACH_CAR || gTeachObject == TEACH_LIFT || gTeachObject == TEACH_ROBOT;
}

static void printPrimitiveMath()
{
    std::cout << "\n[Primitive math] " << teachingObjectName() << "\n";
    if (gTeachObject == TEACH_CUBE)
    {
        std::cout << "  Cube local range: x,y,z in [-0.5,+0.5].\n";
        std::cout << "  24 stored vertices = 6 faces x 4 vertices (flat normals).\n";
        std::cout << "  36 indices = 12 triangles. Example back face:\n";
        std::cout << "    v0=(+0.5,-0.5,-0.5), v1=(-0.5,-0.5,-0.5),\n";
        std::cout << "    v2=(-0.5,+0.5,-0.5), v3=(+0.5,+0.5,-0.5)\n";
        std::cout << "    triangles: (0,1,2) and (2,3,0).\n";
    }
    else if (gTeachObject == TEACH_SPHERE)
    {
        std::cout << "  UV sphere radius = 0.5, centred at origin.\n";
        std::cout << "  24 stacks x 36 slices => 925 stored vertices.\n";
        std::cout << "  1728 triangles, 5184 indices.\n";
        std::cout << "  x=0.5 sin(phi) cos(theta), y=0.5 cos(phi), z=0.5 sin(phi) sin(theta).\n";
    }
    else
    {
        int N = (gTeachObject == TEACH_CYLINDER) ? 28 : 48;
        int V = 4 * N + 6;
        int I = 12 * N;
        int T = 4 * N;
        std::cout << "  N=" << N << " segments => V=4N+6=" << V
            << ", I=12N=" << I << ", triangles=4N=" << T << "\n";
        std::cout << "  a_i = 2*pi*i/N\n";
        std::cout << "  side bottom = (0.5 cos a_i, -0.5, 0.5 sin a_i)\n";
        std::cout << "  side top    = (0.5 cos a_i, +0.5, 0.5 sin a_i)\n";
        std::cout << "  side triangles for segment i:\n";
        std::cout << "    (2i, 2i+2, 2i+3), (2i+3, 2i+1, 2i)\n";
    }
    std::cout << "  Use L to switch fill/wireframe while this primitive is isolated.\n";
}

static void printConstructionStage()
{
    if (gTeachObject == TEACH_SCENE)
    {
        std::cout << "[Teaching] Full scene mode. Press 2=car, 3=lift, 4=robot, 5/6/7/8=primitive meshes.\n";
        return;
    }

    if (!isProgressiveTeachingObject())
    {
        printPrimitiveMath();
        return;
    }

    std::cout << "\n[Teaching] " << teachingObjectName()
        << " | stage " << (gTeachStage + 1) << "/" << teachingStageCount()
        << " | " << teachingStageName() << "\n";
    if (teachingLimit() >= kAllRemaining)
        std::cout << "  Visible primitive draw calls: all ; amber = newly added group.\n";
    else
        std::cout << "  Visible primitive draw calls: 1.." << teachingLimit()
            << " ; amber = newly added group.\n";
    std::cout << "  Vertex rule: v_world = T * Ry * Rx * Rz * S * v_local.\n";
    std::cout << "  Indices never change; each transformed primitive reuses its original EBO.\n";
}

static void setTeachingMode(TeachingObject obj)
{
    gTeachObject = obj;
    gTeachStage = 0;

    if (obj == TEACH_CAR)
    {
        gCamera.position = Vec3(6.0f, 3.0f, 7.2f);
        gCamera.yaw = -130.0f; gCamera.pitch = -18.0f; gCamera.fov = 52.0f;
    }
    else if (obj == TEACH_LIFT)
    {
        gCamera.position = Vec3(6.4f, 4.1f, 7.0f);
        gCamera.yaw = -125.0f; gCamera.pitch = -17.0f; gCamera.fov = 55.0f;
    }
    else if (obj == TEACH_ROBOT)
    {
        gCamera.position = Vec3(8.0f, 4.7f, 8.0f);
        gCamera.yaw = -125.0f; gCamera.pitch = -12.0f; gCamera.fov = 55.0f;
    }
    else if (obj == TEACH_CUBE || obj == TEACH_CYLINDER || obj == TEACH_SMOOTH_CYLINDER || obj == TEACH_SPHERE)
    {
        gCamera.position = Vec3(3.3f, 2.6f, 4.0f);
        gCamera.yaw = -130.0f; gCamera.pitch = -20.0f; gCamera.fov = 48.0f;
    }
    else
    {
        resetCamera();
    }
    printConstructionStage();
}

static void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height)
{
    if (gDemoActive) return;      // the recording size is fixed; the window is only a preview
    gWidth = width;
    gHeight = height;
    glViewport(0, 0, width, height);
}

static void cursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos)
{
    if (!gMouseCaptured || gInputLocked)
    {
        gFirstMouse = true;
        return;
    }

    if (gFirstMouse)
    {
        gLastX = xpos;
        gLastY = ypos;
        gFirstMouse = false;
    }

    float dx = static_cast<float>(xpos - gLastX);
    float dy = static_cast<float>(gLastY - ypos);   // screen Y grows downwards

    gLastX = xpos;
    gLastY = ypos;

    gCamera.addMouseDelta(dx, dy);
}

static void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    if (!gInputLocked) gCamera.addZoom(static_cast<float>(yoffset));
}

static void printTeachingInfo()
{
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << " GEOMETRY TEACHING VIEW\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Unit cube      : " << Primitives::cubeVertexCount() << " vertices, "
        << Primitives::cubeIndexCount() << " indices, "
        << Primitives::cubeTriangleCount() << " triangles\n";
    std::cout << "                  (6 faces x 4 corners = 24 vertices)\n";
    std::cout << "                  (6 faces x 2 triangles = 12 triangles)\n";
    std::cout << " Unit cylinder  : " << Primitives::cylinderVertexCount() << " vertices, "
        << Primitives::cylinderIndexCount() << " indices, "
        << Primitives::cylinderTriangleCount() << " triangles ("
        << Primitives::cylinderSegments() << " segments)\n";

    std::cout << "  smooth cylinder : "
        << Primitives::smoothCylinderVertexCount() << " vertices, "
        << Primitives::smoothCylinderTriangleCount() << " triangles ("
        << Primitives::smoothCylinderSegments() << " segments)\n";
    std::cout << "  -> the smooth mesh is the SAME cylinder shape in the SAME local\n"
        << "     frame, just finer round the barrel. Tyres, drums and hoses use it.\n";
    std::cout << " UV sphere       : " << Primitives::sphereVertexCount() << " vertices, "
        << Primitives::sphereIndexCount() << " indices, "
        << Primitives::sphereTriangleCount() << " triangles\n";
    std::cout << " Draw calls last frame : " << Primitives::drawCallsThisFrame() << "\n";
    std::cout << "\n Every object reuses the same small primitive library.\n";
    std::cout << " Only the model matrix (translate * rotate * scale) changes per instance.\n";
    std::cout << "------------------------------------------------------------\n" << std::endl;
}

static bool gReelActive = false;

static bool saveScreenshotBMP(const char* path, int width, int height)
{
    if (width <= 0 || height <= 0) return false;

    // BMP rows are padded to a 4-byte boundary and stored bottom-up, which is
    // exactly the order glReadPixels hands them back.
    const int rowBytes = width * 3;
    const int padding = (4 - (rowBytes % 4)) % 4;
    const int paddedRow = rowBytes + padding;
    const int imageBytes = paddedRow * height;

    std::vector<unsigned char> pixels(static_cast<size_t>(imageBytes), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || f == nullptr)
    {
        std::cout << "[Capture] could not open " << path << " for writing\n";
        return false;
    }

    const unsigned int fileSize = 54u + static_cast<unsigned int>(imageBytes);
    unsigned char header[54] = { 0 };
    header[0] = 'B'; header[1] = 'M';
    header[2] = static_cast<unsigned char>(fileSize);
    header[3] = static_cast<unsigned char>(fileSize >> 8);
    header[4] = static_cast<unsigned char>(fileSize >> 16);
    header[5] = static_cast<unsigned char>(fileSize >> 24);
    header[10] = 54;                     // pixel data offset
    header[14] = 40;                     // DIB header size
    header[18] = static_cast<unsigned char>(width);
    header[19] = static_cast<unsigned char>(width >> 8);
    header[20] = static_cast<unsigned char>(width >> 16);
    header[21] = static_cast<unsigned char>(width >> 24);
    header[22] = static_cast<unsigned char>(height);
    header[23] = static_cast<unsigned char>(height >> 8);
    header[24] = static_cast<unsigned char>(height >> 16);
    header[25] = static_cast<unsigned char>(height >> 24);
    header[26] = 1;                      // colour planes
    header[28] = 24;                     // bits per pixel
    header[34] = static_cast<unsigned char>(imageBytes);
    header[35] = static_cast<unsigned char>(imageBytes >> 8);
    header[36] = static_cast<unsigned char>(imageBytes >> 16);
    header[37] = static_cast<unsigned char>(imageBytes >> 24);

    fwrite(header, 1, 54, f);
    fwrite(pixels.data(), 1, static_cast<size_t>(imageBytes), f);
    fclose(f);

    if (!gReelActive)
        std::cout << "[Capture] wrote " << path << "  (" << width << " x " << height << ")\n";
    return true;
}

static int gCaptureCounter = 0;
static void captureCurrentFrame()
{
    char name[128];
    std::snprintf(name, sizeof(name), "SmartGarage_shot_%03d.bmp", gCaptureCounter++);
    saveScreenshotBMP(name, gWidth, gHeight);
}

static std::string gReelDir;
static int   gReelFrame = 0;
static int   gReelStart = 0;             // first frame to actually write
static int   gReelTotal = 2880;          // 120 s at 24 fps
static const float kReelFps = 24.0f;
static float gReelTime = 0.0f;
static float gServiceSpeed = 1.0f;       // the service runs faster while recording

struct ReelKey { float t; Vec3 eye; Vec3 look; float fov; };

static const ReelKey kReelKeys[] = {
    // ---- 0-6 s: outside, the vehicle waiting on the apron ---------------
    {   0.0f, Vec3(  9.0f, 2.8f, 24.5f), Vec3( 0.0f, 1.1f, 16.3f), 50.0f },
    {   5.0f, Vec3(  7.4f, 1.9f, 21.0f), Vec3( 0.0f, 0.95f, 16.1f), 46.0f },
    // 0-30 s is the tracked arrival; these only bracket it.
    {   6.0f, Vec3(  7.4f, 1.9f, 21.0f), Vec3( 0.0f, 0.95f, 16.1f), 46.0f },
    {  30.0f, Vec3(  7.5f, 3.2f,  3.0f), Vec3( 0.0f, 2.0f, -2.0f), 48.0f },
    {  34.0f, Vec3(  1.2f, 5.40f, 8.5f), Vec3( 0.0f, 0.9f, -4.0f), 42.0f },
    {  40.0f, Vec3(  5.0f, 3.20f, 7.0f), Vec3( 0.0f, 1.8f, -2.0f), 46.0f },
    // ---- 44-56 s: shading, then textures ---------------------------------
    {  46.0f, Vec3(  8.0f, 2.9f, -0.5f), Vec3( 0.0f, 2.2f, -2.0f), 42.0f },
    {  50.0f, Vec3(  7.0f, 2.6f, -4.5f), Vec3( 0.0f, 2.1f, -2.2f), 40.0f },
    {  54.0f, Vec3(  5.2f, 1.6f,  5.0f), Vec3(-0.5f, 0.9f, -2.0f), 55.0f },
    {  58.0f, Vec3(  6.5f, 2.6f,  4.0f), Vec3( 0.0f, 1.8f, -2.0f), 50.0f },
    // ---- 58-72 s: the three lighting rigs, then reflections ---------------
    {  62.0f, Vec3(  7.0f, 3.0f,  3.0f), Vec3( 0.0f, 2.2f, -2.0f), 46.0f },
    {  66.0f, Vec3(  8.0f, 2.8f, -0.5f), Vec3( 0.0f, 2.2f, -2.0f), 44.0f },
    {  70.0f, Vec3(  6.0f, 2.4f, -2.0f), Vec3( 0.0f, 2.1f, -2.0f), 38.0f },
    // ---- 72-104 s: the service itself ------------------------------------
    {  76.0f, Vec3(  8.5f, 4.4f,  3.5f), Vec3(-2.0f, 2.8f, -2.0f), 50.0f },
    {  82.0f, Vec3(  6.5f, 4.6f, -0.5f), Vec3(-1.5f, 3.0f, -2.0f), 44.0f },
    {  88.0f, Vec3(  5.5f, 4.0f, -5.5f), Vec3(-1.0f, 2.9f, -2.4f), 42.0f },
    {  94.0f, Vec3(  6.5f, 3.2f,  2.0f), Vec3(-1.0f, 2.6f, -2.0f), 42.0f },
    { 100.0f, Vec3(  7.5f, 3.0f, -2.0f), Vec3( 0.0f, 2.5f, -2.0f), 40.0f },
    { 106.0f, Vec3(  6.0f, 2.5f,  3.5f), Vec3( 0.5f, 1.8f, -0.5f), 44.0f },
    // ---- 106-120 s: finished, pull back -----------------------------------
    { 112.0f, Vec3(  8.5f, 3.2f,  6.5f), Vec3( 0.0f, 2.2f, -1.5f), 50.0f },
    { 120.0f, Vec3( 11.5f, 3.6f, 10.5f), Vec3( 0.0f, 2.0f,  0.0f), 56.0f },
};

static bool reelTrackArrival(float t)
{
    if (t > 30.0f) return false;

    const float carZ = -2.00f + Scene::carDriveDistance() * gSwitches.carDrive;
    const float carY = Scene::liftTopY(gLiftHeight) * (1.0f - gSwitches.carDrive);

    // Ease the rig in and out so the hand-off to the keyframes is not a cut.
    float u = t / 2.0f;                          // 2 s to take over
    u = clampf(u, 0.0f, 1.0f);
    float v = (30.0f - t) / 4.0f;                // 4 s to hand back
    v = clampf(v, 0.0f, 1.0f);
    const float blend = (u * u * (3.0f - 2.0f * u)) * (v * v * (3.0f - 2.0f * v));
    if (blend <= 0.001f) return false;

    const float kThresholdZ = 13.40f;
    const float eyeZ = clampf(carZ + 4.50f, -1.0f, 24.0f);
    float openness = std::fabs(eyeZ - kThresholdZ) / 5.0f;
    openness = clampf(openness, 0.0f, 1.0f);
    const float eyeX = 2.60f + 4.80f * openness;

    const Vec3 look(0.0f, carY + 0.95f, carZ - 0.40f);
    const Vec3 eye(eyeX, carY + 1.90f, eyeZ);

    const int n = static_cast<int>(sizeof(kReelKeys) / sizeof(kReelKeys[0]));
    int i = 0;
    while (i + 1 < n - 1 && kReelKeys[i + 1].t <= t) ++i;
    const ReelKey& a = kReelKeys[i];
    const ReelKey& b = kReelKeys[i + 1];
    float w = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0f;
    w = clampf(w, 0.0f, 1.0f);
    const float e = w * w * (3.0f - 2.0f * w);
    const Vec3 keyEye = a.eye + (b.eye - a.eye) * e;
    const Vec3 keyLook = a.look + (b.look - a.look) * e;

    const Vec3 finalEye = keyEye + (eye - keyEye) * blend;
    const Vec3 finalLook = keyLook + (look - keyLook) * blend;

    gCamera.position = finalEye;
    gCamera.fov = 46.0f;
    const Vec3 d = finalLook - finalEye;
    const float flat = std::sqrt(d.x * d.x + d.z * d.z);
    gCamera.yaw = std::atan2(d.z, d.x) * 180.0f / PI_F;
    gCamera.pitch = std::atan2(d.y, (flat > 1e-4f) ? flat : 1e-4f) * 180.0f / PI_F;
    return true;
}

static void reelApplyCamera(float t)
{
    if (reelTrackArrival(t)) return;

    const int n = static_cast<int>(sizeof(kReelKeys) / sizeof(kReelKeys[0]));
    int i = 0;
    while (i + 1 < n - 1 && kReelKeys[i + 1].t <= t) ++i;
    const ReelKey& a = kReelKeys[i];
    const ReelKey& b = kReelKeys[i + 1];
    float u = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0f;
    u = clampf(u, 0.0f, 1.0f);
    const float e = u * u * (3.0f - 2.0f * u);        // ease between keys

    const Vec3 eye  = a.eye  + (b.eye  - a.eye)  * e;
    const Vec3 look = a.look + (b.look - a.look) * e;
    gCamera.position = eye;
    gCamera.fov = a.fov + (b.fov - a.fov) * e;

    const Vec3 d = look - eye;
    const float flat = std::sqrt(d.x * d.x + d.z * d.z);
    gCamera.yaw = std::atan2(d.z, d.x) * 180.0f / PI_F;
    gCamera.pitch = std::atan2(d.y, (flat > 1e-4f) ? flat : 1e-4f) * 180.0f / PI_F;
}

// Feature toggles, fired once as the reel time passes each mark.
static void reelFireEvents(float prev, float now)
{
    auto at = [&](float mark) { return prev < mark && now >= mark; };

    if (at( 6.0f)) { startServiceDemo(); gServiceSpeed = 1.55f; }

    if (at(34.0f)) gOrthographic = true;                       // projection
    if (at(39.0f)) gOrthographic = false;
    if (at(44.0f)) gShadingMode = 1;                           // Gouraud
    if (at(50.0f)) gShadingMode = 0;                           // Cook-Torrance
    if (at(53.0f)) { gTexturesEnabled = false; Scene::setTexturesEnabled(false); }
    if (at(57.0f)) { gTexturesEnabled = true;  Scene::setTexturesEnabled(true);  }
    if (at(60.0f)) gLightingPreset = LIGHT_STUDIO;             // lighting rigs
    if (at(64.0f)) gLightingPreset = LIGHT_NIGHT;
    if (at(68.0f)) gLightingPreset = LIGHT_WORKSHOP;
    if (at(70.0f)) gReflectionsEnabled = false;                // image-based lighting
    if (at(72.0f)) gReflectionsEnabled = true;
}

static void updateShowcaseCamera(float dt, const Vec3& subject)
{
    if (gReelActive) return;      // the reel's keyframe track owns the camera
    if (!gShowcase) return;

    gShowcaseAngle += gShowcaseSpeed * dt;
    if (gShowcaseAngle >= 360.0f) gShowcaseAngle -= 360.0f;

    const float a = radians(gShowcaseAngle);
    gCamera.position = Vec3(
        subject.x + std::sin(a) * gShowcaseRadius,
        subject.y + gShowcaseHeight,
        subject.z + std::cos(a) * gShowcaseRadius);

    // Aim at the subject.  yaw is measured from +X towards -Z, which is the
    // same convention Camera::front() uses.
    const Vec3 toSubject = subject - gCamera.position;
    const float flat = std::sqrt(toSubject.x * toSubject.x + toSubject.z * toSubject.z);
    gCamera.yaw = std::atan2(toSubject.z, toSubject.x) * 180.0f / PI_F;
    gCamera.pitch = std::atan2(toSubject.y, (flat > 0.0001f) ? flat : 0.0001f) * 180.0f / PI_F;
}

static void printRenderReport()
{
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << " RENDER REPORT\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Resolution        : " << gWidth << " x " << gHeight << "\n";
    std::cout << " Frame time        : " << gFrameMsAvg << " ms  (~"
              << ((gFrameMsAvg > 0.0001f) ? (1000.0f / gFrameMsAvg) : 0.0f) << " fps)\n";
    std::cout << " Primitive draws   : " << Primitives::drawCallsThisFrame() << " last frame\n";
    std::cout << "\n Shading model     : "
              << kShadingNames[gShadingMode] << "\n";
    std::cout << " Specular model    : anisotropic GGX + clear-coat lobe\n";
    std::cout << " Environment       : 128px HDR cubemap probe, split-sum IBL, "
              << "intensity " << gIblIntensity << "\n";
    std::cout << " Shadows           : " << (gShadowsEnabled ? "5x5 PCF directional" : "OFF") << "\n";
    std::cout << " Reflections       : " << (gReflectionsEnabled ? "ON" : "OFF") << "\n";
    std::cout << " Projection        : "
              << (gOrthographic ? "orthographic (parallel)" : "perspective")
              << "   vertical FOV " << gCamera.fov << " deg\n";
    std::cout << " Material textures : " << (gTexturesEnabled ? "ON" : "OFF")
              << "   4-layer GL_TEXTURE_2D_ARRAY, triplanar, mipmapped\n";
    std::cout << " Normal maps       : " << (gNormalMapsEnabled ? "ON" : "OFF") << "\n";
    std::cout << " Anti-aliasing     : " << (gMsaaEnabled ? "4x MSAA (resolved)" : "post-process edge filter") << "\n";
    std::cout << " Bloom             : " << (gBloomEnabled ? "half-res, 6 passes" : "OFF")
              << "   strength " << gBloomStrength << "\n";
    std::cout << " Tone map          : ACES filmic + split-tone grade + vignette " << gVignette << "\n";
    std::cout << " Exposure          : " << gExposure << "\n";
    std::cout << " Lighting preset   : " << kLightingPresetNames[gLightingPreset] << "\n";
    std::cout << " Showcase orbit    : " << (gShowcase ? "ON" : "OFF") << "\n";
    std::cout << "\n Vehicle side-panel polish : " << gServiceVisual.polishAmount
              << "   (0 = as rolled, 1 = machine polished)\n";
    std::cout << "------------------------------------------------------------\n" << std::endl;
}

static void handleDiscreteKey(GLFWwindow* window, int key)
{
    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;

    case GLFW_KEY_L:                    // wireframe toggle
        gWireframe = !gWireframe;
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
        break;

    case GLFW_KEY_V:                    // geometry + rendering report
        printTeachingInfo();
        printRenderReport();
        break;

    case GLFW_KEY_O:                    // automatic door: nearest opposite endpoint
        if (!serviceRunning())
        {
            gDoorOpen = !(gDoorOffset > 2.45f);
            gDoorTarget = gDoorOpen ? 4.9f : 0.0f;
        }
        break;

    case GLFW_KEY_P:                    // automatic lift: nearest opposite endpoint
        if (!serviceRunning())
        {
            gLiftRaised = !(gLiftHeight > 1.60f);
            gLiftTarget = gLiftRaised ? 3.20f : 0.0f;
        }
        break;

    case GLFW_KEY_C:                    // stop door exactly where it is
        if (!serviceRunning()) gDoorTarget = gDoorOffset;
        break;

    case GLFW_KEY_BACKSLASH:            // stop lift exactly where it is
        if (!serviceRunning()) gLiftTarget = gLiftHeight;
        break;

    case GLFW_KEY_F1: if (!serviceRunning()) { gRobotTool = 0; std::cout << "Robot tool: scanner\n"; } break;
    case GLFW_KEY_F2: if (!serviceRunning()) { gRobotTool = 1; std::cout << "Robot tool: impact driver\n"; } break;
    case GLFW_KEY_F3: if (!serviceRunning()) { gRobotTool = 2; std::cout << "Robot tool: gripper\n"; } break;
    case GLFW_KEY_F4: if (!serviceRunning()) { gRobotTool = 3; std::cout << "Robot tool: polisher\n"; } break;
    case GLFW_KEY_F5:
        gShadingMode = (gShadingMode + 1) % 3;     // PBR -> Gouraud -> Phong -> PBR
        std::cout << "Shading: " << kShadingNames[gShadingMode] << "\n";
        break;
    case GLFW_KEY_F6:
        gTexturesEnabled = !gTexturesEnabled;
        Scene::setTexturesEnabled(gTexturesEnabled);
        std::cout << "Material textures (sampled array + procedural detail): "
                  << (gTexturesEnabled ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_F7:
        gNormalMapsEnabled = !gNormalMapsEnabled;
        std::cout << "Normal/bump maps: " << (gNormalMapsEnabled ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_F8:
        gShadowsEnabled = !gShadowsEnabled;
        std::cout << "PCF shadows: " << (gShadowsEnabled ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_F9:
        gReflectionsEnabled = !gReflectionsEnabled;
        std::cout << "Environment reflections: " << (gReflectionsEnabled ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_F10:
        gBloomEnabled = !gBloomEnabled;
        std::cout << "Bloom: " << (gBloomEnabled ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_BACKSPACE:
        gOrthographic = !gOrthographic;
        std::cout << "Projection: "
                  << (gOrthographic ? "ORTHOGRAPHIC (parallel, no foreshortening)"
                                    : "PERSPECTIVE (vanishing point)") << "\n";
        break;
    case GLFW_KEY_F11:
        gMsaaEnabled = !gMsaaEnabled;
        std::cout << "Anti-aliasing: "
                  << (gMsaaEnabled ? "4x MSAA" : "post-process edge filter") << "\n";
        break;
    case GLFW_KEY_F12:
        gShowcase = !gShowcase;
        std::cout << "Showcase orbit: " << (gShowcase ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_F:
        captureCurrentFrame();
        break;

    // ---- SECTION I: scene switches ------------------------------------
    case GLFW_KEY_SEMICOLON:
        gSwitches.ceilingLights = !gSwitches.ceilingLights;
        std::cout << "Overhead work lights: " << (gSwitches.ceilingLights ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_APOSTROPHE:
        gSwitches.vehicleLights = !gSwitches.vehicleLights;
        std::cout << "Vehicle light bars: " << (gSwitches.vehicleLights ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_SLASH:
        gSwitches.neonSign = !gSwitches.neonSign;
        std::cout << "Neon sign: " << (gSwitches.neonSign ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_PAGE_UP:
        // The passing car on the road is gone; this key now parks the main
        // vehicle back outside so the arrival can be demonstrated again.
        if (!serviceRunning())
        {
            gCarDriveTarget = 1.0f;
            gDoorOpen = true;
            gDoorTarget = 4.9f;
            std::cout << "Vehicle returning to the apron - press ENTER to run the arrival again.\n";
        }
        break;
    case GLFW_KEY_END:
        gSwitches.hazardBeacon = !gSwitches.hazardBeacon;
        std::cout << "Hazard beacon: " << (gSwitches.hazardBeacon ? "FLASHING" : "OFF") << "\n";
        break;
    case GLFW_KEY_HOME:
        if (!serviceRunning())
        {
            gCarDriveTarget = (gCarDriveTarget > 0.5f) ? 0.0f : 1.0f;
            if (gCarDriveTarget > 0.5f)
            {
                gLiftTarget = 0.0f;   gLiftRaised = false;
                gDoorTarget = 4.9f;   gDoorOpen = true;
            }
            std::cout << "Vehicle " << (gCarDriveTarget > 0.5f ? "driving OUT of the bay"
                                                              : "returning to the service bay") << "\n";
        }
        break;
    case GLFW_KEY_9:
        gRtLarge = !gRtLarge;
        rtApplySize();
        std::cout << "Inspection view: " << (gRtLarge ? "ENLARGED (press 9 to close)" : "monitor only") << "\n";
        break;
    case GLFW_KEY_MINUS:
        gRtShadows = !gRtShadows;
        std::cout << "Inspection-light shadow rays: " << (gRtShadows ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_EQUAL:
        gRtPreset = (gRtPreset + 1) % 4;
        gRtYaw = 0.0f;
        std::cout << "Inspection camera preset " << gRtPreset << "\n";
        break;
    case GLFW_KEY_PAGE_DOWN:
        gRtLightCustom = false;
        gRtLightIdx = (gRtLightIdx + 1) % 5;
        std::cout << "Inspection light preset " << gRtLightIdx << "\n";
        break;
    case GLFW_KEY_0:
        gNight = !gNight;
        std::cout << "Exterior environment: " << (gNight ? "NIGHT" : "DAYLIGHT") << "\n";
        break;
    case GLFW_KEY_M:
        gLightingPreset = static_cast<LightingPreset>((gLightingPreset + 1) % LIGHT_PRESET_COUNT);
        std::cout << "Lighting preset: " << kLightingPresetNames[gLightingPreset] << "\n";
        break;
    case GLFW_KEY_COMMA:
        gIblIntensity = clampf(gIblIntensity - 0.06f, 0.0f, 2.0f);
        std::cout << "Environment lighting intensity: " << gIblIntensity << "\n";
        break;
    case GLFW_KEY_PERIOD:
        gIblIntensity = clampf(gIblIntensity + 0.06f, 0.0f, 2.0f);
        std::cout << "Environment lighting intensity: " << gIblIntensity << "\n";
        break;

    case GLFW_KEY_1:
        setTeachingMode(TEACH_SCENE);
        break;
    case GLFW_KEY_2:
        setTeachingMode(TEACH_CAR);
        break;
    case GLFW_KEY_3:
        setTeachingMode(TEACH_LIFT);
        break;
    case GLFW_KEY_4:
        setTeachingMode(TEACH_ROBOT);
        break;
    case GLFW_KEY_5:
        setTeachingMode(TEACH_CUBE);
        break;
    case GLFW_KEY_6:
        setTeachingMode(TEACH_CYLINDER);
        break;
    case GLFW_KEY_7:
        setTeachingMode(TEACH_SMOOTH_CYLINDER);
        break;
    case GLFW_KEY_8:
        setTeachingMode(TEACH_SPHERE);
        break;
    case GLFW_KEY_N:
        if (isProgressiveTeachingObject() && gTeachStage + 1 < teachingStageCount())
        {
            ++gTeachStage;
            printConstructionStage();
        }
        break;
    case GLFW_KEY_B:
        if (isProgressiveTeachingObject() && gTeachStage > 0)
        {
            --gTeachStage;
            printConstructionStage();
        }
        break;

    case GLFW_KEY_ENTER:
        startServiceDemo();
        break;

    case GLFW_KEY_R:
        if (gTeachObject == TEACH_SCENE)
        {
            resetServiceDemo();
            resetCamera();
        }
        else
        {
            setTeachingMode(gTeachObject);
        }
        break;

    case GLFW_KEY_TAB:                  // release / capture the mouse
        gMouseCaptured = !gMouseCaptured;
        glfwSetInputMode(window, GLFW_CURSOR,
            gMouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        gFirstMouse = true;
        break;

    default:
        break;
    }
}

static bool keyPressedOnce(GLFWwindow* window, int key, bool& wasDown)
{
    const bool down = (glfwGetKey(window, key) == GLFW_PRESS);
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

static void processDiscreteInput(GLFWwindow* window)
{
    static bool esc=false, l=false, v=false, o=false, p=false, c=false, stopLift=false, f1=false, f2=false, f3=false, f4=false, f5=false, f6=false, f7=false, f8=false, f9=false, f10=false;
    static bool k1=false, k2=false, k3=false, k4=false, k5=false, k6=false, k7=false, k8=false;
    static bool n=false, b=false, r=false, enter=false, kpEnter=false, tab=false;
    static bool f11=false, f12=false, m=false, comma=false, period=false, fkey=false;
    static bool semi=false, apos=false, slash=false, endk=false, home=false;
    static bool bksp=false, pgup=false, pgdn=false;
    static bool rt9=false, rtMinus=false, rtEqual=false, zero=false, kpZero=false;

    if (keyPressedOnce(window, GLFW_KEY_ESCAPE, esc)) handleDiscreteKey(window, GLFW_KEY_ESCAPE);
    if (keyPressedOnce(window, GLFW_KEY_L, l))       handleDiscreteKey(window, GLFW_KEY_L);
    if (keyPressedOnce(window, GLFW_KEY_V, v))       handleDiscreteKey(window, GLFW_KEY_V);
    if (keyPressedOnce(window, GLFW_KEY_O, o))       handleDiscreteKey(window, GLFW_KEY_O);
    if (keyPressedOnce(window, GLFW_KEY_P, p))       handleDiscreteKey(window, GLFW_KEY_P);
    if (keyPressedOnce(window, GLFW_KEY_C, c))       handleDiscreteKey(window, GLFW_KEY_C);
    if (keyPressedOnce(window, GLFW_KEY_BACKSLASH, stopLift)) handleDiscreteKey(window, GLFW_KEY_BACKSLASH);
    if (keyPressedOnce(window, GLFW_KEY_F1, f1)) handleDiscreteKey(window, GLFW_KEY_F1);
    if (keyPressedOnce(window, GLFW_KEY_F2, f2)) handleDiscreteKey(window, GLFW_KEY_F2);
    if (keyPressedOnce(window, GLFW_KEY_F3, f3)) handleDiscreteKey(window, GLFW_KEY_F3);
    if (keyPressedOnce(window, GLFW_KEY_F4, f4)) handleDiscreteKey(window, GLFW_KEY_F4);
    if (keyPressedOnce(window, GLFW_KEY_F5, f5)) handleDiscreteKey(window, GLFW_KEY_F5);
    if (keyPressedOnce(window, GLFW_KEY_F6, f6)) handleDiscreteKey(window, GLFW_KEY_F6);
    if (keyPressedOnce(window, GLFW_KEY_F7, f7)) handleDiscreteKey(window, GLFW_KEY_F7);
    if (keyPressedOnce(window, GLFW_KEY_F8, f8)) handleDiscreteKey(window, GLFW_KEY_F8);
    if (keyPressedOnce(window, GLFW_KEY_F9, f9)) handleDiscreteKey(window, GLFW_KEY_F9);
    if (keyPressedOnce(window, GLFW_KEY_F10, f10)) handleDiscreteKey(window, GLFW_KEY_F10);
    if (keyPressedOnce(window, GLFW_KEY_F11, f11)) handleDiscreteKey(window, GLFW_KEY_F11);
    if (keyPressedOnce(window, GLFW_KEY_F12, f12)) handleDiscreteKey(window, GLFW_KEY_F12);
    if (keyPressedOnce(window, GLFW_KEY_M, m))     handleDiscreteKey(window, GLFW_KEY_M);
    if (keyPressedOnce(window, GLFW_KEY_F, fkey))  handleDiscreteKey(window, GLFW_KEY_F);
    if (keyPressedOnce(window, GLFW_KEY_SEMICOLON, semi))  handleDiscreteKey(window, GLFW_KEY_SEMICOLON);
    if (keyPressedOnce(window, GLFW_KEY_APOSTROPHE, apos)) handleDiscreteKey(window, GLFW_KEY_APOSTROPHE);
    if (keyPressedOnce(window, GLFW_KEY_SLASH, slash))     handleDiscreteKey(window, GLFW_KEY_SLASH);
    if (keyPressedOnce(window, GLFW_KEY_END, endk))        handleDiscreteKey(window, GLFW_KEY_END);
    if (keyPressedOnce(window, GLFW_KEY_HOME, home))       handleDiscreteKey(window, GLFW_KEY_HOME);
    if (keyPressedOnce(window, GLFW_KEY_BACKSPACE, bksp))  handleDiscreteKey(window, GLFW_KEY_BACKSPACE);
    if (keyPressedOnce(window, GLFW_KEY_PAGE_UP, pgup))    handleDiscreteKey(window, GLFW_KEY_PAGE_UP);
    if (keyPressedOnce(window, GLFW_KEY_COMMA, comma))   handleDiscreteKey(window, GLFW_KEY_COMMA);
    if (keyPressedOnce(window, GLFW_KEY_PERIOD, period)) handleDiscreteKey(window, GLFW_KEY_PERIOD);

    if (keyPressedOnce(window, GLFW_KEY_9, rt9))             handleDiscreteKey(window, GLFW_KEY_9);
    if (keyPressedOnce(window, GLFW_KEY_MINUS, rtMinus))     handleDiscreteKey(window, GLFW_KEY_MINUS);
    if (keyPressedOnce(window, GLFW_KEY_EQUAL, rtEqual))     handleDiscreteKey(window, GLFW_KEY_EQUAL);
    if (keyPressedOnce(window, GLFW_KEY_PAGE_DOWN, pgdn))    handleDiscreteKey(window, GLFW_KEY_PAGE_DOWN);
    if (keyPressedOnce(window, GLFW_KEY_0, zero) || keyPressedOnce(window, GLFW_KEY_KP_0, kpZero))
        handleDiscreteKey(window, GLFW_KEY_0);

    if (keyPressedOnce(window, GLFW_KEY_1, k1)) handleDiscreteKey(window, GLFW_KEY_1);
    if (keyPressedOnce(window, GLFW_KEY_2, k2)) handleDiscreteKey(window, GLFW_KEY_2);
    if (keyPressedOnce(window, GLFW_KEY_3, k3)) handleDiscreteKey(window, GLFW_KEY_3);
    if (keyPressedOnce(window, GLFW_KEY_4, k4)) handleDiscreteKey(window, GLFW_KEY_4);
    if (keyPressedOnce(window, GLFW_KEY_5, k5)) handleDiscreteKey(window, GLFW_KEY_5);
    if (keyPressedOnce(window, GLFW_KEY_6, k6)) handleDiscreteKey(window, GLFW_KEY_6);
    if (keyPressedOnce(window, GLFW_KEY_7, k7)) handleDiscreteKey(window, GLFW_KEY_7);
    if (keyPressedOnce(window, GLFW_KEY_8, k8)) handleDiscreteKey(window, GLFW_KEY_8);

    if (keyPressedOnce(window, GLFW_KEY_N, n))       handleDiscreteKey(window, GLFW_KEY_N);
    if (keyPressedOnce(window, GLFW_KEY_B, b))       handleDiscreteKey(window, GLFW_KEY_B);
    if (keyPressedOnce(window, GLFW_KEY_ENTER, enter) || keyPressedOnce(window, GLFW_KEY_KP_ENTER, kpEnter)) handleDiscreteKey(window, GLFW_KEY_ENTER);
    if (keyPressedOnce(window, GLFW_KEY_R, r))       handleDiscreteKey(window, GLFW_KEY_R);
    if (keyPressedOnce(window, GLFW_KEY_TAB, tab))   handleDiscreteKey(window, GLFW_KEY_TAB);
}

static void updateTeachingWindowTitle(GLFWwindow* window)
{
    std::string title = "SmartGarage TEACHING | ";
    title += teachingObjectName();
    if (isProgressiveTeachingObject())
    {
        title += " | stage ";
        title += std::to_string(gTeachStage + 1);
        title += "/";
        title += std::to_string(teachingStageCount());
        title += " | ";
        title += teachingStageName();
        title += " | N/B step";
    }
    else if (gTeachObject != TEACH_SCENE)
    {
        title += " | L wireframe | 1 full scene";
    }
    else
    {
        title += " | AUTO SERVICE: ";
        title += serviceStatusText();
        title += " | ENTER start";
    }
    title += " | ";
    title += (gShadingMode == 0 ? "PBR" : (gShadingMode == 1 ? "GOURAUD" : "PHONG"));
    title += gTexturesEnabled ? " | TEX" : " | noTEX";
    title += gShadowsEnabled ? " | SHADOW" : " | noSHADOW";
    title += gReflectionsEnabled ? " | REFL" : " | noREFL";
    if (gSafetyStop) title += " | SAFETY STOP: collision prevented";
    glfwSetWindowTitle(window, title.c_str());
}

static void processMovement(GLFWwindow* window, float dt)
{
    gSafetyStop = false;

    if (gShowcase) return;

    float speedScale = (keyDown(window, GLFW_KEY_LEFT_SHIFT)) ? 2.5f : 1.0f;
    float step = dt * speedScale;

    if (keyDown(window, GLFW_KEY_W)) gCamera.moveForward(step);
    if (keyDown(window, GLFW_KEY_S)) gCamera.moveBackward(step);
    if (keyDown(window, GLFW_KEY_A)) gCamera.moveLeft(step);
    if (keyDown(window, GLFW_KEY_D)) gCamera.moveRight(step);
    if (keyDown(window, GLFW_KEY_SPACE)) gCamera.moveUp(step);
    if (keyDown(window, GLFW_KEY_LEFT_CONTROL)) gCamera.moveDown(step);
    if (keyDown(window, GLFW_KEY_Q)) gCamera.moveDown(step);
    if (keyDown(window, GLFW_KEY_E)) gCamera.moveUp(step);


    {
        const float yawStep = 50.0f * dt, lightStep = 2.2f * dt;
        if (keyDown(window, GLFW_KEY_INSERT)) gRtYaw -= yawStep;
        if (keyDown(window, GLFW_KEY_DELETE)) gRtYaw += yawStep;
        Vec3 dl(0.0f, 0.0f, 0.0f);
        if (keyDown(window, GLFW_KEY_KP_4)) dl.x -= lightStep;
        if (keyDown(window, GLFW_KEY_KP_6)) dl.x += lightStep;
        if (keyDown(window, GLFW_KEY_KP_8)) dl.z -= lightStep;
        if (keyDown(window, GLFW_KEY_KP_2)) dl.z += lightStep;
        if (keyDown(window, GLFW_KEY_KP_ADD)) dl.y += lightStep;
        if (keyDown(window, GLFW_KEY_KP_SUBTRACT)) dl.y -= lightStep;
        if (dl.x != 0.0f || dl.y != 0.0f || dl.z != 0.0f)
        {
            if (!gRtLightCustom) { gRtLight = Vec3(-2.6f, 0.60f, -6.0f); gRtLightCustom = true; }
            gRtLight = Vec3(clampf(gRtLight.x + dl.x, -6.0f, 6.0f), clampf(gRtLight.y + dl.y, 0.15f, 5.0f),
                            clampf(gRtLight.z + dl.z, -9.0f, 7.0f));
        }
    }

    // Camera remains free during the automatic service, but the moving
    // mechanisms are owned by the finite-state sequence until it finishes.
    if (serviceRunning()) return;

    if (keyDown(window, GLFW_KEY_Z))
    {
        gDoorOffset = clampf(gDoorOffset - kDoorSpeed * dt, 0.0f, 4.9f);
        gDoorTarget = gDoorOffset;
    }
    if (keyDown(window, GLFW_KEY_X))
    {
        gDoorOffset = clampf(gDoorOffset + kDoorSpeed * dt, 0.0f, 4.9f);
        gDoorTarget = gDoorOffset;
    }

    if (keyDown(window, GLFW_KEY_LEFT_BRACKET))
    {
        float candidate = clampf(gLiftHeight - kLiftSpeed * dt, 0.0f, 3.80f);
        if (liftPositionSafe(candidate)) gLiftHeight = candidate;
        else gSafetyStop = true;
        gLiftTarget = gLiftHeight;
    }
    if (keyDown(window, GLFW_KEY_RIGHT_BRACKET))
    {
        float candidate = clampf(gLiftHeight + kLiftSpeed * dt, 0.0f, 3.80f);
        if (liftPositionSafe(candidate)) gLiftHeight = candidate;
        else gSafetyStop = true;
        gLiftTarget = gLiftHeight;
    }

    float armStep = kArmSpeed * dt;
    float baseStep = kBaseYawSpeed * dt;
    float wristStep = kWristSpeed * dt;

    auto tryPose = [&](float yaw, float j1, float j2, float wp, float wr)
    {
        yaw = clampf(yaw, kBaseYawMin, kBaseYawMax);
        j1 = clampf(j1, kJoint1Min, kJoint1Max);
        j2 = clampf(j2, kJoint2Min, kJoint2Max);
        wp = clampf(wp, kWristPitchMin, kWristPitchMax);
        wr = clampf(wr, kWristRollMin, kWristRollMax);
        if (robotPoseSafe(yaw, j1, j2, wp, wr, gLiftHeight, true))
        {
            cancelRobotMove();          // the operator has taken over
            gRobotBaseYaw = yaw; gJoint1Angle = j1; gJoint2Angle = j2;
            gWristPitch = wp; gWristRoll = wr;
        }
        else
        {
            gSafetyStop = true;
        }
    };

    if (keyDown(window, GLFW_KEY_T))
        tryPose(gRobotBaseYaw + baseStep, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_G))
        tryPose(gRobotBaseYaw - baseStep, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_U))
        tryPose(gRobotBaseYaw, gJoint1Angle + armStep, gJoint2Angle, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_J))
        tryPose(gRobotBaseYaw, gJoint1Angle - armStep, gJoint2Angle, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_I))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle + armStep, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_K))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle - armStep, gWristPitch, gWristRoll);
    if (keyDown(window, GLFW_KEY_Y))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch + wristStep, gWristRoll);
    if (keyDown(window, GLFW_KEY_H))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch - wristStep, gWristRoll);
    if (keyDown(window, GLFW_KEY_RIGHT))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll + wristStep);
    if (keyDown(window, GLFW_KEY_LEFT))
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll - wristStep);

}

struct DemoKey   { float t; Vec3 eye; Vec3 look; float fov; char ease; bool eyeRel, lookRel; };
struct DemoEvent { float t; int key; };
struct DemoHold  { float t0, t1; int key; };
struct DemoRamp  { float t, v, dur, from; int what; bool started; };   // what 0 speed, 1 exposure

static std::string gDemoScript, gDemoPipeFile;
static float gDemoFps = 60.0f;
static int   gDemoStart = 0, gDemoEnd = -1, gDemoFrame = 0;
static float gDemoTime = 0.0f, gDemoLength = 0.0f;
static std::vector<DemoKey>   gDemoKeys;
static std::vector<DemoEvent> gDemoEvents;
static std::vector<DemoHold>  gDemoHolds;
static std::vector<DemoRamp>  gDemoRamps;
static FILE*  gDemoPipe = nullptr;
static GLuint gOutFbo = 0, gOutRbo = 0, gOutPbo[2] = { 0, 0 };
static int    gPreviewW = 960, gPreviewH = 540;

static int demoKeyCode(const std::string& n)
{
    static const struct { const char* name; int key; } kNames[] = {
        {"ENTER", GLFW_KEY_ENTER}, {"BACKSPACE", GLFW_KEY_BACKSPACE}, {"MINUS", GLFW_KEY_MINUS},
        {"EQUAL", GLFW_KEY_EQUAL}, {"PAGE_DOWN", GLFW_KEY_PAGE_DOWN}, {"PAGE_UP", GLFW_KEY_PAGE_UP},
        {"SEMICOLON", GLFW_KEY_SEMICOLON}, {"APOSTROPHE", GLFW_KEY_APOSTROPHE}, {"SLASH", GLFW_KEY_SLASH},
        {"END", GLFW_KEY_END}, {"HOME", GLFW_KEY_HOME}, {"INSERT", GLFW_KEY_INSERT}, {"DELETE", GLFW_KEY_DELETE},
        {"LEFT", GLFW_KEY_LEFT}, {"RIGHT", GLFW_KEY_RIGHT}, {"LEFT_BRACKET", GLFW_KEY_LEFT_BRACKET},
        {"RIGHT_BRACKET", GLFW_KEY_RIGHT_BRACKET}, {"BACKSLASH", GLFW_KEY_BACKSLASH}, {"COMMA", GLFW_KEY_COMMA},
        {"PERIOD", GLFW_KEY_PERIOD}, {"KP_2", GLFW_KEY_KP_2}, {"KP_4", GLFW_KEY_KP_4}, {"KP_6", GLFW_KEY_KP_6},
        {"KP_8", GLFW_KEY_KP_8}, {"KP_ADD", GLFW_KEY_KP_ADD}, {"KP_SUBTRACT", GLFW_KEY_KP_SUBTRACT},
        {"F1", GLFW_KEY_F1}, {"F2", GLFW_KEY_F2}, {"F3", GLFW_KEY_F3}, {"F4", GLFW_KEY_F4}, {"F5", GLFW_KEY_F5},
        {"F6", GLFW_KEY_F6}, {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8}, {"F9", GLFW_KEY_F9}, {"F10", GLFW_KEY_F10},
        {"F11", GLFW_KEY_F11}, {"F12", GLFW_KEY_F12} };
    for (const auto& k : kNames) if (n == k.name) return k.key;
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return GLFW_KEY_A + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return GLFW_KEY_0 + (n[0] - '0');
    std::cout << "[Demo] unknown key name " << n << "\n";
    return -1;
}

static bool loadDemoScript(const std::string& path)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") != 0 || !f) { std::cout << "[Demo] cannot open " << path << "\n"; return false; }
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        char cmd[32] = { 0 };
        if (sscanf_s(line, "%31s", cmd, static_cast<unsigned>(sizeof(cmd))) != 1 || cmd[0] == '#') continue;
        const std::string c = cmd;
        if (c == "cam" || c == "camc" || c == "camt")
        {
            DemoKey k{}; char e[8] = "s";
            const int n = sscanf_s(line, "%*s %f %f %f %f %f %f %f %f %7s", &k.t, &k.eye.x, &k.eye.y, &k.eye.z,
                                   &k.look.x, &k.look.y, &k.look.z, &k.fov, e, static_cast<unsigned>(sizeof(e)));
            if (n < 8) continue;
            k.ease = e[0]; k.eyeRel = (c == "camc"); k.lookRel = (c != "cam");
            gDemoKeys.push_back(k);
        }
        else if (c == "key")
        {
            DemoEvent ev{}; char name[32] = { 0 };
            if (sscanf_s(line, "%*s %f %31s", &ev.t, name, static_cast<unsigned>(sizeof(name))) == 2 &&
                (ev.key = demoKeyCode(name)) >= 0) gDemoEvents.push_back(ev);
        }
        else if (c == "hold")
        {
            DemoHold h{}; char name[32] = { 0 };
            if (sscanf_s(line, "%*s %f %f %31s", &h.t0, &h.t1, name, static_cast<unsigned>(sizeof(name))) == 3 &&
                (h.key = demoKeyCode(name)) >= 0) gDemoHolds.push_back(h);
        }
        else if (c == "speed" || c == "exposure")
        {
            DemoRamp r{}; r.dur = 0.0f; r.what = (c == "speed") ? 0 : 1;
            if (sscanf_s(line, "%*s %f %f %f", &r.t, &r.v, &r.dur) >= 2) gDemoRamps.push_back(r);
        }
        else if (c == "end") sscanf_s(line, "%*s %f", &gDemoLength);
    }
    fclose(f);
    std::stable_sort(gDemoKeys.begin(), gDemoKeys.end(), [](const DemoKey& a, const DemoKey& b) { return a.t < b.t; });
    std::stable_sort(gDemoEvents.begin(), gDemoEvents.end(), [](const DemoEvent& a, const DemoEvent& b) { return a.t < b.t; });
    if (gDemoLength <= 0.0f && !gDemoKeys.empty()) gDemoLength = gDemoKeys.back().t;
    std::printf("[Demo] %d camera keys, %d key presses, %d holds, %.2f s\n", static_cast<int>(gDemoKeys.size()),
                static_cast<int>(gDemoEvents.size()), static_cast<int>(gDemoHolds.size()), gDemoLength);
    return !gDemoKeys.empty();
}

// Fire every key press whose time falls in (prev, now], hold the held keys and
// run the value ramps.
static void demoFireEvents(GLFWwindow* window, float prev, float now)
{
    for (const DemoEvent& e : gDemoEvents)
        if (e.t > prev && e.t <= now) handleDiscreteKey(window, e.key);
    for (int k = 0; k <= GLFW_KEY_LAST; ++k) gDemoHeld[k] = false;
    for (const DemoHold& h : gDemoHolds)
        if (now >= h.t0 && now < h.t1) gDemoHeld[h.key] = true;
    for (DemoRamp& r : gDemoRamps)
    {
        if (now < r.t) continue;
        if (!r.started) { r.started = true; r.from = (r.what == 0) ? gServiceSpeed : gExposure; }
        float u = (r.dur > 1e-4f) ? clampf((now - r.t) / r.dur, 0.0f, 1.0f) : 1.0f;
        u = u * u * (3.0f - 2.0f * u);
        const float v = r.from + (r.v - r.from) * u;
        if (r.what == 0) gServiceSpeed = v; else gExposure = v;
    }
}

static float demoEase(char e, float u)
{
    u = clampf(u, 0.0f, 1.0f);
    switch (e)
    {
    case 'l': return u;
    case 'i': return u * u;
    case 'o': return 1.0f - (1.0f - u) * (1.0f - u);
    default:  return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);   // smootherstep
    }
}

// Centripetal Catmull-Rom (Barry-Goldman form) between p1 and p2.
static Vec3 demoSpline(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u)
{
    if (length(p2 - p1) < 1e-4f) return p1;
    auto knot = [](const Vec3& a, const Vec3& b) { return std::max(std::sqrt(length(b - a)), 1e-3f); };
    const float t0 = 0.0f, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    const float t = t1 + (t2 - t1) * u;
    const Vec3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
    const Vec3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
    const Vec3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
    const Vec3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
    const Vec3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
    return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

static void demoApplyCamera(float t)
{
    const int n = static_cast<int>(gDemoKeys.size());
    if (n == 0) return;
    const Mat4 cm = Scene::serviceCarMatrix(gLiftHeight, gServiceVisual.carYaw);
    const Vec3 car(cm.m[12], cm.m[13], cm.m[14]);
    auto eyeOf  = [&](int i) { const DemoKey& k = gDemoKeys[i]; return k.eyeRel ? car + k.eye : k.eye; };
    auto lookOf = [&](int i) { const DemoKey& k = gDemoKeys[i]; return k.lookRel ? car + k.look : k.look; };

    int i = 0;
    while (i + 1 < n && gDemoKeys[i + 1].t <= t) ++i;
    Vec3 eye, look; float fov;
    if (i + 1 >= n) { eye = eyeOf(n - 1); look = lookOf(n - 1); fov = gDemoKeys[n - 1].fov; }
    else
    {
        const DemoKey& a = gDemoKeys[i];
        const DemoKey& b = gDemoKeys[i + 1];
        const float u = demoEase(b.ease, (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 1.0f);
        const Vec3 e1 = eyeOf(i), e2 = eyeOf(i + 1), l1 = lookOf(i), l2 = lookOf(i + 1);
        const Vec3 e0 = (i > 0) ? eyeOf(i - 1) : e1 * 2.0f - e2;
        const Vec3 l0 = (i > 0) ? lookOf(i - 1) : l1 * 2.0f - l2;
        const Vec3 e3 = (i + 2 < n) ? eyeOf(i + 2) : e2 * 2.0f - e1;
        const Vec3 l3 = (i + 2 < n) ? lookOf(i + 2) : l2 * 2.0f - l1;
        eye = demoSpline(e0, e1, e2, e3, u);
        look = demoSpline(l0, l1, l2, l3, u);
        fov = a.fov + (b.fov - a.fov) * u;
    }
    gCamera.position = eye;
    gCamera.fov = fov;
    const Vec3 d = look - eye;
    const float flat = std::sqrt(d.x * d.x + d.z * d.z);
    gCamera.yaw = std::atan2(d.z, d.x) * 180.0f / PI_F;
    gCamera.pitch = std::atan2(d.y, (flat > 1e-4f) ? flat : 1e-4f) * 180.0f / PI_F;
}

// Off-screen output target at the recording size, plus two pixel-pack
// buffers so reading frame N overlaps with encoding frame N-1.
static bool demoCreateOutput()
{
    glGenRenderbuffers(1, &gOutRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, gOutRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, gWidth, gHeight);
    glGenFramebuffers(1, &gOutFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gOutFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gOutRbo);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenBuffers(2, gOutPbo);
    for (int i = 0; i < 2; ++i)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gOutPbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(gWidth) * gHeight * 3, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return ok;
}

static void demoDrainPbo(int index)
{
    glBindBuffer(GL_PIXEL_PACK_BUFFER, gOutPbo[index]);
    const void* px = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (px && gDemoPipe) fwrite(px, 1, static_cast<size_t>(gWidth) * gHeight * 3, gDemoPipe);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

// Called once per recorded frame, after the overlay is drawn.
static void demoWriteFrame(int written)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gOutFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, gOutPbo[written & 1]);
    glReadPixels(0, 0, gWidth, gHeight, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (written > 0) demoDrainPbo((written - 1) & 1);

    // Small live preview in the window.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, gWidth, gHeight, 0, 0, gPreviewW, gPreviewH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static bool demoOpenPipe()
{
    if (gDemoPipeFile.empty()) return true;   // dry run: render, write nothing
    FILE* f = nullptr;
    if (fopen_s(&f, gDemoPipeFile.c_str(), "r") != 0 || !f) { std::cout << "[Demo] cannot open " << gDemoPipeFile << "\n"; return false; }
    std::string cmd; char buf[4096];
    while (fgets(buf, sizeof(buf), f)) cmd += buf;
    fclose(f);
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
    gDemoPipe = _popen(cmd.c_str(), "wb");
    if (!gDemoPipe) { std::cout << "[Demo] could not start encoder\n"; return false; }
    return true;
}

static void demoClosePipe(int written)
{
    if (written > 0) demoDrainPbo((written - 1) & 1);
    if (gDemoPipe) { fflush(gDemoPipe); _pclose(gDemoPipe); gDemoPipe = nullptr; }
}

static void printControls()
{
    std::cout << "============================================================\n";
    std::cout << " SmartGarage AUTOMATIC SERVICE v6\n";
    std::cout << " Reference-accurate Cybertruck shell + physically based renderer\n";
    std::cout << " Cook-Torrance GGX | anisotropic brushed metal | clear coat\n";
    std::cout << " split-sum HDR IBL | 5x5 PCF shadows | 4x MSAA | ACES tone map\n";
    std::cout << "============================================================\n";
    std::cout << " W / S          move forward / backward\n";
    std::cout << " A / D          strafe left / right\n";
    std::cout << " SPACE / CTRL   move up / down   (E / Q also work)\n";
    std::cout << " LEFT SHIFT     hold to move faster\n";
    std::cout << " MOUSE          look around\n";
    std::cout << " SCROLL         zoom (field of view)\n";
    std::cout << " TAB            release / capture the mouse cursor\n";
    std::cout << " L              wireframe on / off\n";
    std::cout << " V              print the geometry teaching view\n";
    std::cout << " 1              normal full scene (top row)\n";
    std::cout << " 2 / 3 / 4      isolate car / lift / robot construction (top row)\n";
    std::cout << " 5 / 6 / 7 / 8  isolate cube / 28-seg cylinder / 48-seg cylinder / UV sphere\n";
    std::cout << " N / B          next / previous construction stage\n";
    std::cout << " O              automatic garage-door open/close\n";
    std::cout << " Z / X / C      door close / open / STOP at current position\n";
    std::cout << " P              automatic lift lower/raise\n";
    std::cout << " [ / ] / \\      lift lower / raise / STOP at current height\n";
    std::cout << " T / G          rotate TOP robot turntable left / right (square pedestal fixed)\n";
    std::cout << " U / J          robot shoulder up / down\n";
    std::cout << " I / K          robot elbow up / down\n";
    std::cout << " Y / H          robot wrist pitch (limits -270..+270 deg)\n";
    std::cout << " LEFT / RIGHT   robot wrist roll\n";
    std::cout << " F1/F2/F3/F4    scanner / impact driver / gripper / polisher\n";
    std::cout << " ENTER / NUMPAD ENTER   run complete scan/clean/polish + wheel-fastener service\n";
    std::cout << " F5             cycle shading: Cook-Torrance PBR -> Gouraud -> Phong\n";
    std::cout << " F6             material textures on/off (sampled array + procedural)\n";
    std::cout << " BACKSPACE      PROJECTION: perspective <-> orthographic\n";
    std::cout << " F7             normal/bump maps on/off\n";
    std::cout << " F8             directional PCF shadows on/off\n";
    std::cout << " F9             stainless/glass environment reflections on/off\n";
    std::cout << " F11            4x MSAA on/off\n";
    std::cout << " F12            SHOWCASE ORBIT - hands-free turntable around the vehicle\n";
    std::cout << " M              cycle lighting preset (Workshop / Studio / Night)\n";
    std::cout << " --- RAY-TRACED UNDERBODY INSPECTION MONITOR (beside the lift) ---\n";
    std::cout << " 9              enlarge / close the inspection view\n";
    std::cout << " -              inspection-light shadow rays on / off\n";
    std::cout << " =              cycle the inspection camera preset\n";
    std::cout << " Insert/Delete  orbit the inspection camera\n";
    std::cout << " PageDown       cycle the inspection light position\n";
    std::cout << " Numpad 4/6 8/2 +/-   move the inspection light (x, z, height)\n";
    std::cout << " 0              outside light: daylight <-> night\n";
    std::cout << " F              save a screenshot (SmartGarage_shot_NNN.bmp)\n";
    std::cout << "\n --- INTERACTIVE SCENE SWITCHES ---\n";
    std::cout << " ;              overhead work lights on / off\n";
    std::cout << " '              vehicle light bars + headlight point lights on / off\n";
    std::cout << " /              neon sign on / off\n";
    std::cout << " END            hazard beacon flash on / off\n";
    std::cout << " HOME           drive the vehicle OUT of the bay / bring it back\n";
    std::cout << " PAGE UP        send the vehicle back outside, to replay the arrival\n";
    std::cout << "\n Command line: --autostart --showcase --preset N --capture file.bmp\n";
    std::cout << "               --orbit DEG --radius M --height M --fov DEG --size W H\n";
    std::cout << " , / .          environment lighting intensity down / up\n";
    std::cout << " F10            HDR bloom on/off\n";
    std::cout << "                 collision safety stops arm/lift before intersections\n";
    std::cout << " R              reset camera + automatic service demo\n";
    std::cout << " ESC            quit\n";
    std::cout << "============================================================\n" << std::endl;
}

struct ShadowTarget
{
    unsigned int fbo = 0;
    unsigned int depth = 0;
    int size = 2048;
};

struct HDRTargets
{
    unsigned int fbo = 0;
    unsigned int color[2] = { 0, 0 }; // scene + bright-pass
    unsigned int depthRbo = 0;
    unsigned int pingFbo[2] = { 0, 0 };
    unsigned int pingColor[2] = { 0, 0 };

    unsigned int msFbo = 0;
    unsigned int msColor[2] = { 0, 0 };
    unsigned int msDepthRbo = 0;
    bool  msReady = false;
    int   samples = 4;

    int width = 0;
    int height = 0;
    int bloomWidth = 0;    // the bloom chain runs at half resolution
    int bloomHeight = 0;
};

static unsigned int gMaterialTextures = 0;
static unsigned int gQuadVAO = 0;
static unsigned int gQuadVBO = 0;

// The spot shadow map.  Smaller than the directional one: it only ever has to
// cover the few metres inside the lamp's cone.
static ShadowTarget gSpotShadow;

static Mat4 buildSpotLightMatrix(const Vec3& pos, const Vec3& dir, float outerDeg)
{
    Vec3 d = normalize(dir);
    Vec3 up = (std::fabs(d.y) > 0.95f) ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);
    Mat4 view = lookAt(pos, pos + d, up);
    Mat4 proj = perspective(outerDeg * 2.0f + 6.0f, 1.0f, 0.12f, 24.0f);
    return proj * view;
}

static bool createShadowTarget(ShadowTarget& t)
{
    glGenFramebuffers(1, &t.fbo);
    glGenTextures(1, &t.depth);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
        t.size, t.size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

static void destroyHDRTargets(HDRTargets& t)
{
    if (t.depthRbo) glDeleteRenderbuffers(1, &t.depthRbo);
    if (t.color[0] || t.color[1]) glDeleteTextures(2, t.color);
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.pingColor[0] || t.pingColor[1]) glDeleteTextures(2, t.pingColor);
    if (t.pingFbo[0] || t.pingFbo[1]) glDeleteFramebuffers(2, t.pingFbo);
    if (t.msDepthRbo) glDeleteRenderbuffers(1, &t.msDepthRbo);
    if (t.msColor[0] || t.msColor[1]) glDeleteTextures(2, t.msColor);
    if (t.msFbo) glDeleteFramebuffers(1, &t.msFbo);
    t = HDRTargets();
}

static bool createMultisampleTarget(HDRTargets& t, int width, int height)
{
    int maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    t.samples = (maxSamples >= 4) ? 4 : ((maxSamples >= 2) ? 2 : 0);
    if (t.samples < 2) return false;

    glGenFramebuffers(1, &t.msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.msFbo);
    glGenTextures(2, t.msColor);
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, t.msColor[i]);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, t.samples,
            GL_RGB16F, width, height, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D_MULTISAMPLE, t.msColor[i], 0);
    }
    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);

    glGenRenderbuffers(1, &t.msDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, t.msDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, t.samples,
        GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, t.msDepthRbo);

    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

// Resolve the multisampled scene into the ordinary textures the bloom and
// tone-map passes sample from.  MRT needs one blit per attachment.
static void resolveMultisample(const HDRTargets& t)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, t.msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.fbo);
    for (int i = 0; i < 2; ++i)
    {
        glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
        glBlitFramebuffer(0, 0, t.width, t.height, 0, 0, t.width, t.height,
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    // Leave the draw framebuffer with both attachments enabled again.
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static bool ensureHDRTargets(HDRTargets& t, int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (t.fbo && t.width == width && t.height == height) return true;
    destroyHDRTargets(t);
    t.width = width; t.height = height;

    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glGenTextures(2, t.color);
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, t.color[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, t.color[i], 0);
    }
    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);

    glGenRenderbuffers(1, &t.depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthRbo);
    bool hdrOK = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    t.bloomWidth = (width / 2 > 1) ? width / 2 : 1;
    t.bloomHeight = (height / 2 > 1) ? height / 2 : 1;

    glGenFramebuffers(2, t.pingFbo);
    glGenTextures(2, t.pingColor);
    bool pingOK = true;
    for (int i = 0; i < 2; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, t.pingFbo[i]);
        glBindTexture(GL_TEXTURE_2D, t.pingColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, t.bloomWidth, t.bloomHeight,
            0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.pingColor[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) pingOK = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    t.msReady = createMultisampleTarget(t, width, height);
    if (!t.msReady)
        std::cout << "[Render] Multisampled HDR target unavailable - "
                     "falling back to the post-process edge filter.\n";

    return hdrOK && pingOK;
}

static void initFullscreenQuad()
{
    const float q[] = {
        // pos      uv
        -1,-1,     0,0,
         1,-1,     1,0,
         1, 1,     1,1,
        -1,-1,     0,0,
         1, 1,     1,1,
        -1, 1,     0,1
    };
    glGenVertexArrays(1, &gQuadVAO);
    glGenBuffers(1, &gQuadVBO);
    glBindVertexArray(gQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

static void drawFullscreenQuad()
{
    glBindVertexArray(gQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

static void drawCurrentContent()
{
    if (gTeachObject == TEACH_SCENE)
    {
        Scene::endTeachingDraw();
        Scene::drawScene(gDoorOffset,
            gLiftHeight,
            gRobotBaseYaw,
            gJoint1Angle,
            gJoint2Angle,
            gWristPitch,
            gWristRoll,
            gRobotTool,
            gServiceVisual);
    }
    else if (isProgressiveTeachingObject())
    {
        Scene::beginTeachingDraw(teachingLimit(), gTeachNoHighlight ? -1 : teachingHighlightFrom());
        if (gTeachObject == TEACH_CAR)
            Scene::drawCar(mat4Identity(), Vec3(0.680f, 0.300f, 0.115f));
        else if (gTeachObject == TEACH_LIFT)
            Scene::drawHydraulicLift(mat4Identity(), gLiftHeight);
        else if (gTeachObject == TEACH_ROBOT)
            Scene::drawRobotArm(mat4Identity(), gRobotBaseYaw, gJoint1Angle, gJoint2Angle,
                gWristPitch, gWristRoll, gRobotTool);
        Scene::endTeachingDraw();
    }
    else
    {
        Scene::endTeachingDraw();
        if (gTeachObject == TEACH_CUBE)
            Scene::drawCube(scale(2.0f, 2.0f, 2.0f), Vec3(0.68f, 0.30f, 0.115f));
        else if (gTeachObject == TEACH_CYLINDER)
            Scene::drawCylinder(scale(2.0f, 2.0f, 2.0f), Vec3(0.145f, 0.275f, 0.545f));
        else if (gTeachObject == TEACH_SMOOTH_CYLINDER)
            Scene::drawCylS(scale(2.0f, 2.0f, 2.0f), Vec3(0.145f, 0.275f, 0.545f));
        else if (gTeachObject == TEACH_SPHERE)
            Scene::drawSphere(scale(2.0f, 2.0f, 2.0f), Vec3(0.145f, 0.275f, 0.545f));
    }
}

static Mat4 buildLightSpaceMatrix(Vec3& outDirection)
{
    outDirection = normalize(Vec3(0.18f, -1.0f, -0.38f)); // light -> scene
    Vec3 centre(0.0f, 2.2f, -1.0f);
    Vec3 lightPos = centre - outDirection * 28.0f;
    Mat4 lightView = lookAt(lightPos, centre, Vec3(0.0f, 1.0f, 0.0f));
    Mat4 lightProj = orthographic(-19.0f, 19.0f, -18.0f, 18.0f, 1.0f, 58.0f);
    return lightProj * lightView;
}

static const int kMatTexSize = 256;
static const int kMatTexLayers = 4;

static float matHash(int x, int y, int seed)
{
    // Wrap the lattice so the noise is periodic and the tile is seamless.
    x = ((x % kMatTexSize) + kMatTexSize) % kMatTexSize;
    y = ((y % kMatTexSize) + kMatTexSize) % kMatTexSize;
    unsigned int h = static_cast<unsigned int>(x * 374761393 + y * 668265263 + seed * 69069);
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777215.0f;
}

static float matValueNoise(float u, float v, int freq, int seed)
{
    const float x = u * static_cast<float>(freq);
    const float y = v * static_cast<float>(freq);
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(xi);
    float fy = y - static_cast<float>(yi);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const int step = kMatTexSize / (freq > 0 ? freq : 1);
    auto H = [&](int a, int b) { return matHash(a * step, b * step, seed); };
    const float a = H(xi, yi), b = H(xi + 1, yi), c = H(xi, yi + 1), d = H(xi + 1, yi + 1);
    return (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
}

static float matFbm(float u, float v, int baseFreq, int octaves, int seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int f = baseFreq;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * matValueNoise(u, v, f, seed + i * 17);
        norm += amp;
        amp *= 0.5f;
        f *= 2;
    }
    return sum / (norm > 0.0f ? norm : 1.0f);
}

static unsigned int createMaterialTextureArray()
{
    const int S = kMatTexSize;
    std::vector<unsigned char> pixels(static_cast<size_t>(S * S * 4 * kMatTexLayers));

    auto put = [&](int layer, int x, int y, float r, float g, float b, float rough)
    {
        auto enc = [](float v) -> unsigned char
        {
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            return static_cast<unsigned char>(v * 255.0f + 0.5f);
        };
        const size_t k = (static_cast<size_t>(layer) * S * S + static_cast<size_t>(y) * S + x) * 4;
        pixels[k + 0] = enc(r * 0.5f);      // detail multiplier, 0..2 packed into 0..1
        pixels[k + 1] = enc(g * 0.5f);
        pixels[k + 2] = enc(b * 0.5f);
        pixels[k + 3] = enc(rough);
    };

    for (int y = 0; y < S; ++y)
    {
        for (int x = 0; x < S; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(S);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(S);

            // ---- layer 0: worn concrete -------------------------------
            {
                const float broad = matFbm(u, v, 6, 3, 11);
                const float grit = matValueNoise(u, v, 64, 23);
                const float pores = matValueNoise(u, v, 128, 31);

                // Hairline cracks from a ridged noise, kept genuinely hairline.
                const float ridge = 1.0f - std::fabs(matFbm(u, v, 7, 3, 47) * 2.0f - 1.0f);
                const float crack = (ridge > 0.978f) ? (ridge - 0.978f) / 0.022f : 0.0f;

                float tint = 0.94f + 0.13f * broad + 0.11f * (grit - 0.5f) + 0.06f * (pores - 0.5f);
                tint *= (1.0f - 0.26f * crack);
                const float rough = 0.76f + 0.14f * (1.0f - broad) + 0.08f * crack;
                put(0, x, y, tint, tint * 0.995f, tint * 0.985f, rough);
            }

            // ---- layer 1: painted plaster -----------------------------
            {
                const float broad = matFbm(u, v, 4, 3, 101);
                const float stipple = matValueNoise(u, v, 96, 113);
                const float tint = 0.93f + 0.11f * broad + 0.05f * (stipple - 0.5f);
                const float rough = 0.70f + 0.14f * broad;
                put(1, x, y, tint, tint, tint * 1.005f, rough);
            }

            // ---- layer 2: asphalt -------------------------------------
            {
                const float coarse = matFbm(u, v, 10, 3, 211);
                const float stones = matValueNoise(u, v, 48, 223);
                const float grit = matValueNoise(u, v, 160, 229);
                float tint = 0.92f + 0.16f * coarse + 0.20f * (stones - 0.5f) + 0.10f * (grit - 0.5f);
                const float rough = 0.88f + 0.10f * grit;
                put(2, x, y, tint, tint * 0.99f, tint * 0.98f, rough);
            }

            // ---- layer 3: steel tread plate ---------------------------
            {
                // Two sets of short oblique bars in a brick layout - the
                // classic teardrop/diamond non-slip plate.
                const float cell = 4.0f;             // bars across the tile
                const float gy = v * cell;
                const int   row = static_cast<int>(std::floor(gy));
                const float fy = gy - static_cast<float>(row);
                const float slant = ((row & 1) == 0) ? 0.55f : -0.55f;
                const float offset = ((row & 1) == 0) ? 0.0f : 0.5f;
                float gx = u * cell + offset + slant * (fy - 0.5f);
                gx = gx - std::floor(gx);

                // Raised bar occupies the middle of each cell in both axes.
                const float bx = 1.0f - std::fabs(gx - 0.5f) * 2.0f;          // 0 at edges, 1 at centre
                const float by = 1.0f - std::fabs(fy - 0.5f) * 2.0f;
                float bar = bx * 1.35f - 0.35f;
                bar *= (by > 0.30f) ? 1.0f : (by / 0.30f);
                if (bar < 0.0f) bar = 0.0f;
                if (bar > 1.0f) bar = 1.0f;
                const float smooth = bar * bar * (3.0f - 2.0f * bar);

                const float wear = matValueNoise(u, v, 32, 307);
                const float tint = 0.80f + 0.45f * smooth + 0.08f * (wear - 0.5f);
                const float rough = 0.52f - 0.20f * smooth + 0.10f * (wear - 0.5f);
                put(3, x, y, tint, tint, tint, rough);
            }
        }
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, S, S, kMatTexLayers, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    float maxAniso = 0.0f;
    glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &maxAniso);
    if (maxAniso > 1.0f)
    {
        const float want = (maxAniso > 8.0f) ? 8.0f : maxAniso;
        glTexParameterf(GL_TEXTURE_2D_ARRAY, 0x84FE /* TEXTURE_MAX_ANISOTROPY_EXT */, want);
        std::cout << "[Texture] material array: 4 layers, " << S << "px, "
                  << want << "x anisotropic\n";
    }
    else
    {
        std::cout << "[Texture] material array: 4 layers, " << S << "px, trilinear\n";
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return tex;
}

static unsigned int createEnvironmentCubemap()
{
    const int S = 128;
    std::vector<float> pixels(static_cast<size_t>(S * S * 3));
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    for (int face = 0; face < 6; ++face)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int x = 0; x < S; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(S);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(S);
                float r, g, b;

                if (face == 0 || face == 1)   // +/-X  side walls
                {
                    // Painted blockwork with a brighter band at eye height
                    // where the wall catches the fixtures.
                    const float panel = ((x / 14) & 1) ? 0.030f : -0.020f;
                    const float band = std::exp(-std::pow((v - 0.46f) * 6.0f, 2.0f));
                    r = 0.230f + panel + 0.070f * v + 0.210f * band;
                    g = 0.244f + panel + 0.074f * v + 0.224f * band;
                    b = 0.268f + panel + 0.082f * v + 0.246f * band;

                    // A dark skirting strip grounds the reflection.
                    if (v < 0.14f) { r *= 0.42f; g *= 0.44f; b *= 0.48f; }

                    const float barPhase = std::fmod(u * 5.0f + 0.5f, 1.0f);
                    if (barPhase > 0.455f && barPhase < 0.545f &&
                        v > 0.34f && v < 0.70f)
                    {
                        r = 3.60f; g = 2.45f; b = 1.25f;
                    }

                    // Cool strip lights washing the upper wall.
                    if (v > 0.80f && v < 0.855f && std::fmod(u * 3.0f, 1.0f) > 0.18f)
                    {
                        r = 2.10f; g = 2.15f; b = 2.20f;
                    }
                }
                else if (face == 2)           // +Y  ceiling with the strip lights
                {
                    r = 0.380f; g = 0.394f; b = 0.420f;

                    // Eight runs of fluorescent strips, broken into fittings.
                    const float stripPhase = std::fmod(u * 8.0f, 1.0f);
                    const float runPhase = std::fmod(v * 3.0f, 1.0f);
                    const bool stripe = stripPhase > 0.455f && stripPhase < 0.545f;
                    const bool lit = runPhase > 0.10f && runPhase < 0.92f;
                    if (stripe && lit)
                    {
                        // Well above 1.0 on purpose: this is what a metal panel
                        // reflects as a hot, legible highlight band.
                        r = 5.20f; g = 5.00f; b = 4.55f;
                    }
                    else if (stripe)
                    {
                        r = 0.44f; g = 0.45f; b = 0.47f;   // the fitting body
                    }
                }
                else if (face == 3)           // -Y  concrete floor
                {
                    unsigned int h = static_cast<unsigned int>(x * 1973 + y * 9277 + 89173);
                    h = (h << 13) ^ h;
                    const float n = 0.5f + 0.5f * (1.0f -
                        static_cast<float>((h * (h * h * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)
                        / 1073741824.0f);
                    r = 0.152f + 0.072f * n;
                    g = 0.152f + 0.071f * n;
                    b = 0.162f + 0.077f * n;
                }
                else if (face == 4)           // +Z  the open door and daylight beyond
                {
                    const float sky = 0.30f + 0.70f * v;
                    const float horizon = std::exp(-std::pow((v - 0.38f) * 7.0f, 2.0f));
                    r = 1.05f * sky + 0.55f * horizon;
                    g = 1.30f * sky + 0.62f * horizon;
                    b = 1.75f * sky + 0.74f * horizon;

                    // Below the horizon is sunlit apron, not sky.
                    if (v < 0.30f)
                    {
                        const float t = v / 0.30f;
                        r = 0.52f + 0.30f * t;
                        g = 0.50f + 0.30f * t;
                        b = 0.48f + 0.32f * t;
                    }
                }
                else                           // -Z  back wall and racking
                {
                    const float band = std::exp(-std::pow((v - 0.48f) * 7.5f, 2.0f));
                    r = 0.150f + 0.090f * v + 0.120f * band;
                    g = 0.160f + 0.096f * v + 0.130f * band;
                    b = 0.182f + 0.106f * v + 0.146f * band;
                    if (((x / 16) & 1) == 0) { r *= 0.84f; g *= 0.85f; b *= 0.88f; }

                    // The lit SMART AUTO sign on the back wall.
                    if (u > 0.30f && u < 0.72f && v > 0.56f && v < 0.68f)
                    {
                        r = 2.40f; g = 2.05f; b = 1.30f;
                    }
                }

                const size_t k = static_cast<size_t>((y * S + x) * 3);
                pixels[k + 0] = (r > 0.0f) ? r : 0.0f;
                pixels[k + 1] = (g > 0.0f) ? g : 0.0f;
                pixels[k + 2] = (b > 0.0f) ? b : 0.0f;
            }
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
            S, S, 0, GL_RGB, GL_FLOAT, pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

static const float kHeadlightIntensity = 1.6f;

struct LightRig
{
    Vec3  pos[10];          // 8 rig lights + 2 vehicle headlights
    Vec3  color[10];
    float intensity[10];
    int   count;
    Vec3  keyColor;
    float keyIntensity;
    float exposure;
    float iblScale;
};

// The honest working shop: eight ceiling fixtures in two rows down the bays.
static void buildWorkshopRig(LightRig& r)
{
    const Vec3 p[8] = {
        Vec3(-6.4f, 6.32f, -10.10f), Vec3(6.4f, 6.32f, -9.55f),
        Vec3(-6.4f, 6.32f,  -3.90f), Vec3(6.4f, 6.32f, -3.35f),
        Vec3(-6.4f, 6.32f,   2.30f), Vec3(6.4f, 6.32f,  2.85f),
        Vec3(-6.4f, 6.32f,   8.50f), Vec3(6.4f, 6.32f,  9.05f)
    };
    const float bayLevel = gSwitches.ceilingLights ? 1.28f : 0.06f;
    for (int i = 0; i < 8; ++i)
    {
        r.pos[i] = p[i];
        r.color[i] = Vec3(0.96f, 0.91f, 0.82f);
        r.intensity[i] = bayLevel;
    }
    r.count = 8;
    r.keyColor = Vec3(0.95f, 0.91f, 0.84f);
    r.keyIntensity = 0.68f;
    r.exposure = gSwitches.ceilingLights ? 0.74f : 1.25f;
    r.iblScale = gSwitches.ceilingLights ? 1.0f : 0.30f;
}

static void buildStudioRig(LightRig& r, const Vec3& subject)
{
    r.count = 8;
    // key
    r.pos[0] = subject + Vec3(-4.2f, 4.3f, -4.6f);
    r.color[0] = Vec3(1.00f, 0.97f, 0.93f);  r.intensity[0] = 6.2f;
    // fill
    r.pos[1] = subject + Vec3(4.6f, 2.2f, 3.9f);
    r.color[1] = Vec3(0.80f, 0.86f, 1.00f);  r.intensity[1] = 1.9f;
    // rim left / rim right - these are what make a faceted metal body read
    r.pos[2] = subject + Vec3(-3.4f, 2.5f, 4.8f);
    r.color[2] = Vec3(1.00f, 0.94f, 0.86f);  r.intensity[2] = 4.4f;
    r.pos[3] = subject + Vec3(3.4f, 2.5f, -4.8f);
    r.color[3] = Vec3(1.00f, 0.94f, 0.86f);  r.intensity[3] = 4.4f;
    // overhead softbox pair
    r.pos[4] = subject + Vec3(-1.5f, 5.2f, -1.2f);
    r.color[4] = Vec3(0.98f, 0.97f, 0.95f);  r.intensity[4] = 2.6f;
    r.pos[5] = subject + Vec3(1.5f, 5.2f, 1.2f);
    r.color[5] = Vec3(0.98f, 0.97f, 0.95f);  r.intensity[5] = 2.6f;
    // low kickers that lift the rocker and the wheel faces out of shadow
    r.pos[6] = subject + Vec3(-3.0f, 0.55f, 0.0f);
    r.color[6] = Vec3(0.92f, 0.94f, 1.00f);  r.intensity[6] = 0.9f;
    r.pos[7] = subject + Vec3(3.0f, 0.55f, 0.0f);
    r.color[7] = Vec3(0.92f, 0.94f, 1.00f);  r.intensity[7] = 0.9f;

    r.keyColor = Vec3(0.97f, 0.95f, 0.92f);
    r.keyIntensity = 0.30f;
    r.exposure = 0.70f;
    r.iblScale = 0.52f;
}

// Night: the fixtures are dimmed to standby, so the emissive signature, the
// neon and the environment reflections carry the image.
static void buildNightRig(LightRig& r)
{
    buildWorkshopRig(r);
    for (int i = 0; i < 8; ++i)
    {
        r.color[i] = Vec3(0.62f, 0.72f, 0.96f);
        r.intensity[i] = 0.34f;
    }
    r.keyColor = Vec3(0.42f, 0.52f, 0.78f);
    r.keyIntensity = 0.14f;
    r.exposure = 1.05f;
    r.iblScale = 0.55f;
}

// Night swaps the sun for a dim blue moon and weakens the environment probe.
// Daylight leaves the rig untouched: the rig's key light IS the sun.
static void applyDayNight(LightRig& r)
{
    if (!gNight) return;
    r.keyColor = Vec3(0.36f, 0.46f, 0.78f);
    r.keyIntensity = 0.05f;
    r.iblScale *= 0.30f;
}

static void buildActiveRig(LightRig& r, const Vec3& subject)
{
    switch (gLightingPreset)
    {
    case LIGHT_STUDIO: buildStudioRig(r, subject); break;
    case LIGHT_NIGHT:  buildNightRig(r);           break;
    default:           buildWorkshopRig(r);        break;
    }
    applyDayNight(r);
}

static void uploadSceneUniforms(unsigned int program,
                                const Mat4& view,
                                const Mat4& projection,
                                const Mat4& lightSpace,
                                const Vec3& dirLight,
                                unsigned int shadowTexture,
                                unsigned int environmentTexture,
                                const Vec3& subject)
{
    glUniformMatrix4fv(glGetUniformLocation(program, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1, GL_FALSE, projection.m);
    glUniformMatrix4fv(glGetUniformLocation(program, "uLightSpaceMatrix"), 1, GL_FALSE, lightSpace.m);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"),
        gCamera.position.x, gCamera.position.y, gCamera.position.z);

    glUniform1i(glGetUniformLocation(program, "uShadingMode"), gShadingMode);
    glUniform1i(glGetUniformLocation(program, "uTexturesEnabled"), gTexturesEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uNormalMapsEnabled"), gNormalMapsEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uShadowsEnabled"), gShadowsEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uReflectionsEnabled"), gReflectionsEnabled ? 1 : 0);

    LightRig rig;
    buildActiveRig(rig, subject);

    if (gSwitches.vehicleLights && gTeachObject == TEACH_SCENE && rig.count <= 8)
    {
        const Mat4 car = Scene::serviceCarMatrix(gLiftHeight, gServiceVisual.carYaw);
        for (int side = -1; side <= 1; side += 2)
        {
            rig.pos[rig.count] = transformPoint(car, Scene::headlightLocalPosition(side));
            rig.color[rig.count] = Vec3(1.00f, 0.96f, 0.88f);
            rig.intensity[rig.count] = kHeadlightIntensity;
            ++rig.count;
        }
    }

    glUniform1f(glGetUniformLocation(program, "uIblIntensity"),
        gIblIntensity * rig.iblScale);

    glUniform1f(glGetUniformLocation(program, "uPolish"), 0.0f);
    glUniform1f(glGetUniformLocation(program, "uClearCoat"), 0.0f);
    glUniform1f(glGetUniformLocation(program, "uAnisotropy"), 0.0f);

    glUniform3f(glGetUniformLocation(program, "uDirLightDirection"), dirLight.x, dirLight.y, dirLight.z);
    glUniform3f(glGetUniformLocation(program, "uDirLightColor"),
        rig.keyColor.x, rig.keyColor.y, rig.keyColor.z);
    glUniform1f(glGetUniformLocation(program, "uDirLightIntensity"), rig.keyIntensity);

    glUniform1i(glGetUniformLocation(program, "uPointLightCount"), rig.count);
    for (int i = 0; i < rig.count; ++i)
    {
        const std::string posName = "uPointLightPos[" + std::to_string(i) + "]";
        const std::string colName = "uPointLightColor[" + std::to_string(i) + "]";
        const std::string intName = "uPointLightIntensity[" + std::to_string(i) + "]";
        glUniform3f(glGetUniformLocation(program, posName.c_str()),
            rig.pos[i].x, rig.pos[i].y, rig.pos[i].z);
        glUniform3f(glGetUniformLocation(program, colName.c_str()),
            rig.color[i].x, rig.color[i].y, rig.color[i].z);
        glUniform1f(glGetUniformLocation(program, intName.c_str()), rig.intensity[i]);
    }

    Vec3 shoulder, elbow, wrist, toolEnd;
    // Use the real tool length so the inspection beam leaves the scanner's
    // lens rather than a point 2 cm behind it.
    robotSafetyPoints(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch,
        shoulder, elbow, wrist, toolEnd, currentToolReach());
    Vec3 spotDir = normalize(toolEnd - wrist);
    glUniform3f(glGetUniformLocation(program, "uSpotPos"), toolEnd.x, toolEnd.y, toolEnd.z);
    glUniform3f(glGetUniformLocation(program, "uSpotDir"), spotDir.x, spotDir.y, spotDir.z);
    glUniform3f(glGetUniformLocation(program, "uSpotColor"), 0.48f, 0.86f, 1.00f);
    glUniform1f(glGetUniformLocation(program, "uSpotIntensity"), gRobotTool == 0 ? 4.6f : 0.0f);
    glUniform1f(glGetUniformLocation(program, "uSpotInnerCos"), std::cos(radians(14.0f)));
    glUniform1f(glGetUniformLocation(program, "uSpotOuterCos"), std::cos(radians(23.0f)));

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, shadowTexture);
    glUniform1i(glGetUniformLocation(program, "uShadowMap"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentTexture);
    glUniform1i(glGetUniformLocation(program, "uEnvironmentMap"), 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gMaterialTextures);
    glUniform1i(glGetUniformLocation(program, "uMaterialTextures"), 6);
    glActiveTexture(GL_TEXTURE0);
}

// The exposure the active rig was balanced for.  Keeping this beside the rig
// means switching preset never blows out or crushes the image.
static float activeRigExposure(const Vec3& subject)
{
    LightRig rig;
    buildActiveRig(rig, subject);
    return rig.exposure;
}

// ---- offline capture configuration, set from argv ------------------------
static std::string gCapturePath;
static int   gCaptureFrames = 24;
static float gCapturePolish = -1.0f;
static float gCaptureDirt = -1.0f;
static float gCaptureCarYaw = 0.0f;
static bool  gCaptureHasCam = false;
static float gCaptureCam[5] = { 0, 0, 0, 0, 0 };
static float gCaptureLift = -1.0f;
static float gCaptureDoor = -1.0f;      // freeze the roller door at this opening (0..4.9 m)
static float gCaptureCarDrive = -1.0f;   // freeze the arrival part-way, for inspection
static bool  gCaptureNoTextures = false;
static float gCaptureFov = -1.0f;
static bool  gAutoStartService = false;
static bool  gCaptureRobot = false;
// More capture-only switches, so every comparison figure in the report can be
// rendered by the project itself from the command line.
static int   gCaptureTeach = 0;        // 1..8 = the same object as keys 1..8
static int   gCaptureStage = -1;       // construction stage for teach 2/3/4
static bool  gCaptureWire = false;
static float gCaptureSimTime = -1.0f;
static bool  gCaptureFreeze = false;
// Background colour of the HDR scene pass (--clear r g b); a light one is used
// for the engineering-style orthographic views.
static float gClearColor[3] = { 0.012f, 0.015f, 0.022f };   // used only with --clear
static bool  gClearOverride = false;
// The sky is the clear colour: bright daylight blue, or a near-black night.
static const float kSkyDay[3]   = { 0.42f, 0.70f, 1.30f };
static const float kSkyNight[3] = { 0.008f, 0.011f, 0.025f };

struct RtMonitor
{
    unsigned int prog = 0, screenProg = 0, blitProg = 0;
    unsigned int fbo = 0, tex = 0, itemsTex = 0, chunksTex = 0;
    int w = 0, h = 0;
    bool ok = false;
    int itemCount = 0, triCount = 0, chunkCount = 0;
    float traceMs = 0.0f;
};
static RtMonitor gRt;

static const int kRtItemTexW = 4096;     // texels per row of the item texture
static const int kRtItemTexH = 64;       // 4096 * 64 / 4 = 65536 items
static const int kRtChunkTexW = 1024;
static const int kRtChunkTexH = 16;      // 1024 * 16 / 2 = 8192 chunks
static const int kRtChunkSize = 16;      // items per bounding sphere

static void rtPickSize(int& w, int& h)
{
    if (gRtLarge) { w = 1024; h = 640; }
    else          { w = 512;  h = 320; }
    w *= gRtScale; h *= gRtScale;
}

static void rtAllocTarget(int w, int h)
{
    if (gRt.tex) glDeleteTextures(1, &gRt.tex);
    if (gRt.fbo) glDeleteFramebuffers(1, &gRt.fbo);
    glGenTextures(1, &gRt.tex);
    glBindTexture(GL_TEXTURE_2D, gRt.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &gRt.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gRt.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gRt.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cout << "[RayTrace] framebuffer incomplete - monitor disabled.\n";
        gRt.ok = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gRt.w = w;
    gRt.h = h;
}

static unsigned int rtMakeFloatTexture(int w, int h)
{
    unsigned int t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static bool rtInit()
{
    gRt.prog = createRayTraceShaderProgram();
    gRt.screenProg = createMonitorShaderProgram();
    gRt.blitProg = createBlitShaderProgram();
    if (!gRt.prog || !gRt.screenProg || !gRt.blitProg)
    {
        std::cout << "[RayTrace] shader compilation failed - the rest of the garage is unaffected; "
                     "the inspection monitor stays off.\n";
        if (gRt.prog) glDeleteProgram(gRt.prog);
        if (gRt.screenProg) glDeleteProgram(gRt.screenProg);
        if (gRt.blitProg) glDeleteProgram(gRt.blitProg);
        gRt.prog = gRt.screenProg = gRt.blitProg = 0;
        return false;
    }
    gRt.itemsTex = rtMakeFloatTexture(kRtItemTexW, kRtItemTexH);
    gRt.chunksTex = rtMakeFloatTexture(kRtChunkTexW, kRtChunkTexH);
    gRt.ok = true;
    int w, h;
    rtPickSize(w, h);
    rtAllocTarget(w, h);
    return gRt.ok;
}

static void rtApplySize()
{
    if (!gRt.ok) return;
    int w, h;
    rtPickSize(w, h);
    if (w != gRt.w || h != gRt.h) rtAllocTarget(w, h);
}

static void rtDestroy()
{
    if (gRt.tex) glDeleteTextures(1, &gRt.tex);
    if (gRt.itemsTex) glDeleteTextures(1, &gRt.itemsTex);
    if (gRt.chunksTex) glDeleteTextures(1, &gRt.chunksTex);
    if (gRt.fbo) glDeleteFramebuffers(1, &gRt.fbo);
    if (gRt.prog) glDeleteProgram(gRt.prog);
    if (gRt.screenProg) glDeleteProgram(gRt.screenProg);
    if (gRt.blitProg) glDeleteProgram(gRt.blitProg);
    gRt = RtMonitor();
}

// world -> local rows of an affine model matrix (column-major m).
static void rtInverseRows(const Mat4& M, float out[12])
{
    const float* m = M.m;
    const float a = m[0], b = m[4], c = m[8];
    const float d = m[1], e = m[5], f = m[9];
    const float g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) det = 1e-12f;
    const float r = 1.0f / det;
    const float inv[3][3] = {
        { A * r, -(b * i - c * h) * r, (b * f - c * e) * r },
        { B * r, (a * i - c * g) * r, -(a * f - c * d) * r },
        { C * r, -(a * h - b * g) * r, (a * e - b * d) * r } };
    const float t[3] = { m[12], m[13], m[14] };
    for (int row = 0; row < 3; ++row)
    {
        out[row * 4 + 0] = inv[row][0];
        out[row * 4 + 1] = inv[row][1];
        out[row * 4 + 2] = inv[row][2];
        out[row * 4 + 3] = -(inv[row][0] * t[0] + inv[row][1] * t[1] + inv[row][2] * t[2]);
    }
}

// Inspection light presets (world space).  Key PageDown cycles them; the numpad
// moves the light continuously.
static const Vec3 kRtLights[5] = {
    Vec3(0.0f, 0.30f, -2.0f),    // 0: work lamp on the floor under the lift
    Vec3(3.2f, 0.60f, -5.0f),    // 1: floor lamp, front right
    Vec3(-2.6f, 0.60f, -6.0f),   // 2: floor lamp, robot side
    Vec3(3.4f, 2.60f, -2.0f),    // 3: side lamp at car height
    Vec3(0.0f, 0.60f, 3.0f) };   // 4: floor lamp behind the ramp
static const char* kRtPresetNames[4] = {
    "Rear underbody", "Nose underbody", "Opposite-side overview", "LIVE robot-side diagnostic" };

static void rtCamera(Vec3& pos, Vec3& fwd, Vec3& right, Vec3& up)
{
    const float topY = Scene::liftTopY(gLiftHeight);
    Vec3 T(0.0f, 0.5f * topY + 0.45f, -2.0f);
    static const Vec3 basePos[4] = {
        Vec3(0.35f, 0.55f, 6.8f),     // 0 behind the car, low, looking along the underbody
        Vec3(0.35f, 0.55f, -9.0f),    // 1 in front of the nose
        Vec3(5.4f, 0.75f, -3.6f),     // 2 opposite side, useful for comparison
        Vec3(-5.75f, 0.78f, -5.10f) };// 3 robot side, front-left of the work area
    if ((gRtPreset & 3) == 3)
        T = Vec3(-0.72f, 0.5f * topY + 0.52f, -2.10f);

    Vec3 p = basePos[gRtPreset & 3];
    const float yaw = radians(gRtYaw);
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const Vec3 rel(p.x - T.x, 0.0f, p.z - T.z);
    p = Vec3(T.x + rel.x * cs + rel.z * sn, p.y, T.z - rel.x * sn + rel.z * cs);
    pos = p;
    fwd = normalize(T - p);
    right = normalize(cross(fwd, Vec3(0.0f, 1.0f, 0.0f)));
    up = cross(right, fwd);
}

static void rtRender()
{
    if (!gRt.ok || !gRtEnabled) return;
    // The small monitor refreshes every second frame (it is a monitor, not the main view);
    // the enlarged view and offline captures trace every frame.
    static unsigned frameTick = 0;
    ++frameTick;
    if (!gRtLarge && gCapturePath.empty() && !gDemoActive && (frameTick & 1u)) return;
    const double t0 = glfwGetTime();

    // ---- 1. record the live scene -----------------------------------------
    std::vector<Scene::RtItem> items;
    items.reserve(6000);
    Scene::recordInspectionGeometry(items, gLiftHeight, gRobotBaseYaw, gJoint1Angle, gJoint2Angle,
                                    gWristPitch, gWristRoll, gRobotTool, gServiceVisual);
    const int maxItems = kRtItemTexW * kRtItemTexH / 4;
    if (static_cast<int>(items.size()) > maxItems) items.resize(maxItems);
    // Order the items along a Morton (Z-order) curve of their centres so that each
    // chunk of 24 consecutive items is spatially compact and its bounding sphere tight.
    {
        const int cnt = static_cast<int>(items.size());
        std::vector<Vec3> cen(cnt);
        Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for (int i = 0; i < cnt; ++i)
        {
            const Scene::RtItem& it = items[i];
            cen[i] = (it.type == 3) ? (it.a + it.b + it.c) * (1.0f / 3.0f) : Vec3(it.m.m[12], it.m.m[13], it.m.m[14]);
            lo = Vec3(std::fmin(lo.x, cen[i].x), std::fmin(lo.y, cen[i].y), std::fmin(lo.z, cen[i].z));
            hi = Vec3(std::fmax(hi.x, cen[i].x), std::fmax(hi.y, cen[i].y), std::fmax(hi.z, cen[i].z));
        }
        auto spread = [](unsigned v) { v &= 0x3ff; v = (v | (v << 16)) & 0x030000FF; v = (v | (v << 8)) & 0x0300F00F; v = (v | (v << 4)) & 0x030C30C3; v = (v | (v << 2)) & 0x09249249; return v; };
        std::vector<std::pair<unsigned, int>> keyed(cnt);
        for (int i = 0; i < cnt; ++i)
        {
            const float ex = std::fmax(hi.x - lo.x, 1e-3f), ey = std::fmax(hi.y - lo.y, 1e-3f), ez = std::fmax(hi.z - lo.z, 1e-3f);
            const unsigned qx = static_cast<unsigned>((cen[i].x - lo.x) / ex * 1023.0f);
            const unsigned qy = static_cast<unsigned>((cen[i].y - lo.y) / ey * 1023.0f);
            const unsigned qz = static_cast<unsigned>((cen[i].z - lo.z) / ez * 1023.0f);
            keyed[i] = std::make_pair(spread(qx) | (spread(qy) << 1) | (spread(qz) << 2), i);
        }
        std::sort(keyed.begin(), keyed.end());
        std::vector<Scene::RtItem> sorted;
        sorted.reserve(cnt);
        for (int i = 0; i < cnt; ++i) sorted.push_back(items[keyed[i].second]);
        items.swap(sorted);
    }
    const int n = static_cast<int>(items.size());

    // ---- 2. pack into textures --------------------------------------------
    std::vector<float> itemData(static_cast<size_t>(kRtItemTexW) * kRtItemTexH * 4, 0.0f);
    std::vector<float> chunkData(static_cast<size_t>(kRtChunkTexW) * kRtChunkTexH * 4, 0.0f);
    std::vector<Vec3> ic(n);
    std::vector<float> ir(n);
    int tris = 0;
    for (int i = 0; i < n; ++i)
    {
        const Scene::RtItem& it = items[i];
        float* d = &itemData[static_cast<size_t>(i) * 16];
        if (it.type == 3)
        {
            d[0] = it.a.x; d[1] = it.a.y; d[2] = it.a.z; d[3] = 0.0f;
            d[4] = it.b.x; d[5] = it.b.y; d[6] = it.b.z; d[7] = 0.0f;
            d[8] = it.c.x; d[9] = it.c.y; d[10] = it.c.z; d[11] = 0.0f;
            ic[i] = (it.a + it.b + it.c) * (1.0f / 3.0f);
            ir[i] = std::fmax(length(it.a - ic[i]), std::fmax(length(it.b - ic[i]), length(it.c - ic[i])));
            ++tris;
        }
        else
        {
            rtInverseRows(it.m, d);
            const float* m = it.m.m;
            ic[i] = Vec3(m[12], m[13], m[14]);
            const float sx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
            const float sy = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
            const float sz = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
            ir[i] = 0.5f * std::sqrt(sx + sy + sz);
        }
        d[12] = it.color.x; d[13] = it.color.y; d[14] = it.color.z;
        d[15] = static_cast<float>(it.type) + (it.emissive ? 8.0f : 0.0f);
    }
    int chunks = 0;
    for (int c0 = 0; c0 < n && chunks < kRtChunkTexW * kRtChunkTexH / 2; c0 += kRtChunkSize, ++chunks)
    {
        const int c1 = (c0 + kRtChunkSize < n) ? c0 + kRtChunkSize : n;
        Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for (int i = c0; i < c1; ++i)
        {
            lo = Vec3(std::fmin(lo.x, ic[i].x - ir[i]), std::fmin(lo.y, ic[i].y - ir[i]), std::fmin(lo.z, ic[i].z - ir[i]));
            hi = Vec3(std::fmax(hi.x, ic[i].x + ir[i]), std::fmax(hi.y, ic[i].y + ir[i]), std::fmax(hi.z, ic[i].z + ir[i]));
        }
        const Vec3 ctr = (lo + hi) * 0.5f;
        float rad = 0.0f;
        for (int i = c0; i < c1; ++i) rad = std::fmax(rad, length(ic[i] - ctr) + ir[i]);
        float* d = &chunkData[static_cast<size_t>(chunks) * 8];
        d[0] = ctr.x; d[1] = ctr.y; d[2] = ctr.z; d[3] = rad + 0.01f;
        d[4] = static_cast<float>(c0); d[5] = static_cast<float>(c1 - c0); d[6] = 0.0f; d[7] = 0.0f;
    }
    gRt.itemCount = n;
    gRt.triCount = tris;
    gRt.chunkCount = chunks;

    // ---- save state --------------------------------------------------------
    GLint prevFbo = 0, prevProg = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean wasBlend = glIsEnabled(GL_BLEND);
    const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRt.itemsTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kRtItemTexW, std::min(kRtItemTexH, (n * 4 + kRtItemTexW - 1) / kRtItemTexW + 1), GL_RGBA, GL_FLOAT, itemData.data());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gRt.chunksTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kRtChunkTexW, std::min(kRtChunkTexH, (chunks * 2 + kRtChunkTexW - 1) / kRtChunkTexW + 1), GL_RGBA, GL_FLOAT, chunkData.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRt.itemsTex);

    // ---- 3. one ray (plus a shadow ray) per pixel ---------------------------
    Vec3 cp, cf, cr, cu;
    rtCamera(cp, cf, cr, cu);
    const Vec3 light = gRtLightCustom ? gRtLight : kRtLights[gRtLightIdx % 5];
    glBindFramebuffer(GL_FRAMEBUFFER, gRt.fbo);
    glViewport(0, 0, gRt.w, gRt.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glUseProgram(gRt.prog);
    glUniform1i(glGetUniformLocation(gRt.prog, "uItems"), 0);
    glUniform1i(glGetUniformLocation(gRt.prog, "uChunks"), 1);
    glUniform1i(glGetUniformLocation(gRt.prog, "uChunkCount"), chunks);
    glUniform3f(glGetUniformLocation(gRt.prog, "uCamPos"), cp.x, cp.y, cp.z);
    glUniform3f(glGetUniformLocation(gRt.prog, "uCamFwd"), cf.x, cf.y, cf.z);
    glUniform3f(glGetUniformLocation(gRt.prog, "uCamRight"), cr.x, cr.y, cr.z);
    glUniform3f(glGetUniformLocation(gRt.prog, "uCamUp"), cu.x, cu.y, cu.z);
    glUniform1f(glGetUniformLocation(gRt.prog, "uTanHalfFov"), std::tan(radians(32.0f)));
    glUniform1f(glGetUniformLocation(gRt.prog, "uAspect"), static_cast<float>(gRt.w) / static_cast<float>(gRt.h));
    glUniform3f(glGetUniformLocation(gRt.prog, "uLightPos"), light.x, light.y, light.z);
    glUniform1f(glGetUniformLocation(gRt.prog, "uLightPower"), 4.6f);
    glUniform1i(glGetUniformLocation(gRt.prog, "uShadows"), gRtShadows ? 1 : 0);
    drawFullscreenQuad();

    // ---- restore -------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glUseProgram(static_cast<GLuint>(prevProg));
    if (wasDepth) glEnable(GL_DEPTH_TEST);
    if (wasBlend) glEnable(GL_BLEND);
    if (wasCull) glEnable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!gCapturePath.empty()) glFinish();   // capture runs: include the GPU time in the measurement
    gRt.traceMs = static_cast<float>((glfwGetTime() - t0) * 1000.0);
}

// The monitor's screen in the garage: a quad that shows the traced texture.
static void rtDrawScreen(const Mat4& view, const Mat4& projection)
{
    if (!gRt.ok || !gRtEnabled || gTeachObject != TEACH_SCENE) return;
    glUseProgram(gRt.screenProg);
    const Mat4 model = Scene::inspectionMonitorMatrix();
    glUniformMatrix4fv(glGetUniformLocation(gRt.screenProg, "uModel"), 1, GL_FALSE, model.m);
    glUniformMatrix4fv(glGetUniformLocation(gRt.screenProg, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(gRt.screenProg, "uProjection"), 1, GL_FALSE, projection.m);
    glUniform1f(glGetUniformLocation(gRt.screenProg, "uGain"), 1.05f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRt.tex);
    glUniform1i(glGetUniformLocation(gRt.screenProg, "uTex"), 0);
    Primitives::drawCubeMesh();
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Enlarged view (key 9): the same texture, drawn over the finished frame.
static void rtDrawOverlay()
{
    if (!gRt.ok || !gRtEnabled || !gRtLarge) return;
    int ow, oh;
    if (gRtFull) { ow = gWidth; oh = gHeight; }
    else
    {
        ow = static_cast<int>(gWidth * 0.84f);
        oh = static_cast<int>(ow / 1.6f);
        if (oh > static_cast<int>(gHeight * 0.86f)) { oh = static_cast<int>(gHeight * 0.86f); ow = static_cast<int>(oh * 1.6f); }
    }
    glDisable(GL_DEPTH_TEST);
    glViewport((gWidth - ow) / 2, (gHeight - oh) / 2, ow, oh);
    glUseProgram(gRt.blitProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRt.tex);
    glUniform1i(glGetUniformLocation(gRt.blitProg, "uTex"), 0);
    drawFullscreenQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
    glViewport(0, 0, gWidth, gHeight);
    glEnable(GL_DEPTH_TEST);
}

static void advanceSimulation(float dt)
{
    updateServiceAutomation(dt * gServiceSpeed);
    updateCarDrive(dt);
    Scene::setSwitches(gSwitches);
    gDoorOffset = moveToward(gDoorOffset, gDoorTarget, kDoorSpeed * dt);
    const float lc = clampf(moveToward(gLiftHeight, gLiftTarget, kLiftSpeed * dt), 0.0f, 3.80f);
    if (liftPositionSafe(lc)) gLiftHeight = lc;
}

static void parseCommandLine(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&](float& dst) { if (i + 1 < argc) dst = static_cast<float>(atof(argv[++i])); };

        if (a == "--capture" && i + 1 < argc)      gCapturePath = argv[++i];
        else if (a == "--frames" && i + 1 < argc)  gCaptureFrames = atoi(argv[++i]);
        else if (a == "--polish")                  next(gCapturePolish);
        else if (a == "--lift")                    next(gCaptureLift);
        else if (a == "--door")                    next(gCaptureDoor);
        else if (a == "--cardrive")                next(gCaptureCarDrive);
        else if (a == "--preset" && i + 1 < argc)
        {
            int preset = atoi(argv[++i]);
            if (preset < 0) preset = 0;
            if (preset >= LIGHT_PRESET_COUNT) preset = LIGHT_PRESET_COUNT - 1;
            gLightingPreset = static_cast<LightingPreset>(preset);
        }
        else if (a == "--size" && i + 2 < argc)
        {
            gWidth = atoi(argv[++i]);
            gHeight = atoi(argv[++i]);
        }
        else if (a == "--cam" && i + 5 < argc)
        {
            for (int k = 0; k < 5; ++k) gCaptureCam[k] = static_cast<float>(atof(argv[++i]));
            gCaptureHasCam = true;
        }
        else if (a == "--showcase") gShowcase = true;
        else if (a == "--ortho")    gOrthographic = true;
        // Capture-only switches, so the figures in the report and the slides
        // are rendered by the project itself rather than mocked up.
        else if (a == "--gouraud")  gShadingMode = 1;
        else if (a == "--phong")    gShadingMode = 2;
        else if (a == "--notex")    gCaptureNoTextures = true;
        else if (a == "--dirt")     next(gCaptureDirt);
        else if (a == "--caryaw")   next(gCaptureCarYaw);
        else if (a == "--reel" && i + 1 < argc)
        {
            gReelDir = argv[++i];
            gReelActive = true;
        }
        else if (a == "--reelframes" && i + 1 < argc) gReelTotal = atoi(argv[++i]);
        else if (a == "--reelstart" && i + 1 < argc)  gReelStart = atoi(argv[++i]);
        else if (a == "--autostart") gAutoStartService = true;
        else if (a == "--tool" && i + 1 < argc) gRobotTool = atoi(argv[++i]);
        else if (a == "--robot" && i + 5 < argc)
        {
            // Park the arm at an exact joint pose.  Used to photograph a single
            // service pose without stepping through the whole sequence.
            gRobotBaseYaw = static_cast<float>(atof(argv[++i]));
            gJoint1Angle  = static_cast<float>(atof(argv[++i]));
            gJoint2Angle  = static_cast<float>(atof(argv[++i]));
            gWristPitch   = static_cast<float>(atof(argv[++i]));
            gWristRoll    = static_cast<float>(atof(argv[++i]));
            gCaptureRobot = true;
        }
        else if (a == "--orbit" && i + 1 < argc)
        {
            // Freeze the showcase orbit at an exact angle.  This is how the
            // repeatable stills for the report are taken.
            gShowcase = true;
            gShowcaseAngle = static_cast<float>(atof(argv[++i]));
            gShowcaseSpeed = 0.0f;
        }
        else if (a == "--radius") next(gShowcaseRadius);
        else if (a == "--height") next(gShowcaseHeight);
        else if (a == "--fov")    next(gCaptureFov);
        else if (a == "--noshadow") gShadowsEnabled = false;
        else if (a == "--nobump")   gNormalMapsEnabled = false;
        else if (a == "--norefl")   gReflectionsEnabled = false;
        else if (a == "--nobloom")  gBloomEnabled = false;
        else if (a == "--rtfull")  { gRtLarge = true; gRtFull = true; }
        else if (a == "--rtlarge") gRtLarge = true;
        else if (a == "--rtpreset" && i + 1 < argc) gRtPreset = atoi(argv[++i]) & 3;
        else if (a == "--rtshadow" && i + 1 < argc) gRtShadows = atoi(argv[++i]) != 0;
        else if (a == "--rtyaw")   next(gRtYaw);
        else if (a == "--rtlightidx" && i + 1 < argc) gRtLightIdx = atoi(argv[++i]) % 5;
        else if (a == "--rtlight" && i + 3 < argc)
        {
            gRtLight = Vec3(static_cast<float>(atof(argv[i + 1])), static_cast<float>(atof(argv[i + 2])), static_cast<float>(atof(argv[i + 3])));
            i += 3;
            gRtLightCustom = true;
        }
        else if (a == "--night")  { gNight = true; }
        else if (a == "--nolights") gSwitches.ceilingLights = false;
        else if (a == "--headlightsoff") gSwitches.vehicleLights = false;
        else if (a == "--wire")     gCaptureWire = true;
        else if (a == "--nohighlight") gTeachNoHighlight = true;
        else if (a == "--clear" && i + 3 < argc)
        {
            for (int k = 0; k < 3; ++k) gClearColor[k] = static_cast<float>(atof(argv[++i]));
            gClearOverride = true;
        }
        else if (a == "--vignette") next(gVignette);
        else if (a == "--grain")    next(gGrain);
        else if (a == "--teach" && i + 1 < argc) gCaptureTeach = atoi(argv[++i]);
        else if (a == "--stage" && i + 1 < argc) gCaptureStage = atoi(argv[++i]);
        else if (a == "--simtime")  next(gCaptureSimTime);
        else if (a == "--ibl")      next(gIblIntensity);
        else if (a == "--demo" && i + 1 < argc)      { gDemoScript = argv[++i]; gDemoActive = true; }
        else if (a == "--pipe" && i + 1 < argc)      gDemoPipeFile = argv[++i];
        else if (a == "--fps")                       next(gDemoFps);
        else if (a == "--demostart" && i + 1 < argc) gDemoStart = atoi(argv[++i]);
        else if (a == "--demoend" && i + 1 < argc)   gDemoEnd = atoi(argv[++i]);
        else if (a == "--rtscale" && i + 1 < argc)   gRtScale = std::max(1, atoi(argv[++i]));
        else if (a == "--exposure") next(gExposure);
    }
}

int main(int argc, char** argv)
{
    parseCommandLine(argc, argv);

    if (!glfwInit())
    {
        std::cout << "Failed to initialise GLFW." << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, gDemoActive ? 0 : 4);   // 4x multisampling - smoother cube edges
    if (gDemoActive && !loadDemoScript(gDemoScript)) return -1;

    // While recording, the window is only a small preview; frames are drawn
    // into an off-screen target at the full recording size.
    GLFWwindow* window = gDemoActive
        ? glfwCreateWindow(gPreviewW, gPreviewH, kTitle, nullptr, nullptr)
        : glfwCreateWindow(gWidth, gHeight, kTitle, nullptr, nullptr);
    if (window == nullptr)
    {
        std::cout << "Failed to create the GLFW window." << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval((gReelActive || gDemoActive) ? 0 : 1);   // v-sync, off while recording

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    // A recording runs to its last frame: closing the preview window must not cut it short.
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { if (gDemoActive) glfwSetWindowShouldClose(w, GLFW_FALSE); });
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cout << "Failed to initialise GLAD." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

    glViewport(0, 0, gWidth, gHeight);

    glEnable(GL_DEPTH_TEST);

    glEnable(GL_MULTISAMPLE);

    unsigned int shaderProgram = createSceneShaderProgram();
    unsigned int depthProgram  = createDepthShaderProgram();
    unsigned int blurProgram   = createBlurShaderProgram();
    unsigned int postProgram   = createPostShaderProgram();
    if (shaderProgram == 0 || depthProgram == 0 || blurProgram == 0 || postProgram == 0)
    {
        std::cout << "One or more shader programs failed - aborting." << std::endl;
        if (shaderProgram) glDeleteProgram(shaderProgram);
        if (depthProgram) glDeleteProgram(depthProgram);
        if (blurProgram) glDeleteProgram(blurProgram);
        if (postProgram) glDeleteProgram(postProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    ShadowTarget shadow;
    HDRTargets hdr;
    gSpotShadow.size = 1024;
    if (!createShadowTarget(gSpotShadow))
        std::cout << "[Render] Spot shadow map unavailable - the inspection lamp "
                     "will not be occluded.\n";

    if (!createShadowTarget(shadow))
    {
        std::cout << "Shadow framebuffer creation failed - aborting." << std::endl;
        glDeleteProgram(shaderProgram); glDeleteProgram(depthProgram);
        glDeleteProgram(blurProgram); glDeleteProgram(postProgram);
        glfwDestroyWindow(window); glfwTerminate(); return -1;
    }
    if (!ensureHDRTargets(hdr, gWidth, gHeight))
    {
        std::cout << "HDR framebuffer creation failed - aborting." << std::endl;
        glDeleteProgram(shaderProgram); glDeleteProgram(depthProgram);
        glDeleteProgram(blurProgram); glDeleteProgram(postProgram);
        glfwDestroyWindow(window); glfwTerminate(); return -1;
    }

    unsigned int environmentCubemap = createEnvironmentCubemap();
    gMaterialTextures = createMaterialTextureArray();
    initFullscreenQuad();
    Primitives::init();
    Scene::init(shaderProgram);
    if (!rtInit())
        std::cout << "[RayTrace] inspection monitor unavailable.\n";
    else
        rtApplySize();

    resetCamera();
    if (gCaptureTeach >= 1 && gCaptureTeach <= 8)
    {
        setTeachingMode(static_cast<TeachingObject>(gCaptureTeach - 1));
        if (gCaptureStage >= 0 && gCaptureStage < teachingStageCount())
            gTeachStage = gCaptureStage;
    }
    if (gCaptureWire)
    {
        gWireframe = true;
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    if (gCaptureHasCam)
    {
        gCamera.position = Vec3(gCaptureCam[0], gCaptureCam[1], gCaptureCam[2]);
        gCamera.yaw = gCaptureCam[3];
        gCamera.pitch = gCaptureCam[4];
    }
    if (gCapturePolish >= 0.0f)
    {
        gServiceVisual.polishAmount = gCapturePolish;
        gServiceVisual.dirtAmount = 0.0f;
    }
    if (gCaptureDirt >= 0.0f) gServiceVisual.dirtAmount = gCaptureDirt;
    gServiceVisual.carYaw = gCaptureCarYaw;
    if (gCaptureLift >= 0.0f) { gLiftHeight = gCaptureLift; gLiftTarget = gCaptureLift; }
    if (gCaptureDoor >= 0.0f) { gDoorOffset = gCaptureDoor; gDoorTarget = gCaptureDoor; }
    if (gCaptureCarDrive >= 0.0f) { gSwitches.carDrive = gCaptureCarDrive; gCarDriveTarget = gCaptureCarDrive; }
    if (gCaptureNoTextures) { gTexturesEnabled = false; Scene::setTexturesEnabled(false); }
    if (gCaptureFov > 0.0f) gCamera.fov = gCaptureFov;
    if (gCaptureRobot)
    {
        // resetServiceDemo() would snap the arm back to HOME, so the explicit
        // pose is re-applied here, after all the other start-up defaults.
        gServiceVisual.lugProgress[0] = 0.0f;
    }
    printControls();

    if (gAutoStartService) startServiceDemo();

    if (gCaptureSimTime >= 0.0f)
    {
        startServiceDemo();
        ServiceState last = gServiceState;
        const int steps = static_cast<int>(gCaptureSimTime * 24.0f);
        for (int i = 0; i < steps; ++i)
        {
            advanceSimulation(1.0f / 24.0f);
            if (gServiceState != last)
            {
                std::printf("[SimTime] t=%.2f s -> state %d\n",
                            static_cast<float>(i + 1) / 24.0f, static_cast<int>(gServiceState));
                last = gServiceState;
            }
        }
        gCaptureFreeze = true;   // hold this exact state while the frame is captured
    }

    int captureCountdown = gCapturePath.empty() ? -1 : gCaptureFrames;
    gInputLocked = !gCapturePath.empty() || gDemoActive;
    int demoWritten = 0;
    const int demoFrames = gDemoActive ? static_cast<int>(std::lround(gDemoLength * gDemoFps)) : 0;
    if (gDemoActive)
    {
        if (gDemoEnd < 0 || gDemoEnd > demoFrames) gDemoEnd = demoFrames;
        if (!demoCreateOutput() || !demoOpenPipe()) { glfwDestroyWindow(window); glfwTerminate(); return -1; }
        std::printf("[Demo] frames %d..%d of %d at %.0f fps, %d x %d\n", gDemoStart, gDemoEnd, demoFrames, gDemoFps, gWidth, gHeight);
    }
    const double demoClock0 = glfwGetTime();

    // ---- Render loop -------------------------------------------------------
    float lastFrame = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window))
    {
        float now = static_cast<float>(glfwGetTime());
        float deltaTime = now - lastFrame;
        lastFrame = now;
        if (gCaptureFreeze) deltaTime = 0.0f;

        if (gDemoActive)
        {
            if (gDemoFrame >= gDemoEnd) break;
            deltaTime = 1.0f / gDemoFps;
            const float prevTime = gDemoTime;
            gDemoTime = static_cast<float>(gDemoFrame + 1) / gDemoFps;
            now = gDemoTime;
            demoFireEvents(window, (gDemoFrame == 0) ? -1.0f : prevTime, gDemoTime);
        }

        if (gReelActive)
        {
            // Fixed timestep: the finished file plays at a known speed no
            // matter how long each frame actually took to produce.
            deltaTime = 1.0f / kReelFps;
            const float prevTime = gReelTime;
            gReelTime += deltaTime;
            now = gReelTime;
            reelFireEvents(prevTime, gReelTime);
            reelApplyCamera(gReelTime);

            if (gReelFrame < gReelStart)
            {
                advanceSimulation(deltaTime);
                ++gReelFrame;
                glfwPollEvents();
                continue;
            }
        }

        if (!gInputLocked)
        {
            processDiscreteInput(window);
            processMovement(window, deltaTime);
        }
        else if (gDemoActive)
        {
            processMovement(window, deltaTime);   // the script's held keys
            static float lastStop = -10.0f;
            if (gSafetyStop && gDemoTime - lastStop > 0.5f)
            {
                std::printf("[Demo] SAFETY STOP t=%.2f  yaw %.1f j1 %.1f j2 %.1f wrist %.1f lift %.2f\n", gDemoTime, gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gLiftHeight);
                lastStop = gDemoTime;
            }
        }
        updateServiceAutomation(deltaTime * gServiceSpeed);
        updateTeachingWindowTitle(window);

        // ---- SECTION I: interactive motion ---------------------------
        updateCarDrive(deltaTime);
        if (gSwitches.hazardBeacon)
            gServiceVisual.warningPulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(now * 7.5f));
        Scene::setSwitches(gSwitches);

        gDoorOffset = moveToward(gDoorOffset, gDoorTarget, kDoorSpeed * deltaTime);

        float liftCandidate = moveToward(gLiftHeight, gLiftTarget, kLiftSpeed * deltaTime);
        liftCandidate = clampf(liftCandidate, 0.0f, 3.80f);
        if (liftPositionSafe(liftCandidate))
            gLiftHeight = liftCandidate;
        else
        {
            gLiftTarget = gLiftHeight;
            gSafetyStop = true;
        }

        if (gDemoActive)
        {
            demoApplyCamera(gDemoTime);
            // Fast-forward to the first frame of this chunk: the simulation and
            // the script run, nothing is drawn or written.
            if (gDemoFrame < gDemoStart) { ++gDemoFrame; glfwPollEvents(); continue; }
        }

        if (!ensureHDRTargets(hdr, gWidth, gHeight))
        {
            glfwPollEvents();
            continue;
        }

        // Frame-time statistics for the render report (V key).
        gFrameMsAccum += deltaTime * 1000.0f;
        if (++gFrameMsCount >= 30)
        {
            gFrameMsAvg = gFrameMsAccum / static_cast<float>(gFrameMsCount);
            gFrameMsAccum = 0.0f;
            gFrameMsCount = 0;
        }

        const Vec3 subject(0.0f, Scene::liftTopY(gLiftHeight) + 0.95f, -2.00f);
        updateShowcaseCamera(deltaTime, subject);

        // Keep the orthographic box the same apparent size as the frustum is
        // at the subject, so toggling the projection does not also zoom.
        {
            const Vec3 toSubject = subject - gCamera.position;
            const float dist = length(toSubject);
            gOrthoHalfHeight = dist * std::tan(radians(gCamera.fov * 0.5f));
            if (gOrthoHalfHeight < 0.5f) gOrthoHalfHeight = 0.5f;
        }

        float aspect = (gHeight > 0)
            ? static_cast<float>(gWidth) / static_cast<float>(gHeight)
            : 1.0f;
        Mat4 view = gCamera.viewMatrix();

        Mat4 projection;
        if (gOrthographic)
        {
            const float halfH = gOrthoHalfHeight;
            const float halfW = halfH * aspect;
            // Same near/far as the frustum.  A negative near would reach back
            // past the eye and drag in the wall standing behind the camera.
            projection = orthographic(-halfW, halfW, -halfH, halfH, 0.05f, 200.0f);
        }
        else
        {
            projection = perspective(gCamera.fov, aspect, 0.05f, 200.0f);
        }
        Vec3 dirLight;
        Mat4 lightSpace = buildLightSpaceMatrix(dirLight);

        // The inspection lamp's own view-projection, rebuilt every frame
        // because the lamp is bolted to a moving wrist.
        Vec3 sShoulder, sElbow, sWrist, sToolEnd;
        robotSafetyPoints(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch,
            sShoulder, sElbow, sWrist, sToolEnd, currentToolReach());
        const Vec3 spotDirection = normalize(sToolEnd - sWrist);
        const Mat4 spotSpace = buildSpotLightMatrix(sToolEnd, spotDirection, 23.0f);

        // Ray-traced inspection monitor (own pass; restores GL state).
        rtRender();

        if (gShadowsEnabled)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe must not punch holes in shadows
            glViewport(0, 0, shadow.size, shadow.size);
            glBindFramebuffer(GL_FRAMEBUFFER, shadow.fbo);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUseProgram(depthProgram);
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "uLightSpaceMatrix"),
                1, GL_FALSE, lightSpace.m);
            Scene::init(depthProgram);
            drawCurrentContent();

            // Second depth pass, from the robot's inspection lamp.
            glViewport(0, 0, gSpotShadow.size, gSpotShadow.size);
            glBindFramebuffer(GL_FRAMEBUFFER, gSpotShadow.fbo);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(glGetUniformLocation(depthProgram, "uLightSpaceMatrix"),
                1, GL_FALSE, spotSpace.m);
            drawCurrentContent();
        }

        const bool useMsaa = gMsaaEnabled && hdr.msReady;
        glBindFramebuffer(GL_FRAMEBUFFER, useMsaa ? hdr.msFbo : hdr.fbo);
        glViewport(0, 0, gWidth, gHeight);
        const float* sky = gClearOverride ? gClearColor : (gNight ? kSkyNight : kSkyDay);
        glClearColor(sky[0], sky[1], sky[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
        glUseProgram(shaderProgram);
        Scene::init(shaderProgram);
        uploadSceneUniforms(shaderProgram, view, projection, lightSpace, dirLight,
            shadow.depth, environmentCubemap, subject);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uSpotLightSpaceMatrix"),
            1, GL_FALSE, spotSpace.m);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, gSpotShadow.depth);
        glUniform1i(glGetUniformLocation(shaderProgram, "uSpotShadowMap"), 7);
        glActiveTexture(GL_TEXTURE0);
        Primitives::resetDrawCallCounter();
        drawCurrentContent();
        rtDrawScreen(view, projection);

        // Resolve the multisampled scene into the textures the post chain reads.
        if (useMsaa) resolveMultisample(hdr);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_DEPTH_TEST);
        unsigned int bloomTexture = 0;
        if (gBloomEnabled)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, hdr.fbo);
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr.pingFbo[0]);
            glBlitFramebuffer(0, 0, hdr.width, hdr.height,
                0, 0, hdr.bloomWidth, hdr.bloomHeight,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

            glUseProgram(blurProgram);
            glUniform1i(glGetUniformLocation(blurProgram, "uImage"), 0);
            glUniform1f(glGetUniformLocation(blurProgram, "uRadiusScale"), 1.35f);

            bool horizontal = true;
            const int passes = 6;
            for (int i = 0; i < passes; ++i)
            {
                const int target = horizontal ? 1 : 0;
                const int source = horizontal ? 0 : 1;
                glBindFramebuffer(GL_FRAMEBUFFER, hdr.pingFbo[target]);
                glViewport(0, 0, hdr.bloomWidth, hdr.bloomHeight);
                glUniform1i(glGetUniformLocation(blurProgram, "uHorizontal"), horizontal ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, hdr.pingColor[source]);
                drawFullscreenQuad();
                bloomTexture = hdr.pingColor[target];
                horizontal = !horizontal;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, gOutFbo);
        glViewport(0, 0, gWidth, gHeight);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(postProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr.color[0]);
        glUniform1i(glGetUniformLocation(postProgram, "uScene"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTexture);
        glUniform1i(glGetUniformLocation(postProgram, "uBloom"), 1);
        glUniform1i(glGetUniformLocation(postProgram, "uBloomEnabled"), gBloomEnabled ? 1 : 0);
        glUniform1f(glGetUniformLocation(postProgram, "uBloomStrength"), gBloomStrength);
        glUniform1f(glGetUniformLocation(postProgram, "uExposure"),
            gExposure * activeRigExposure(subject) / 0.74f);
        glUniform2f(glGetUniformLocation(postProgram, "uInvResolution"),
            1.0f / static_cast<float>(gWidth), 1.0f / static_cast<float>(gHeight));
        glUniform1f(glGetUniformLocation(postProgram, "uTime"), now);
        // The software edge filter is only needed when hardware MSAA is off.
        glUniform1i(glGetUniformLocation(postProgram, "uEdgeAA"), useMsaa ? 0 : 1);
        glUniform1f(glGetUniformLocation(postProgram, "uVignette"),
            gVignette * (gShowcase ? 1.35f : 1.0f));
        glUniform1f(glGetUniformLocation(postProgram, "uGrain"), gGrain);
        drawFullscreenQuad();
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
        rtDrawOverlay();

        if (captureCountdown > 0 && --captureCountdown == 0)
        {
            { int cl = 0, cc = 0, cr = 0; Scene::lastRecordCounts(cl, cc, cr);
              std::printf("[RayTrace] items %d (triangles %d; lift %d, car+wheels %d, robot %d), chunks %d, trace %.2f ms (GPU-synchronised)\n", gRt.itemCount, gRt.triCount, cl, cc, cr, gRt.chunkCount, gRt.traceMs); }
            saveScreenshotBMP(gCapturePath.c_str(), gWidth, gHeight);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (gDemoActive)
        {
            demoWriteFrame(demoWritten++);
            ++gDemoFrame;
            if (demoWritten % 60 == 0)
            {
                const double el = glfwGetTime() - demoClock0;
                std::printf("[Demo] frame %d / %d  t=%.2f s  %.1f ms/frame  state %d\n", gDemoFrame, gDemoEnd, gDemoTime,
                            1000.0 * el / demoWritten, static_cast<int>(gServiceState));
                std::fflush(stdout);
            }
        }

        if (gReelActive)
        {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/f%05d.bmp", gReelDir.c_str(), gReelFrame);
            saveScreenshotBMP(path, gWidth, gHeight);
            if (++gReelFrame >= gReelTotal) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (gDemoActive)
    {
        demoClosePipe(demoWritten);
        std::printf("[Demo] wrote %d frames in %.1f s\n", demoWritten, glfwGetTime() - demoClock0);
    }
    rtDestroy();
    Primitives::destroy();
    destroyHDRTargets(hdr);
    if (gSpotShadow.depth) glDeleteTextures(1, &gSpotShadow.depth);
    if (gSpotShadow.fbo) glDeleteFramebuffers(1, &gSpotShadow.fbo);
    if (shadow.depth) glDeleteTextures(1, &shadow.depth);
    if (shadow.fbo) glDeleteFramebuffers(1, &shadow.fbo);
    if (environmentCubemap) glDeleteTextures(1, &environmentCubemap);
    if (gMaterialTextures) glDeleteTextures(1, &gMaterialTextures);
    if (gQuadVBO) glDeleteBuffers(1, &gQuadVBO);
    if (gQuadVAO) glDeleteVertexArrays(1, &gQuadVAO);
    glDeleteProgram(shaderProgram);
    glDeleteProgram(depthProgram);
    glDeleteProgram(blurProgram);
    glDeleteProgram(postProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
