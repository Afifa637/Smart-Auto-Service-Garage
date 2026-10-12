#include <glad/glad.h>
#include <cstring>
#include <cmath>
#include <vector>

#include "SceneObjects.h"
#include "Primitives.h"

static unsigned int gProgram = 0;
static int gLocModel = -1;
static int gLocNormalMat = -1;
static int gLocColor = -1;
static int gLocEmissive = -1;
static int gLocMetallic = -1;
static int gLocRoughness = -1;
static int gLocMaterialType = -1;
static int gLocPolish = -1;
static int gLocClearCoat = -1;
static int gLocAnisotropy = -1;
static int gLocTextureLayer = -1;
static int gLocTextureScale = -1;
static int gLocDirt = -1;

static bool gTexturesOn = true;

struct SurfaceFinish
{
    float polish = 0.0f;      // 0 = as manufactured, 1 = machine polished
    float clearCoat = -1.0f;  // < 0 = use the material default
    float anisotropy = -1.0f; // < 0 = use the material default
    int materialType = -1;    // < 0 = let each draw infer its own material
    float dirt = 0.0f;        // road grime lying on the surface
};
static SurfaceFinish gFinish;

static std::vector<Scene::RtItem> *gRec = nullptr;

static void recordPrim(int type, const Mat4 &model, const Vec3 &color, float emissive)
{
    const float *m = model.m;
    const float sx = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float sy = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float sz = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (std::fmax(sx, std::fmax(sy, sz)) < 0.06f && emissive < 0.3f)
        return; // bolts, rivets and other detail smaller than a pixel on the monitor
    Scene::RtItem it;
    it.type = type;
    it.m = model;
    it.color = color;
    it.emissive = emissive > 0.3f;
    gRec->push_back(it);
}

// ---- SECTION 0c - SCENE SWITCHES ------------------------------------------
static Scene::SceneSwitches gSwitches;

void Scene::setSwitches(const SceneSwitches &s) { gSwitches = s; }
const Scene::SceneSwitches &Scene::switches() { return gSwitches; }

// Presentation-only draw filter: progressively reveals existing draw calls.
static bool gTeachingDrawEnabled = false;
static int gTeachingVisibleLimit = -1;
static int gTeachingHighlightFrom = -1;
static int gTeachingDrawCounter = 0;

static bool teachingPrepareColor(const Vec3 &original, Vec3 &shown)
{
    shown = original;
    if (!gTeachingDrawEnabled)
        return true;
    ++gTeachingDrawCounter;
    if (gTeachingVisibleLimit >= 0 && gTeachingDrawCounter > gTeachingVisibleLimit)
        return false;
    if (gTeachingHighlightFrom > 0 && gTeachingDrawCounter >= gTeachingHighlightFrom &&
        (gTeachingVisibleLimit < 0 || gTeachingDrawCounter <= gTeachingVisibleLimit))
        shown = Vec3(0.98f, 0.72f, 0.12f);
    return true;
}

void Scene::beginTeachingDraw(int visibleLimit, int highlightFrom)
{
    gTeachingDrawEnabled = true;
    gTeachingVisibleLimit = visibleLimit;
    gTeachingHighlightFrom = highlightFrom;
    gTeachingDrawCounter = 0;
}

void Scene::endTeachingDraw()
{
    gTeachingDrawEnabled = false;
    gTeachingVisibleLimit = -1;
    gTeachingHighlightFrom = -1;
}

int Scene::teachingDrawCount() { return gTeachingDrawCounter; }

void Scene::init(unsigned int shaderProgram)
{
    gProgram = shaderProgram;
    gLocModel = glGetUniformLocation(gProgram, "uModel");
    gLocNormalMat = glGetUniformLocation(gProgram, "uNormalMatrix");
    gLocColor = glGetUniformLocation(gProgram, "uObjectColor");
    gLocEmissive = glGetUniformLocation(gProgram, "uEmissive");
    gLocMetallic = glGetUniformLocation(gProgram, "uMetallic");
    gLocRoughness = glGetUniformLocation(gProgram, "uRoughness");
    gLocMaterialType = glGetUniformLocation(gProgram, "uMaterialType");
    gLocPolish = glGetUniformLocation(gProgram, "uPolish");
    gLocClearCoat = glGetUniformLocation(gProgram, "uClearCoat");
    gLocAnisotropy = glGetUniformLocation(gProgram, "uAnisotropy");
    gLocTextureLayer = glGetUniformLocation(gProgram, "uTextureLayer");
    gLocTextureScale = glGetUniformLocation(gProgram, "uTextureScale");
    gLocDirt = glGetUniformLocation(gProgram, "uDirt");
}

void Scene::setSurfaceDirt(float dirt)
{
    gFinish.dirt = (dirt < 0.0f) ? 0.0f : (dirt > 1.0f ? 1.0f : dirt);
}

void Scene::setTexturesEnabled(bool on) { gTexturesOn = on; }

void Scene::setSurfaceFinish(float polish, float clearCoat, float anisotropy,
                             int materialType)
{
    gFinish.polish = (polish < 0.0f) ? 0.0f : (polish > 1.0f ? 1.0f : polish);
    gFinish.clearCoat = clearCoat;
    gFinish.anisotropy = anisotropy;
    gFinish.materialType = materialType;
}

void Scene::clearSurfaceFinish()
{
    gFinish = SurfaceFinish();
}

float Scene::currentPolish() { return gFinish.polish; }

//  SECTION 1 - THE TWO DRAWING HELPERS

Mat4 Scene::place(const Vec3 &pos, const Vec3 &size, const Vec3 &rotDeg)
{
    return translate(pos) * rotateY(rotDeg.y) * rotateX(rotDeg.x) * rotateZ(rotDeg.z) * scale(size);
}

Mat4 Scene::place(const Vec3 &pos, const Vec3 &size)
{
    return translate(pos) * scale(size);
}

static const int MAT_CONCRETE = 1;
static const int MAT_WALL = 2;
static const int MAT_BRUSHED_STEEL = 3;
static const int MAT_GALV = 4;
static const int MAT_CHROME = 5;
static const int MAT_PLASTIC = 6;
static const int MAT_RUBBER = 9;
static const int MAT_PAINTED_METAL = 10;
static const int MAT_GLASS = 11;
static const int MAT_TIRE = 12;
static const int MAT_STAINLESS = 13;
static const int MAT_BRAKE = 14;
static const int MAT_ASPHALT = 15;
static const int MAT_PAINT = 17;
static const int MAT_CARBON = 18;
static const int MAT_TREAD = 19; // steel non-slip plate (lift runways)

static const int MAT_FABRIC = 21;

static int inferMaterialType(const Vec3 &c, float emissive, float metallic, float roughness)
{
    if (emissive > 0.20f)
        return 17; // light/paint family; emissive dominates

    const float mx = (c.x > c.y ? (c.x > c.z ? c.x : c.z) : (c.y > c.z ? c.y : c.z));
    const float mn = (c.x < c.y ? (c.x < c.z ? c.x : c.z) : (c.y < c.z ? c.y : c.z));
    const float avg = (c.x + c.y + c.z) / 3.0f;
    const float chroma = mx - mn;

    if (metallic > 0.72f && metallic < 0.88f && roughness > 0.30f && roughness < 0.58f && avg > 0.34f && avg < 0.58f)
        return 14; // scored brake iron
    if (metallic > 0.82f && chroma < 0.11f)
        return 13; // stainless / polished metal
    if (roughness < 0.14f && avg < 0.24f)
        return 11; // tinted glass
    if (roughness > 0.90f && avg < 0.16f)
        return 12; // tyre/rubber
    if (metallic > 0.62f && avg > 0.58f)
        return 5; // chrome / bright alloy
    if (metallic > 0.45f)
        return 3; // generic brushed metal
    if (c.z > c.x + 0.035f && avg < 0.23f && roughness < 0.78f)
        return 11; // blue-black glass
    if (chroma > 0.28f)
        return 10; // saturated powder-coated machinery

    // Neutral palette families.
    if (chroma < 0.065f)
    {
        if (avg < 0.125f)
            return 9; // rubber/dark gasket
        if (avg < 0.185f)
            return 10; // powder-coated dark steel
        if (avg < 0.215f)
            return 1; // concrete floor / dark slab
        if (avg < 0.255f)
            return 2; // wall/ceiling industrial paint
        if (avg < 0.315f)
            return 15; // asphalt / dark aggregate
        if (avg > 0.655f)
            return 5; // chrome / bright trim
        if (avg > 0.495f)
            return 4; // galvanised / steel
        return 10;    // painted sheet metal
    }

    // Brown materials: rust, wood and cardboard are deliberately different.
    if (c.x > c.y && c.y > c.z)
    {
        if (c.x > 0.40f && c.y > 0.32f && c.z > 0.24f)
            return 8; // cardboard
        if (c.x > 0.38f && c.y > 0.27f)
            return 7; // wood
        if (c.x > 0.28f && c.y < 0.28f)
            return 16; // rust/oxidation
        if (c.x > 0.55f && c.y > 0.35f && c.z < 0.22f)
            return 17; // safety/paint line
    }

    if (avg < 0.22f && chroma < 0.18f)
        return 18; // carbon/dark composite
    if (roughness > 0.80f && avg < 0.35f)
        return 6; // matte plastic
    return 10;    // painted metal default
}

static void resolveMaterial(int type, float &metallic, float &roughness)
{
    switch (type)
    {
    case 1:
        metallic = 0.0f;
        roughness = (roughness < 0.82f ? 0.86f : roughness);
        break;
    case 2:
        metallic = 0.0f;
        roughness = (roughness < 0.72f ? 0.78f : roughness);
        break;
    case 3:
        metallic = (metallic < 0.68f ? 0.72f : metallic);
        roughness = (roughness < 0.06f) ? 0.06f : ((roughness > 0.52f) ? 0.52f : roughness);
        break;
    case 4:
        metallic = (metallic < 0.55f ? 0.62f : metallic);
        roughness = 0.48f;
        break;
    case 5:
        metallic = (metallic < 0.90f ? 0.95f : metallic);
        roughness = (roughness > 0.22f ? 0.15f : roughness);
        break;
    case 6:
        metallic = 0.0f;
        roughness = (roughness < 0.48f ? 0.58f : roughness);
        break;
    case 7:
        metallic = 0.0f;
        roughness = 0.72f;
        break;
    case 8:
        metallic = 0.0f;
        roughness = 0.90f;
        break;
    case 9:
        metallic = 0.0f;
        roughness = 0.93f;
        break;
    case 10:
        metallic = 0.03f;
        roughness = (roughness > 0.72f ? 0.58f : roughness);
        break;
    case 11:
        metallic = 0.04f;
        roughness = 0.075f;
        break;
    case 12:
        metallic = 0.0f;
        roughness = 0.95f;
        break;
    case MAT_FABRIC:
        metallic = 0.0f;
        roughness = 0.97f;
        break;

    case 13:
        metallic = (metallic < 0.90f ? 0.92f : metallic);
        roughness = (roughness < 0.05f) ? 0.05f : ((roughness > 0.48f) ? 0.48f : roughness);
        break;
    case 14:
        metallic = 0.78f;
        roughness = 0.45f;
        break;
    case 15:
        metallic = 0.0f;
        roughness = 0.96f;
        break;
    case 16:
        metallic = 0.22f;
        roughness = 0.84f;
        break;
    case 17:
        metallic = 0.0f;
        roughness = 0.68f;
        break;
    case 18:
        metallic = 0.10f;
        roughness = 0.44f;
        break;
    case 19:
        metallic = 0.72f;
        roughness = 0.46f;
        break;
    case 20:
        metallic = 0.0f;
        roughness = 1.0f;
        break;
    default:
        break;
    }
}

static void defaultFinish(int type, float &clearCoat, float &anisotropy)
{
    clearCoat = 0.0f;
    anisotropy = 0.0f;
    switch (type)
    {
    case MAT_BRUSHED_STEEL:
        clearCoat = 0.04f;
        anisotropy = 0.62f;
        break;
    case MAT_STAINLESS:
        clearCoat = 0.06f;
        anisotropy = 0.78f;
        break;
    case MAT_CHROME:
        clearCoat = 0.10f;
        anisotropy = 0.10f;
        break;
    case MAT_GALV:
        clearCoat = 0.02f;
        anisotropy = 0.30f;
        break;
    case MAT_PAINTED_METAL:
        clearCoat = 0.45f;
        anisotropy = 0.0f;
        break;
    case MAT_PAINT:
        clearCoat = 0.30f;
        anisotropy = 0.0f;
        break;
    case MAT_PLASTIC:
        clearCoat = 0.12f;
        anisotropy = 0.0f;
        break;
    case MAT_CARBON:
        clearCoat = 0.55f;
        anisotropy = 0.0f;
        break;
    case MAT_BRAKE:
        clearCoat = 0.0f;
        anisotropy = 0.45f;
        break;
    default:
        break;
    }
}

static int textureLayerFor(int type)
{
    switch (type)
    {
    case MAT_CONCRETE:
        return 0;
    case MAT_WALL:
        return 1;
    case MAT_ASPHALT:
        return 2;
    case MAT_TREAD:
        return 3;
    default:
        return -1;
    }
}

static float textureScaleFor(int type)
{
    switch (type)
    {
    case MAT_CONCRETE:
        return 0.34f; // one tile per ~2.9 m of floor
    case MAT_WALL:
        return 0.46f;
    case MAT_ASPHALT:
        return 0.30f;
    case MAT_TREAD:
        return 3.60f; // one tile per ~0.28 m - fine plate
    default:
        return 1.0f;
    }
}

static void uploadMaterialUniforms(const Vec3 &color, float emissive, float metallic,
                                   float roughness, int forcedType = -1)
{

    int type = (forcedType >= 0) ? forcedType
                                 : ((gFinish.materialType >= 0) ? gFinish.materialType
                                                                : inferMaterialType(color, emissive, metallic, roughness));
    resolveMaterial(type, metallic, roughness);

    float coat = 0.0f, aniso = 0.0f;
    defaultFinish(type, coat, aniso);
    if (gFinish.clearCoat >= 0.0f)
        coat = gFinish.clearCoat;
    if (gFinish.anisotropy >= 0.0f)
        aniso = gFinish.anisotropy;

    glUniform3f(gLocColor, color.x, color.y, color.z);
    glUniform1f(gLocEmissive, emissive);
    glUniform1f(gLocMetallic, metallic);
    glUniform1f(gLocRoughness, roughness);
    glUniform1i(gLocMaterialType, type);
    glUniform1f(gLocPolish, gFinish.polish);
    glUniform1f(gLocClearCoat, coat);
    glUniform1f(gLocAnisotropy, aniso);
    glUniform1i(gLocTextureLayer, gTexturesOn ? textureLayerFor(type) : -1);
    glUniform1f(gLocTextureScale, textureScaleFor(type));
    glUniform1f(gLocDirt, gFinish.dirt);
}

void Scene::drawCube(const Mat4 &model, const Vec3 &color, float emissive, float metallic, float roughness, int materialType)
{
    if (gRec)
    {
        recordPrim(0, model, color, emissive);
        return;
    }
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor))
        return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawCubeMesh();
}

void Scene::drawCylinder(const Mat4 &model, const Vec3 &color, float emissive, float metallic, float roughness, int materialType)
{
    if (gRec)
    {
        recordPrim(1, model, color, emissive);
        return;
    }
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor))
        return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawCylinderMesh();
}

void Scene::drawCylS(const Mat4 &model, const Vec3 &color, float emissive, float metallic, float roughness, int materialType)
{
    if (gRec)
    {
        recordPrim(1, model, color, emissive);
        return;
    }
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor))
        return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawSmoothCylinderMesh();
}

void Scene::drawSphere(const Mat4 &model, const Vec3 &color, float emissive, float metallic, float roughness, int materialType)
{
    if (gRec)
    {
        recordPrim(2, model, color, emissive);
        return;
    }
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor))
        return;

    Mat3 nrm = normalMatrix(model);
    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);
    Primitives::drawSphereMesh();
}

//  SECTION 1d - CURVE-BASED GEOMETRY

void Scene::drawTubeSegment(const Mat4 &parent, const Vec3 &from, const Vec3 &to,
                            float radius, const Vec3 &color,
                            float metallic, float roughness, int materialType)
{
    Vec3 d = to - from;
    const float len = length(d);
    if (len < 1e-5f)
        return;
    d = d * (1.0f / len);

    const float b = std::acos((d.y < -1.0f) ? -1.0f : ((d.y > 1.0f) ? 1.0f : d.y));
    const float a = std::atan2(d.z, -d.x);

    const Vec3 mid = (from + to) * 0.5f;
    drawCylS(parent * translate(mid) * rotateY(a * 180.0f / PI_F) * rotateZ(b * 180.0f / PI_F) * scale(radius * 2.0f, len, radius * 2.0f),
             color, 0.0f, metallic, roughness, materialType);
}

void Scene::drawBezierHose(const Mat4 &parent,
                           const Vec3 &p0, const Vec3 &p1, const Vec3 &p2, const Vec3 &p3,
                           float radius, int segments, const Vec3 &color,
                           int materialType)
{
    if (segments < 2)
        segments = 2;

    // B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
    auto bezier = [&](float t) -> Vec3
    {
        const float u = 1.0f - t;
        return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
    };

    Vec3 prev = bezier(0.0f);
    for (int i = 1; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 cur = bezier(t);
        drawTubeSegment(parent, prev, cur, radius, color, 0.0f, 0.86f, materialType);

        if (i < segments)
            drawSphere(parent * translate(cur) * scale(radius * 2.05f, radius * 2.05f, radius * 2.05f),
                       color, 0.0f, 0.0f, 0.86f, materialType);
        prev = cur;
    }
}

//  SECTION 1b - IMPERFECTION HELPERS

float Scene::hash01(int index)
{
    float x = std::sin(static_cast<float>(index) * 12.9898f) * 43758.5453f;
    return x - std::floor(x); // fractional part only
}

float Scene::jitter(int index, float amount)
{
    return (hash01(index) * 2.0f - 1.0f) * amount;
}

Vec3 Scene::tintVary(const Vec3 &c, int index, float percent)
{
    Vec3 out(c.x * (1.0f + jitter(index * 3 + 1, percent)),
             c.y * (1.0f + jitter(index * 3 + 2, percent)),
             c.z * (1.0f + jitter(index * 3 + 3, percent)));

    if (out.x > 0.85f)
        out.x = 0.85f;
    if (out.y > 0.85f)
        out.y = 0.85f;
    if (out.z > 0.85f)
        out.z = 0.85f;
    if (out.x < 0.04f)
        out.x = 0.04f;
    if (out.y < 0.04f)
        out.y = 0.04f;
    if (out.z < 0.04f)
        out.z = 0.04f;
    return out;
}

Mat4 Scene::messy(int index, float posAmt, float yawAmt, float scaleAmt)
{
    float s = 1.0f + jitter(index * 7 + 5, scaleAmt);
    return translate(jitter(index * 7 + 1, posAmt), 0.0f, jitter(index * 7 + 2, posAmt)) * rotateY(jitter(index * 7 + 3, yawAmt)) * scale(s, s, s);
}

//  SECTION 1c - SURFACE DETAIL HELPERS

void Scene::drawPanel(const Mat4 &parent, const Vec3 &pos, const Vec3 &size,
                      const Vec3 &color, const Vec3 &rotDeg)
{
    // 1. The recess behind: same panel, very slightly bigger, much darker.
    drawCube(parent * place(pos, Vec3(size.x + 0.030f, size.y + 0.030f, size.z * 0.92f), rotDeg),
             color * 0.42f);

    // 2. The panel face itself.
    drawCube(parent * place(pos, size, rotDeg), color);

    // 3. The lit chamfer along the top edge.
    drawCube(parent * place(Vec3(pos.x, pos.y + size.y * 0.5f, pos.z),
                            Vec3(size.x * 0.99f, 0.014f, size.z + 0.012f), rotDeg),
             color * 1.45f + Vec3(0.06f, 0.06f, 0.06f));
}

void Scene::drawContactPatch(const Mat4 &parent, const Vec3 &centre, float sizeX, float sizeZ)
{
    drawCube(parent * place(Vec3(centre.x, 0.009f, centre.z), Vec3(sizeX, 0.014f, sizeZ)),
             Vec3(0.085f, 0.088f, 0.095f));
}

// ---- Building shell --------------------------------------------------------
static const float GARAGE_W = 28.0f;                   // interior width  (X): -14.0 .. +14.0
static const float GARAGE_D = 26.0f;                   // interior depth  (Z): -13.0 .. +13.0
static const float GARAGE_H = 7.0f;                    // interior height (Y):   0.0 ..  +7.0
static const float WALL_T = 0.4f;                      // wall thickness
static const float SHELL_W = GARAGE_W + 2.0f * WALL_T; // 28.8
static const float SHELL_D = GARAGE_D + 2.0f * WALL_T; // 26.8
static const float WALL_X = GARAGE_W * 0.5f;           // 14.0 inner face
static const float WALL_Z = GARAGE_D * 0.5f;           // 13.0 inner face

// ---- Entrance and sliding door ---------------------------------------------
static const float ENTRY_Z = 13.2f; // plane of the entrance frame
static const float DOOR_W = 13.0f;  // clear opening width
static const float DOOR_H = 4.9f;   // clear opening height (leaves room for the roll)
static const int DOOR_PANELS = 5;
static const float DOOR_PANEL_H = DOOR_H / static_cast<float>(DOOR_PANELS); // 0.98
static const float DOOR_Z = ENTRY_Z - 0.32f;                                // panels sit just inside the frame

static const float LIFT_Z = -2.00f;       // the lift sits behind the middle
static const float LIFT_ARM_LEN = 3.30f;  // pivot-to-pivot length of one arm
static const float LIFT_ARM_W = 0.20f;    // arm thickness across X
static const float LIFT_ARM_T = 0.32f;    // arm depth (its "tall" dimension)
static const float LIFT_BASE_TOP = 0.22f; // top of the base frame
static const float LIFT_REST_H = 0.58f;   // platform lift when fully lowered
static const float LIFT_PLAT_T = 0.18f;   // blue platform beam thickness
static const float LIFT_PAD_T = 0.10f;    // yellow runway pad thickness
static const float LIFT_PAD_L = 5.20f;    // runway pad length along Z

static const float LIFT_RAMP_LEN = 4.60f; // run, floor to platform edge
static const float LIFT_TRACK = 0.98f;    // half distance between the two runways
static const float LIFT_MAX_H = 4.00f;    // travel of the animation parameter

static const float CAR_FWHEEL_R = 0.450f;
static const float CAR_FWHEEL_W = 0.380f;
static const float CAR_RWHEEL_R = 0.455f;
static const float CAR_RWHEEL_W = 0.390f;
static const float CAR_FAXLE_Z = -1.73f; // long production-style wheelbase
static const float CAR_RAXLE_Z = 1.73f;
static const float CAR_FTRACK = 1.010f; // wheel-centre half-track along project X axis
static const float CAR_RTRACK = 1.010f;
static const float CAR_WHEEL_OUTSET = 0.035f; // tyres remain tucked close to the flush arch cladding

// ---------------------------------------------------------------------------
//  CYBERTRUCK MASTER PROFILE

static const float CT_NOSE_Z = -2.760f;     // front face of the stainless shell
static const float CT_COWL_Z = -1.540f;     // base of the windscreen
static const float CT_APEX_Z = -0.545f;     // roof peak, at 40% of the length
static const float CT_ROOF_R_Z = 0.000f;    // rear edge of the cab roof
static const float CT_BACKLIGHT_Z = 0.620f; // bottom of the rear screen
static const float CT_BED_FRONT_Z = 0.760f; // front wall of the vault / tonneau
static const float CT_TAIL_Z = 2.800f;      // tailgate plane

