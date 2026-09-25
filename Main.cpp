#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <vector>

#include "Math3D.h"
#include "Camera.h"
#include "Shader.h"
#include "Primitives.h"
#include "SceneObjects.h"


static int   gWidth = 1280;
static int   gHeight = 720;
static const char* kTitle = "Futuristic 3D Smart Automobile Service Garage - Object Construction Stage";


static Camera gCamera;
static bool   gMouseCaptured = true;
static bool   gFirstMouse = true;
static double gLastX = 0.0;
static double gLastY = 0.0;

//  Scene / display toggles
static bool  gWireframe = false;

// Rendering feature toggles.  Cook-Torrance PBR is the photoreal default; Gouraud remains
// available as an explicit coursework comparison mode.
static int   gShadingMode = 0; // 0 Cook-Torrance PBR, 1 legacy Gouraud
static bool  gTexturesEnabled = true;
static bool  gNormalMapsEnabled = true;
static bool  gShadowsEnabled = true;
static bool  gReflectionsEnabled = true;
static bool  gBloomEnabled = true;
static float gExposure = 0.74f;
static bool  gDoorOpen = true;
static bool  gLiftRaised = false;

enum TeachingObject {
    TEACH_SCENE = 0, TEACH_CAR, TEACH_LIFT, TEACH_ROBOT,
    TEACH_CUBE, TEACH_CYLINDER, TEACH_SMOOTH_CYLINDER, TEACH_SPHERE
};
static TeachingObject gTeachObject = TEACH_SCENE;
static int gTeachStage = 0;

static const int kCarLimits[]   = { 1, 2, 3, 4, 5, 6, 8, 18, 32, 188 };
static const char* kCarStages[] = {
    "broad stainless-steel faceted body shell",
    "inset windshield + two-pane tinted side glazing",
    "panoramic roof + sloping covered tonneau",
    "flush window seals / black trim",
    "sealed five-facet 3D wheel-arch fender flares",
    "full 3D protruding front + rear bumper blocks",
    "full-width front white + rear red light bars",
    "boxy rockers, door shut-lines + flush handle recesses",
    "front-door seams, micro-fold highlights + production side mirrors",
    "four rounded-shoulder all-terrain wheels with tread, aero covers + brakes"
};

static const int kLiftLimits[]   = { 14, 39, 64, 66, 76, 86 };
static const char* kLiftStages[] = {
    "base rails + anchors", "left scissor stack + hydraulic ram",
    "right scissor stack + hydraulic ram", "platform cross beams",
    "left runway", "right runway"
};

static const int kRobotLimits[]   = { 5, 7, 8, 9, 10, 14, 15, 17, 19, 28 };
static const char* kRobotStages[] = {
    "base plate + four bolts", "base column + status band", "turntable",
    "shoulder yoke", "joint 1 disc", "lower arm + bands + cable",
    "joint 2 disc", "upper arm", "wrist pitch/roll", "quick-change tool head"
};


//  ANIMATION PARAMETERS - constants for this stage, variables later.
static float gDoorOffset = 4.9f;    // 4.9 = fully open (the default now), 0.0 = closed
static float gDoorTarget = 4.9f;    // where gDoorOffset is currently easing towards
static float gLiftHeight = 0.0f;    // 0.0 = lowered, 4.0 = fully raised
static float gLiftTarget = 0.0f;    // where gLiftHeight is currently easing towards
static float gRobotBaseYaw = 60.0f;   // parked sideways; TOP round turntable only, square pedestal fixed
static float gJoint1Angle = -44.0f;   // shoulder pitch
static float gJoint2Angle = -30.0f;   // elbow pitch
static float gWristPitch = 0.0f;      // end-effector pitch
static float gWristRoll = 0.0f;       // end-effector roll
static int   gRobotTool = 0;           // 0 scanner, 1 driver, 2 gripper, 3 polisher
static float gFanAngle = 0.0f;
static bool  gSafetyStop = false;       // true for a frame when collision prevention blocks motion

