#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>

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
static bool  gDoorOpen = true;
static bool  gLiftRaised = false;

enum TeachingObject {
    TEACH_SCENE = 0, TEACH_CAR, TEACH_LIFT, TEACH_ROBOT,
    TEACH_CUBE, TEACH_CYLINDER, TEACH_SMOOTH_CYLINDER, TEACH_SPHERE
};
static TeachingObject gTeachObject = TEACH_SCENE;
static int gTeachStage = 0;

static const int kCarLimits[]   = { 1, 2, 3, 4, 5, 6, 8, 18, 166 };
static const char* kCarStages[] = {
    "broad stainless-steel faceted body shell",
    "inset windshield + two-pane tinted side glazing",
    "panoramic roof + sloping covered tonneau",
    "flush window seals / black trim",
    "sealed five-facet 3D wheel-arch fender flares",
    "full 3D protruding front + rear bumper blocks",
    "full-width front white + rear red light bars",
    "boxy rockers, door shut-lines + flush handle recesses",
    "four heavier outward all-terrain wheels with tread"
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
    static bool esc=false, l=false, v=false, o=false, p=false, c=false, stopLift=false, f1=false, f2=false, f3=false, f4=false;
    static bool k1=false, k2=false, k3=false, k4=false, k5=false, k6=false, k7=false;
    static bool kp1=false, kp2=false, kp3=false, kp4=false, kp5=false, kp6=false, kp7=false;
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

    if (keyPressedOnce(window, GLFW_KEY_1, k1) || keyPressedOnce(window, GLFW_KEY_KP_1, kp1)) handleDiscreteKey(window, GLFW_KEY_1);
    if (keyPressedOnce(window, GLFW_KEY_2, k2) || keyPressedOnce(window, GLFW_KEY_KP_2, kp2)) handleDiscreteKey(window, GLFW_KEY_2);
    if (keyPressedOnce(window, GLFW_KEY_3, k3) || keyPressedOnce(window, GLFW_KEY_KP_3, kp3)) handleDiscreteKey(window, GLFW_KEY_3);
    if (keyPressedOnce(window, GLFW_KEY_4, k4) || keyPressedOnce(window, GLFW_KEY_KP_4, kp4)) handleDiscreteKey(window, GLFW_KEY_4);
    if (keyPressedOnce(window, GLFW_KEY_5, k5) || keyPressedOnce(window, GLFW_KEY_KP_5, kp5)) handleDiscreteKey(window, GLFW_KEY_5);
    if (keyPressedOnce(window, GLFW_KEY_6, k6) || keyPressedOnce(window, GLFW_KEY_KP_6, kp6)) handleDiscreteKey(window, GLFW_KEY_6);
    if (keyPressedOnce(window, GLFW_KEY_7, k7) || keyPressedOnce(window, GLFW_KEY_KP_7, kp7)) handleDiscreteKey(window, GLFW_KEY_7);

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
    std::cout << "                 collision safety stops arm/lift before intersections\n";
    std::cout << " R              reset the camera\n";
    std::cout << " ESC            quit\n";
    std::cout << "============================================================\n" << std::endl;
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
    if (shaderProgram == 0)
    {
        std::cout << "Shader creation failed - aborting." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    Primitives::init();      // uploads the unit cube and the unit cylinder
    Scene::init(shaderProgram);

    resetCamera();
    printControls();

    // ---- 6. The render loop -------------------------------------------------
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

        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        float aspect = (gHeight > 0)
            ? static_cast<float>(gWidth) / static_cast<float>(gHeight)
            : 1.0f;

        Mat4 view = gCamera.viewMatrix();
        Mat4 projection = perspective(gCamera.fov, aspect, 0.05f, 200.0f);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uView"), 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uProjection"), 1, GL_FALSE, projection.m);
        glUniform3f(glGetUniformLocation(shaderProgram, "uCameraPos"),
                    gCamera.position.x, gCamera.position.y, gCamera.position.z);

        Primitives::resetDrawCallCounter();

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
        else
        {
            if (isProgressiveTeachingObject())
            {
                Scene::beginTeachingDraw(teachingLimit(), teachingHighlightFrom());

                if (gTeachObject == TEACH_CAR)
                    Scene::drawCar(mat4Identity(), Vec3(0.680f, 0.300f, 0.115f));
                else if (gTeachObject == TEACH_LIFT)
                    Scene::drawHydraulicLift(mat4Identity(), gLiftHeight);
                else if (gTeachObject == TEACH_ROBOT)
                    Scene::drawRobotArm(mat4Identity(), gRobotBaseYaw, gJoint1Angle, gJoint2Angle, gWristPitch, gWristRoll, gRobotTool);

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

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Primitives::destroy();
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