static const float CT_FLOOR_Y = 0.300f;    // rocker underside at the nose
static const float CT_NOSE_TOP_Y = 0.951f; // top of the front fascia (measured)
static const float CT_COWL_Y = 1.254f;     // windscreen base (measured)
static const float CT_BELT_Y = 1.150f;     // belt line / bottom of the glazing
static const float CT_APEX_Y = 1.791f;     // roof apex - the real car is 1.791 m tall
// On the apex -> tail line, so the rear silhouette is one unbroken straight run.
static const float CT_ROOF_R_Y = 1.704f;
static const float CT_BACKLIGHT_Y = 1.455f;
static const float CT_BED_RAIL_Y = 1.260f; // bed rail AT THE TAIL - the rail slopes
static const float CT_TAIL_BOT_Y = 0.300f; // stainless skin bottom at the tail

static const float CT_FRONT_BLACK_TOP_Y = 0.600f;
static const float CT_REAR_BLACK_TOP_Y = 0.620f;
static const float CT_SILL_BOT_Y = 0.220f;
static const float CT_SILL_TOP_Y = 0.360f;

static const float CT_STEEL_OVERLAP = 0.020f;

static const float CT_ARCH_REACH = 0.670f;

static const float CT_X_BODY = 1.030f;  // half width of the stainless side skin
static const float CT_X_GLASS = 1.006f; // side glazing sits ~24 mm inboard
static const float CT_X_WIND = 0.996f;  // windscreen / backlight half width
static const float CT_X_ROOF = 0.992f;  // roof and tonneau half width

static const float CT_DLO_FRONT_Z = CT_COWL_Z; // front corner, at the cowl
static const float CT_DLO_REAR_Z = 0.750f;     // rear bottom corner, on the belt
static const float CT_DLO_REAR_TOP_Z = 0.700f; // rear top corner - near-vertical rear edge
static const float CT_B_PILLAR_Z = -0.260f;
static const float CT_B_PILLAR_HALF = 0.028f;
static const float CT_ROOF_RAIL_T = 0.088f; // stainless left above the glass

static float ctLerp(float a, float b, float t) { return a + (b - a) * t; }

static float ctRoofLineY(float z)
{
    if (z <= CT_COWL_Z)
        return ctLerp(CT_NOSE_TOP_Y, CT_COWL_Y, (z - CT_NOSE_Z) / (CT_COWL_Z - CT_NOSE_Z));
    if (z <= CT_APEX_Z)
        return ctLerp(CT_COWL_Y, CT_APEX_Y, (z - CT_COWL_Z) / (CT_APEX_Z - CT_COWL_Z));
    if (z <= CT_ROOF_R_Z)
        return ctLerp(CT_APEX_Y, CT_ROOF_R_Y, (z - CT_APEX_Z) / (CT_ROOF_R_Z - CT_APEX_Z));
    return ctLerp(CT_ROOF_R_Y, CT_BED_RAIL_Y, (z - CT_ROOF_R_Z) / (CT_TAIL_Z - CT_ROOF_R_Z));
}

static float ctBeltCrossZ()
{
    const float slope = (CT_COWL_Y - CT_NOSE_TOP_Y) / (CT_COWL_Z - CT_NOSE_Z);
    return CT_NOSE_Z + (CT_BELT_Y - CT_NOSE_TOP_Y) / slope;
}

static const float CAR_DRIVE_DISTANCE = 18.50f;

static const float PARK_X = 10.60f;    // centre line of a parked car
static const float PARK_BAY_W = 3.10f; // bay width  (across the aisle, along Z)
static const float PARK_BAY_D = 5.60f; // bay depth  (into the wall, along X)
static const float PARK_Z_A = -7.00f;  // first bay on each side
static const float PARK_Z_B = 4.20f;   // second bay on each side

// ---- Robotic inspection arm ---------------------------------------------------
static const float ARM_X = -4.60f; // where the arm is bolted to the floor
static const float ARM_Z = -2.20f;
static const float ARM_SHOULDER_Y = 1.05f; // height of joint 1
static const float ARM_LOWER_LEN = 4.50f;  // long-reach shoulder link: clears the Cybertruck and reaches its far side
static const float ARM_UPPER_LEN = 4.00f;  // long-reach forearm: full serviced-vehicle footprint coverage

// ---- Ceiling fittings ----------------------------------------------------------
static const float CEIL_Y = GARAGE_H; // underside of the ceiling slab
static const float LAMP_Y = 6.55f;

// ============================================================================
//  SECTION 3 - COLOUR PALETTE

// ---- Building --------------------------------------------------------------
static const Vec3 C_FLOOR(0.190f, 0.190f, 0.200f);       // worn concrete   spec .18  shine 12
static const Vec3 C_FLOOR_BAY(0.150f, 0.152f, 0.162f);   // darker painted bay
static const Vec3 C_FLOOR_STAIN(0.098f, 0.096f, 0.094f); // oil staining near the lift
static const Vec3 C_WALL(0.240f, 0.250f, 0.270f);        // matte industrial paint
static const Vec3 C_WALL_DARK(0.150f, 0.158f, 0.172f);
static const Vec3 C_CEILING(0.205f, 0.212f, 0.228f);
static const Vec3 C_BEAM(0.165f, 0.170f, 0.182f);
static const Vec3 C_COLUMN(0.215f, 0.222f, 0.238f);
static const Vec3 C_STEEL(0.520f, 0.540f, 0.570f);   // brushed steel   spec .70  shine 64
static const Vec3 C_GALV(0.545f, 0.560f, 0.570f);    // galvanised      spec .45  shine 32
static const Vec3 C_CHROME(0.700f, 0.715f, 0.740f);  // chrome trim     spec 1.0  shine 220
static const Vec3 C_PLASTIC(0.300f, 0.320f, 0.340f); // matte plastic   spec .30  shine 40
static const Vec3 C_WOOD(0.420f, 0.330f, 0.225f);
static const Vec3 C_CARDBOARD(0.440f, 0.360f, 0.265f);
static const Vec3 C_RUBBER(0.090f, 0.090f, 0.095f);

static const Vec3 C_NEON(0.980f, 0.545f, 0.225f);       // neon tube, emissive
static const Vec3 C_NEON_CYAN(0.380f, 0.860f, 0.930f);  // diagnostic / scanner cyan
static const Vec3 C_PARK_LINE(0.720f, 0.450f, 0.120f);  // faded painted line
static const Vec3 C_WHITE_LINE(0.700f, 0.705f, 0.715f); // scuffed white paint
static const Vec3 C_TUBE(0.980f, 0.940f, 0.860f);       // fluorescent tube, emissive
static const Vec3 C_HAZARD_Y(0.740f, 0.620f, 0.140f);   // safety yellow - muted
static const Vec3 C_HAZARD_K(0.110f, 0.112f, 0.118f);

// ---- Sliding door ----------------------------------------------------------
static const Vec3 C_DOOR(0.515f, 0.530f, 0.545f); // galvanised panel
static const Vec3 C_DOOR_RIB(0.400f, 0.415f, 0.430f);

// ---- Scissor lift (blue frame + yellow pads, as in the reference photo) -----
static const Vec3 C_LIFT_BLUE(0.145f, 0.275f, 0.545f);
static const Vec3 C_LIFT_BLUE_D(0.095f, 0.185f, 0.375f);
static const Vec3 C_LIFT_YELLOW(0.740f, 0.615f, 0.145f);
static const Vec3 C_LIFT_DARK(0.155f, 0.158f, 0.168f);

// ---- Car -------------------------------------------------------------------

static const Vec3 C_CAR_ORANGE(0.680f, 0.300f, 0.115f); // gloss   spec .85 shine 128
static const Vec3 C_CAR_WHITE(0.720f, 0.725f, 0.730f);
static const Vec3 C_CAR_GUNMETAL(0.280f, 0.292f, 0.315f); // matte   spec .25 shine 24

static const Vec3 C_GLASS(0.070f, 0.085f, 0.105f);      // deep neutral tint; reflections carry the sky/garage
static const Vec3 C_TIRE(0.058f, 0.059f, 0.064f);       // tread - never mathematically black
static const Vec3 C_TIRE_WALL(0.074f, 0.075f, 0.080f);  // subtle sidewall lift
static const Vec3 C_RIM(0.105f, 0.110f, 0.120f);        // near-black production aero cover
static const Vec3 C_BRAKE_DISC(0.480f, 0.470f, 0.455f); // scored cast iron
static const Vec3 C_TAILLIGHT(0.870f, 0.185f, 0.140f);  // emissive

// ---- Robot arm and scanner --------------------------------------------------
static const Vec3 C_ROBOT(0.615f, 0.628f, 0.650f);
static const Vec3 C_ROBOT_DARK(0.215f, 0.222f, 0.240f);
static const Vec3 C_JOINT(0.720f, 0.400f, 0.165f);
static const Vec3 C_SCANNER(0.185f, 0.192f, 0.208f);

// ---- Fittings ----------------------------------------------------------------
static const Vec3 C_LAMP_HOUSING(0.235f, 0.243f, 0.258f);
static const Vec3 C_LAMP_LIGHT(0.960f, 0.935f, 0.880f); // emissive

static const Vec3 C_CABINET(0.540f, 0.190f, 0.150f);

static const Vec3 C_SHELF(0.360f, 0.372f, 0.395f);
static const Vec3 C_SHELF_DARK(0.245f, 0.255f, 0.272f);

// ---- Repeated equipment -------------------------------------------------------
static const Vec3 C_DRUM[4] = {
    Vec3(0.230f, 0.330f, 0.420f), // blue drum
    Vec3(0.480f, 0.410f, 0.170f), // yellow drum, well faded
    Vec3(0.260f, 0.365f, 0.285f), // green drum
    Vec3(0.400f, 0.250f, 0.175f)  // rusted
};
static const Vec3 C_EXTING(0.560f, 0.145f, 0.120f);

// ---- Exterior ------------------------------------------------------------------
static const Vec3 C_APRON(0.480f, 0.470f, 0.445f); // sunlit concrete, warm
static const Vec3 C_APRON_DARK(0.400f, 0.392f, 0.372f);
static const Vec3 C_ASPHALT(0.105f, 0.108f, 0.116f);
static const Vec3 C_ASPHALT_PATCH(0.073f, 0.078f, 0.086f);
static const Vec3 C_PERIM_WALL(0.505f, 0.492f, 0.462f);
static const Vec3 C_GROUND(0.440f, 0.450f, 0.415f);        // open ground beyond the apron
static const Vec3 C_KERB(0.560f, 0.552f, 0.528f);

//  SECTION 4 - THE GARAGE SHELL

void Scene::drawParkingBay(const Mat4 &parent)
{
    const float halfW = PARK_BAY_W * 0.5f; // 1.55  (across, along Z)
    const float halfD = PARK_BAY_D * 0.5f; // 2.80  (into the wall, along X)
    const float y = 0.018f;                // a hair above the floor
    const float t = 0.020f;                // paint thickness

    // The two long side lines of the bay.
    drawCube(parent * place(Vec3(0.0f, y, -halfW), Vec3(PARK_BAY_D, t, 0.13f)), C_PARK_LINE, 0.80f);
    drawCube(parent * place(Vec3(0.0f, y, halfW), Vec3(PARK_BAY_D, t, 0.13f)), C_PARK_LINE, 0.80f);

    // The short line at the back of the bay (against the wall).
    drawCube(parent * place(Vec3(halfD, y, 0.0f), Vec3(0.13f, t, PARK_BAY_W)), C_PARK_LINE, 0.80f);

    // Two white wheel-stop stripes at the aisle end, like the reference photo.
    drawCube(parent * place(Vec3(-halfD + 0.30f, y, -0.95f), Vec3(0.55f, t, 0.10f)), C_WHITE_LINE, 0.55f);
    drawCube(parent * place(Vec3(-halfD + 0.30f, y, 0.95f), Vec3(0.55f, t, 0.10f)), C_WHITE_LINE, 0.55f);

    // A small charging bollard with a glowing indicator - the "smart" part.
    Mat4 bollard = parent * translate(halfD - 0.18f, 0.0f, -halfW + 0.20f);
    drawCube(bollard * place(Vec3(0.0f, 0.55f, 0.0f), Vec3(0.20f, 1.10f, 0.20f)), C_WALL_DARK);
    drawCube(bollard * place(Vec3(-0.11f, 0.90f, 0.0f), Vec3(0.02f, 0.26f, 0.12f)), C_NEON, 1.0f);
}

void Scene::drawGarage(const Mat4 &parent)
{
    // ---- Floor ------------------------------------------------------------

    drawCube(parent * place(Vec3(0.0f, -0.15f, 0.0f), Vec3(SHELL_W, 0.30f, SHELL_D)), C_FLOOR, 0.0f, 0.0f, 0.88f, MAT_CONCRETE);

    // Darker painted service bay under the lift.
    drawCube(parent * place(Vec3(0.0f, 0.012f, LIFT_Z), Vec3(8.4f, 0.02f, 9.0f)), C_FLOOR_BAY, 0.0f, 0.0f, 0.78f, MAT_PAINT);

    for (int side = -1; side <= 1; side += 2)
    {
        float x = 1.10f * static_cast<float>(side);
        drawCube(parent * place(Vec3(x, 0.018f, LIFT_Z + 0.20f), Vec3(0.10f, 0.012f, 5.60f)),
                 Vec3(0.105f, 0.107f, 0.112f), 0.0f, 0.0f, 0.94f, MAT_RUBBER);
        drawCube(parent * place(Vec3(x + 0.12f * static_cast<float>(side), 0.019f, LIFT_Z - 0.55f),
                                Vec3(0.045f, 0.010f, 3.10f), Vec3(0.0f, 2.0f * static_cast<float>(side), 0.0f)),
                 Vec3(0.125f, 0.126f, 0.130f), 0.0f, 0.0f, 0.92f, MAT_RUBBER);
    }
    for (int i = 0; i < 7; ++i)
    {
        float x = jitter(900 + i * 3, 3.0f);
        float z = LIFT_Z + jitter(910 + i * 5, 3.5f);
        drawCube(parent * place(Vec3(x, 0.020f, z), Vec3(0.020f, 0.009f, 0.45f + 0.18f * hash01(920 + i)),
                                Vec3(0.0f, jitter(930 + i, 48.0f), 0.0f)),
                 Vec3(0.31f, 0.315f, 0.325f));
    }

    // Amber outline around that service bay (four thin strips = a rectangle).
    drawCube(parent * place(Vec3(0.0f, 0.02f, LIFT_Z - 4.5f), Vec3(8.4f, 0.02f, 0.12f)), C_PARK_LINE, 0.55f);
    drawCube(parent * place(Vec3(0.0f, 0.02f, LIFT_Z + 4.5f), Vec3(8.4f, 0.02f, 0.12f)), C_PARK_LINE, 0.80f);
    drawCube(parent * place(Vec3(-4.2f, 0.02f, LIFT_Z), Vec3(0.12f, 0.02f, 9.0f)), C_PARK_LINE, 0.80f);
    drawCube(parent * place(Vec3(4.2f, 0.02f, LIFT_Z), Vec3(0.12f, 0.02f, 9.0f)), C_PARK_LINE, 0.80f);

    // Dashed centre line running the length of the aisle, entrance to lift.
    for (int i = 0; i < 6; ++i)
    {
        float z = 11.6f - static_cast<float>(i) * 1.55f;
        drawCube(parent * place(Vec3(0.0f, 0.02f, z), Vec3(0.13f, 0.02f, 0.85f)), C_NEON, 0.85f);
    }

    // ---- Back wall --------------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, GARAGE_H * 0.5f, -(WALL_Z + WALL_T * 0.5f)),
                            Vec3(SHELL_W, GARAGE_H, WALL_T)),
             C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);

    // Recessed dark panels give the flat wall some built scale.
    for (int i = 0; i < 4; ++i)
    {
        float x = -9.0f + static_cast<float>(i) * 6.0f;
        drawCube(parent * place(Vec3(x, 1.90f, -WALL_Z + 0.06f), Vec3(4.60f, 2.80f, 0.08f)), C_WALL_DARK);
    }

    // ---- Left and right walls ---------------------------------------------
    drawCube(parent * place(Vec3(-(WALL_X + WALL_T * 0.5f), GARAGE_H * 0.5f, 0.0f),
                            Vec3(WALL_T, GARAGE_H, SHELL_D)),
             C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);
    drawCube(parent * place(Vec3((WALL_X + WALL_T * 0.5f), GARAGE_H * 0.5f, 0.0f),
                            Vec3(WALL_T, GARAGE_H, SHELL_D)),
             C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);

    drawCube(parent * place(Vec3(0.0f, 0.16f, -WALL_Z + 0.03f), Vec3(SHELL_W, 0.32f, 0.06f)), C_WALL * 0.46f);
    drawCube(parent * place(Vec3(-WALL_X + 0.03f, 0.16f, 0.0f), Vec3(0.06f, 0.32f, SHELL_D)), C_WALL * 0.46f);
    drawCube(parent * place(Vec3(WALL_X - 0.03f, 0.16f, 0.0f), Vec3(0.06f, 0.32f, SHELL_D)), C_WALL * 0.46f);
    drawCube(parent * place(Vec3(0.0f, 0.34f, -WALL_Z + 0.04f), Vec3(SHELL_W, 0.20f, 0.04f)), C_WALL * 0.72f);

    // ---- Ceiling slab -----------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, CEIL_Y + 0.15f, 0.0f), Vec3(SHELL_W, 0.30f, SHELL_D)), C_CEILING, 0.0f, 0.0f, 0.84f, MAT_WALL);

    // ---- Ceiling beams (cross members) ------------------------------------
    for (int i = 0; i < 7; ++i)
    {
        float z = -10.5f + static_cast<float>(i) * 3.50f;
        drawCube(parent * place(Vec3(0.0f, CEIL_Y - 0.14f, z), Vec3(SHELL_W, 0.28f, 0.40f)), C_BEAM, 0.0f, 0.12f, 0.55f, MAT_PAINTED_METAL);
    }

    const float postW = (SHELL_W - DOOR_W) * 0.5f;    // 7.9
    const float postX = DOOR_W * 0.5f + postW * 0.5f; // 10.45

    drawCube(parent * place(Vec3(-postX, GARAGE_H * 0.5f, ENTRY_Z), Vec3(postW, GARAGE_H, 0.50f)), C_WALL_DARK);
    drawCube(parent * place(Vec3(postX, GARAGE_H * 0.5f, ENTRY_Z), Vec3(postW, GARAGE_H, 0.50f)), C_WALL_DARK);

    drawCube(parent * place(Vec3(0.0f, GARAGE_H - 0.22f, ENTRY_Z - 0.10f),
                            Vec3(DOOR_W + 1.2f, 0.44f, 0.70f)),
             C_WALL_DARK);
    drawCube(parent * place(Vec3(0.0f, GARAGE_H - 0.52f, ENTRY_Z + 0.16f),
                            Vec3(DOOR_W + 1.2f, 0.60f, 0.22f)),
             C_WALL_DARK);
    // Two cheek plates the drum shaft passes between.
    for (int s2 = 0; s2 < 2; ++s2)
    {
        float sx = (s2 == 0) ? -1.0f : 1.0f;
        drawCube(parent * place(Vec3(sx * (DOOR_W * 0.5f + 0.42f), DOOR_H + 0.85f, ENTRY_Z - 0.20f),
                                Vec3(0.12f, 1.30f, 0.80f)),
                 C_GALV * 0.70f);
    }

    // Amber safety strip under the header and guide rails either side.
    drawCube(parent * place(Vec3(0.0f, DOOR_H + 0.10f, ENTRY_Z - 0.78f), Vec3(DOOR_W - 0.4f, 0.09f, 0.06f)), C_NEON, 1.0f);
    // (the guide rails the door runs in are part of drawGarageDoor)
}