static const float kDoorSpeed = 0.80f;   
static const float kLiftSpeed = 0.55f;     
static const float kArmSpeed = 35.0f;      
static const float kBaseYawSpeed = 45.0f;  
static const float kWristSpeed = 55.0f;   

static const float kJoint1Min = -110.0f, kJoint1Max = 55.0f;
static const float kJoint2Min = -150.0f, kJoint2Max = 120.0f;
static const float kBaseYawMin = -180.0f, kBaseYawMax = 180.0f;
static const float kWristPitchMin = -90.0f, kWristPitchMax = 90.0f;
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


struct SafetyAABB
{
    Vec3 mn;
    Vec3 mx;
};

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

static void robotSafetyPoints(float baseYaw, float joint1Deg, float joint2Deg, float wristPitchDeg,
    Vec3& shoulder, Vec3& elbow, Vec3& wrist, Vec3& toolEnd)
{
    const float shoulderY = 1.05f;
    const float L1 = 4.50f;
    const float L2 = 4.00f;
    const float toolReach = 0.95f;

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
    robotSafetyPoints(baseYaw, joint1, joint2, wristPitch, s, e, w, t);

    const Vec3 pts[4] = { s, e, w, t };
    for (int i = 0; i < 4; ++i)
    {
        if (pts[i].y < 0.18f || pts[i].y > 6.72f) return false;
        if (pts[i].x < -13.15f || pts[i].x > 13.15f) return false;
        if (pts[i].z < -12.20f || pts[i].z > 12.20f) return false;
    }

    const SafetyAABB pedestal = { Vec3(-5.47f, 0.0f, -3.07f), Vec3(-3.73f, 0.82f, -1.33f) };

    const SafetyAABB toolStation = { Vec3(-8.75f, 0.0f, -2.55f), Vec3(-5.95f, 2.12f, -1.15f) };

    float carY = Scene::liftTopY(liftHeight);
    const SafetyAABB centralLift = { Vec3(-1.62f, 0.0f, -4.72f), Vec3(1.62f, carY + 0.02f, 0.72f) };
    const SafetyAABB centralTruck = { Vec3(-1.30f, carY - 0.02f, -4.86f), Vec3(1.30f, carY + 1.90f, 0.96f) };

    const SafetyAABB rearLiftA = { Vec3(-10.25f, 0.0f, 0.45f), Vec3(-6.95f, 3.62f, 5.95f) };
    const SafetyAABB rearLiftB = { Vec3(-10.25f, 0.0f, -9.35f), Vec3(-6.95f, 1.35f, -3.85f) };

    if (segmentHitsAABB(s, e, pedestal, 0.20f) ||
        segmentHitsAABB(e, w, pedestal, 0.18f) ||
        segmentHitsAABB(w, t, pedestal, 0.28f)) return false;

    if (gTeachObject == TEACH_SCENE)
    {
        const SafetyAABB hardBoxes[] = { toolStation, rearLiftA, rearLiftB };
        for (int i = 0; i < static_cast<int>(sizeof(hardBoxes) / sizeof(hardBoxes[0])); ++i)
        {
            if (segmentHitsAABB(s, e, hardBoxes[i], 0.20f)) return false;
            if (segmentHitsAABB(e, w, hardBoxes[i], 0.18f)) return false;
            if (segmentHitsAABB(w, t, hardBoxes[i], 0.28f)) return false;
        }

        if (segmentHitsAABB(s, e, centralLift, 0.20f) ||
            segmentHitsAABB(e, w, centralLift, 0.18f) ||
            segmentHitsAABB(w, t, centralLift, 0.22f)) return false;

        if (segmentHitsAABB(s, e, centralTruck, 0.20f)) return false;
        if (!allowVehicleServiceContact)
        {
            if (segmentHitsAABB(e, w, centralTruck, 0.18f) ||
                segmentHitsAABB(w, t, centralTruck, 0.28f)) return false;
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
    gWidth = width;
    gHeight = height;
    glViewport(0, 0, width, height);
}


static void cursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos)
{
    if (!gMouseCaptured)
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
    gCamera.addZoom(static_cast<float>(yoffset));
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

    case GLFW_KEY_V:                    // geometry teaching view
        printTeachingInfo();
        break;

    case GLFW_KEY_O:                    // automatic door: nearest opposite endpoint
        gDoorOpen = !(gDoorOffset > 2.45f);
        gDoorTarget = gDoorOpen ? 4.9f : 0.0f;
        break;

    case GLFW_KEY_P:                    // automatic lift: nearest opposite endpoint
        gLiftRaised = !(gLiftHeight > 1.60f);
        gLiftTarget = gLiftRaised ? 3.20f : 0.0f;
        break;

    case GLFW_KEY_C:                    // stop door exactly where it is
        gDoorTarget = gDoorOffset;
        break;

    case GLFW_KEY_BACKSLASH:            // stop lift exactly where it is
        gLiftTarget = gLiftHeight;
        break;

    case GLFW_KEY_F1: gRobotTool = 0; std::cout << "Robot tool: scanner\n"; break;
    case GLFW_KEY_F2: gRobotTool = 1; std::cout << "Robot tool: impact driver\n"; break;
    case GLFW_KEY_F3: gRobotTool = 2; std::cout << "Robot tool: gripper\n"; break;
    case GLFW_KEY_F4: gRobotTool = 3; std::cout << "Robot tool: polisher\n"; break;
    case GLFW_KEY_F5:
        gShadingMode = 1 - gShadingMode;
        std::cout << "Shading: " << (gShadingMode == 0 ? "Cook-Torrance PBR (per-fragment)" : "Gouraud (per-vertex)") << "\n";
        break;
    case GLFW_KEY_F6:
        gTexturesEnabled = !gTexturesEnabled;
        std::cout << "Procedural material maps: " << (gTexturesEnabled ? "ON" : "OFF") << "\n";
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

    case GLFW_KEY_R:
        if (gTeachObject == TEACH_SCENE) resetCamera();
        else setTeachingMode(gTeachObject);
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
    static bool kp1=false, kp2=false, kp3=false, kp4=false, kp5=false, kp6=false, kp7=false, kp8=false;
    static bool n=false, b=false, r=false, tab=false;

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

    if (keyPressedOnce(window, GLFW_KEY_1, k1) || keyPressedOnce(window, GLFW_KEY_KP_1, kp1)) handleDiscreteKey(window, GLFW_KEY_1);
    if (keyPressedOnce(window, GLFW_KEY_2, k2) || keyPressedOnce(window, GLFW_KEY_KP_2, kp2)) handleDiscreteKey(window, GLFW_KEY_2);
    if (keyPressedOnce(window, GLFW_KEY_3, k3) || keyPressedOnce(window, GLFW_KEY_KP_3, kp3)) handleDiscreteKey(window, GLFW_KEY_3);
    if (keyPressedOnce(window, GLFW_KEY_4, k4) || keyPressedOnce(window, GLFW_KEY_KP_4, kp4)) handleDiscreteKey(window, GLFW_KEY_4);
    if (keyPressedOnce(window, GLFW_KEY_5, k5) || keyPressedOnce(window, GLFW_KEY_KP_5, kp5)) handleDiscreteKey(window, GLFW_KEY_5);
    if (keyPressedOnce(window, GLFW_KEY_6, k6) || keyPressedOnce(window, GLFW_KEY_KP_6, kp6)) handleDiscreteKey(window, GLFW_KEY_6);
    if (keyPressedOnce(window, GLFW_KEY_7, k7) || keyPressedOnce(window, GLFW_KEY_KP_7, kp7)) handleDiscreteKey(window, GLFW_KEY_7);
    if (keyPressedOnce(window, GLFW_KEY_8, k8) || keyPressedOnce(window, GLFW_KEY_KP_8, kp8)) handleDiscreteKey(window, GLFW_KEY_8);

    if (keyPressedOnce(window, GLFW_KEY_N, n))       handleDiscreteKey(window, GLFW_KEY_N);
    if (keyPressedOnce(window, GLFW_KEY_B, b))       handleDiscreteKey(window, GLFW_KEY_B);
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
        title += " | 2 car | 3 lift | 4 robot | 5/6/7/8 primitives";
    }
    title += " | ";
    title += (gShadingMode == 0 ? "PHONG" : "GOURAUD");
    title += gTexturesEnabled ? " | TEX" : " | noTEX";
    title += gShadowsEnabled ? " | SHADOW" : " | noSHADOW";
    title += gReflectionsEnabled ? " | REFL" : " | noREFL";
    if (gSafetyStop) title += " | SAFETY STOP: collision prevented";
    glfwSetWindowTitle(window, title.c_str());
}

static void processMovement(GLFWwindow* window, float dt)
{
    gSafetyStop = false;

    float speedScale = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 2.5f : 1.0f;
    float step = dt * speedScale;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) gCamera.moveForward(step);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) gCamera.moveBackward(step);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) gCamera.moveLeft(step);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) gCamera.moveRight(step);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) gCamera.moveUp(step);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) gCamera.moveDown(step);
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) gCamera.moveDown(step);
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) gCamera.moveUp(step);

    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
    {
        gDoorOffset = clampf(gDoorOffset - kDoorSpeed * dt, 0.0f, 4.9f);
        gDoorTarget = gDoorOffset;
    }
    if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
    {
        gDoorOffset = clampf(gDoorOffset + kDoorSpeed * dt, 0.0f, 4.9f);
        gDoorTarget = gDoorOffset;
    }

    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
    {
        float candidate = clampf(gLiftHeight - kLiftSpeed * dt, 0.0f, 3.80f);
        if (liftPositionSafe(candidate)) gLiftHeight = candidate;
        else gSafetyStop = true;
        gLiftTarget = gLiftHeight;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
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
            gRobotBaseYaw = yaw; gJoint1Angle = j1; gJoint2Angle = j2;
            gWristPitch = wp; gWristRoll = wr;
        }
        else
        {
            gSafetyStop = true;
        }
    };

    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS)
        tryPose(gRobotBaseYaw + baseStep, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS)
        tryPose(gRobotBaseYaw - baseStep, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle + armStep, gJoint2Angle, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle - armStep, gJoint2Angle, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle + armStep, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle - armStep, gWristPitch, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch + wristStep, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch - wristStep, gWristRoll);
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll + wristStep);
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        tryPose(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll - wristStep);

}