void Scene::drawGarageDoor(const Mat4 &parent, float doorOffset)
{

    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        float x = sx * (DOOR_W * 0.5f + 0.16f);

        drawCube(parent * place(Vec3(x, DOOR_H * 0.5f + 0.20f, DOOR_Z), Vec3(0.230f, DOOR_H + 0.40f, 0.075f)), C_GALV);
        drawCube(parent * place(Vec3(x - sx * 0.090f, DOOR_H * 0.5f + 0.20f, DOOR_Z + 0.075f), Vec3(0.050f, DOOR_H + 0.40f, 0.190f)), C_GALV * 0.86f);
        drawCube(parent * place(Vec3(x - sx * 0.090f, DOOR_H * 0.5f + 0.20f, DOOR_Z - 0.075f), Vec3(0.050f, DOOR_H + 0.40f, 0.190f)), C_GALV * 0.86f);


        for (int i = 0; i < 6; ++i)
        {
            float y = 0.45f + static_cast<float>(i) * 1.15f;
            drawCube(parent * place(Vec3(x + sx * 0.130f, y, DOOR_Z), Vec3(0.090f, 0.140f, 0.230f)), C_GALV);
            drawCylinder(parent * place(Vec3(x + sx * 0.130f, y + 0.045f, DOOR_Z), Vec3(0.045f, 0.035f, 0.045f)), C_ROBOT_DARK);
            drawCylinder(parent * place(Vec3(x + sx * 0.130f, y - 0.045f, DOOR_Z), Vec3(0.045f, 0.035f, 0.045f)), C_ROBOT_DARK);
        }
    }

    // ---- The drum housing the panels roll into -----------------------------
    const float DRUM_Y = DOOR_H + 0.95f; // centre of the roll
    const float DRUM_R = 0.52f;

    drawCylS(parent * translate(0.0f, DRUM_Y, DOOR_Z) * rotateZ(90.0f) * scale(0.180f, DOOR_W + 0.50f, 0.180f), C_STEEL); // the shaft

    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        drawCylS(parent * translate(sx * (DOOR_W * 0.5f + 0.12f), DRUM_Y, DOOR_Z) * rotateZ(90.0f) * scale(DRUM_R * 2.05f, 0.060f, DRUM_R * 2.05f), C_GALV);
        drawCube(parent * place(Vec3(sx * (DOOR_W * 0.5f + 0.26f), DRUM_Y, DOOR_Z), Vec3(0.150f, 0.330f, 0.330f)), C_GALV * 0.85f);
        drawCylinder(parent * place(Vec3(sx * (DOOR_W * 0.5f + 0.26f), DRUM_Y + 0.115f, DOOR_Z + 0.115f), Vec3(0.050f, 0.040f, 0.050f)), C_ROBOT_DARK);
        drawCylinder(parent * place(Vec3(sx * (DOOR_W * 0.5f + 0.26f), DRUM_Y - 0.115f, DOOR_Z + 0.115f), Vec3(0.050f, 0.040f, 0.050f)), C_ROBOT_DARK);
    }

    // ---- The five panels ---------------------------------------------------
    for (int i = 0; i < DOOR_PANELS; ++i)
    {
        // Where this panel's centre would be if it just slid straight up.
        float travel = DOOR_PANEL_H * (0.5f + static_cast<float>(i)) + doorOffset;

        // How far past the top of the opening it has gone.
        float over = travel - DOOR_H;

        Mat4 panel;
        bool rolled = (over > 0.0f);

        if (!rolled)
        {
            panel = parent * translate(0.0f, travel, DOOR_Z);
        }
        else
        {

            float ang = (over / DRUM_R) * 180.0f / PI_F;
            if (ang > 560.0f)
                ang = 560.0f; // the drum is full

            float r = DRUM_R + (ang / 360.0f) * 0.095f;

            panel = parent * translate(0.0f, DRUM_Y, DOOR_Z) * rotateX(ang) * translate(0.0f, -r, 0.0f) * rotateX(90.0f);
        }

        // ---- The panel itself, identical whether flat or curled -------------
        Vec3 face = (i == 0) ? C_DOOR * 0.94f : C_DOOR; // bottom panel is scuffed

        drawCube(panel * scale(DOOR_W, DOOR_PANEL_H * 0.96f, 0.070f), face);

        // Three pressed ribs across each panel.
        for (int r = 0; r < 3; ++r)
            drawCube(panel * translate(0.0f, (static_cast<float>(r) - 1.0f) * 0.310f, 0.042f) * scale(DOOR_W - 0.10f, 0.070f, 0.022f), C_DOOR_RIB);

        // The hinge line between this panel and the one below it.
        drawCube(panel * translate(0.0f, -DOOR_PANEL_H * 0.48f, 0.030f) * scale(DOOR_W - 0.06f, 0.040f, 0.055f), C_DOOR_RIB * 0.80f);
        for (int h = 0; h < 5; ++h)
            drawCube(panel * translate((static_cast<float>(h) - 2.0f) * 2.55f, -DOOR_PANEL_H * 0.48f, 0.058f) * scale(0.230f, 0.075f, 0.050f), C_GALV);

        // Roller wheels: one at each edge, riding in the guide rail.
        for (int s = 0; s < 2; ++s)
        {
            float sx = (s == 0) ? -1.0f : 1.0f;
            drawCylinder(panel * translate(sx * (DOOR_W * 0.5f + 0.055f), -DOOR_PANEL_H * 0.30f, 0.0f) * rotateZ(90.0f) * scale(0.140f, 0.060f, 0.140f), C_ROBOT_DARK);
            drawCube(panel * translate(sx * (DOOR_W * 0.5f + 0.020f), -DOOR_PANEL_H * 0.30f, 0.0f) * scale(0.075f, 0.075f, 0.075f), C_GALV);
        }

        if (i == 3)
        {
            // Vision slots in the fourth panel, as most shutter doors have.
            for (int w = 0; w < 5; ++w)
                drawCube(panel * translate((static_cast<float>(w) - 2.0f) * 2.45f, 0.150f, -0.040f) * scale(1.55f, 0.360f, 0.020f), C_GLASS);
        }

        if (i == 0)
        {
            // Bottom panel: rubber weather seal and a hazard band.
            drawCube(panel * translate(0.0f, -DOOR_PANEL_H * 0.50f, 0.0f) * scale(DOOR_W, 0.075f, 0.110f), C_RUBBER);

            for (int b = 0; b < 12; ++b)
                drawCube(panel * translate((static_cast<float>(b) - 5.5f) * 1.05f, -0.290f, 0.045f) * rotateZ(38.0f) * scale(0.560f, 0.190f, 0.020f),
                         (b & 1) ? C_HAZARD_Y : C_HAZARD_K);

            // Lift handle.
            drawCylS(panel * translate(0.0f, -0.120f, 0.070f) * rotateZ(90.0f) * scale(0.050f, 0.620f, 0.050f), C_GALV);
        }
    }

    // ---- Chain drive and motor, top right corner ---------------------------
    const float MX = DOOR_W * 0.5f + 0.42f;

    drawCylS(parent * translate(MX, DRUM_Y, DOOR_Z - 0.16f) * rotateZ(90.0f) * scale(0.360f, 0.070f, 0.360f), C_ROBOT_DARK); // sprocket
    for (int i = 0; i < 16; ++i)                                                                                             // sprocket teeth
        drawCube(parent * translate(MX, DRUM_Y, DOOR_Z - 0.16f) * rotateX(static_cast<float>(i) * 22.5f) * translate(0.0f, 0.195f, 0.0f) * scale(0.080f, 0.050f, 0.045f), C_ROBOT_DARK);

    // The chain hanging down to the motor, as a column of small links.
    for (int i = 0; i < 9; ++i)
        drawCube(parent * place(Vec3(MX, DRUM_Y - 0.28f - static_cast<float>(i) * 0.175f, DOOR_Z - 0.16f),
                                Vec3(0.045f, 0.130f, 0.045f)),
                 C_ROBOT_DARK);

    // Motor and gearbox bolted to the wall.
    drawCube(parent * place(Vec3(MX, DRUM_Y - 1.90f, DOOR_Z - 0.16f), Vec3(0.420f, 0.480f, 0.420f)), C_LIFT_BLUE_D);
    drawCylS(parent * translate(MX, DRUM_Y - 1.90f, DOOR_Z - 0.06f) * rotateX(90.0f) * scale(0.300f, 0.240f, 0.300f), C_LIFT_BLUE_D * 1.15f);
    for (int i = 0; i < 4; ++i)
        drawCylinder(parent * place(Vec3(MX + ((i & 1) ? 0.16f : -0.16f), DRUM_Y - 1.90f + ((i & 2) ? 0.19f : -0.19f), DOOR_Z + 0.08f),
                                    Vec3(0.055f, 0.040f, 0.055f)),
                     C_CHROME);
    // Conduit from the motor to a control box beside the opening.
    drawCube(parent * place(Vec3(MX, DRUM_Y - 2.85f, DOOR_Z - 0.16f), Vec3(0.055f, 1.45f, 0.055f)), C_GALV);
    drawCube(parent * place(Vec3(MX, DRUM_Y - 3.70f, DOOR_Z - 0.16f), Vec3(0.230f, 0.330f, 0.140f)), C_GALV);
    drawCube(parent * place(Vec3(MX - 0.125f, DRUM_Y - 3.66f, DOOR_Z - 0.16f), Vec3(0.020f, 0.075f, 0.075f)), C_NEON_CYAN, 0.9f);
}

void Scene::drawExterior(const Mat4 &parent)
{

    const float TH = WALL_Z + WALL_T; // z of the threshold (13.4)

    const float COURT_HALF_W = 10.00f;                 // forecourt half width
    const float APRON_D = 6.00f;                       // apron depth, threshold to kerb
    const float ROAD_D = 8.00f;                        // carriageway depth
    const float ROAD_Z = TH + APRON_D + ROAD_D * 0.5f; // centre line
    const float FAR_Z = TH + APRON_D + ROAD_D;         // far kerb

    drawCube(parent * place(Vec3(0.0f, -0.145f, TH + APRON_D * 0.5f),
                            Vec3(COURT_HALF_W * 2.0f, 0.30f, APRON_D)),
             C_APRON, 0.0f, 0.0f, 0.86f, MAT_CONCRETE);

    // Expansion joints, every 2.6 m across plus two lengthways.
    for (int i = 0; i < 3; ++i)
        drawCube(parent * place(Vec3(0.0f, 0.007f, TH + 1.6f + static_cast<float>(i) * 2.6f),
                                Vec3(COURT_HALF_W * 2.0f, 0.016f, 0.055f)),
                 C_APRON_DARK);
    drawCube(parent * place(Vec3(-5.2f, 0.007f, TH + APRON_D * 0.5f), Vec3(0.055f, 0.016f, APRON_D)), C_APRON_DARK);
    drawCube(parent * place(Vec3(5.2f, 0.007f, TH + APRON_D * 0.5f), Vec3(0.055f, 0.016f, APRON_D)), C_APRON_DARK);

    for (int side = -1; side <= 1; side += 2)
        drawCube(parent * place(Vec3(static_cast<float>(side) * 2.60f, 0.004f, TH + APRON_D * 0.5f),
                                Vec3(0.110f, 0.008f, APRON_D)),
                 C_WHITE_LINE * 0.70f);

    // ---- 3. Kerb and gullies at the edge of the apron ----------------------
    drawCube(parent * place(Vec3(0.0f, 0.075f, TH + APRON_D), Vec3(COURT_HALF_W * 2.0f, 0.150f, 0.300f)), C_KERB);
    for (int i = 0; i < 3; ++i)
    {
        float x = -6.0f + static_cast<float>(i) * 6.0f;
        drawCube(parent * place(Vec3(x, 0.020f, TH + APRON_D - 0.45f), Vec3(0.780f, 0.040f, 0.420f)), C_GALV * 0.80f);
        for (int b = 0; b < 5; ++b)
            drawCube(parent * place(Vec3(x - 0.30f + static_cast<float>(b) * 0.15f, 0.034f, TH + APRON_D - 0.45f),
                                    Vec3(0.055f, 0.030f, 0.420f)),
                     C_GALV);
    }

    // ---- 4. The through road -----------------------------------------------
    drawCube(parent * place(Vec3(0.0f, -0.16f, ROAD_Z),
                            Vec3(COURT_HALF_W * 2.0f + 4.0f, 0.30f, ROAD_D)),
             C_ASPHALT, 0.0f, 0.0f, 0.97f, MAT_ASPHALT);

    for (int i = 0; i < 5; ++i)
    {
        float x = jitter(i * 9 + 1, 9.0f);
        float z = ROAD_Z + jitter(i * 9 + 2, 3.2f);
        drawCube(parent * place(Vec3(x, 0.004f, z), Vec3(1.8f + hash01(i * 9) * 2.0f, 0.012f, 1.3f + hash01(i * 9 + 3) * 1.5f),
                                Vec3(0.0f, jitter(i * 9 + 4, 30.0f), 0.0f)),
                 C_ASPHALT_PATCH, 0.0f, 0.0f, 0.98f, MAT_ASPHALT);
    }
    for (int i = 0; i < 20; ++i)
    {
        float x = jitter(700 + i * 5, 11.0f);
        float z = ROAD_Z + jitter(710 + i * 7, 3.8f);
        float sz = 0.035f + hash01(720 + i * 11) * 0.075f;
        Vec3 stone = (i % 3 == 0) ? Vec3(0.18f, 0.18f, 0.19f) : Vec3(0.135f, 0.138f, 0.145f);
        drawCube(parent * place(Vec3(x, 0.009f, z), Vec3(sz, 0.012f, sz * 0.70f),
                                Vec3(0.0f, jitter(730 + i, 60.0f), 0.0f)),
                 stone, 0.0f, 0.0f, 0.94f, MAT_ASPHALT);
    }

    // Centre line, dashed, running the length of the carriageway.
    for (int i = 0; i < 9; ++i)
        drawCube(parent * place(Vec3(-11.2f + static_cast<float>(i) * 2.8f, 0.010f, ROAD_Z),
                                Vec3(1.40f, 0.018f, 0.160f)),
                 C_WHITE_LINE * 0.70f);
    // Edge lines.
    drawCube(parent * place(Vec3(0.0f, 0.010f, ROAD_Z - ROAD_D * 0.5f + 0.45f),
                            Vec3(COURT_HALF_W * 2.0f + 3.0f, 0.016f, 0.120f)),
             C_WHITE_LINE * 0.55f);
    drawCube(parent * place(Vec3(0.0f, 0.010f, ROAD_Z + ROAD_D * 0.5f - 0.45f),
                            Vec3(COURT_HALF_W * 2.0f + 3.0f, 0.016f, 0.120f)),
             C_WHITE_LINE * 0.55f);

    // Far kerb.
    drawCube(parent * place(Vec3(0.0f, 0.075f, FAR_Z), Vec3(COURT_HALF_W * 2.0f + 4.0f, 0.150f, 0.300f)), C_KERB);

    // ---- 5. Boundary walls with piers -------------------------------------
    for (int side = -1; side <= 1; side += 2)
    {
        const float sx = static_cast<float>(side) * COURT_HALF_W;
        drawCube(parent * place(Vec3(sx, 1.35f, TH + APRON_D * 0.5f), Vec3(0.300f, 2.70f, APRON_D)),
                 C_PERIM_WALL, 0.0f, 0.0f, 0.88f, MAT_CONCRETE);
        drawCube(parent * place(Vec3(sx, 2.76f, TH + APRON_D * 0.5f), Vec3(0.420f, 0.120f, APRON_D)), C_PERIM_WALL * 1.10f);
        for (int i = 0; i < 3; ++i)
            drawCube(parent * place(Vec3(sx, 1.45f, TH + 1.2f + static_cast<float>(i) * 3.0f), Vec3(0.440f, 2.90f, 0.420f)),
                     C_PERIM_WALL * 1.06f);
    }
    drawCube(parent * place(Vec3(0.0f, 1.20f, FAR_Z + 1.60f), Vec3(COURT_HALF_W * 2.0f + 5.0f, 2.40f, 0.300f)),
             C_PERIM_WALL, 0.0f, 0.0f, 0.88f, MAT_CONCRETE);
    drawCube(parent * place(Vec3(0.0f, 2.46f, FAR_Z + 1.60f), Vec3(COURT_HALF_W * 2.0f + 5.0f, 0.120f, 0.420f)), C_PERIM_WALL * 1.10f);
    for (int i = 0; i < 7; ++i)
        drawCube(parent * place(Vec3(-10.5f + static_cast<float>(i) * 3.5f, 1.32f, FAR_Z + 1.60f), Vec3(0.420f, 2.64f, 0.460f)),
                 C_PERIM_WALL * 1.06f);

    drawCube(parent * place(Vec3(0.0f, -0.17f, 0.0f), Vec3(900.0f, 0.30f, 900.0f)),
             C_GROUND, 0.0f, 0.0f, 0.92f, MAT_CONCRETE);
}

//  SECTION 6 - DOUBLE-SCISSOR HYDRAULIC LIFT

static float scissorAngle(float liftHeight)
{
    float h = liftHeight;
    if (h < 0.0f)
        h = 0.0f;
    if (h > LIFT_MAX_H)
        h = LIFT_MAX_H;

    float rise = LIFT_REST_H + h;

    // rise = 2 * L * sin(theta)   ->   sin(theta) = rise / (2L)
    float s = rise / (2.0f * LIFT_ARM_LEN);
    if (s > 0.985f)
        s = 0.985f; // never let the arms go fully vertical
    if (s < 0.0f)
        s = 0.0f;

    return asinf(s);
}

float Scene::carDriveDistance() { return CAR_DRIVE_DISTANCE; }
float Scene::carWheelRadius() { return CAR_FWHEEL_R; }

float Scene::liftTopY(float liftHeight)
{

    float rise = 2.0f * LIFT_ARM_LEN * sinf(scissorAngle(liftHeight));
    return LIFT_BASE_TOP + rise + LIFT_PLAT_T + LIFT_PAD_T;
}

static void drawScissorStack(const Mat4 &lift, float x, float theta)
{
    using namespace Scene;

    const float thetaDeg = theta * 180.0f / PI_F;
    const float rise = LIFT_ARM_LEN * sinf(theta); // height gained per stage
    const float span = LIFT_ARM_LEN * cosf(theta); // pivot-to-pivot along Z
    const float halfSpan = span * 0.5f;

    const float armOff = 0.12f;

    for (int stage = 0; stage < 2; ++stage)
    {
        // Height of the two BOTTOM pivots of this stage.
        float yBot = LIFT_BASE_TOP + static_cast<float>(stage) * rise;
        float yMid = yBot + rise * 0.5f; // where the two arms cross

        drawCube(lift * place(Vec3(x - armOff, yMid, 0.0f),
                              Vec3(LIFT_ARM_W, LIFT_ARM_T, LIFT_ARM_LEN),
                              Vec3(-thetaDeg, 0.0f, 0.0f)),
                 C_LIFT_BLUE);

        drawCube(lift * place(Vec3(x + armOff, yMid, 0.0f),
                              Vec3(LIFT_ARM_W, LIFT_ARM_T, LIFT_ARM_LEN),
                              Vec3(thetaDeg, 0.0f, 0.0f)),
                 C_LIFT_BLUE);

        drawCylinder(lift * translate(x, yMid, 0.0f) * rotateZ(90.0f) * scale(0.17f, 0.46f, 0.17f), C_LIFT_DARK);
        drawCylinder(lift * translate(x, yMid, 0.0f) * rotateZ(90.0f) * scale(0.09f, 0.52f, 0.09f), C_CHROME);

        // ---- The four pivot pins of this stage ------------------------------
        const float pinZ[2] = {-halfSpan, halfSpan};
        const float pinY[2] = {yBot, yBot + rise};
        for (int a = 0; a < 2; ++a)
        {
            for (int b = 0; b < 2; ++b)
            {
                drawCylinder(lift * translate(x, pinY[a], pinZ[b]) * rotateZ(90.0f) * scale(0.15f, 0.54f, 0.15f), C_LIFT_DARK);

                drawCylinder(lift * translate(x - 0.19f, pinY[a], pinZ[b]) * rotateZ(90.0f) * scale(0.195f, 0.075f, 0.195f), C_STEEL);
                drawCylinder(lift * translate(x + 0.19f, pinY[a], pinZ[b]) * rotateZ(90.0f) * scale(0.195f, 0.075f, 0.195f), C_STEEL);
            }
        }

        drawCylinder(lift * translate(x, yBot + 0.02f, halfSpan) * rotateZ(90.0f) * scale(0.20f, 0.30f, 0.20f), C_LIFT_YELLOW);

        // ---- Connecting plate between the two stages ---------------------------
        if (stage == 0)
        {
            drawCube(lift * place(Vec3(x, yBot + rise, -halfSpan), Vec3(0.40f, 0.16f, 0.46f)), C_LIFT_BLUE_D);
            drawCube(lift * place(Vec3(x, yBot + rise, halfSpan), Vec3(0.40f, 0.16f, 0.46f)), C_LIFT_BLUE_D);
        }
    }

    {
        const float d = 90.0f - thetaDeg;
        const float sy = sinf(theta), cz = cosf(theta);

        // Bottom pivot of arm A in the lift's frame.
        const float bx = x, by = LIFT_BASE_TOP, bz = -halfSpan;

        auto along = [&](float f)
        {
            return Vec3(bx, by + f * LIFT_ARM_LEN * sy, bz + f * LIFT_ARM_LEN * cz);
        };

        // Polished rod - thin, bright, runs most of the way up.
        drawCylinder(lift * translate(along(0.52f)) * rotateX(d) * scale(0.105f, LIFT_ARM_LEN * 0.84f, 0.105f), C_CHROME);

        // Cylinder barrel - fat and dark, covering the lower half.
        drawCylinder(lift * translate(along(0.34f)) * rotateX(d) * scale(0.215f, LIFT_ARM_LEN * 0.52f, 0.215f), C_LIFT_DARK);

        // End caps and the hose fitting.
        drawCylinder(lift * translate(along(0.09f)) * rotateX(d) * scale(0.25f, 0.14f, 0.25f), C_LIFT_BLUE_D);
        drawCylinder(lift * translate(along(0.60f)) * rotateX(d) * scale(0.23f, 0.12f, 0.23f), C_LIFT_BLUE_D);
        drawCube(lift * place(Vec3(bx, by + 0.30f, bz + 0.18f), Vec3(0.09f, 0.42f, 0.09f)), C_LIFT_DARK);
    }
}

void Scene::drawHydraulicLift(const Mat4 &parent, float liftHeight)
{

    Mat4 lift = parent * translate(0.0f, 0.0f, LIFT_Z);

    const float theta = scissorAngle(liftHeight);
    const float rise = LIFT_ARM_LEN * sinf(theta);

    // ---- Base frame bolted to the floor ------------------------------------
    for (int s = 0; s < 2; ++s)
    {
        float x = (s == 0) ? -LIFT_TRACK : LIFT_TRACK;

        // Long base rail under each scissor stack.
        drawCube(lift * place(Vec3(x, LIFT_BASE_TOP * 0.5f, 0.0f),
                              Vec3(0.66f, LIFT_BASE_TOP, LIFT_ARM_LEN + 0.60f)),
                 C_LIFT_BLUE);

        drawCube(lift * place(Vec3(x + 0.335f, LIFT_BASE_TOP - 0.012f, 0.0f),
                              Vec3(0.020f, 0.020f, LIFT_ARM_LEN + 0.58f)),
                 C_CHROME * 0.85f);

        // Row of bolt heads securing the rail to the floor.
        for (int i = 0; i < 5; ++i)
        {
            float z = -0.42f * LIFT_ARM_LEN + static_cast<float>(i) * (0.84f * LIFT_ARM_LEN / 4.0f);
            drawCylinder(lift * translate(x, LIFT_BASE_TOP + 0.008f, z) * scale(0.045f, 0.020f, 0.045f), C_CHROME * 0.70f);
        }

        // Dark slide track the rolling pivot runs in.
        drawCube(lift * place(Vec3(x, LIFT_BASE_TOP + 0.015f, 0.55f),
                              Vec3(0.44f, 0.04f, LIFT_ARM_LEN * 0.72f)),
                 C_LIFT_DARK);

        // Four anchor feet, each a proper 4-bolt anchor plate.
        for (int i = 0; i < 2; ++i)
        {
            float z = (i == 0) ? -(LIFT_ARM_LEN * 0.5f) : (LIFT_ARM_LEN * 0.5f);
            drawCube(lift * place(Vec3(x, 0.035f, z), Vec3(0.86f, 0.07f, 0.52f)), C_LIFT_BLUE_D);
            for (int cx = -1; cx <= 1; cx += 2)
                for (int cz = -1; cz <= 1; cz += 2)
                    drawCylinder(lift * translate(x + 0.33f * static_cast<float>(cx), 0.075f, z + 0.19f * static_cast<float>(cz)) * scale(0.055f, 0.045f, 0.055f), C_CHROME);
        }
    }

    // Two cross members tying the left and right base rails together.
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP * 0.5f, -1.80f), Vec3(2.60f, 0.18f, 0.34f)), C_LIFT_BLUE);
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP * 0.5f, 1.80f), Vec3(2.60f, 0.18f, 0.34f)), C_LIFT_BLUE);
    // Bevel strip along the top edge of each cross member.
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP - 0.010f, -1.80f), Vec3(2.55f, 0.018f, 0.020f)), C_CHROME * 0.80f);
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP - 0.010f, 1.80f), Vec3(2.55f, 0.018f, 0.020f)), C_CHROME * 0.80f);

    drawScissorStack(lift, -LIFT_TRACK, theta);
    drawScissorStack(lift, LIFT_TRACK, theta);

    // ---- The lifting platform -----------------------------------------------
    const float platY = LIFT_BASE_TOP + 2.0f * rise; // underside of the beam
    const float padY = platY + LIFT_PLAT_T;          // top of the blue beam
    const float topY = padY + LIFT_PAD_T;            // the surface wheels rest on

    // Cross beams under the platform, linking the two runways.
    drawCube(lift * place(Vec3(0.0f, platY + LIFT_PLAT_T * 0.5f, -1.55f), Vec3(2.70f, 0.16f, 0.34f)), C_LIFT_BLUE_D);
    drawCube(lift * place(Vec3(0.0f, platY + LIFT_PLAT_T * 0.5f, 1.55f), Vec3(2.70f, 0.16f, 0.34f)), C_LIFT_BLUE_D);

    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        float x = sx * LIFT_TRACK;

        // Blue structural beam.
        drawCube(lift * place(Vec3(x, platY + LIFT_PLAT_T * 0.5f, 0.0f),
                              Vec3(0.88f, LIFT_PLAT_T, LIFT_PAD_L)),
                 C_LIFT_BLUE);

        drawCube(lift * place(Vec3(x, padY + LIFT_PAD_T * 0.5f, 0.0f),
                              Vec3(1.00f, LIFT_PAD_T, LIFT_PAD_L)),
                 C_LIFT_YELLOW,
                 0.0f, 0.55f, 0.46f, MAT_TREAD);

        drawCube(lift * place(Vec3(x - sx * 0.49f, topY - 0.006f, 0.0f),
                              Vec3(0.020f, 0.020f, LIFT_PAD_L)),
                 C_CHROME * 0.80f);

        for (int i = 0; i < 5; ++i)
        {
            float z = -1.80f + static_cast<float>(i) * 0.90f;
            drawCube(lift * place(Vec3(x, topY + 0.006f, z), Vec3(0.90f, 0.012f, 0.10f)), C_LIFT_DARK);
        }
    }
}

void Scene::drawLiftApproachRamp(const Mat4 &parent)
{
    const float top = liftTopY(0.0f); // platform height at rest
    const float run = LIFT_RAMP_LEN;
    const float slopeDeg = std::atan2(top, run) * 180.0f / PI_F;
    const float hyp = std::sqrt(top * top + run * run);

    // The ramp meets the platform at the +Z end of the runway pad.
    const float zTop = LIFT_Z + LIFT_PAD_L * 0.5f;
    const float zMid = zTop + run * 0.5f;

    for (int s = 0; s < 2; ++s)
    {
        const float sx = (s == 0) ? -1.0f : 1.0f;
        const float x = sx * LIFT_TRACK;

        drawCube(parent * place(Vec3(x, top * 0.5f, zMid),
                                Vec3(1.00f, 0.075f, hyp), Vec3(slopeDeg, 0.0f, 0.0f)),
                 C_LIFT_YELLOW, 0.0f, 0.55f, 0.46f, MAT_TREAD);

        // Guide kerb along the outer edge, so a tyre cannot run off the side.
        drawCube(parent * place(Vec3(x + sx * 0.47f, top * 0.5f + 0.055f, zMid),
                                Vec3(0.070f, 0.075f, hyp), Vec3(slopeDeg, 0.0f, 0.0f)),
                 C_LIFT_YELLOW * 0.88f);

        // Supporting legs under the deck, three per side.
        for (int i = 1; i <= 3; ++i)
        {
            const float t = static_cast<float>(i) / 4.0f; // 0 at the top
            const float z = zTop + run * t;
            const float h = top * (1.0f - t);
            if (h < 0.08f)
                continue;
            drawCube(parent * place(Vec3(x, h * 0.5f, z), Vec3(0.60f, h, 0.10f)),
                     C_LIFT_BLUE_D, 0.0f, 0.35f, 0.58f);
        }

        // Toe plate where the deck meets the slab, so it does not end in a
        // knife edge floating a few millimetres above the concrete.
        drawCube(parent * place(Vec3(x, 0.012f, zTop + run + 0.10f),
                                Vec3(1.00f, 0.024f, 0.34f)),
                 C_LIFT_YELLOW * 0.80f,
                 0.0f, 0.55f, 0.46f, MAT_TREAD);

        // Black and yellow hazard banding across the foot of the ramp.
        for (int i = 0; i < 6; ++i)
            drawCube(parent * place(Vec3(x - 0.40f + static_cast<float>(i) * 0.16f, 0.026f,
                                         zTop + run + 0.10f),
                                    Vec3(0.075f, 0.010f, 0.34f),
                                    Vec3(0.0f, 32.0f, 0.0f)),
                     C_LIFT_DARK);
    }
}

void Scene::drawWheel(const Mat4 &parent, const Vec3 &localPos, float radius, float width,
                      float outward, float spinDeg)
{
    Mat4 hub = parent * translate(localPos) * rotateZ(90.0f) * rotateY(spinDeg);
    const float D = radius * 2.0f;
    const float s = outward;

    drawCylS(hub * scale(D, width * 0.56f, D),
             C_TIRE, 0.0f, 0.0f, 0.96f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.992f, width * 0.68f, D * 0.992f),
             C_TIRE * 1.02f, 0.0f, 0.0f, 0.95f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.978f, width * 0.78f, D * 0.978f),
             C_TIRE_WALL, 0.0f, 0.0f, 0.94f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.952f, width * 0.86f, D * 0.952f),
             C_TIRE_WALL * 1.03f, 0.0f, 0.0f, 0.93f, MAT_TIRE);
    drawCylS(hub * translate(0.0f, s * width * 0.445f, 0.0f) * scale(D * 0.900f, width * 0.030f, D * 0.900f),
             C_TIRE_WALL * 1.08f, 0.0f, 0.0f, 0.90f, MAT_TIRE);

    for (int i = 0; i < 16; ++i)
    {
        Mat4 lug = hub * rotateY(static_cast<float>(i) * 22.5f) * translate(radius * 0.965f, 0.0f, 0.0f);
        drawCube(lug * scale(radius * 0.105f, width * 0.62f, radius * 0.145f),
                 C_TIRE * 0.82f, 0.0f, 0.0f, 0.98f, MAT_TIRE);
    }

    const Vec3 coverDark = Vec3(0.062f, 0.065f, 0.072f);
    const Vec3 coverSpoke = Vec3(0.232f, 0.240f, 0.258f);
    const Vec3 coverRing = Vec3(0.330f, 0.340f, 0.360f);

    // Recessed web, sitting back from the rim face.
    drawCylS(hub * translate(0.0f, s * width * 0.455f, 0.0f) * scale(D * 0.775f, width * 0.030f, D * 0.775f),
             coverDark, 0.0f, 0.20f, 0.56f, MAT_PAINTED_METAL);

    // Raised outer ring - the bright edge that defines the wheel's circle.
    drawCylS(hub * translate(0.0f, s * width * 0.498f, 0.0f) * scale(D * 0.800f, width * 0.052f, D * 0.800f),
             coverRing, 0.0f, 0.45f, 0.36f, MAT_PAINTED_METAL);
    drawCylS(hub * translate(0.0f, s * width * 0.502f, 0.0f) * scale(D * 0.735f, width * 0.050f, D * 0.735f),
             coverDark * 0.90f, 0.0f, 0.18f, 0.58f, MAT_PAINTED_METAL);

    // Seven broad flat-topped spokes, each tapering towards the hub.
    for (int i = 0; i < 7; ++i)
    {
        Mat4 blade = hub * translate(0.0f, s * width * 0.480f, 0.0f) * rotateY(static_cast<float>(i) * (360.0f / 7.0f));
        drawCube(blade * translate(D * 0.255f, 0.0f, 0.0f) * scale(D * 0.300f, width * 0.044f, D * 0.150f),
                 coverSpoke, 0.0f, 0.35f, 0.44f, MAT_PAINTED_METAL);
        drawCube(blade * translate(D * 0.130f, 0.0f, 0.0f) * scale(D * 0.180f, width * 0.042f, D * 0.098f),
                 coverSpoke * 0.86f, 0.0f, 0.32f, 0.48f, MAT_PAINTED_METAL);
    }

    // Centre cap with a machined lip.
    drawCylS(hub * translate(0.0f, s * width * 0.515f, 0.0f) * scale(D * 0.225f, width * 0.040f, D * 0.225f),
             coverSpoke * 0.78f, 0.0f, 0.40f, 0.42f, MAT_PAINTED_METAL);
    drawCylS(hub * translate(0.0f, s * width * 0.530f, 0.0f) * scale(D * 0.150f, width * 0.028f, D * 0.150f),
             coverRing * 1.15f, 0.0f, 0.60f, 0.28f, MAT_PAINTED_METAL);

    drawCylS(hub * translate(0.0f, -s * width * 0.300f, 0.0f) * scale(D * 0.555f, width * 0.045f, D * 0.555f),
             C_BRAKE_DISC * 0.62f, 0.0f, 0.76f, 0.46f, MAT_BRAKE);
}

namespace
{
    struct TruckMeshBatch
    {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int count = 0;
        bool trace = false;          // used by the ray-traced inspection monitor
        std::vector<float> cpu;      // CPU copy: 6 floats per vertex (position, normal)
    };

    static TruckMeshBatch gTruckSideMesh;
    static TruckMeshBatch gTruckBodyMesh;
    static TruckMeshBatch gTruckGlassMesh;
    static TruckMeshBatch gTruckDarkTopMesh;
    static TruckMeshBatch gTruckTrimMesh;
    static TruckMeshBatch gTruckFenderMesh;
    static TruckMeshBatch gTruckBumperMesh;
    static TruckMeshBatch gTruckFrontLightMesh;
    static TruckMeshBatch gTruckRearLightMesh;
    static TruckMeshBatch gTruckMirrorHousingMesh;
    static TruckMeshBatch gTruckMirrorGlassMesh;
    static TruckMeshBatch gTruckLightRecessMesh;
    static TruckMeshBatch gTruckFasciaMesh;
    static bool gTruckMeshReady = false;

    static void truckPushVertex(std::vector<float> &out, const Vec3 &p, const Vec3 &n)
    {
        out.push_back(p.x);
        out.push_back(p.y);
        out.push_back(p.z);
        out.push_back(n.x);
        out.push_back(n.y);
        out.push_back(n.z);
    }

    static void truckPushTriN(std::vector<float> &out,
                              const Vec3 &a, const Vec3 &b, const Vec3 &c,
                              const Vec3 &n)
    {
        truckPushVertex(out, a, n);
        truckPushVertex(out, b, n);
        truckPushVertex(out, c, n);
    }

    static void truckPushQuadN(std::vector<float> &out,
                               const Vec3 &a, const Vec3 &b,
                               const Vec3 &c, const Vec3 &d,
                               const Vec3 &n)
    {
        truckPushTriN(out, a, b, c, n);
        truckPushTriN(out, a, c, d, n);
    }

    static Vec3 truckFaceNormal(const Vec3 &a, const Vec3 &b, const Vec3 &c,
                                const Vec3 &preferredDirection)
    {
        Vec3 n = normalize(cross(b - a, c - a));
        if (dot(n, preferredDirection) < 0.0f)
            n = -n;
        return n;
    }

    static void truckPushQuadAuto(std::vector<float> &out,
                                  const Vec3 &a, const Vec3 &b,
                                  const Vec3 &c, const Vec3 &d,
                                  const Vec3 &preferredDirection)
    {
        Vec3 n = truckFaceNormal(a, b, c, preferredDirection);
        truckPushQuadN(out, a, b, c, d, n);
    }

    static void truckPushTriAuto(std::vector<float> &out,
                                 const Vec3 &a, const Vec3 &b, const Vec3 &c,
                                 const Vec3 &preferredDirection)
    {
        Vec3 n = truckFaceNormal(a, b, c, preferredDirection);
        truckPushTriN(out, a, b, c, n);
    }

    static void truckPushFoldRib(std::vector<float> &out,
                                 const Vec3 &p0, const Vec3 &p1,
                                 const Vec3 &nA, const Vec3 &nB,
                                 float width, float lift)
    {
        Vec3 t = p1 - p0;
        if (dot(t, t) < 1e-9f)
            return;
        t = normalize(t);
        Vec3 bis = normalize(nA + nB);

        // In-plane directions that point AWAY from the opposite face.
        Vec3 dA = normalize(cross(nA, t));
        if (dot(dA, nB) > 0.0f)
            dA = -dA;
        Vec3 dB = normalize(cross(nB, t));
        if (dot(dB, nA) > 0.0f)
            dB = -dB;

        const Vec3 a0 = p0 + dA * width + nA * (lift * 0.25f);
        const Vec3 a1 = p1 + dA * width + nA * (lift * 0.25f);
        const Vec3 b0 = p0 + dB * width + nB * (lift * 0.25f);
        const Vec3 b1 = p1 + dB * width + nB * (lift * 0.25f);
        const Vec3 c0 = p0 + bis * lift;
        const Vec3 c1 = p1 + bis * lift;

        truckPushQuadAuto(out, a0, a1, c1, c0, normalize(nA + bis));
        truckPushQuadAuto(out, c0, c1, b1, b0, normalize(nB + bis));
    }

    struct CtProfilePoint
    {
        float z;
        float y;
    };

    static void truckPushSidePanel(std::vector<float> &out, int side,
                                   const CtProfilePoint *pts, int count)
    {
        if (count < 3)
            return;
        const float sx = static_cast<float>(side);
        const float x = sx * CT_X_BODY;
        const Vec3 n(sx, 0.0f, 0.0f);
        for (int i = 1; i + 1 < count; ++i)
        {
            const Vec3 a(x, pts[0].y, pts[0].z);
            const Vec3 b(x, pts[i].y, pts[i].z);
            const Vec3 c(x, pts[i + 1].y, pts[i + 1].z);
            if (sx > 0.0f)
                truckPushTriN(out, a, b, c, n);
            else
                truckPushTriN(out, a, c, b, n);
        }
    }


    static void truckPushBox(std::vector<float> &out,
                             float x0, float x1,
                             float y0, float y1,
                             float z0, float z1)
    {
        truckPushQuadAuto(out,
                          Vec3(x0, y0, z0), Vec3(x1, y0, z0), Vec3(x1, y1, z0), Vec3(x0, y1, z0),
                          Vec3(0.0f, 0.0f, -1.0f));
        truckPushQuadAuto(out,
                          Vec3(x0, y0, z1), Vec3(x0, y1, z1), Vec3(x1, y1, z1), Vec3(x1, y0, z1),
                          Vec3(0.0f, 0.0f, 1.0f));
        truckPushQuadAuto(out,
                          Vec3(x0, y0, z0), Vec3(x0, y1, z0), Vec3(x0, y1, z1), Vec3(x0, y0, z1),
                          Vec3(-1.0f, 0.0f, 0.0f));
        truckPushQuadAuto(out,
                          Vec3(x1, y0, z1), Vec3(x1, y1, z1), Vec3(x1, y1, z0), Vec3(x1, y0, z0),
                          Vec3(1.0f, 0.0f, 0.0f));
        truckPushQuadAuto(out,
                          Vec3(x0, y1, z0), Vec3(x1, y1, z0), Vec3(x1, y1, z1), Vec3(x0, y1, z1),
                          Vec3(0.0f, 1.0f, 0.0f));
        truckPushQuadAuto(out,
                          Vec3(x0, y0, z1), Vec3(x1, y0, z1), Vec3(x1, y0, z0), Vec3(x0, y0, z0),
                          Vec3(0.0f, -1.0f, 0.0f));
    }

    static void truckPushFiveFacetFender(std::vector<float> &out,
                                         int side,
                                         float axleZ,
                                         float wheelRadius)
    {
        const float sx = static_cast<float>(side);
        const float xInner = sx * (CT_X_BODY + 0.002f);
        const float xOuter = sx * (CT_X_BODY + 0.088f);
        const Vec3 nSide(sx, 0.0f, 0.0f);

        struct YZ
        {
            float y;
            float z;
        };

        const int N = 8;
        const YZ outer[N] = {
            {0.235f, -0.690f},
            {wheelRadius + 0.020f, -0.665f},
            {wheelRadius + 0.330f, -0.580f},
            {wheelRadius + 0.600f, -0.335f},
            {wheelRadius + 0.600f, 0.335f},
            {wheelRadius + 0.330f, 0.580f},
            {wheelRadius + 0.020f, 0.665f},
            {0.235f, 0.690f}};
        const YZ inner[N] = {
            {0.235f, -0.595f},
            {wheelRadius + 0.015f, -0.570f},
            {wheelRadius + 0.280f, -0.475f},
            {wheelRadius + 0.505f, -0.268f},
            {wheelRadius + 0.505f, 0.268f},
            {wheelRadius + 0.280f, 0.475f},
            {wheelRadius + 0.015f, 0.570f},
            {0.235f, 0.595f}};

        for (int i = 0; i < N - 1; ++i)
        {
            Vec3 oo0(xOuter, outer[i].y, axleZ + outer[i].z);
            Vec3 oo1(xOuter, outer[i + 1].y, axleZ + outer[i + 1].z);
            Vec3 oi1(xOuter, inner[i + 1].y, axleZ + inner[i + 1].z);
            Vec3 oi0(xOuter, inner[i].y, axleZ + inner[i].z);

            Vec3 io0(xInner, outer[i].y, axleZ + outer[i].z);
            Vec3 io1(xInner, outer[i + 1].y, axleZ + outer[i + 1].z);
            Vec3 ii1(xInner, inner[i + 1].y, axleZ + inner[i + 1].z);
            Vec3 ii0(xInner, inner[i].y, axleZ + inner[i].z);

            // Visible outboard ring segment.
            truckPushQuadN(out, oo0, oo1, oi1, oi0, nSide);

            // Sealed inboard back face.
            truckPushQuadN(out, io0, ii0, ii1, io1, -nSide);

            // Outer perimeter wall - physically projects out from the steel.
            Vec3 outerPref(0.0f,
                           (outer[i].y + outer[i + 1].y) * 0.5f - wheelRadius,
                           (outer[i].z + outer[i + 1].z) * 0.5f);
            truckPushQuadAuto(out, io0, io1, oo1, oo0, outerPref);

            // Inner perimeter wall faces the tyre opening.
            Vec3 innerPref(0.0f,
                           -((inner[i].y + inner[i + 1].y) * 0.5f - wheelRadius),
                           -((inner[i].z + inner[i + 1].z) * 0.5f));
            truckPushQuadAuto(out, oi0, oi1, ii1, ii0, innerPref);
        }

        // Front and rear end caps close the extrusion completely.
        Vec3 ioF(xInner, outer[0].y, axleZ + outer[0].z);
        Vec3 ooF(xOuter, outer[0].y, axleZ + outer[0].z);
        Vec3 oiF(xOuter, inner[0].y, axleZ + inner[0].z);
        Vec3 iiF(xInner, inner[0].y, axleZ + inner[0].z);
        truckPushQuadAuto(out, ioF, ooF, oiF, iiF, Vec3(0.0f, 0.0f, -1.0f));

        Vec3 ioR(xInner, outer[N - 1].y, axleZ + outer[N - 1].z);
        Vec3 iiR(xInner, inner[N - 1].y, axleZ + inner[N - 1].z);
        Vec3 oiR(xOuter, inner[N - 1].y, axleZ + inner[N - 1].z);
        Vec3 ooR(xOuter, outer[N - 1].y, axleZ + outer[N - 1].z);
        truckPushQuadAuto(out, ioR, iiR, oiR, ooR, Vec3(0.0f, 0.0f, 1.0f));
    }

    static void truckUpload(TruckMeshBatch &mesh, const std::vector<float> &data)
    {
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                     data.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        mesh.count = static_cast<int>(data.size() / 6);
        mesh.cpu = data;
    }

    static void buildCybertruckShellMesh()
    {
        if (gTruckMeshReady)
            return;

        std::vector<float> sides; // the two side skins - polishable
        std::vector<float> body;  // hood, fascia, tailgate, floor pan
        std::vector<float> glass;
        std::vector<float> darkTop;
        std::vector<float> trim;
        std::vector<float> fenders;
        std::vector<float> bumpers;
        std::vector<float> frontLight;
        std::vector<float> rearLight;
        std::vector<float> lightRecess;
        std::vector<float> fascia;
        std::vector<float> mirrorHousing;
        std::vector<float> mirrorGlass;

        // Project vehicle axes: X = width, Y = up, Z = length, nose toward -Z.
        const float xB = CT_X_BODY;
        const float xG = CT_X_GLASS;
        const float xW = CT_X_WIND;
        const float xR = CT_X_ROOF;

        const float zBeltCross = ctBeltCrossZ();
        const float zBpF = CT_B_PILLAR_Z - CT_B_PILLAR_HALF;
        const float zBpR = CT_B_PILLAR_Z + CT_B_PILLAR_HALF;

        // Height of the glass top edge: the roofline less the stainless rail.
        auto dloTopY = [](float z)
        { return ctRoofLineY(z) - CT_ROOF_RAIL_T; };

        for (int side = -1; side <= 1; side += 2)
        {
            const float zArchF = CAR_FAXLE_Z - CT_ARCH_REACH;
            const float zArchR = CAR_RAXLE_Z + CT_ARCH_REACH;
            const float yFrontSkin = CT_FRONT_BLACK_TOP_Y - CT_STEEL_OVERLAP;
            const float yRearSkin = CT_REAR_BLACK_TOP_Y - CT_STEEL_OVERLAP;
            const CtProfilePoint R1f[4] = {
                {CT_NOSE_Z, CT_NOSE_TOP_Y},
                {zArchF, ctRoofLineY(zArchF)},
                {zArchF, yFrontSkin},
                {CT_NOSE_Z, yFrontSkin}};
            truckPushSidePanel(sides, side, R1f, 4);
            const CtProfilePoint R1m[5] = {
                {zArchF, ctRoofLineY(zArchF)},
                {zBeltCross, CT_BELT_Y},
                {zArchR, CT_BELT_Y},
                {zArchR, CT_FLOOR_Y},
                {zArchF, CT_FLOOR_Y}};
            truckPushSidePanel(sides, side, R1m, 5);
            const CtProfilePoint R1r[4] = {
                {zArchR, CT_BELT_Y},
                {CT_TAIL_Z, CT_BELT_Y},
                {CT_TAIL_Z, yRearSkin},
                {zArchR, yRearSkin}};
            truckPushSidePanel(sides, side, R1r, 4);

            // Ahead of the glass: the wing between the hood line and the belt.
            const CtProfilePoint R2[3] = {
                {zBeltCross, CT_BELT_Y},
                {CT_DLO_FRONT_Z, CT_BELT_Y},
                {CT_COWL_Z, CT_COWL_Y}};
            truckPushSidePanel(sides, side, R2, 3);

            // Roof rail over the front glass, split at the apex kink so each
            // fan stays convex: cowl -> apex, then apex -> B-pillar.
            const CtProfilePoint R2b[4] = {
                {CT_COWL_Z, dloTopY(CT_COWL_Z)},
                {CT_APEX_Z, dloTopY(CT_APEX_Z)},
                {CT_APEX_Z, CT_APEX_Y},
                {CT_COWL_Z, CT_COWL_Y}};
            truckPushSidePanel(sides, side, R2b, 4);
            const CtProfilePoint R4a[4] = {
                {CT_APEX_Z, dloTopY(CT_APEX_Z)},
                {zBpF, dloTopY(zBpF)},
                {zBpF, ctRoofLineY(zBpF)},
                {CT_APEX_Z, CT_APEX_Y}};
            truckPushSidePanel(sides, side, R4a, 4);

            // B-pillar, belt to roof.
            const CtProfilePoint R3[4] = {
                {zBpF, CT_BELT_Y},
                {zBpR, CT_BELT_Y},
                {zBpR, ctRoofLineY(zBpR)},
                {zBpF, ctRoofLineY(zBpF)}};
            truckPushSidePanel(sides, side, R3, 4);

            // Roof rail over the rear glass.
            const CtProfilePoint R4b[4] = {
                {zBpR, dloTopY(zBpR)},
                {CT_DLO_REAR_TOP_Z, dloTopY(CT_DLO_REAR_TOP_Z)},
                {CT_DLO_REAR_TOP_Z, ctRoofLineY(CT_DLO_REAR_TOP_Z)},
                {zBpR, ctRoofLineY(zBpR)}};
            truckPushSidePanel(sides, side, R4b, 4);

            // Sail panel and bed side, behind the glass.  The roof is one
            // straight line from the apex to the tail, so this is convex.
            const CtProfilePoint R5[5] = {
                {CT_DLO_REAR_TOP_Z, ctRoofLineY(CT_DLO_REAR_TOP_Z)},
                {CT_TAIL_Z, CT_BED_RAIL_Y},
                {CT_TAIL_Z, CT_BELT_Y},
                {CT_DLO_REAR_Z, CT_BELT_Y},
                {CT_DLO_REAR_TOP_Z, dloTopY(CT_DLO_REAR_TOP_Z)}};
            truckPushSidePanel(sides, side, R5, 5);
        }

        const Vec3 nHood = normalize(Vec3(0.0f, 1.0f,
                                          -(CT_COWL_Y - CT_NOSE_TOP_Y) / (CT_COWL_Z - CT_NOSE_Z)));
        const Vec3 nWind = normalize(Vec3(0.0f, 1.0f,
                                          -(CT_APEX_Y - CT_COWL_Y) / (CT_APEX_Z - CT_COWL_Z)));

        truckPushQuadAuto(body,
                          Vec3(-xB, CT_NOSE_TOP_Y, CT_NOSE_Z),
                          Vec3(xB, CT_NOSE_TOP_Y, CT_NOSE_Z),
                          Vec3(xB, CT_COWL_Y, CT_COWL_Z),
                          Vec3(-xB, CT_COWL_Y, CT_COWL_Z),
                          nHood);

        // Panoramic roof and the covered vault are dark glass, not steel.
        truckPushQuadAuto(darkTop,
                          Vec3(-xR, CT_APEX_Y, CT_APEX_Z),
                          Vec3(xR, CT_APEX_Y, CT_APEX_Z),
                          Vec3(xR, CT_ROOF_R_Y, CT_ROOF_R_Z),
                          Vec3(-xR, CT_ROOF_R_Y, CT_ROOF_R_Z),
                          Vec3(0.0f, 1.0f, 0.0f));
        const float yVaultFront = ctRoofLineY(CT_BED_FRONT_Z);
        truckPushQuadAuto(darkTop,
                          Vec3(-xR, yVaultFront, CT_BED_FRONT_Z),
                          Vec3(xR, yVaultFront, CT_BED_FRONT_Z),
                          Vec3(xR, CT_BED_RAIL_Y, CT_TAIL_Z),
                          Vec3(-xR, CT_BED_RAIL_Y, CT_TAIL_Z),
                          Vec3(0.0f, 1.0f, 0.25f));

        // Bed front wall below the rear screen.
        truckPushQuadAuto(body,
                          Vec3(-xW, CT_BACKLIGHT_Y, CT_BACKLIGHT_Z),
                          Vec3(xW, CT_BACKLIGHT_Y, CT_BACKLIGHT_Z),
                          Vec3(xR, ctRoofLineY(CT_BED_FRONT_Z), CT_BED_FRONT_Z),
                          Vec3(-xR, ctRoofLineY(CT_BED_FRONT_Z), CT_BED_FRONT_Z),
                          Vec3(0.0f, 0.4f, 1.0f));

        const float xFace = xB - 0.055f; // the nose is only slightly inboard
        const float yBarTop = CT_NOSE_TOP_Y - 0.004f;
        const float yBarBot = CT_NOSE_TOP_Y - 0.052f;                      // thinner channel: 48 mm, was 66 mm
        const float yFascia0 = 0.420f;                                     // meets the top of the bumper
        const float ySteelSplit = CT_FRONT_BLACK_TOP_Y - CT_STEEL_OVERLAP; // black fascia below
        const float zFasciaTop = CT_NOSE_Z + 0.012f;
        const float zFasciaBot = CT_NOSE_Z + 0.050f;
        const float splitT = (ySteelSplit - yFascia0) / (yBarBot - yFascia0);
        const float zSplit = ctLerp(zFasciaBot, zFasciaTop, splitT);

        // Stainless lower hood/nose panel - the same raked plane, continuing
        // up to the light channel.  This is what used to be dark.
        truckPushQuadAuto(body,
                          Vec3(-xFace, ySteelSplit, zSplit),
                          Vec3(xFace, ySteelSplit, zSplit),
                          Vec3(xFace, yBarBot, zFasciaTop),
                          Vec3(-xFace, yBarBot, zFasciaTop),
                          Vec3(0.0f, -0.25f, -1.0f));

        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const Vec3 cheekN(sx * 0.8f, 0.0f, -0.6f);
            truckPushQuadAuto(body,
                              Vec3(sx * xFace, ySteelSplit, zSplit),
                              Vec3(sx * xB, ySteelSplit, CT_NOSE_Z),
                              Vec3(sx * xB, yBarBot, CT_NOSE_Z),
                              Vec3(sx * xFace, yBarBot, zFasciaTop),
                              cheekN);
        }