static void printControls()
{
    std::cout << "============================================================\n";
    std::cout << " Futuristic 3D Smart Automobile Service Garage\n";
    std::cout << " Stage 1 - 3D object construction from primitive geometry\n";
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
    std::cout << " 1              normal full scene (top row or numpad)\n";
    std::cout << " 2 / 3 / 4      isolate car / lift / robot construction (top row or numpad)\n";
    std::cout << " 5 / 6 / 7      isolate cube / 28-seg cylinder / 48-seg cylinder (top row or numpad)\n";
    std::cout << " N / B          next / previous construction stage\n";
    std::cout << " O              automatic garage-door open/close\n";
    std::cout << " Z / X / C      door close / open / STOP at current position\n";
    std::cout << " P              automatic lift lower/raise\n";
    std::cout << " [ / ] / \\      lift lower / raise / STOP at current height\n";
    std::cout << " T / G          rotate TOP robot turntable left / right (square pedestal fixed)\n";
    std::cout << " U / J          robot shoulder up / down\n";
    std::cout << " I / K          robot elbow up / down\n";
    std::cout << " Y / H          robot head tilt top/bottom (full 180 deg: -90..+90)\n";
    std::cout << " LEFT / RIGHT   robot wrist roll\n";
    std::cout << " F1/F2/F3/F4    scanner / impact driver / gripper / polisher\n";
    std::cout << " F5             toggle Cook-Torrance PBR / Gouraud shading\n";
    std::cout << " F6             procedural albedo/roughness/AO maps on/off\n";
    std::cout << " F7             normal/bump maps on/off\n";
    std::cout << " F8             directional PCF shadows on/off\n";
    std::cout << " F9             stainless/glass environment reflections on/off\n";
    std::cout << " F10            HDR bloom on/off\n";
    std::cout << "                 collision safety stops arm/lift before intersections\n";
    std::cout << " R              reset the camera\n";
    std::cout << " ESC            quit\n";
    std::cout << "============================================================\n" << std::endl;
}