        // The stainless hood lip that overhangs the whole assembly, and the
        // short return down its front edge to the top of the light blade.
        truckPushQuadAuto(body,
                          Vec3(-xB, yBarTop, CT_NOSE_Z),
                          Vec3(xB, yBarTop, CT_NOSE_Z),
                          Vec3(xB, CT_NOSE_TOP_Y, CT_NOSE_Z),
                          Vec3(-xB, CT_NOSE_TOP_Y, CT_NOSE_Z),
                          Vec3(0.0f, 0.0f, -1.0f));

        const float yGateBot = CT_REAR_BLACK_TOP_Y - CT_STEEL_OVERLAP;
        truckPushQuadAuto(body,
                          Vec3(-xB, yGateBot, CT_TAIL_Z),
                          Vec3(-xB, CT_BED_RAIL_Y, CT_TAIL_Z - 0.040f),
                          Vec3(xB, CT_BED_RAIL_Y, CT_TAIL_Z - 0.040f),
                          Vec3(xB, yGateBot, CT_TAIL_Z),
                          Vec3(0.0f, 0.0f, 1.0f));

        // Underbody floor pan.  It closes the shell so nothing shows through
        // from below once the lift carries the vehicle to eye level.
        truckPushQuadAuto(body,
                          Vec3(-xB, CT_FLOOR_Y, CT_NOSE_Z),
                          Vec3(-xB, CT_TAIL_BOT_Y, CT_TAIL_Z),
                          Vec3(xB, CT_TAIL_BOT_Y, CT_TAIL_Z),
                          Vec3(xB, CT_FLOOR_Y, CT_NOSE_Z),
                          Vec3(0.0f, -1.0f, 0.0f));

        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const Vec3 nSide(sx, 0.0f, 0.0f);
            const float x = sx * xB;

            // Shoulder: the single wedge line, nose to roof apex.
            truckPushFoldRib(sides,
                             Vec3(x, CT_NOSE_TOP_Y, CT_NOSE_Z), Vec3(x, CT_COWL_Y, CT_COWL_Z),
                             nSide, nHood, 0.014f, 0.004f);
            truckPushFoldRib(sides,
                             Vec3(x, CT_COWL_Y, CT_COWL_Z), Vec3(x, CT_APEX_Y, CT_APEX_Z),
                             nSide, nWind, 0.013f, 0.004f);

            // Roof shoulder and the long sail line down to the tailgate.
            truckPushFoldRib(sides,
                             Vec3(x, CT_APEX_Y, CT_APEX_Z), Vec3(x, CT_ROOF_R_Y, CT_ROOF_R_Z),
                             nSide, Vec3(0.0f, 1.0f, 0.0f), 0.012f, 0.0035f);
            truckPushFoldRib(sides,
                             Vec3(x, CT_ROOF_R_Y, CT_ROOF_R_Z), Vec3(x, CT_BED_RAIL_Y, CT_TAIL_Z),
                             nSide, normalize(Vec3(0.0f, 1.0f, 0.25f)), 0.012f, 0.0035f);

            // (The rocker fold rib along the old skin bottom is gone; the
            // sill now forms that edge.)

            // Vertical corner folds at the nose and the tailgate, down to
            // where the stainless meets the black.
            truckPushFoldRib(sides,
                             Vec3(x, CT_FRONT_BLACK_TOP_Y - CT_STEEL_OVERLAP, CT_NOSE_Z),
                             Vec3(x, CT_NOSE_TOP_Y, CT_NOSE_Z),
                             nSide, Vec3(0.0f, 0.0f, -1.0f), 0.011f, 0.003f);
            truckPushFoldRib(sides,
                             Vec3(x, CT_REAR_BLACK_TOP_Y - CT_STEEL_OVERLAP, CT_TAIL_Z),
                             Vec3(x, CT_BED_RAIL_Y, CT_TAIL_Z - 0.040f),
                             nSide, Vec3(0.0f, 0.0f, 1.0f), 0.011f, 0.003f);

            // Belt-line crease.  This is the line that reads strongest in a
            // three-quarter view, so it gets a rib of its own.
            truckPushFoldRib(sides,
                             Vec3(x, CT_BELT_Y, CT_DLO_FRONT_Z),
                             Vec3(x, CT_BELT_Y, CT_TAIL_Z - 0.050f),
                             nSide, normalize(Vec3(sx * 0.55f, 1.0f, 0.0f)), 0.010f, 0.0026f);
        }

        // Cowl fold across the full width, plus the roof apex ridge.
        truckPushFoldRib(body,
                         Vec3(-xB, CT_COWL_Y, CT_COWL_Z), Vec3(xB, CT_COWL_Y, CT_COWL_Z),
                         nHood, nWind, 0.013f, 0.0035f);
        truckPushFoldRib(body,
                         Vec3(-xR, CT_APEX_Y, CT_APEX_Z), Vec3(xR, CT_APEX_Y, CT_APEX_Z),
                         nWind, Vec3(0.0f, 1.0f, 0.0f), 0.012f, 0.003f);

        truckPushQuadAuto(glass,
                          Vec3(-xW, CT_COWL_Y + 0.012f, CT_COWL_Z + 0.010f),
                          Vec3(xW, CT_COWL_Y + 0.012f, CT_COWL_Z + 0.010f),
                          Vec3(xW, CT_APEX_Y - 0.004f, CT_APEX_Z),
                          Vec3(-xW, CT_APEX_Y - 0.004f, CT_APEX_Z),
                          nWind);

        truckPushQuadAuto(glass,
                          Vec3(-xW, CT_ROOF_R_Y - 0.006f, CT_ROOF_R_Z),
                          Vec3(-xW, CT_BACKLIGHT_Y, CT_BACKLIGHT_Z),
                          Vec3(xW, CT_BACKLIGHT_Y, CT_BACKLIGHT_Z),
                          Vec3(xW, CT_ROOF_R_Y - 0.006f, CT_ROOF_R_Z),
                          Vec3(0.0f, 1.0f, 0.62f));

        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float x = sx * xG;
            const Vec3 nSide(sx, 0.0f, 0.0f);

            {
                const Vec3 f0(x, CT_BELT_Y + 0.012f, CT_DLO_FRONT_Z + 0.040f);
                const Vec3 f1(x, CT_BELT_Y + 0.012f, zBpF - 0.006f);
                const Vec3 f2(x, dloTopY(zBpF) - 0.008f, zBpF - 0.006f);
                const Vec3 f3(x, dloTopY(CT_APEX_Z) - 0.008f, CT_APEX_Z);
                const Vec3 f4(x, dloTopY(CT_COWL_Z + 0.040f) - 0.004f, CT_COWL_Z + 0.040f);
                truckPushTriN(glass, f0, f1, f2, nSide);
                truckPushTriN(glass, f0, f2, f3, nSide);
                truckPushTriN(glass, f0, f3, f4, nSide);
            }

            // Rear door and quarter glass.
            const Vec3 r0(x, CT_BELT_Y + 0.012f, zBpR + 0.006f);
            const Vec3 r1(x, CT_BELT_Y + 0.012f, CT_DLO_REAR_Z - 0.030f);
            const Vec3 r2(x, dloTopY(CT_DLO_REAR_TOP_Z) - 0.008f,
                          CT_DLO_REAR_TOP_Z - 0.010f);
            const Vec3 r3(x, dloTopY(zBpR) - 0.008f, zBpR + 0.006f);
            truckPushTriN(glass, r0, r1, r2, nSide);
            truckPushTriN(glass, r0, r2, r3, nSide);
        }

        const float xTrim = (CT_X_BODY + CT_X_GLASS) * 0.5f;
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float x = sx * xTrim;
            const Vec3 nSide(sx, 0.0f, 0.0f);

            // Belt-line weather seal running the length of the glazing.
            truckPushQuadN(trim,
                           Vec3(x, CT_BELT_Y - 0.004f, CT_DLO_FRONT_Z),
                           Vec3(x, CT_BELT_Y + 0.020f, CT_DLO_FRONT_Z),
                           Vec3(x, CT_BELT_Y + 0.020f, CT_DLO_REAR_Z),
                           Vec3(x, CT_BELT_Y - 0.004f, CT_DLO_REAR_Z),
                           nSide);

            // B-pillar blackout.
            truckPushQuadN(trim,
                           Vec3(x, CT_BELT_Y, zBpF - 0.004f),
                           Vec3(x, dloTopY(zBpF) + 0.010f, zBpF - 0.004f),
                           Vec3(x, dloTopY(zBpR) + 0.010f, zBpR + 0.004f),
                           Vec3(x, CT_BELT_Y, zBpR + 0.004f),
                           nSide);

            const float zs[4] = {CT_DLO_FRONT_Z, CT_APEX_Z, zBpF, CT_DLO_REAR_TOP_Z};
            for (int k = 0; k < 3; ++k)
            {
                const float za = zs[k], zb = zs[k + 1];
                truckPushQuadN(trim,
                               Vec3(x, dloTopY(za) - 0.012f, za), Vec3(x, dloTopY(zb) - 0.012f, zb),
                               Vec3(x, dloTopY(zb) + 0.006f, zb), Vec3(x, dloTopY(za) + 0.006f, za),
                               nSide);
            }
        }

        truckPushFiveFacetFender(fenders, -1, CAR_FAXLE_Z, CAR_FWHEEL_R);
        truckPushFiveFacetFender(fenders, 1, CAR_FAXLE_Z, CAR_FWHEEL_R);
        truckPushFiveFacetFender(fenders, -1, CAR_RAXLE_Z, CAR_RWHEEL_R);
        truckPushFiveFacetFender(fenders, 1, CAR_RAXLE_Z, CAR_RWHEEL_R);

        for (int w = 0; w < 4; ++w)
        {
            const float sx = (w & 1) ? 1.0f : -1.0f;
            const float axleZ = (w < 2) ? CAR_FAXLE_Z : CAR_RAXLE_Z;
            const float R = (w < 2) ? CAR_FWHEEL_R : CAR_RWHEEL_R;
            const float x = sx * (CT_X_BODY + 0.004f);
            const float yz[8][2] = {
                {0.235f, -0.595f}, {R + 0.015f, -0.570f}, {R + 0.280f, -0.475f}, {R + 0.505f, -0.268f}, {R + 0.505f, 0.268f}, {R + 0.280f, 0.475f}, {R + 0.015f, 0.570f}, {0.235f, 0.595f}};
            const Vec3 n(sx, 0.0f, 0.0f);
            for (int i = 1; i + 1 < 8; ++i)
                truckPushTriN(lightRecess,
                              Vec3(x, yz[0][0], axleZ + yz[0][1]),
                              Vec3(x, yz[i][0], axleZ + yz[i][1]),
                              Vec3(x, yz[i + 1][0], axleZ + yz[i + 1][1]), n);
        }

        {
            const float xIn = xB - 0.090f;  // end faces sit well inboard
            const float xOut = xB - 0.006f; // sides just inside the skin line
            const float zArchF = CAR_FAXLE_Z - CT_ARCH_REACH;
            const float zArchR = CAR_RAXLE_Z + CT_ARCH_REACH;

            // One end piece.  `dir` is -1 at the nose, +1 at the tail.
            auto endPiece = [&](float dir, float zEnd, float zArch, float yTop,
                                float yBotFace, float yBotCorner, float yBotArch)
            {
                const float zFaceTop = zEnd - dir * 0.050f;
                const float zFaceBot = zEnd - dir * 0.110f;
                const float zCorner = zEnd - dir * 0.180f;

                // Raked end face: its top leans out over the bottom.
                truckPushQuadAuto(bumpers,
                                  Vec3(-xIn, yBotFace, zFaceBot), Vec3(xIn, yBotFace, zFaceBot),
                                  Vec3(xIn, yTop, zFaceTop), Vec3(-xIn, yTop, zFaceTop),
                                  Vec3(0.0f, -0.15f, dir));

                for (int side = -1; side <= 1; side += 2)
                {
                    const float sx = static_cast<float>(side);
                    // Angled corner facet: carries the end face round to the side.
                    truckPushQuadAuto(bumpers,
                                      Vec3(sx * xIn, yBotFace, zFaceBot),
                                      Vec3(sx * xOut, yBotCorner, zCorner),
                                      Vec3(sx * xOut, yTop, zCorner),
                                      Vec3(sx * xIn, yTop, zFaceTop),
                                      Vec3(sx * 0.7f, 0.0f, dir * 0.7f));
                    // Side face, back to the wheel arch.
                    truckPushQuadAuto(bumpers,
                                      Vec3(sx * xOut, yBotCorner, zCorner),
                                      Vec3(sx * xOut, yBotArch, zArch),
                                      Vec3(sx * xOut, yTop, zArch),
                                      Vec3(sx * xOut, yTop, zCorner),
                                      Vec3(sx, 0.0f, 0.0f));
                }

                // Top ledge and floor, as convex hexagons.
                const Vec3 topRing[6] = {
                    Vec3(-xIn, yTop, zFaceTop), Vec3(xIn, yTop, zFaceTop),
                    Vec3(xOut, yTop, zCorner), Vec3(xOut, yTop, zArch),
                    Vec3(-xOut, yTop, zArch), Vec3(-xOut, yTop, zCorner)};
                const Vec3 botRing[6] = {
                    Vec3(-xIn, yBotFace, zFaceBot), Vec3(xIn, yBotFace, zFaceBot),
                    Vec3(xOut, yBotCorner, zCorner), Vec3(xOut, yBotArch, zArch),
                    Vec3(-xOut, yBotArch, zArch), Vec3(-xOut, yBotCorner, zCorner)};
                for (int i = 1; i + 1 < 6; ++i)
                {
                    truckPushTriN(bumpers, topRing[0], topRing[i], topRing[i + 1], Vec3(0.0f, 1.0f, 0.0f));
                    const Vec3 n = truckFaceNormal(botRing[0], botRing[i], botRing[i + 1], Vec3(0.0f, -1.0f, 0.0f));
                    truckPushTriN(bumpers, botRing[0], botRing[i], botRing[i + 1], n);
                }
                // Back wall inside the wheel well.
                truckPushQuadAuto(bumpers,
                                  Vec3(-xOut, yBotArch, zArch), Vec3(xOut, yBotArch, zArch),
                                  Vec3(xOut, yTop, zArch), Vec3(-xOut, yTop, zArch),
                                  Vec3(0.0f, 0.0f, -dir));
            };

            endPiece(-1.0f, CT_NOSE_Z, zArchF, CT_FRONT_BLACK_TOP_Y, 0.280f, 0.270f, 0.240f);
            endPiece(1.0f, CT_TAIL_Z, zArchR, CT_REAR_BLACK_TOP_Y, 0.300f, 0.290f, 0.260f);

            const float zS0 = CAR_FAXLE_Z + CT_ARCH_REACH;
            const float zS1 = CAR_RAXLE_Z - CT_ARCH_REACH;
            struct SillXY
            {
                float x;
                float y;
            };
            const SillXY prof[5] = {
                {xB - 0.002f, CT_SILL_TOP_Y + 0.012f}, // meets the skin
                {xB + 0.028f, CT_SILL_TOP_Y - 0.018f}, // top chamfer
                {xB + 0.028f, CT_SILL_BOT_Y + 0.030f}, // outer face
                {xB - 0.020f, CT_SILL_BOT_Y},          // bottom chamfer
                {xB - 0.180f, CT_SILL_BOT_Y}           // underside
            };
            const Vec3 sillPref[4] = {
                Vec3(0.6f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f),
                Vec3(0.6f, -1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)};
            for (int side = -1; side <= 1; side += 2)
            {
                const float sx = static_cast<float>(side);
                for (int i = 0; i < 4; ++i)
                {
                    const Vec3 a(sx * prof[i].x, prof[i].y, zS0);
                    const Vec3 b(sx * prof[i + 1].x, prof[i + 1].y, zS0);
                    const Vec3 c(sx * prof[i + 1].x, prof[i + 1].y, zS1);
                    const Vec3 d(sx * prof[i].x, prof[i].y, zS1);
                    truckPushQuadAuto(bumpers, a, b, c, d,
                                      Vec3(sx * sillPref[i].x, sillPref[i].y, 0.0f));
                }
                // End caps, hidden by the arch trims but closed anyway.
                for (int e = 0; e < 2; ++e)
                {
                    const float z = (e == 0) ? zS0 : zS1;
                    const Vec3 n(0.0f, 0.0f, (e == 0) ? -1.0f : 1.0f);
                    for (int i = 1; i + 1 < 5; ++i)
                        truckPushTriN(bumpers,
                                      Vec3(sx * prof[0].x, prof[0].y, z),
                                      Vec3(sx * prof[i].x, prof[i].y, z),
                                      Vec3(sx * prof[i + 1].x, prof[i + 1].y, z), n);
                }
            }
        }

        {
            const float yBar0 = yBarBot;
            const float yBar1 = yBarTop;
            const float zFace = CT_NOSE_Z + 0.006f; // just behind the hood lip
            const float zBack = CT_NOSE_Z + 0.044f; // back of the channel
            const float xBar = xFace;               // essentially full width

            // Dark channel the blade sits in, so it has a physical opening.
            truckPushQuadN(lightRecess,
                           Vec3(-xBar, yBar0, zBack), Vec3(xBar, yBar0, zBack),
                           Vec3(xBar, yBar1, zBack), Vec3(-xBar, yBar1, zBack),
                           Vec3(0.0f, 0.0f, -1.0f));
            truckPushQuadAuto(lightRecess,
                              Vec3(-xBar, yBar1, zBack), Vec3(xBar, yBar1, zBack),
                              Vec3(xBar, yBar1, zFace), Vec3(-xBar, yBar1, zFace),
                              Vec3(0.0f, -1.0f, 0.0f));
            truckPushQuadAuto(lightRecess,
                              Vec3(-xBar, yBar0, zFace), Vec3(xBar, yBar0, zFace),
                              Vec3(xBar, yBar0, zBack), Vec3(-xBar, yBar0, zBack),
                              Vec3(0.0f, 1.0f, 0.0f));

            // The blade itself, nearly flush with the fascia.
            truckPushQuadN(frontLight,
                           Vec3(-xBar + 0.004f, yBar0 + 0.013f, zFace + 0.002f),
                           Vec3(xBar - 0.004f, yBar0 + 0.013f, zFace + 0.002f),
                           Vec3(xBar - 0.004f, yBar1 - 0.013f, zFace + 0.002f),
                           Vec3(-xBar + 0.004f, yBar1 - 0.013f, zFace + 0.002f),
                           Vec3(0.0f, 0.0f, -1.0f));

            // It wraps a short way around each front corner, which is what
            // makes it read as a blade rather than a decal.
            for (int side = -1; side <= 1; side += 2)
            {
                const float sx = static_cast<float>(side);
                truckPushQuadAuto(frontLight,
                                  Vec3(sx * (xBar - 0.004f), yBar0 + 0.013f, zFace + 0.002f),
                                  Vec3(sx * (xB - 0.004f), yBar0 + 0.013f, CT_NOSE_Z + 0.040f),
                                  Vec3(sx * (xB - 0.004f), yBar1 - 0.013f, CT_NOSE_Z + 0.040f),
                                  Vec3(sx * (xBar - 0.004f), yBar1 - 0.013f, zFace + 0.002f),
                                  Vec3(sx * 0.6f, 0.0f, -0.8f));
            }

            {
                const float zTop = CT_NOSE_Z + 0.050f, zBot = CT_NOSE_Z + 0.110f;
                const float yTop = CT_FRONT_BLACK_TOP_Y, yBot = 0.280f;
                auto zAt = [&](float y)
                {
                    return zBot + (zTop - zBot) * (y - yBot) / (yTop - yBot) - 0.004f;
                };
                const float y0 = 0.360f, y1 = 0.455f, hx = 0.720f;
                truckPushQuadAuto(fascia,
                                  Vec3(-hx, y0, zAt(y0)), Vec3(hx, y0, zAt(y0)),
                                  Vec3(hx, y1, zAt(y1)), Vec3(-hx, y1, zAt(y1)),
                                  Vec3(0.0f, -0.1f, -1.0f));
            }
        }
        {
            const float yL0 = CT_BED_RAIL_Y - 0.090f;
            const float yL1 = CT_BED_RAIL_Y - 0.030f;
            const float zOuter = CT_TAIL_Z + 0.004f;
            const float zInner = CT_TAIL_Z - 0.034f;
            const float xL = xB - 0.028f;

            truckPushQuadN(lightRecess,
                           Vec3(-xL, yL0, zInner), Vec3(-xL, yL1, zInner),
                           Vec3(xL, yL1, zInner), Vec3(xL, yL0, zInner),
                           Vec3(0.0f, 0.0f, 1.0f));
            truckPushQuadAuto(lightRecess,
                              Vec3(-xL, yL1, zInner), Vec3(xL, yL1, zInner),
                              Vec3(xL, yL1, zOuter), Vec3(-xL, yL1, zOuter),
                              Vec3(0.0f, -1.0f, 0.0f));

            truckPushQuadN(rearLight,
                           Vec3(-xL + 0.012f, yL0 + 0.012f, zInner + 0.008f),
                           Vec3(-xL + 0.012f, yL1 - 0.012f, zInner + 0.008f),
                           Vec3(xL - 0.012f, yL1 - 0.012f, zInner + 0.008f),
                           Vec3(xL - 0.012f, yL0 + 0.012f, zInner + 0.008f),
                           Vec3(0.0f, 0.0f, 1.0f));
        }

        const float zMirrorA = CT_DLO_FRONT_Z - 0.080f; // leading edge
        const float zMirrorB = CT_DLO_FRONT_Z + 0.060f; // widest point
        const float zMirrorC = CT_DLO_FRONT_Z + 0.220f; // trailing edge
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float xIn = sx * (xB + 0.012f);
            const float xOut = sx * (xB + 0.205f);
            const Vec3 outN(sx, 0.0f, 0.0f);

            Vec3 aIn(xIn, 1.150f, zMirrorA);
            Vec3 bIn(xIn, 1.322f, zMirrorB);
            Vec3 cIn(xIn, 1.162f, zMirrorC);
            Vec3 aOut(xOut, 1.150f, zMirrorA);
            Vec3 bOut(xOut, 1.322f, zMirrorB);
            Vec3 cOut(xOut, 1.162f, zMirrorC);

            truckPushTriN(mirrorHousing, aOut, bOut, cOut, outN);
            truckPushTriN(mirrorHousing, cIn, bIn, aIn, -outN);
            truckPushQuadAuto(mirrorHousing, aIn, aOut, bOut, bIn, Vec3(0.0f, 0.55f, -0.55f));
            truckPushQuadAuto(mirrorHousing, bIn, bOut, cOut, cIn, Vec3(0.0f, 0.45f, 0.45f));
            truckPushQuadAuto(mirrorHousing, cIn, cOut, aOut, aIn, Vec3(0.0f, -1.0f, 0.0f));

            const float xFaceM = sx * (xB + 0.207f);
            truckPushTriN(mirrorGlass,
                          Vec3(xFaceM, 1.168f, zMirrorA + 0.032f),
                          Vec3(xFaceM, 1.300f, zMirrorB),
                          Vec3(xFaceM, 1.176f, zMirrorC - 0.032f), outN);
        }

        truckUpload(gTruckSideMesh, sides);
        truckUpload(gTruckBodyMesh, body);
        truckUpload(gTruckGlassMesh, glass);
        truckUpload(gTruckDarkTopMesh, darkTop);
        truckUpload(gTruckTrimMesh, trim);
        truckUpload(gTruckFenderMesh, fenders);
        truckUpload(gTruckBumperMesh, bumpers);
        truckUpload(gTruckLightRecessMesh, lightRecess);
        truckUpload(gTruckFasciaMesh, fascia);
        truckUpload(gTruckFrontLightMesh, frontLight);
        truckUpload(gTruckRearLightMesh, rearLight);
        truckUpload(gTruckMirrorHousingMesh, mirrorHousing);
        truckUpload(gTruckMirrorGlassMesh, mirrorGlass);
        gTruckSideMesh.trace = gTruckBodyMesh.trace = gTruckFenderMesh.trace = gTruckBumperMesh.trace = true;
        gTruckTrimMesh.trace = gTruckFasciaMesh.trace = gTruckDarkTopMesh.trace = true;
        gTruckMeshReady = true;
    }

    static void drawTruckMeshBatch(const TruckMeshBatch &mesh,
                                   const Mat4 &model,
                                   const Vec3 &color,
                                   float emissive = 0.0f,
                                   float metallic = 0.0f,
                                   float roughness = 0.65f,
                                   int materialType = -1)
    {
        if (gRec)
        {
            if (mesh.trace)
                for (size_t v = 0; v + 17 < mesh.cpu.size(); v += 18)
                {
                    Scene::RtItem it;
                    it.type = 3;
                    it.a = transformPoint(model, Vec3(mesh.cpu[v], mesh.cpu[v + 1], mesh.cpu[v + 2]));
                    it.b = transformPoint(model, Vec3(mesh.cpu[v + 6], mesh.cpu[v + 7], mesh.cpu[v + 8]));
                    it.c = transformPoint(model, Vec3(mesh.cpu[v + 12], mesh.cpu[v + 13], mesh.cpu[v + 14]));
                    it.color = color;
                    gRec->push_back(it);
                }
            return;
        }
        Vec3 shownColor;
        if (!teachingPrepareColor(color, shownColor))
            return;

        Mat3 nrm = normalMatrix(model);
        glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
        glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
        uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.count);
        glBindVertexArray(0);
    }
}

void Scene::drawCar(const Mat4 &parent, const Vec3 &bodyColor, float sidePolish,
                    float wheelSpinDeg, bool lightsOn, float dirtAmount)
{
    buildCybertruckShellMesh();

    const float polish = (sidePolish < 0.0f) ? 0.0f : (sidePolish > 1.0f ? 1.0f : sidePolish);

    const Vec3 stainless(0.515f, 0.532f, 0.556f);
    const Vec3 body = stainless * 0.985f + bodyColor * 0.015f;
    const Vec3 glass(0.020f, 0.028f, 0.040f);
    const Vec3 tonneau(0.115f, 0.122f, 0.134f);
    const Vec3 trim(0.028f, 0.030f, 0.034f);

    const float kStainlessAsRolled = 0.40f;

    Scene::setSurfaceFinish(polish);
    Scene::setSurfaceDirt(dirtAmount);
    drawTruckMeshBatch(gTruckSideMesh, parent, body, 0.0f, 0.97f, kStainlessAsRolled, MAT_STAINLESS);
    Scene::clearSurfaceFinish();

    Scene::setSurfaceFinish(polish);
    drawTruckMeshBatch(gTruckBodyMesh, parent, body, 0.0f, 0.97f, kStainlessAsRolled, MAT_STAINLESS);
    Scene::clearSurfaceFinish();
    drawTruckMeshBatch(gTruckGlassMesh, parent, glass, 0.0f, 0.02f, 0.050f, MAT_GLASS);
    drawTruckMeshBatch(gTruckDarkTopMesh, parent, tonneau, 0.0f, 0.04f, 0.42f, MAT_CARBON);
    drawTruckMeshBatch(gTruckTrimMesh, parent, trim, 0.0f, 0.01f, 0.78f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckFenderMesh, parent, trim, 0.0f, 0.01f, 0.80f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckBumperMesh, parent, trim, 0.0f, 0.01f, 0.84f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckLightRecessMesh, parent, trim * 0.70f, 0.0f, 0.0f, 0.88f, MAT_PLASTIC);
    // The front fascia is a dark grey moulding, not bodywork.
    drawTruckMeshBatch(gTruckFasciaMesh, parent, Vec3(0.088f, 0.092f, 0.100f),
                       0.0f, 0.04f, 0.62f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckMirrorHousingMesh, parent, trim, 0.0f, 0.01f, 0.66f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckMirrorGlassMesh, parent, glass * 1.15f, 0.0f, 0.02f, 0.045f, MAT_GLASS);
    const float lampGlow = lightsOn ? 1.0f : 0.06f;
    drawTruckMeshBatch(gTruckFrontLightMesh, parent,
                       Vec3(1.0f, 0.975f, 0.900f) * (lightsOn ? 1.0f : 0.55f),
                       lampGlow, 0.0f, 0.10f, MAT_PAINT);
    drawTruckMeshBatch(gTruckRearLightMesh, parent,
                       Vec3(0.940f, 0.060f, 0.045f) * (lightsOn ? 1.0f : 0.60f),
                       lampGlow, 0.0f, 0.10f, MAT_PAINT);

    const float zDoorFront = CAR_FAXLE_Z + 0.720f; // just behind the front arch
    const float zDoorMid = CT_B_PILLAR_Z;          // B-pillar cut
    const float zDoorRear = CT_DLO_REAR_Z + 0.04f; // trailing edge of the rear door

    for (int side = -1; side <= 1; side += 2)
    {
        const float sx = static_cast<float>(side);
        const float xSkin = sx * (CT_X_BODY + 0.003f);

        // Three vertical shut lines, from the sill up to the belt.
        const float cuts[3] = {zDoorFront, zDoorMid, zDoorRear};
        const float cutY0 = CT_SILL_TOP_Y + 0.010f, cutY1 = CT_BELT_Y - 0.010f;
        for (int c = 0; c < 3; ++c)
        {
            drawCube(parent * place(Vec3(xSkin, (cutY0 + cutY1) * 0.5f, cuts[c]),
                                    Vec3(0.004f, cutY1 - cutY0, 0.007f)),
                     trim * 0.55f, 0.0f, 0.0f, 0.90f, MAT_PLASTIC);
        }

        // Flush handle recesses, just below the belt line on each door.
        drawCube(parent * place(Vec3(xSkin, CT_BELT_Y - 0.135f, CT_B_PILLAR_Z - 0.180f),
                                Vec3(0.006f, 0.030f, 0.185f)),
                 trim * 0.52f, 0.0f, 0.0f, 0.80f, MAT_PLASTIC);
        drawCube(parent * place(Vec3(xSkin, CT_BELT_Y - 0.135f, CT_DLO_REAR_Z - 0.150f),
                                Vec3(0.006f, 0.030f, 0.185f)),
                 trim * 0.52f, 0.0f, 0.0f, 0.80f, MAT_PLASTIC);

        // Charge-port door on the rear quarter.
        drawCube(parent * place(Vec3(xSkin, 0.930f, 1.760f),
                                Vec3(0.005f, 0.200f, 0.145f)),
                 body * 0.965f, 0.0f, 0.94f, 0.33f, MAT_STAINLESS);
    }

   
    for (int i = 0; i < 4; ++i)
    {
        const float z = 1.420f + static_cast<float>(i) * 0.330f;
        const float y = ctRoofLineY(z) + 0.012f;
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            drawCube(parent * place(Vec3(sx * (CT_X_BODY - 0.055f), y, z),
                                    Vec3(0.050f, 0.022f, 0.070f)),
                     trim * 1.25f, 0.0f, 0.55f, 0.45f, MAT_PAINTED_METAL);
        }
    }

    const float fTrack = CAR_FTRACK + CAR_WHEEL_OUTSET;
    const float rTrack = CAR_RTRACK + CAR_WHEEL_OUTSET;
    drawWheel(parent, Vec3(-fTrack, CAR_FWHEEL_R, CAR_FAXLE_Z), CAR_FWHEEL_R, CAR_FWHEEL_W, 1.0f, wheelSpinDeg);
    drawWheel(parent, Vec3(fTrack, CAR_FWHEEL_R, CAR_FAXLE_Z), CAR_FWHEEL_R, CAR_FWHEEL_W, -1.0f, -wheelSpinDeg);
    drawWheel(parent, Vec3(-rTrack, CAR_RWHEEL_R, CAR_RAXLE_Z), CAR_RWHEEL_R, CAR_RWHEEL_W, 1.0f, wheelSpinDeg);
    drawWheel(parent, Vec3(rTrack, CAR_RWHEEL_R, CAR_RAXLE_Z), CAR_RWHEEL_R, CAR_RWHEEL_W, -1.0f, -wheelSpinDeg);
}

void Scene::drawScanner(const Mat4 &parent)
{
    // Neck connecting the scanner to the arm tip.
    drawCube(parent * place(Vec3(0.0f, -0.16f, 0.0f), Vec3(0.18f, 0.34f, 0.18f)), C_ROBOT_DARK);

    // Main housing.
    drawCube(parent * place(Vec3(0.0f, -0.48f, 0.0f), Vec3(0.62f, 0.34f, 0.76f)), C_SCANNER);

    // Cooling fins on both sides.
    drawCube(parent * place(Vec3(-0.35f, -0.48f, 0.0f), Vec3(0.06f, 0.24f, 0.56f)), C_ROBOT);
    drawCube(parent * place(Vec3(0.35f, -0.48f, 0.0f), Vec3(0.06f, 0.24f, 0.56f)), C_ROBOT);

    // Lens barrel (cylinder) pointing straight down at the vehicle.
    drawCylinder(parent * place(Vec3(0.0f, -0.68f, 0.0f), Vec3(0.40f, 0.10f, 0.40f)), C_ROBOT_DARK);

    // The glowing lens itself. This is where the spotlight will live later.
    drawCylinder(parent * place(Vec3(0.0f, -0.74f, 0.0f), Vec3(0.30f, 0.06f, 0.30f)), C_NEON_CYAN, 1.0f);

    // Two status indicator lights on the back of the housing.
    drawCube(parent * place(Vec3(-0.18f, -0.38f, 0.40f), Vec3(0.08f, 0.08f, 0.05f)), C_NEON_CYAN, 0.9f);
    drawCube(parent * place(Vec3(0.18f, -0.38f, 0.40f), Vec3(0.08f, 0.08f, 0.05f)), C_LIFT_YELLOW, 0.9f);
}

static void drawCleaningSpongeImpl(const Mat4 &parent)
{
    const Vec3 foamBlue(0.175f, 0.285f, 0.455f);
    const Vec3 foamTop(0.225f, 0.365f, 0.565f);
    const Vec3 seam(0.055f, 0.085f, 0.135f);

    // Flattened UV spheres give the soft, rounded foam/vinyl silhouette.
    Scene::drawSphere(parent * Scene::place(Vec3(0.0f, 0.0f, 0.0f),
                                            Vec3(0.42f, 0.105f, 0.27f)),
                      foamBlue, 0.0f, 0.0f, 0.82f, MAT_PLASTIC);
    Scene::drawSphere(parent * Scene::place(Vec3(0.0f, 0.045f, 0.0f),
                                            Vec3(0.385f, 0.072f, 0.235f)),
                      foamTop, 0.0f, 0.0f, 0.88f, MAT_PLASTIC);
    Scene::drawCylS(parent * translate(0.0f, -0.070f, 0.0f) *
                        scale(0.365f, 0.018f, 0.230f),
                    seam, 0.0f, 0.0f, 0.90f, MAT_RUBBER);
}

static float toolBottomDrop(int toolType)
{
    switch (toolType)
    {
    case 1:
        return 0.990f; // impact driver
    case 2:
        return 0.870f; // gripper
    case 3:
        return 0.772f; // polisher
    default:
        return 0.770f; // scanner
    }
}

static void drawRobotToolHeadImpl(const Mat4 &parent, int toolType, float gripperOpen = 0.0f, float polisherAngle = 0.0f, float driverAngle = 0.0f)
{
    // Common quick-change coupler.  This is the part that stays on the wrist.
    Scene::drawCylS(parent * translate(0.0f, -0.08f, 0.0f) * scale(0.25f, 0.14f, 0.25f), C_JOINT);

    if (toolType < 0)
    {
        Scene::drawCylS(parent * translate(0.0f, -0.165f, 0.0f) * scale(0.17f, 0.05f, 0.17f),
                        C_CHROME, 0.0f, 0.0f, 0.45f, MAT_CHROME);
        for (int i = 0; i < 3; ++i)
        {
            Mat4 pin = parent * translate(0.0f, -0.20f, 0.0f) * rotateY(static_cast<float>(i) * 120.0f) * translate(0.085f, 0.0f, 0.0f);
            Scene::drawCylS(pin * scale(0.035f, 0.055f, 0.035f),
                            C_CHROME * 0.85f, 0.0f, 0.0f, 0.45f, MAT_CHROME);
        }
        return;
    }

    if (toolType == 1) // impact driver / drill
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.34f, 0.0f), Vec3(0.42f, 0.42f, 0.34f)), C_ROBOT);
        Scene::drawCylS(parent * translate(0.0f, -0.63f, 0.0f) * scale(0.14f, 0.30f, 0.14f), C_ROBOT_DARK);
        Mat4 driverSpin = parent * translate(0.0f, -0.88f, 0.0f) * rotateY(driverAngle);
        Scene::drawCylS(driverSpin * scale(0.055f, 0.22f, 0.055f), C_CHROME);
        // Small offset marker on the socket makes the spin visible during tightening.
        Scene::drawCube(driverSpin * Scene::place(Vec3(0.055f, -0.105f, 0.0f), Vec3(0.035f, 0.020f, 0.025f)),
                        Vec3(0.22f, 0.65f, 0.92f), 0.35f, 0.65f, 0.22f, MAT_PAINTED_METAL);
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.31f, 0.19f), Vec3(0.25f, 0.10f, 0.05f)), C_NEON_CYAN, 0.55f);
    }
    else if (toolType == 2) // two-finger gripper
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.30f, 0.0f), Vec3(0.48f, 0.24f, 0.38f)), C_ROBOT_DARK);
        const float open01 = (gripperOpen < 0.0f) ? 0.0f : (gripperOpen > 1.0f ? 1.0f : gripperOpen);
        const float fingerX = 0.18f + open01 * 0.16f;
        for (int side = -1; side <= 1; side += 2)
        {
            float x = fingerX * static_cast<float>(side);
            Scene::drawCube(parent * Scene::place(Vec3(x, -0.56f, 0.0f), Vec3(0.10f, 0.50f, 0.12f),
                                                  Vec3(0.0f, 0.0f, -8.0f * static_cast<float>(side) * (1.0f - 0.35f * open01))),
                            C_CHROME);
            Scene::drawCube(parent * Scene::place(Vec3(x + 0.045f * static_cast<float>(side), -0.81f, 0.0f),
                                                  Vec3(0.15f, 0.12f, 0.19f)),
                            C_RUBBER);
        }
    }
    else if (toolType == 3) // rotary polisher
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.30f, 0.0f), Vec3(0.42f, 0.34f, 0.38f)), C_ROBOT);
        Scene::drawCylS(parent * translate(0.0f, -0.55f, 0.0f) * scale(0.13f, 0.22f, 0.13f), C_ROBOT_DARK);

        Mat4 spinningPad = parent * translate(0.0f, -0.72f, 0.0f) * rotateY(polisherAngle);
        const Vec3 padBlue(0.145f, 0.235f, 0.390f);
        Scene::drawCylS(spinningPad * scale(0.58f, 0.085f, 0.58f),
                        padBlue, 0.0f, 0.0f, 0.86f, MAT_PLASTIC);

        // Three small contrasting fabric marks make the rotation visible even
        // though the pad itself is circular.
        for (int i = 0; i < 3; ++i)
        {
            Mat4 mark = spinningPad * rotateY(static_cast<float>(i) * 120.0f) * Scene::place(Vec3(0.25f, -0.046f, 0.0f), Vec3(0.22f, 0.012f, 0.055f));
            Scene::drawCube(mark, Vec3(0.48f, 0.55f, 0.66f), 0.0f, 0.0f, 0.92f, MAT_PLASTIC);
        }
    }
    else // scanner / inspection light head
    {
        Scene::drawScanner(parent);
    }
}

Vec3 Scene::toolDockPosition(int toolType)
{
    const float x = (toolType & 1) ? 0.78f : -0.78f; // scanner/gripper left
    const float z = (toolType < 2) ? -0.52f : 0.52f; // scanner/driver front
    return Vec3(x, kToolDockTopY, z);
}

void Scene::drawRobotToolStation(const Mat4 &parent, int selectedTool, bool spongeOnTable)
{
    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), 2.45f, 1.95f);

    // Tabletop and frame.
    drawCube(parent * place(Vec3(0.0f, 0.78f, 0.0f), Vec3(2.35f, 0.11f, 1.85f)), C_STEEL);
    drawCube(parent * place(Vec3(0.0f, 0.70f, 0.0f), Vec3(2.20f, 0.06f, 1.70f)), C_ROBOT_DARK);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            drawCube(parent * place(Vec3(1.00f * sx, 0.37f, 0.78f * sz),
                                    Vec3(0.11f, 0.74f, 0.11f)),
                     C_ROBOT_DARK);
    // Low stretcher rails - frame only, nothing that stands up.
    for (int sz = -1; sz <= 1; sz += 2)
        drawCube(parent * place(Vec3(0.0f, 0.17f, 0.78f * static_cast<float>(sz)),
                                Vec3(2.00f, 0.06f, 0.07f)),
                 C_ROBOT_DARK);

    // ---- four docks in a 2 x 2 grid ------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        const Vec3 d = toolDockPosition(i);
        const bool fitted = (i == selectedTool);

        // Dock pedestal: a dark base with a short connector post, so it is
        // obvious where each tool belongs even when it has been taken away.
        drawCube(parent * place(Vec3(d.x, 0.865f, d.z), Vec3(0.46f, 0.05f, 0.46f)),
                 C_ROBOT_DARK * 0.85f);
        drawCylS(parent * translate(d.x, 0.905f, d.z) * scale(0.30f, 0.035f, 0.30f),
                 fitted ? C_NEON_CYAN : C_ROBOT_DARK, fitted ? 0.55f : 0.0f);
        drawCylS(parent * translate(d.x, 0.945f, d.z) * scale(0.15f, 0.055f, 0.15f),
                 C_CHROME * 0.80f, 0.0f, 0.0f, 0.55f, MAT_CHROME);

        // Status lamp set INTO the tabletop rather than onto a backboard.
        drawCube(parent * place(Vec3(d.x, 0.842f, d.z + 0.30f), Vec3(0.20f, 0.02f, 0.05f)),
                 fitted ? C_NEON : C_STEEL, fitted ? 0.70f : 0.0f);

        if (!fitted)
        {
            // Stand the tool ON its dock: the frame origin is lifted by the
            // tool's own drop, so its lowest part just touches the connector.
            const float y = kToolDockTopY + toolBottomDrop(i) * kToolRackScale;
            Mat4 dock = parent * translate(d.x, y, d.z) * scale(kToolRackScale, kToolRackScale, kToolRackScale);
            drawRobotToolHeadImpl(dock, i);
        }
    }

    // The soft sponge is not a quick-change head; the gripper picks it up.
    if (spongeOnTable)
        drawCleaningSpongeImpl(parent * translate(0.0f, 0.90f, 0.0f));
}