// ============================================================================
// Rendering pipeline helpers: shadow map -> HDR MRT -> bloom -> tone map.
// ============================================================================

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
    int width = 0;
    int height = 0;
};

static unsigned int gQuadVAO = 0;
static unsigned int gQuadVBO = 0;

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
    t = HDRTargets();
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

    glGenFramebuffers(2, t.pingFbo);
    glGenTextures(2, t.pingColor);
    bool pingOK = true;
    for (int i = 0; i < 2; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, t.pingFbo[i]);
        glBindTexture(GL_TEXTURE_2D, t.pingColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.pingColor[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) pingOK = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
            gFanAngle);
    }
    else if (isProgressiveTeachingObject())
    {
        Scene::beginTeachingDraw(teachingLimit(), teachingHighlightFrom());
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
    // Rays enter from the open garage side and descend across the vehicle,
    // giving the stainless planes long photographic highlight/shadow bands.
    outDirection = normalize(Vec3(0.18f, -1.0f, -0.38f)); // light -> scene
    Vec3 centre(0.0f, 2.2f, -1.0f);
    Vec3 lightPos = centre - outDirection * 28.0f;
    Mat4 lightView = lookAt(lightPos, centre, Vec3(0.0f, 1.0f, 0.0f));
    Mat4 lightProj = orthographic(-19.0f, 19.0f, -18.0f, 18.0f, 1.0f, 58.0f);
    return lightProj * lightView;
}