void Scene::drawRobotArm(const Mat4 &parent, float baseYaw, float joint1Angle, float joint2Angle,
                         float wristPitch, float wristRoll, int toolType,
                         float gripperOpen, bool spongeAttached, float polisherAngle, float driverAngle)
{

    Scene::setSurfaceFinish(0.0f, 0.06f, 0.0f, MAT_PAINTED_METAL);

    // ---- FIXED FOUNDATION ------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 0.09f, 0.0f), Vec3(1.70f, 0.18f, 1.70f)), C_ROBOT_DARK);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
        {
            Vec3 bp(0.68f * static_cast<float>(sx), 0.0f, 0.68f * static_cast<float>(sz));
            drawCylinder(parent * translate(bp) * scale(0.095f, 0.20f, 0.095f), C_STEEL,
                         0.0f, 0.0f, 0.65f, MAT_BRUSHED_STEEL); // bolt shank
            drawCylinder(parent * translate(bp.x, 0.205f, bp.z) * scale(0.115f, 0.045f, 0.115f),
                         C_CHROME, 0.0f, 0.0f, 0.65f, MAT_CHROME); // hex head
            drawSphere(parent * place(Vec3(bp.x, 0.235f, bp.z), Vec3(0.045f, 0.030f, 0.045f)),
                       C_CHROME * 1.15f, 0.0f, 0.0f, 0.65f, MAT_CHROME); // highlight dome
        }

    drawCube(parent * place(Vec3(0.0f, 0.48f, 0.0f), Vec3(1.05f, 0.62f, 1.05f)), C_ROBOT);
    drawCube(parent * place(Vec3(0.0f, 0.55f, 0.54f), Vec3(0.72f, 0.10f, 0.05f)), C_NEON, 0.9f);

    Mat4 rotatingBase = parent * rotateY(baseYaw);
    drawCylS(rotatingBase * place(Vec3(0.0f, 0.86f, 0.0f), Vec3(1.00f, 0.18f, 1.00f)), C_ROBOT_DARK);
    drawCube(rotatingBase * place(Vec3(0.0f, 1.00f, 0.0f), Vec3(0.55f, 0.30f, 0.66f)), C_ROBOT);

    // Shoulder pitch.
    Mat4 j1 = rotatingBase * translate(0.0f, ARM_SHOULDER_Y, 0.0f) * rotateZ(joint1Angle);
    drawCylS(j1 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.48f, 0.70f, 0.48f),
                        Vec3(90.0f, 0.0f, 0.0f)),
             C_JOINT);
    // Bearing collar flanges at each end of the joint barrel - the fitted-ring
    // detail that reads as a real pinned bearing rather than a plain cylinder.
    drawCylS(j1 * place(Vec3(0.0f, 0.0f, 0.24f), Vec3(0.53f, 0.07f, 0.53f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawCylS(j1 * place(Vec3(0.0f, 0.0f, -0.24f), Vec3(0.53f, 0.07f, 0.53f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawSphere(j1 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.54f, 0.54f, 0.54f)), C_JOINT * 0.94f);
    drawCube(j1 * place(Vec3(0.0f, ARM_LOWER_LEN * 0.5f, 0.0f), Vec3(0.30f, ARM_LOWER_LEN, 0.30f)), C_ROBOT);
    drawCube(j1 * place(Vec3(0.0f, ARM_LOWER_LEN * 0.32f, 0.0f), Vec3(0.34f, 0.10f, 0.34f)), C_ROBOT_DARK);
    drawCube(j1 * place(Vec3(0.0f, ARM_LOWER_LEN * 0.68f, 0.0f), Vec3(0.34f, 0.10f, 0.34f)), C_ROBOT_DARK);
    // Cable conduit running the length of the segment, with glands where it
    // enters the joint housing at each end.
    drawCube(j1 * place(Vec3(0.0f, ARM_LOWER_LEN * 0.5f, -0.20f), Vec3(0.10f, ARM_LOWER_LEN - 0.30f, 0.10f)), C_ROBOT_DARK);
    drawCylS(j1 * place(Vec3(0.0f, 0.16f, -0.20f), Vec3(0.075f, 0.10f, 0.075f)), C_CHROME * 0.70f,
             0.0f, 0.0f, 0.65f, MAT_CHROME);
    drawCylS(j1 * place(Vec3(0.0f, ARM_LOWER_LEN - 0.16f, -0.20f), Vec3(0.075f, 0.10f, 0.075f)), C_CHROME * 0.70f,
             0.0f, 0.0f, 0.65f, MAT_CHROME);

    // Elbow pitch.
    Mat4 j2 = j1 * translate(0.0f, ARM_LOWER_LEN, 0.0f) * rotateZ(joint2Angle);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.42f, 0.60f, 0.42f),
                        Vec3(90.0f, 0.0f, 0.0f)),
             C_JOINT);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, 0.20f), Vec3(0.465f, 0.06f, 0.465f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, -0.20f), Vec3(0.465f, 0.06f, 0.465f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawSphere(j2 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.47f, 0.47f, 0.47f)), C_JOINT * 0.94f);
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.5f, 0.0f), Vec3(0.26f, ARM_UPPER_LEN, 0.26f)), C_ROBOT);
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.55f, 0.0f), Vec3(0.30f, 0.09f, 0.30f)), C_ROBOT_DARK);
    // Matching cable conduit continuing up the forearm from the shoulder run.
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.5f, -0.17f), Vec3(0.08f, ARM_UPPER_LEN - 0.26f, 0.08f)), C_ROBOT_DARK);
    drawCylS(j2 * place(Vec3(0.0f, 0.14f, -0.17f), Vec3(0.060f, 0.09f, 0.060f)), C_CHROME * 0.70f,
             0.0f, 0.0f, 0.65f, MAT_CHROME);

    Mat4 tip = j2 * translate(0.0f, ARM_UPPER_LEN, 0.0f);
    drawSphere(tip * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.38f, 0.38f, 0.38f)), C_JOINT);
    drawCube(tip * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.28f, 0.20f, 0.28f)), C_JOINT * 0.82f);
    Mat4 wrist = tip * rotateZ(wristPitch) * rotateY(wristRoll);
    drawCylS(wrist * translate(0.0f, -0.08f, 0.0f) * scale(0.31f, 0.13f, 0.31f), C_JOINT);
    Mat4 toolFrame = wrist * translate(0.0f, -0.16f, 0.0f);
    drawRobotToolHeadImpl(toolFrame, toolType, gripperOpen, polisherAngle, driverAngle);

    // The sponge is just parented to the gripper while held. This is the
    // intentionally simple substitute for rigid-body grasping.
    if (toolType == 2 && spongeAttached)
        drawCleaningSpongeImpl(toolFrame * translate(0.0f, -0.91f, 0.0f));

    Scene::clearSurfaceFinish();
}

// Each glyph is 7 strings of 5 characters. Row 0 is the TOP row.
static const char *GLYPH_SPACE[7] = {".....", ".....", ".....", ".....", ".....", ".....", "....."};
static const char *GLYPH_S[7] = {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."};
static const char *GLYPH_M[7] = {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"};
static const char *GLYPH_A[7] = {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"};
static const char *GLYPH_R[7] = {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"};
static const char *GLYPH_T[7] = {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."};
static const char *GLYPH_U[7] = {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."};
static const char *GLYPH_O[7] = {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."};
static const char *GLYPH_E[7] = {"#####", "#....", "#....", "####.", "#....", "#....", "#####"};
static const char *GLYPH_V[7] = {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."};
static const char *GLYPH_I[7] = {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"};
static const char *GLYPH_C[7] = {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."};

// Returns the 7-row pattern for one character (only the letters we need).
static const char *const *glyphFor(char c)
{
    switch (c)
    {
    case 'S':
        return GLYPH_S;
    case 'M':
        return GLYPH_M;
    case 'A':
        return GLYPH_A;
    case 'R':
        return GLYPH_R;
    case 'T':
        return GLYPH_T;
    case 'U':
        return GLYPH_U;
    case 'O':
        return GLYPH_O;
    case 'E':
        return GLYPH_E;
    case 'V':
        return GLYPH_V;
    case 'I':
        return GLYPH_I;
    case 'C':
        return GLYPH_C;
    default:
        return GLYPH_SPACE;
    }
}

// Draws `text` as a grid of small cubes in the parent's XY plane.
static void drawCubeText(const Mat4 &parent, const char *text,
                         float cell, float zFront, const Vec3 &color)
{
    const int GW = 5;                                        // glyph width in cells
    const int GH = 7;                                        // glyph height in cells
    const float advance = static_cast<float>(GW + 1) * cell; // 1 blank column between letters

    int count = static_cast<int>(std::strlen(text));
    float totalW = static_cast<float>(count) * advance - cell;
    float startX = -totalW * 0.5f + cell * 0.5f;
    float topY = (static_cast<float>(GH) * cell) * 0.5f - cell * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        const char *const *g = glyphFor(text[i]);
        float baseX = startX + static_cast<float>(i) * advance;

        for (int row = 0; row < GH; ++row)
        {
            for (int col = 0; col < GW; ++col)
            {
                if (g[row][col] != '#')
                    continue;

                float x = baseX + static_cast<float>(col) * cell;
                float y = topY - static_cast<float>(row) * cell;

                // One lit cell = one small glowing cube.
                Scene::drawCube(parent * Scene::place(Vec3(x, y, zFront),
                                                      Vec3(cell * 0.88f, cell * 0.88f, 0.07f)),
                                color, 1.0f);
            }
        }
    }
}

void Scene::drawNeonSign(const Mat4 &parent)
{
    const float cell = 0.105f;

    // ---- Dark backing panel ------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 0.0f, -0.06f), Vec3(12.60f, 1.90f, 0.12f)), C_WALL_DARK);

    // ---- Glowing frame: four thin strips forming a rectangle ---------------
    drawCube(parent * place(Vec3(0.0f, 0.90f, 0.0f), Vec3(12.60f, 0.10f, 0.16f)), C_NEON, 1.0f);
    drawCube(parent * place(Vec3(0.0f, -0.90f, 0.0f), Vec3(12.60f, 0.10f, 0.16f)), C_NEON, 1.0f);
    drawCube(parent * place(Vec3(-6.25f, 0.0f, 0.0f), Vec3(0.10f, 1.90f, 0.16f)), C_NEON, 1.0f);
    drawCube(parent * place(Vec3(6.25f, 0.0f, 0.0f), Vec3(0.10f, 1.90f, 0.16f)), C_NEON, 1.0f);

    // ---- The text itself, spelled out in cubes -----------------------------
    drawCubeText(parent, "SMART AUTO SERVICE", cell, 0.06f, C_NEON);
}

void Scene::drawOilDrum(const Mat4 &parent, int index)
{
    const float H = 0.88f, R = 0.585f;
    Vec3 body = tintVary(C_DRUM[index & 3], index, 0.05f);

    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), R * 2.2f, R * 2.2f);

    drawCylS(parent * place(Vec3(0.0f, H * 0.5f, 0.0f), Vec3(R, H, R)), body);

    // Three rolling ribs - the pressed rings around the barrel.
    for (int i = 0; i < 3; ++i)
    {
        float y = H * (0.28f + static_cast<float>(i) * 0.22f);
        drawCylS(parent * place(Vec3(0.0f, y, 0.0f), Vec3(R * 1.045f, 0.045f, R * 1.045f)), body * 0.80f);
    }

    // Rolled rims top and bottom.
    drawCylS(parent * place(Vec3(0.0f, H - 0.030f, 0.0f), Vec3(R * 1.06f, 0.060f, R * 1.06f)), body * 0.88f);
    drawCylS(parent * place(Vec3(0.0f, 0.030f, 0.0f), Vec3(R * 1.06f, 0.060f, R * 1.06f)), body * 0.72f);

    // Detail tier: two bung plugs.
    drawCylinder(parent * place(Vec3(R * 0.46f, H + 0.012f, 0.0f), Vec3(0.085f, 0.030f, 0.085f)), C_GALV);
    drawCylinder(parent * place(Vec3(-R * 0.40f, H + 0.010f, R * 0.22f), Vec3(0.050f, 0.024f, 0.050f)), C_GALV);

    // Where it has sat on a wet floor, the bottom 5 cm is always darker.
    drawCylS(parent * place(Vec3(0.0f, 0.028f, 0.0f), Vec3(R * 1.015f, 0.055f, R * 1.015f)), body * 0.52f);
}

void Scene::drawRackingBay(const Mat4 &parent, int levels, int index)
{
    const float W = 2.70f, D = 1.05f, LEVEL_H = 1.32f;
    const float H = LEVEL_H * static_cast<float>(levels) + 0.30f;

    // Two uprights, each a slotted column with a bolted base plate.
    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        drawCube(parent * place(Vec3(sx * W * 0.5f, H * 0.5f, -D * 0.38f), Vec3(0.105f, H, 0.105f)), C_LIFT_BLUE_D);
        drawCube(parent * place(Vec3(sx * W * 0.5f, H * 0.5f, D * 0.38f), Vec3(0.105f, H, 0.105f)), C_LIFT_BLUE_D);

        // Diagonal bracing between the two uprights of the frame.
        for (int i = 0; i < levels * 2; ++i)
        {
            float y = 0.45f + static_cast<float>(i) * (LEVEL_H * 0.5f);
            float a = (i & 1) ? 38.0f : -38.0f;
            drawCube(parent * place(Vec3(sx * W * 0.5f, y, 0.0f), Vec3(0.050f, 0.050f, D * 0.98f),
                                    Vec3(a, 0.0f, 0.0f)),
                     C_LIFT_BLUE_D);
        }

        // Base plate + four floor bolts. Detail tier.
        drawCube(parent * place(Vec3(sx * W * 0.5f, 0.014f, 0.0f), Vec3(0.230f, 0.028f, D * 0.92f)), C_GALV);
        for (int b = 0; b < 4; ++b)
            drawCylinder(parent * place(Vec3(sx * W * 0.5f + ((b & 1) ? 0.070f : -0.070f), 0.036f,
                                             ((b & 2) ? 0.36f : -0.36f) * D),
                                        Vec3(0.034f, 0.026f, 0.034f)),
                         C_ROBOT_DARK);
    }

    // Beams and deck boards for each level.
    for (int L = 0; L < levels; ++L)
    {
        float y = 0.45f + static_cast<float>(L) * LEVEL_H;

        drawCube(parent * place(Vec3(0.0f, y, -D * 0.38f), Vec3(W, 0.105f, 0.060f)), C_JOINT);
        drawCube(parent * place(Vec3(0.0f, y, D * 0.38f), Vec3(W, 0.105f, 0.060f)), C_JOINT);

        for (int b = 0; b < 3; ++b)
            drawCube(parent * place(Vec3(0.0f, y + 0.062f, (static_cast<float>(b) - 1.0f) * D * 0.31f),
                                    Vec3(W - 0.14f, 0.028f, D * 0.28f)),
                     tintVary(C_WOOD, index * 9 + L * 3 + b, 0.06f));
    }
}

void Scene::drawFireExtinguisher(const Mat4 &parent)
{
    const float Y = 1.15f; // height of the bracket

    drawCube(parent * place(Vec3(0.0f, Y, -0.085f), Vec3(0.200f, 0.300f, 0.030f)), C_GALV);       // wall bracket
    drawCube(parent * place(Vec3(0.0f, Y, -0.050f), Vec3(0.145f, 0.060f, 0.045f)), C_ROBOT_DARK); // strap

    drawCylS(parent * place(Vec3(0.0f, Y, 0.0f), Vec3(0.185f, 0.440f, 0.185f)), C_EXTING);                    // body
    drawSphere(parent * place(Vec3(0.0f, Y + 0.215f, 0.0f), Vec3(0.185f, 0.115f, 0.185f)), C_EXTING);         // rounded shoulder
    drawSphere(parent * place(Vec3(0.0f, Y - 0.215f, 0.0f), Vec3(0.185f, 0.085f, 0.185f)), C_EXTING * 0.90f); // rounded base
    drawCylS(parent * place(Vec3(0.0f, Y - 0.250f, 0.0f), Vec3(0.185f, 0.030f, 0.185f)), C_EXTING * 0.80f);
    drawCylS(parent * place(Vec3(0.0f, Y + 0.268f, 0.0f), Vec3(0.070f, 0.075f, 0.070f)), C_GALV); // neck

    drawCube(parent * place(Vec3(0.0f, Y + 0.322f, 0.0f), Vec3(0.145f, 0.048f, 0.060f)), C_ROBOT_DARK);   // handle
    drawCylinder(parent * place(Vec3(0.075f, Y + 0.300f, 0.0f), Vec3(0.055f, 0.022f, 0.055f)), C_CHROME); // gauge
    // Hose: a smooth Bezier loop from the valve down the right side to a clip.
    drawBezierHose(parent, Vec3(0.045f, Y + 0.28f, 0.0f), Vec3(0.20f, Y + 0.30f, 0.03f),
                   Vec3(0.17f, Y - 0.05f, 0.08f), Vec3(0.075f, Y - 0.08f, 0.10f),
                   0.014f, 10, C_ROBOT_DARK, MAT_RUBBER);
    drawCylS(parent * place(Vec3(0.075f, Y - 0.10f, 0.10f), Vec3(0.040f, 0.060f, 0.040f)), C_ROBOT_DARK); // nozzle
    // Label: white plate with a dark header band, wrapped on the front.
    drawCube(parent * place(Vec3(0.0f, Y + 0.040f, 0.0925f), Vec3(0.120f, 0.190f, 0.012f)), Vec3(0.86f, 0.85f, 0.82f));
    drawCube(parent * place(Vec3(0.0f, Y + 0.115f, 0.0995f), Vec3(0.120f, 0.040f, 0.008f)), C_ROBOT_DARK);
    drawCube(parent * place(Vec3(0.0f, Y - 0.010f, 0.0995f), Vec3(0.090f, 0.050f, 0.008f)), Vec3(0.70f, 0.14f, 0.12f));
}

void Scene::drawCeilingFixture(const Mat4 &parent, float dropLen)
{
    // Two chain drops from the ceiling.
    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -0.62f : 0.62f;
        drawCylinder(parent * place(Vec3(sx, dropLen * 0.5f, 0.0f), Vec3(0.022f, dropLen, 0.022f)), C_ROBOT_DARK);
        drawCube(parent * place(Vec3(sx, dropLen, 0.0f), Vec3(0.070f, 0.035f, 0.070f)), C_GALV);
        drawCube(parent * place(Vec3(sx, 0.055f, 0.0f), Vec3(0.055f, 0.045f, 0.055f)), C_GALV);
    }

    // Housing, with reflector wings either side.
    drawCube(parent * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.62f, 0.090f, 0.300f)), C_LAMP_HOUSING);
    drawCube(parent * place(Vec3(0.0f, -0.030f, -0.190f), Vec3(1.62f, 0.100f, 0.090f), Vec3(38.0f, 0.0f, 0.0f)), C_LAMP_HOUSING * 1.25f);
    drawCube(parent * place(Vec3(0.0f, -0.030f, 0.190f), Vec3(1.62f, 0.100f, 0.090f), Vec3(-38.0f, 0.0f, 0.0f)), C_LAMP_HOUSING * 1.25f);

    // Two tubes. Axis along X, so rotateZ(90).
    for (int s = 0; s < 2; ++s)
    {
        float sz = (s == 0) ? -0.075f : 0.075f;
        drawCylS(parent * translate(0.0f, -0.062f, sz) * rotateZ(90.0f) * scale(0.062f, 1.50f, 0.062f), C_TUBE, gSwitches.ceilingLights ? 1.0f : 0.0f);
        // End caps.
        drawCube(parent * place(Vec3(-0.775f, -0.062f, sz), Vec3(0.045f, 0.080f, 0.080f)), C_LAMP_HOUSING);
        drawCube(parent * place(Vec3(0.775f, -0.062f, sz), Vec3(0.045f, 0.080f, 0.080f)), C_LAMP_HOUSING);
    }
}

void Scene::drawWorkStool(const Mat4 &parent, int index)
{
    Mat4 s = parent * rotateY(jitter(index, 45.0f));

    drawContactPatch(s, Vec3(0.0f, 0.0f, 0.0f), 0.60f, 0.60f);

    const Vec3 seatBlue(0.120f, 0.205f, 0.350f);
    const Vec3 seatTop(0.150f, 0.250f, 0.415f);
    const Vec3 seatSeam(0.040f, 0.060f, 0.100f);
    drawSphere(s * place(Vec3(0.0f, 0.492f, 0.0f), Vec3(0.415f, 0.112f, 0.415f)),
               seatBlue, 0.0f, 0.0f, 0.97f, MAT_FABRIC);
    drawSphere(s * place(Vec3(0.0f, 0.535f, 0.0f), Vec3(0.370f, 0.068f, 0.370f)),
               seatTop, 0.0f, 0.0f, 0.97f, MAT_FABRIC);
    drawSphere(s * place(Vec3(0.0f, 0.568f, 0.0f), Vec3(0.050f, 0.026f, 0.050f)),
               seatSeam, 0.0f, 0.0f, 0.97f, MAT_FABRIC);
    drawCylS(s * translate(0.0f, 0.420f, 0.0f) * scale(0.375f, 0.018f, 0.375f),
             seatSeam, 0.0f, 0.0f, 0.97f, MAT_FABRIC);

    drawCylS(s * place(Vec3(0.0f, 0.405f, 0.0f), Vec3(0.185f, 0.040f, 0.185f)), C_GALV);   // seat plate
    drawCylS(s * place(Vec3(0.0f, 0.220f, 0.0f), Vec3(0.070f, 0.370f, 0.070f)), C_CHROME); // gas post

    // Five leg spokes at 72 degrees, each ending in a caster.
    for (int i = 0; i < 5; ++i)
    {
        Mat4 leg = s * rotateY(static_cast<float>(i) * 72.0f);
        drawCube(leg * place(Vec3(0.145f, 0.055f, 0.0f), Vec3(0.270f, 0.040f, 0.048f)), C_ROBOT_DARK);
        drawCylinder(leg * translate(0.270f, 0.030f, 0.0f) * rotateZ(90.0f) * scale(0.060f, 0.028f, 0.060f), C_RUBBER);
    }
}

void Scene::drawBoxStack(const Mat4 &parent, int count, int index)
{
    float y = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        int seed = index * 23 + i;
        float w = 0.520f + jitter(seed, 0.090f);
        float h = 0.330f + jitter(seed + 1, 0.060f);
        float d = 0.420f + jitter(seed + 2, 0.070f);

        Mat4 b = parent * translate(jitter(seed + 3, 0.075f), y + h * 0.5f, jitter(seed + 4, 0.075f)) * rotateY(jitter(seed + 5, 14.0f));

        drawCube(b * scale(w, h, d), tintVary(C_CARDBOARD, seed, 0.07f));
        // Packing tape down the seam, and a shipping label.
        drawCube(b * translate(0.0f, h * 0.5f + 0.004f, 0.0f) * scale(0.070f, 0.008f, d * 1.01f), Vec3(0.560f, 0.520f, 0.430f));
        drawCube(b * translate(0.0f, 0.0f, d * 0.5f + 0.004f) * scale(w * 0.42f, h * 0.34f, 0.006f), Vec3(0.650f, 0.640f, 0.615f));
        y += h;
    }
}

void Scene::drawWorkbench(const Mat4 &parent, int index)
{
    const float W = 2.35f, D = 0.78f, TOP_Y = 0.90f;

    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), W + 0.20f, D + 0.20f);

    // Four box legs with levelling feet - detail tier.
    for (int i = 0; i < 4; ++i)
    {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        drawCube(parent * place(Vec3(sx * (W * 0.45f), TOP_Y * 0.5f + 0.030f, sz * (D * 0.35f)),
                                Vec3(0.080f, TOP_Y - 0.030f, 0.080f)),
                 C_SHELF_DARK);
        drawCylinder(parent * place(Vec3(sx * (W * 0.45f), 0.016f, sz * (D * 0.35f)), Vec3(0.105f, 0.032f, 0.105f)), C_GALV);
    }

    // Thick timber top with a lit chamfer, and a steel edge strip.
    drawPanel(parent, Vec3(0.0f, TOP_Y, 0.0f), Vec3(W, 0.085f, D), tintVary(C_WOOD, index, 0.05f));
    drawCube(parent * place(Vec3(0.0f, TOP_Y, D * 0.5f + 0.012f), Vec3(W, 0.100f, 0.020f)), C_GALV);

    // Lower shelf with three drawers under the other end.
    drawCube(parent * place(Vec3(W * 0.18f, 0.320f, 0.0f), Vec3(W * 0.58f, 0.045f, D * 0.86f)), C_SHELF_DARK);
    for (int i = 0; i < 3; ++i)
    {
        float y = 0.430f + static_cast<float>(i) * 0.150f;
        drawCube(parent * place(Vec3(W * 0.18f, y, D * 0.5f - 0.020f), Vec3(W * 0.52f, 0.130f, 0.030f)), C_SHELF);
        drawCylinder(parent * translate(W * 0.18f, y, D * 0.5f + 0.010f) * rotateZ(90.0f) * scale(0.022f, 0.380f, 0.022f), C_CHROME);
    }

    // ---- Pegboard and the tools hung on it --------------------------------
    drawPanel(parent, Vec3(0.0f, TOP_Y + 0.82f, -D * 0.5f + 0.030f), Vec3(W, 1.45f, 0.040f), C_SHELF_DARK);
    // The peg holes: a sparse grid of tiny dark cubes.
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 14; ++c)
            drawCube(parent * place(Vec3((static_cast<float>(c) - 6.5f) * 0.155f,
                                         TOP_Y + 0.35f + static_cast<float>(r) * 0.225f,
                                         -D * 0.5f + 0.052f),
                                    Vec3(0.022f, 0.022f, 0.006f)),
                     C_HAZARD_K);

    // 16 hanging tools: alternating thin cylinders (spanners, screwdrivers)
    // and small boxes (pliers, sockets), at loosely varying heights.
    for (int i = 0; i < 16; ++i)
    {
        int seed = index * 41 + i;
        float x = (static_cast<float>(i) - 7.5f) * 0.142f;
        float y = TOP_Y + 0.62f + jitter(seed, 0.30f) + ((i & 1) ? 0.26f : 0.0f);
        float z = -D * 0.5f + 0.080f;

        if (i % 3 == 0)
            drawCube(parent * place(Vec3(x, y, z), Vec3(0.060f, 0.185f, 0.030f),
                                    Vec3(0.0f, 0.0f, jitter(seed + 1, 8.0f))),
                     C_ROBOT_DARK);
        else
            drawCylS(parent * translate(x, y, z) * rotateZ(jitter(seed + 2, 7.0f)) * scale(0.030f, 0.255f, 0.030f), (i & 1) ? C_CHROME : C_STEEL);
    }
}