static unsigned int createEnvironmentCubemap()
{
    // A small procedural reflection probe.  It is intentionally generated in
    // code so the final ZIP is self-contained and never fails because a texture
    // path changed.  The six faces represent side walls, ceiling strip lights,
    // floor, bright entrance/sky and the darker rear wall.
    const int S = 64;
    std::vector<unsigned char> pixels(static_cast<size_t>(S * S * 3));
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    for (int face = 0; face < 6; ++face)
    {
        for (int y = 0; y < S; ++y)
        {
            for (int x = 0; x < S; ++x)
            {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(S);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(S);
                float r = 0.095f, g = 0.105f, b = 0.120f;

                if (face == 0 || face == 1) // +/-X industrial walls + horizon band
                {
                    float panel = ((x / 8) & 1) ? 0.012f : -0.008f;
                    float horizon = std::exp(-std::pow((v - 0.46f) * 8.0f, 2.0f));
                    r = 0.085f + panel + 0.028f * v + 0.105f * horizon;
                    g = 0.095f + panel + 0.030f * v + 0.115f * horizon;
                    b = 0.115f + panel + 0.034f * v + 0.130f * horizon;
                }
                else if (face == 2) // +Y ceiling with controlled fluorescent strips
                {
                    r = 0.125f; g = 0.132f; b = 0.148f;
                    float stripePhase = std::fmod(u * 8.0f, 1.0f);
                    bool stripe = stripePhase > 0.43f && stripePhase < 0.57f;
                    bool broken = std::fmod(v * 3.0f, 1.0f) > 0.08f;
                    if (stripe && broken)
                    {
                        r = 0.72f; g = 0.68f; b = 0.60f;
                    }
                }
                else if (face == 3) // -Y concrete floor
                {
                    unsigned int h = static_cast<unsigned int>(x * 1973 + y * 9277 + 89173);
                    h = (h << 13) ^ h;
                    float n = 0.5f + 0.5f * (1.0f - static_cast<float>((h * (h * h * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 1073741824.0f);
                    r = 0.050f + 0.030f * n;
                    g = 0.049f + 0.029f * n;
                    b = 0.054f + 0.032f * n;
                }
                else if (face == 4) // +Z open entrance / cool daylight
                {
                    float sky = 0.32f + 0.68f * v;
                    float horizon = std::exp(-std::pow((v - 0.38f) * 8.5f, 2.0f));
                    r = 0.18f + 0.24f * sky + 0.10f*horizon;
                    g = 0.24f + 0.28f * sky + 0.12f*horizon;
                    b = 0.34f + 0.34f * sky + 0.16f*horizon;
                    if (v < 0.20f) { r *= 0.52f; g *= 0.56f; b *= 0.62f; }
                }
                else // -Z back wall / shelving side
                {
                    float horizon = std::exp(-std::pow((v - 0.48f) * 9.0f, 2.0f));
                    r = 0.055f + 0.036f * v + 0.045f*horizon;
                    g = 0.062f + 0.038f * v + 0.050f*horizon;
                    b = 0.076f + 0.043f * v + 0.058f*horizon;
                    if (((x / 10) & 1) == 0) { r *= 0.91f; g *= 0.92f; b *= 0.94f; }
                }

                size_t k = static_cast<size_t>((y * S + x) * 3);
                auto toByte = [](float c) -> unsigned char
                {
                    if (c < 0.0f) c = 0.0f;
                    if (c > 1.0f) c = 1.0f;
                    return static_cast<unsigned char>(c * 255.0f + 0.5f);
                };
                pixels[k + 0] = toByte(r);
                pixels[k + 1] = toByte(g);
                pixels[k + 2] = toByte(b);
            }
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB8,
            S, S, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
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

static void uploadSceneUniforms(unsigned int program,
                                const Mat4& view,
                                const Mat4& projection,
                                const Mat4& lightSpace,
                                const Vec3& dirLight,
                                unsigned int shadowTexture,
                                unsigned int environmentTexture)
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

    glUniform3f(glGetUniformLocation(program, "uDirLightDirection"), dirLight.x, dirLight.y, dirLight.z);
    glUniform3f(glGetUniformLocation(program, "uDirLightColor"), 0.95f, 0.91f, 0.84f);
    glUniform1f(glGetUniformLocation(program, "uDirLightIntensity"), 0.68f);

    // Eight representative ceiling fixtures.  The visible scene contains more
    // housings, but grouping nearby fixtures keeps the shader inexpensive.
    const Vec3 lightPos[8] = {
        Vec3(-6.4f,6.32f,-10.10f), Vec3(6.4f,6.32f,-9.55f),
        Vec3(-6.4f,6.32f, -3.90f), Vec3(6.4f,6.32f,-3.35f),
        Vec3(-6.4f,6.32f,  2.30f), Vec3(6.4f,6.32f, 2.85f),
        Vec3(-6.4f,6.32f,  8.50f), Vec3(6.4f,6.32f, 9.05f)
    };
    glUniform1i(glGetUniformLocation(program, "uPointLightCount"), 8);
    for (int i = 0; i < 8; ++i)
    {
        std::string posName = "uPointLightPos[" + std::to_string(i) + "]";
        std::string colName = "uPointLightColor[" + std::to_string(i) + "]";
        std::string intName = "uPointLightIntensity[" + std::to_string(i) + "]";
        glUniform3f(glGetUniformLocation(program, posName.c_str()), lightPos[i].x, lightPos[i].y, lightPos[i].z);
        glUniform3f(glGetUniformLocation(program, colName.c_str()), 0.96f, 0.91f, 0.82f);
        glUniform1f(glGetUniformLocation(program, intName.c_str()), 1.28f);
    }

    Vec3 shoulder, elbow, wrist, toolEnd;
    robotSafetyPoints(gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch,
        shoulder, elbow, wrist, toolEnd);
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
    glActiveTexture(GL_TEXTURE0);
}


int main()
{
    if (!glfwInit())
    {
        std::cout << "Failed to initialise GLFW." << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);   // 4x multisampling - smoother cube edges

    GLFWwindow* window = glfwCreateWindow(gWidth, gHeight, kTitle, nullptr, nullptr);
    if (window == nullptr)
    {
        std::cout << "Failed to create the GLFW window." << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);   // v-sync

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
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
    initFullscreenQuad();
    Primitives::init();
    Scene::init(shaderProgram);

    resetCamera();
    printControls();

    // ---- Render loop -------------------------------------------------------
    float lastFrame = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window))
    {
        float now = static_cast<float>(glfwGetTime());
        float deltaTime = now - lastFrame;
        lastFrame = now;

        processDiscreteInput(window);
        processMovement(window, deltaTime);
        updateTeachingWindowTitle(window);

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

        if (!ensureHDRTargets(hdr, gWidth, gHeight))
        {
            glfwPollEvents();
            continue;
        }

        float aspect = (gHeight > 0)
            ? static_cast<float>(gWidth) / static_cast<float>(gHeight)
            : 1.0f;
        Mat4 view = gCamera.viewMatrix();
        Mat4 projection = perspective(gCamera.fov, aspect, 0.05f, 200.0f);
        Vec3 dirLight;
        Mat4 lightSpace = buildLightSpaceMatrix(dirLight);

        // -----------------------------------------------------------------
        // 1) Directional shadow depth pass.
        // -----------------------------------------------------------------
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
        }

        // -----------------------------------------------------------------
        // 2) HDR scene pass (two MRT outputs: full scene + bloom bright pass).
        // -----------------------------------------------------------------
        glBindFramebuffer(GL_FRAMEBUFFER, hdr.fbo);
        glViewport(0, 0, gWidth, gHeight);
        glClearColor(0.012f, 0.015f, 0.022f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
        glUseProgram(shaderProgram);
        Scene::init(shaderProgram);
        uploadSceneUniforms(shaderProgram, view, projection, lightSpace, dirLight, shadow.depth, environmentCubemap);
        Primitives::resetDrawCallCounter();
        drawCurrentContent();

        // -----------------------------------------------------------------
        // 3) Separable Gaussian bloom on the bright-pass target.
        // -----------------------------------------------------------------
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_DEPTH_TEST);
        unsigned int bloomTexture = 0;
        if (gBloomEnabled)
        {
            glUseProgram(blurProgram);
            glUniform1i(glGetUniformLocation(blurProgram, "uImage"), 0);
            bool horizontal = true;
            bool first = true;
            const int passes = 8;
            for (int i = 0; i < passes; ++i)
            {
                const int target = horizontal ? 0 : 1;
                glBindFramebuffer(GL_FRAMEBUFFER, hdr.pingFbo[target]);
                glViewport(0, 0, gWidth, gHeight);
                glUniform1i(glGetUniformLocation(blurProgram, "uHorizontal"), horizontal ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, first ? hdr.color[1]
                    : hdr.pingColor[horizontal ? 1 : 0]);
                drawFullscreenQuad();
                horizontal = !horizontal;
                if (first) first = false;
            }
            bloomTexture = hdr.pingColor[horizontal ? 1 : 0];
        }

        // -----------------------------------------------------------------
        // 4) Final tone-map + gamma correction to the window framebuffer.
        // -----------------------------------------------------------------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
        glUniform1f(glGetUniformLocation(postProgram, "uExposure"), gExposure);
        glUniform2f(glGetUniformLocation(postProgram, "uInvResolution"),
            1.0f / static_cast<float>(gWidth), 1.0f / static_cast<float>(gHeight));
        drawFullscreenQuad();
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Primitives::destroy();
    destroyHDRTargets(hdr);
    if (shadow.depth) glDeleteTextures(1, &shadow.depth);
    if (shadow.fbo) glDeleteFramebuffers(1, &shadow.fbo);
    if (environmentCubemap) glDeleteTextures(1, &environmentCubemap);
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