void Scene::drawSupportColumn(const Mat4 &parent, float height)
{
    drawCube(parent * place(Vec3(0.0f, height * 0.5f, 0.0f), Vec3(0.70f, height, 0.90f)), C_COLUMN);

    // Cast base plate and its four anchor bolts.
    drawCube(parent * place(Vec3(0.0f, 0.030f, 0.0f), Vec3(0.98f, 0.060f, 1.18f)), C_GALV);
    drawCube(parent * place(Vec3(0.0f, 0.062f, 0.0f), Vec3(0.98f, 0.006f, 1.18f)), C_STEEL * 0.85f);
    for (int i = 0; i < 4; ++i)
        drawCylinder(parent * place(Vec3((i & 1) ? 0.400f : -0.400f, 0.078f, (i & 2) ? 0.480f : -0.480f),
                                    Vec3(0.070f, 0.040f, 0.070f)),
                     C_ROBOT_DARK);

    // Hazard band around the bottom: alternating yellow and black blocks.
    for (int i = 0; i < 6; ++i)
    {
        Vec3 c = (i & 1) ? C_HAZARD_Y : C_HAZARD_K;
        drawCube(parent * place(Vec3(0.0f, 0.34f + static_cast<float>(i) * 0.075f, 0.455f),
                                Vec3(0.72f, 0.078f, 0.014f), Vec3(38.0f, 0.0f, 0.0f)),
                 c);
    }

    // Cap plate where the column meets the beam.
    drawCube(parent * place(Vec3(0.0f, height - 0.040f, 0.0f), Vec3(0.86f, 0.080f, 1.06f)), C_GALV);
    drawCube(parent * place(Vec3(0.0f, height - 0.006f, 0.0f), Vec3(0.86f, 0.006f, 1.06f)), C_STEEL * 0.85f);

}

void Scene::drawWheelChock(const Mat4 &parent, int index)
{
    Mat4 c = parent * rotateY(jitter(index, 10.0f));
    drawCube(c * place(Vec3(0.0f, 0.055f, 0.0f), Vec3(0.230f, 0.110f, 0.300f)), C_HAZARD_Y);
    drawCube(c * place(Vec3(0.0f, 0.130f, -0.055f), Vec3(0.230f, 0.180f, 0.210f), Vec3(31.0f, 0.0f, 0.0f)), C_HAZARD_Y);
    drawCube(c * place(Vec3(0.0f, 0.062f, 0.130f), Vec3(0.235f, 0.050f, 0.050f)), C_HAZARD_K);
}

static float serviceClamp01(float v)
{
    return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static Vec3 serviceMix(const Vec3 &a, const Vec3 &b, float t)
{
    t = serviceClamp01(t);
    return a * (1.0f - t) + b * t;
}

static void drawServiceCarEffects(const Mat4 &car, const Scene::ServiceVisualState &service)
{
    (void)car;
    (void)service;
}

static void drawServiceWheelEffects(const Mat4 &car, const Scene::ServiceVisualState &service)
{
    const float track = CAR_FTRACK + CAR_WHEEL_OUTSET;
    const float radius = CAR_FWHEEL_R;
    const float width = CAR_FWHEEL_W;
    const float outward = 1.0f; // left side of vehicle
    Mat4 hub = car * translate(-track, radius, CAR_FAXLE_Z) * rotateZ(90.0f);

    const float lugRadius = radius * 0.28f;
    const float looseFaceY = outward * width * 0.605f;
    const float seatedFaceY = outward * width * 0.525f;
    const Vec3 lugMetal(0.46f, 0.49f, 0.52f);

    for (int i = 0; i < 5; ++i)
    {
        const float p = serviceClamp01(service.lugProgress[i]);
        const float a = 90.0f - static_cast<float>(i) * 72.0f;
        const float ar = radians(a);
        const float faceY = looseFaceY + (seatedFaceY - looseFaceY) * p;
        // In the wheel frame, local X becomes vehicle vertical after the
        // 90-degree axle rotation, while local Z stays longitudinal.
        const float lx = std::sin(ar) * lugRadius;
        const float lz = std::cos(ar) * lugRadius;
        Mat4 lugFrame = hub * translate(lx, faceY, lz) * rotateY(service.driverAngle * (0.25f + 0.75f * p));

        Scene::drawCylS(lugFrame * scale(0.080f, 0.060f, 0.080f),
                        serviceMix(lugMetal * 0.78f, C_CHROME, p), 0.0f, 0.82f, 0.30f, MAT_PAINTED_METAL);
        // Tiny bright line on each nut makes inward motion and rotation legible.
        Scene::drawCube(lugFrame * Scene::place(Vec3(0.037f, outward * 0.033f, 0.0f), Vec3(0.028f, 0.012f, 0.012f)),
                        C_CHROME, 0.18f, 0.90f, 0.22f, MAT_PAINTED_METAL);
    }

    // Amber before-service ring fades out while the green secured ring fades in.
    float avg = 0.0f;
    for (int i = 0; i < 5; ++i)
        avg += serviceClamp01(service.lugProgress[i]);
    avg *= 0.20f;
    auto drawStatusRing = [&](float ringRadius, float faceOffset,
                              const Vec3 &colour, float glow)
    {
        if (glow <= 0.01f)
            return;
        for (int i = 0; i < 12; ++i)
        {
            Mat4 seg = hub * translate(0.0f, outward * width * faceOffset, 0.0f) * rotateY(static_cast<float>(i) * 30.0f) * translate(ringRadius, 0.0f, 0.0f);
            Scene::drawCube(seg * scale(0.030f, 0.012f, 0.055f),
                            colour, glow, 0.0f, 0.30f, MAT_PAINT);
        }
    };

    const float amber = (1.0f - avg) * 0.80f;
    drawStatusRing(radius * 0.46f, 0.545f, Vec3(0.95f, 0.54f, 0.08f), amber);

    const float green = serviceClamp01(service.wheelServiceGlow);
    drawStatusRing(radius * 0.52f, 0.560f, Vec3(0.16f, 0.88f, 0.34f), 0.95f * green);
}

static void drawServiceIndicators(const Mat4 &world, const Scene::ServiceVisualState &service)
{
    // (the floating amber beacon and the green lamp above the tool table were removed)
    (void)world; (void)service;
}

Mat4 Scene::serviceCarMatrix(float liftHeight, float carYaw)
{
    const float driveZ = CAR_DRIVE_DISTANCE * gSwitches.carDrive;

    const float rampTopY = liftTopY(0.0f);             // platform at rest
    const float rampTopZ = LIFT_Z + LIFT_PAD_L * 0.5f; // where ramp meets pad
    const float rampFootZ = rampTopZ + LIFT_RAMP_LEN;  // where it meets the slab

    auto rampSurfaceY = [&](float z) -> float
    {
        if (z <= rampTopZ)
            return rampTopY;
        if (z >= rampFootZ)
            return 0.0f;
        return rampTopY * (rampFootZ - z) / LIFT_RAMP_LEN;
    };

    const float carZ = LIFT_Z + driveZ;
    const float yFront = rampSurfaceY(carZ + CAR_FAXLE_Z);
    const float yRear = rampSurfaceY(carZ + CAR_RAXLE_Z);
    const float wheelbase = CAR_RAXLE_Z - CAR_FAXLE_Z;

 
    const float carRampY = (yFront + yRear) * 0.5f - rampTopY;
    const float carPitch = std::atan2(yFront - yRear, wheelbase) * 180.0f / PI_F;

    const float onLift = std::fmax(0.0f, std::fmin(1.0f, (rampTopZ + 0.05f - (carZ + CAR_RAXLE_Z)) / 0.30f));
    const float turnY = liftTopY(0.0f) + (liftTopY(liftHeight) - liftTopY(0.0f)) * onLift;
    return translate(0.0f, turnY + carRampY, LIFT_Z + driveZ) * rotateX(carPitch) * rotateY(carYaw);
}

Vec3 Scene::headlightLocalPosition(int side)
{
    const float y = CT_NOSE_TOP_Y - 0.028f; // middle of the light blade (0.923)
    return Vec3(static_cast<float>(side) * 0.62f, y, CT_NOSE_Z + 0.025f);
}

void Scene::drawScene(float doorOffset,
                      float liftHeight,
                      float robotBaseYaw,
                      float joint1Angle,
                      float joint2Angle,
                      float wristPitch,
                      float wristRoll,
                      int robotTool,
                      const ServiceVisualState &service)
{
    // The world root. Everything is drawn relative to this.
    Mat4 world = mat4Identity();

    // ---- 1. The building and what is outside it ----------------------------
    drawGarage(world);
    drawExterior(world);
    drawGarageDoor(world, doorOffset);

    // ---- 2. Eight structural columns, four down each side --------------------
    const float COL_Z[4] = {-9.5f, -4.0f, 1.5f, 7.0f};
    for (int i = 0; i < 4; ++i)
    {
        drawSupportColumn(world * translate(-13.30f, 0.0f, COL_Z[i]), GARAGE_H);
        drawSupportColumn(world * translate(13.30f, 0.0f, COL_Z[i]), GARAGE_H);
    }

    // ---- 3. Neon sign on the back wall ---------------------------------------
    if (gSwitches.neonSign)
        drawNeonSign(world * translate(0.0f, 5.15f, -12.88f));

    drawHydraulicLift(world, liftHeight);
    drawHydraulicLift(world * translate(-8.60f, 0.0f, 5.20f), 2.40f);
    drawHydraulicLift(world * translate(-8.60f, 0.0f, -4.60f), 0.00f);

    // The approach ramp the vehicle drives up to reach the main lift.
    drawLiftApproachRamp(world);

    drawLiftHydraulicHose(world * translate(-4.55f, 0.0f, LIFT_Z - 1.95f), 0);
    drawLiftHydraulicHose(world * translate(-4.55f, 0.0f, LIFT_Z - 1.95f), 1);

    drawInspectionMonitorHousing(world);

    const float turnY = liftTopY(liftHeight);
    const float TURN_D = 4.20f;
    const float discTopY = turnY + 0.006f;
    drawCylS(world * translate(0.0f, discTopY - 0.025f, LIFT_Z) * scale(TURN_D, 0.050f, TURN_D), C_LIFT_BLUE_D * 1.35f, 0.0f, 0.42f, 0.50f, MAT_TREAD);
    // Skirt under the plate, so it reads as a thick rotating table carried by
    // the runways rather than as a disc hanging in mid-air.
    drawCylS(world * translate(0.0f, discTopY - 0.085f, LIFT_Z) * scale(TURN_D - 0.22f, 0.080f, TURN_D - 0.22f), C_LIFT_BLUE_D, 0.0f, 0.35f, 0.58f);
    for (int i = 0; i < 8; ++i)
        drawCube(world * translate(0.0f, discTopY + 0.003f, LIFT_Z) * rotateY(service.carYaw + static_cast<float>(i) * 45.0f) * place(Vec3(1.76f, 0.0f, 0.0f), Vec3(0.46f, 0.006f, 0.075f)),
                 C_WHITE_LINE, 0.0f, 0.0f, 0.55f, MAT_PAINT);

    Mat4 serviceCar = world * serviceCarMatrix(liftHeight, service.carYaw);
    drawCar(serviceCar, C_CAR_ORANGE, service.polishAmount,
            gSwitches.wheelSpin, gSwitches.vehicleLights, service.dirtAmount);

    if (gSwitches.carDrive < 0.02f)
    {
        drawServiceCarEffects(serviceCar, service);
        drawServiceWheelEffects(serviceCar, service);
    }

    // ---- 6. Robotic inspection arm ---------------------------------------------
    drawRobotArm(world * translate(ARM_X, 0.0f, ARM_Z), robotBaseYaw, joint1Angle, joint2Angle,
                 wristPitch, wristRoll, robotTool, service.gripperOpen, service.spongeAttached, service.polisherAngle, service.driverAngle);
    drawRobotToolStation(world * translate(ARM_X - 3.30f, 0.0f, ARM_Z + 0.35f), robotTool, service.spongeOnTable);
    drawServiceIndicators(world, service);

    drawParkingBay(world * translate(PARK_X, 0.0f, PARK_Z_A));
    drawParkingBay(world * translate(PARK_X, 0.0f, PARK_Z_B));

    drawCar(world * translate(PARK_X, 0.0f, PARK_Z_A) * messy(101, 0.16f, 5.0f, 0.0f) * rotateY(90.0f),
            C_CAR_WHITE, 0.0f, 0.0f, gSwitches.vehicleLights);
    drawCar(world * translate(PARK_X, 0.0f, PARK_Z_B) * messy(207, 0.16f, 5.0f, 0.0f) * rotateY(90.0f),
            C_CAR_GUNMETAL, 0.0f, 0.0f, gSwitches.vehicleLights);

    // Wheel chocks at their wheels - 4 pairs.
    for (int i = 0; i < 2; ++i)
    {
        float z = (i == 0) ? PARK_Z_A : PARK_Z_B;
        drawWheelChock(world * translate(PARK_X - 1.35f, 0.0f, z - 0.95f) * rotateY(90.0f), i * 2);
        drawWheelChock(world * translate(PARK_X - 1.35f, 0.0f, z + 0.95f) * rotateY(90.0f), i * 2 + 1);
        drawWheelChock(world * translate(PARK_X + 1.60f, 0.0f, z - 0.95f) * rotateY(-90.0f), i * 2 + 4);
        drawWheelChock(world * translate(PARK_X + 1.60f, 0.0f, z + 0.95f) * rotateY(-90.0f), i * 2 + 5);
    }

    for (int i = 0; i < 4; ++i)
    {
        float z = -9.6f + static_cast<float>(i) * 5.20f;
        drawCeilingFixture(world * translate(-6.40f + jitter(i * 3, 0.10f), CEIL_Y - 0.62f, z), 0.62f);
        drawCeilingFixture(world * translate(6.40f + jitter(i * 3 + 1, 0.10f), CEIL_Y - 0.62f, z + 0.55f), 0.62f);
    }

    // ---- 10. Back wall: four racking bays and the benches ------------------------
    drawRackingBay(world * translate(-11.00f, 0.0f, -12.25f), 3, 1);
    drawRackingBay(world * translate(-7.70f, 0.0f, -12.25f), 3, 2);
    drawRackingBay(world * translate(7.70f, 0.0f, -12.25f), 3, 3);
    drawRackingBay(world * translate(11.00f, 0.0f, -12.25f), 2, 4);


    drawBoxStack(world * translate(7.10f, 0.525f, -12.25f), 2, 11);
    drawBoxStack(world * translate(8.35f, 0.525f, -12.25f), 3, 12);
    drawBoxStack(world * translate(7.70f, 1.845f, -12.25f), 2, 13);

    drawWorkbench(world * translate(-3.00f, 0.0f, -12.10f), 1);
    drawWorkbench(world * translate(2.70f, 0.0f, -12.10f), 2);
    drawWorkbench(world * translate(12.72f, 0.0f, -1.60f) * rotateY(-90.0f), 3);

    const float DRUM_XZ[12][2] = {
        {12.55f,  8.45f}, {12.55f,  9.72f}, {12.55f, 10.99f}, {12.55f, 12.22f},
        {11.28f,  8.68f}, {11.28f,  9.95f}, {11.28f, 11.22f}, {11.28f, 12.43f},
        {10.02f,  8.47f}, {10.02f,  9.74f}, {10.02f, 11.01f}, {10.02f, 12.20f}
    };
    for (int i = 0; i < 12; ++i)
        drawOilDrum(world * translate(DRUM_XZ[i][0], 0.0f, DRUM_XZ[i][1])
                    * messy(i + 50, 0.045f, 18.0f, 0.025f), i + 50);

    drawFireExtinguisher(world * translate(-12.78f, 0.0f, -4.00f) * rotateY(90.0f));
    drawFireExtinguisher(world * translate(-12.78f, 0.0f, 1.50f) * rotateY(90.0f));
    drawFireExtinguisher(world * translate(12.78f, 0.0f, -9.50f) * rotateY(-90.0f));
    drawFireExtinguisher(world * translate(12.78f, 0.0f, 7.00f) * rotateY(-90.0f));
    drawFireExtinguisher(world * translate(-1.40f, 0.0f, -12.72f));

    drawWorkStool(world * translate(-3.40f, 0.0f, -10.90f), 1);
    drawWorkStool(world * translate(3.60f, 0.0f, -10.80f), 2);
    drawWorkStool(world * translate(11.70f, 0.0f, -1.60f), 3);
    drawWorkStool(world * translate(-5.60f, 0.0f, 3.40f), 4);

    for (int i = 0; i < 9; ++i)
    {
        float x = jitter(i * 4 + 1, 2.60f);
        float z = LIFT_Z + jitter(i * 4 + 2, 3.10f);
        float w = 0.55f + hash01(i * 4 + 3) * 1.30f;
        drawCube(world * place(Vec3(x, 0.014f, z), Vec3(w, 0.014f, w * 0.78f),
                               Vec3(0.0f, jitter(i * 4, 45.0f), 0.0f)),
                 C_FLOOR_STAIN);
    }
}

void Scene::drawLiftHydraulicHose(const Mat4 &parent, int index)
{
    const Vec3 hose(0.072f, 0.076f, 0.084f);
    const float lane = (index == 0) ? -0.16f : 0.16f; // the two hoses run side by side
    const float bow = 0.55f + hash01(index * 7 + 3) * 0.30f;

    // Power pack: a small motor-and-tank unit standing on the slab.
    if (index == 0)
    {
        drawCube(parent * place(Vec3(0.0f, 0.46f, 0.0f), Vec3(0.52f, 0.92f, 0.44f)),
                 C_LIFT_BLUE, 0.0f, 0.30f, 0.55f, MAT_PAINTED_METAL);
        drawCube(parent * place(Vec3(0.0f, 0.95f, 0.0f), Vec3(0.56f, 0.06f, 0.48f)),
                 C_LIFT_BLUE_D);
        drawCylS(parent * translate(0.0f, 1.10f, 0.0f) * rotateZ(90.0f) * scale(0.26f, 0.44f, 0.26f),
                 C_GALV, 0.0f, 0.55f, 0.45f, MAT_GALV);
        drawCube(parent * place(Vec3(0.0f, 0.035f, 0.0f), Vec3(0.66f, 0.07f, 0.58f)), C_LIFT_BLUE_D);
    }


    drawBezierHose(parent,
                   Vec3(0.20f, 0.30f + lane * 0.4f, lane),
                   Vec3(1.05f, 0.09f, lane - bow),
                   Vec3(2.20f, 0.07f, lane - bow),
                   Vec3(3.05f, 0.14f, lane),
                   0.045f, 14, hose, MAT_RUBBER);

    // Crimped fittings at both ends, so the hose terminates in hardware
    // instead of simply stopping.
    drawCylS(parent * translate(0.20f, 0.30f + lane * 0.4f, lane) * rotateZ(90.0f) * scale(0.070f, 0.13f, 0.070f), C_CHROME * 0.72f, 0.0f, 0.70f, 0.38f, MAT_CHROME);
    drawCylS(parent * translate(3.05f, 0.14f, lane) * rotateZ(90.0f) * scale(0.070f, 0.13f, 0.070f), C_CHROME * 0.72f, 0.0f, 0.70f, 0.38f, MAT_CHROME);
}

static int gCountLift = 0, gCountCar = 0, gCountRobot = 0;
void Scene::lastRecordCounts(int &lift, int &car, int &robot) { lift = gCountLift; car = gCountCar; robot = gCountRobot; }
static const Vec3 kMonitorPos(4.40f, 0.0f, -5.60f);
static const float kMonitorYaw = -32.0f; // turns the screen toward the aisle

Mat4 Scene::inspectionMonitorMatrix()
{
    return translate(kMonitorPos) * rotateY(kMonitorYaw) * translate(0.0f, 1.95f, 0.064f) * scale(2.0f, 1.25f, 0.004f);
}

void Scene::drawInspectionMonitorHousing(const Mat4 &parent)
{
    const Mat4 m = parent * translate(kMonitorPos) * rotateY(kMonitorYaw);
    drawCylS(m * place(Vec3(0.0f, 0.04f, 0.0f), Vec3(0.90f, 0.08f, 0.90f)), C_ROBOT_DARK);
    drawCylS(m * place(Vec3(0.0f, 0.80f, -0.02f), Vec3(0.12f, 1.56f, 0.12f)), C_GALV);
    drawCube(m * place(Vec3(0.0f, 1.95f, 0.0f), Vec3(2.16f, 1.41f, 0.12f)), C_ROBOT_DARK);
    drawCube(m * place(Vec3(0.0f, 1.95f, -0.12f), Vec3(1.00f, 0.70f, 0.14f)), C_ROBOT_DARK * 1.2f);
    drawCube(m * place(Vec3(0.0f, 1.20f, 0.01f), Vec3(2.16f, 0.04f, 0.13f)), C_NEON_CYAN, 0.6f);
}

void Scene::recordInspectionGeometry(std::vector<RtItem> &out, float liftHeight,
                                     float robotBaseYaw, float joint1Angle, float joint2Angle,
                                     float wristPitch, float wristRoll, int robotTool,
                                     const ServiceVisualState &service)
{
    buildCybertruckShellMesh();
    const Mat4 world = mat4Identity();
    gRec = &out;
    const size_t start = out.size();
    drawHydraulicLift(world, liftHeight);
    gCountLift = static_cast<int>(out.size() - start);
    const size_t carStart = out.size();
    drawCar(world * serviceCarMatrix(liftHeight, service.carYaw), C_CAR_ORANGE, 0.0f,
            gSwitches.wheelSpin, false, 0.0f);
    gCountCar = static_cast<int>(out.size() - carStart);
    const size_t robotStart = out.size();
    drawRobotArm(world * translate(ARM_X, 0.0f, ARM_Z), robotBaseYaw, joint1Angle, joint2Angle,
                 wristPitch, wristRoll, robotTool, service.gripperOpen, service.spongeAttached,
                 service.polisherAngle, service.driverAngle);
    gCountRobot = static_cast<int>(out.size() - robotStart);
    gRec = nullptr;
}
