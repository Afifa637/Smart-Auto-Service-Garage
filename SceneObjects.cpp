// ============================================================================
//  SceneObjects.cpp  -  Building a whole service garage out of one cube
// ============================================================================

#include <glad/glad.h>
#include <cstring>
#include <vector>

#include "SceneObjects.h"
#include "Primitives.h"


// ============================================================================
//  SECTION 0 - SHADER PLUMBING
//  The uniform locations are looked up once instead of every draw call.
// ============================================================================

static unsigned int gProgram = 0;
static int gLocModel = -1;
static int gLocNormalMat = -1;
static int gLocColor = -1;
static int gLocEmissive = -1;
static int gLocMetallic = -1;
static int gLocRoughness = -1;
static int gLocMaterialType = -1;

// Presentation-only draw filter: progressively reveals existing draw calls.
static bool gTeachingDrawEnabled = false;
static int  gTeachingVisibleLimit = -1;
static int  gTeachingHighlightFrom = -1;
static int  gTeachingDrawCounter = 0;

static bool teachingPrepareColor(const Vec3& original, Vec3& shown)
{
    shown = original;
    if (!gTeachingDrawEnabled) return true;
    ++gTeachingDrawCounter;
    if (gTeachingVisibleLimit >= 0 && gTeachingDrawCounter > gTeachingVisibleLimit) return false;
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
}


// ============================================================================
//  SECTION 1 - THE TWO DRAWING HELPERS
//
//  This is the whole modelling system. Everything below is built from these.
// ============================================================================

// ----------------------------------------------------------------------------
//  place() - turns three human-readable numbers into a model matrix.
//
//      pos     where the CENTRE of the box should sit
//      size    how wide / tall / deep the box should become
//      rotDeg  rotation in degrees about X, Y and Z
//
//  The multiplication order matters:
//
//      translate * rotate * scale
//
//  Matrices apply right to left, so the unit cube is SCALED first (turning
//  it into a slab, a post or a panel), then ROTATED about its own centre,
//  and only then TRANSLATED to its final place. Doing it in any other order
//  would rotate the object about the world origin and fling it across the room.
// ----------------------------------------------------------------------------
Mat4 Scene::place(const Vec3& pos, const Vec3& size, const Vec3& rotDeg)
{
    return translate(pos)
        * rotateY(rotDeg.y) * rotateX(rotDeg.x) * rotateZ(rotDeg.z)
        * scale(size);
}

Mat4 Scene::place(const Vec3& pos, const Vec3& size)
{
    return translate(pos) * scale(size);
}



// Material ids shared with Shader.cpp.  They intentionally stay integer based
// to keep the shader interface OpenGL-3.3 friendly.
static const int MAT_CONCRETE = 1;
static const int MAT_WALL = 2;
static const int MAT_BRUSHED_STEEL = 3;
static const int MAT_GALV = 4;
static const int MAT_CHROME = 5;
static const int MAT_PLASTIC = 6;
static const int MAT_WOOD = 7;
static const int MAT_CARDBOARD = 8;
static const int MAT_RUBBER = 9;
static const int MAT_PAINTED_METAL = 10;
static const int MAT_GLASS = 11;
static const int MAT_TIRE = 12;
static const int MAT_STAINLESS = 13;
static const int MAT_BRAKE = 14;
static const int MAT_ASPHALT = 15;
static const int MAT_RUST = 16;
static const int MAT_PAINT = 17;
static const int MAT_CARBON = 18;

// ---------------------------------------------------------------------------
// Material inference keeps the original object-building API compact while
// upgrading every existing prop to a physically distinct surface.  The scene
// was authored with a disciplined palette, so hue/value + the optional
// metallic/roughness hints are enough to identify concrete, steel, rubber,
// cardboard, glass, painted metal, etc.  The Cybertruck batches pass explicit
// material ids below where exact control matters most.
// ---------------------------------------------------------------------------
static int inferMaterialType(const Vec3& c, float emissive, float metallic, float roughness)
{
    if (emissive > 0.20f) return 17; // light/paint family; emissive dominates

    const float mx = (c.x > c.y ? (c.x > c.z ? c.x : c.z) : (c.y > c.z ? c.y : c.z));
    const float mn = (c.x < c.y ? (c.x < c.z ? c.x : c.z) : (c.y < c.z ? c.y : c.z));
    const float avg = (c.x + c.y + c.z) / 3.0f;
    const float chroma = mx - mn;

    if (metallic > 0.72f && metallic < 0.88f && roughness > 0.30f && roughness < 0.58f && avg > 0.34f && avg < 0.58f) return 14; // scored brake iron
    if (metallic > 0.82f && chroma < 0.11f) return 13; // stainless / polished metal
    if (roughness < 0.14f && avg < 0.24f) return 11;   // tinted glass
    if (roughness > 0.90f && avg < 0.16f) return 12;   // tyre/rubber
    if (metallic > 0.62f && avg > 0.58f) return 5;     // chrome / bright alloy
    if (metallic > 0.45f) return 3;                    // generic brushed metal
    if (c.z > c.x + 0.035f && avg < 0.23f && roughness < 0.78f) return 11; // blue-black glass
    if (chroma > 0.28f) return 10;                     // saturated powder-coated machinery

    // Neutral palette families.
    if (chroma < 0.065f)
    {
        if (avg < 0.125f) return 9;                    // rubber/dark gasket
        if (avg < 0.185f) return 10;                   // powder-coated dark steel
        if (avg < 0.215f) return 1;                    // concrete floor / dark slab
        if (avg < 0.255f) return 2;                    // wall/ceiling industrial paint
        if (avg < 0.315f) return 15;                   // asphalt / dark aggregate
        if (avg > 0.655f) return 5;                    // chrome / bright trim
        if (avg > 0.495f) return 4;                    // galvanised / steel
        return 10;                                     // painted sheet metal
    }

    // Brown materials: rust, wood and cardboard are deliberately different.
    if (c.x > c.y && c.y > c.z)
    {
        if (c.x > 0.40f && c.y > 0.32f && c.z > 0.24f) return 8; // cardboard
        if (c.x > 0.38f && c.y > 0.27f) return 7;                 // wood
        if (c.x > 0.28f && c.y < 0.28f) return 16;                // rust/oxidation
        if (c.x > 0.55f && c.y > 0.35f && c.z < 0.22f) return 17;// safety/paint line
    }

    // Saturated machinery colours are powder-coated metal or plastics.
    if (avg < 0.22f && chroma < 0.18f) return 18;       // carbon/dark composite
    if (roughness > 0.80f && avg < 0.35f) return 6;    // matte plastic
    return 10;                                          // painted metal default
}

static void resolveMaterial(int type, float& metallic, float& roughness)
{
    switch (type)
    {
    case 1:  metallic = 0.0f;  roughness = (roughness < 0.82f ? 0.86f : roughness); break;
    case 2:  metallic = 0.0f;  roughness = (roughness < 0.72f ? 0.78f : roughness); break;
    case 3:  metallic = (metallic < 0.68f ? 0.72f : metallic); roughness = (roughness > 0.48f ? 0.38f : roughness); break;
    case 4:  metallic = (metallic < 0.55f ? 0.62f : metallic); roughness = 0.48f; break;
    case 5:  metallic = (metallic < 0.90f ? 0.95f : metallic); roughness = (roughness > 0.22f ? 0.15f : roughness); break;
    case 6:  metallic = 0.0f;  roughness = (roughness < 0.48f ? 0.58f : roughness); break;
    case 7:  metallic = 0.0f;  roughness = 0.72f; break;
    case 8:  metallic = 0.0f;  roughness = 0.90f; break;
    case 9:  metallic = 0.0f;  roughness = 0.93f; break;
    case 10: metallic = 0.03f; roughness = (roughness > 0.72f ? 0.58f : roughness); break;
    case 11: metallic = 0.04f; roughness = 0.075f; break;
    case 12: metallic = 0.0f;  roughness = 0.95f; break;
    case 13: metallic = (metallic < 0.90f ? 0.92f : metallic); roughness = (roughness > 0.34f ? 0.25f : roughness); break;
    case 14: metallic = 0.78f; roughness = 0.45f; break;
    case 15: metallic = 0.0f;  roughness = 0.96f; break;
    case 16: metallic = 0.22f; roughness = 0.84f; break;
    case 17: metallic = 0.0f;  roughness = 0.68f; break;
    case 18: metallic = 0.10f; roughness = 0.44f; break;
    default: break;
    }
}

static void uploadMaterialUniforms(const Vec3& color, float emissive, float metallic,
                                   float roughness, int forcedType = -1)
{
    int type = (forcedType >= 0) ? forcedType : inferMaterialType(color, emissive, metallic, roughness);
    resolveMaterial(type, metallic, roughness);
    glUniform3f(gLocColor, color.x, color.y, color.z);
    glUniform1f(gLocEmissive, emissive);
    glUniform1f(gLocMetallic, metallic);
    glUniform1f(gLocRoughness, roughness);
    glUniform1i(gLocMaterialType, type);
}

void Scene::drawCube(const Mat4& model, const Vec3& color, float emissive, float metallic, float roughness, int materialType)
{
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor)) return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawCubeMesh();
}


void Scene::drawCylinder(const Mat4& model, const Vec3& color, float emissive, float metallic, float roughness, int materialType)
{
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor)) return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawCylinderMesh();
}


// ---------------------------------------------------------------------------
//  drawCylS - the SAME call as drawCylinder but using the 48-segment mesh.
//  Use it whenever the camera can see that the shape is round: tyres, oil
//  drums, hoses, pipes, arm segments, cones, extinguishers. Keep drawCylinder
//  for bolt heads, pins and casters, where the extra triangles buy nothing.
// ---------------------------------------------------------------------------
void Scene::drawCylS(const Mat4& model, const Vec3& color, float emissive, float metallic, float roughness, int materialType)
{
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor)) return;

    Mat3 nrm = normalMatrix(model);

    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);

    Primitives::drawSmoothCylinderMesh();
}

void Scene::drawSphere(const Mat4& model, const Vec3& color, float emissive, float metallic, float roughness, int materialType)
{
    Vec3 shownColor;
    if (!teachingPrepareColor(color, shownColor)) return;

    Mat3 nrm = normalMatrix(model);
    glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
    glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
    uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);
    Primitives::drawSphereMesh();
}


// ============================================================================
//  SECTION 1b - IMPERFECTION HELPERS
//
//  Real objects are never identical and never perfectly aligned. A row of
//  twenty drums that share one colour, one angle and one exact spacing reads
//  instantly as copy-paste, which is one of the strongest "this is computer
//  generated" signals there is.
//
//  The fix is to give every repeated instance a small, DETERMINISTIC variation
//  derived from its index. Deterministic matters: if we used rand() the scene
//  would shimmer differently every frame. The same index always produces the
//  same number, so the garage looks untidy but never flickers.
// ============================================================================

// A cheap hash: takes an integer, returns a repeatable pseudo-random 0..1.
// This is the classic fract(sin(x) * large) trick from shader code.
float Scene::hash01(int index)
{
    float x = std::sin(static_cast<float>(index) * 12.9898f) * 43758.5453f;
    return x - std::floor(x);              // fractional part only
}

// The same thing mapped to -amount .. +amount, for jittering angles,
// positions and scales.
float Scene::jitter(int index, float amount)
{
    return (hash01(index) * 2.0f - 1.0f) * amount;
}

// Nudge a colour by +-percent on each channel, so no two instances of the same
// object are exactly the same shade. Three different seeds off one index gives
// three independent channel offsets.
Vec3 Scene::tintVary(const Vec3& c, int index, float percent)
{
    Vec3 out(c.x * (1.0f + jitter(index * 3 + 1, percent)),
        c.y * (1.0f + jitter(index * 3 + 2, percent)),
        c.z * (1.0f + jitter(index * 3 + 3, percent)));

    // Keep the result inside the same limits the palette obeys (see SECTION 3).
    if (out.x > 0.85f) out.x = 0.85f;
    if (out.y > 0.85f) out.y = 0.85f;
    if (out.z > 0.85f) out.z = 0.85f;
    if (out.x < 0.04f) out.x = 0.04f;
    if (out.y < 0.04f) out.y = 0.04f;
    if (out.z < 0.04f) out.z = 0.04f;
    return out;
}

// A full "untidy placement" matrix: small offsets in X and Z, a small yaw, and
// a small uniform scale change. One call turns a perfect row into a real one.
Mat4 Scene::messy(int index, float posAmt, float yawAmt, float scaleAmt)
{
    float s = 1.0f + jitter(index * 7 + 5, scaleAmt);
    return translate(jitter(index * 7 + 1, posAmt), 0.0f, jitter(index * 7 + 2, posAmt))
        * rotateY(jitter(index * 7 + 3, yawAmt))
        * scale(s, s, s);
}


// ============================================================================
//  SECTION 1c - SURFACE DETAIL HELPERS
//
//  Two tricks that buy a lot of apparent realism for very little code, and
//  neither needs a shader change.
// ============================================================================

// ---- Fake chamfer ---------------------------------------------------------
//  A real manufactured panel has a ~2 mm radius on every edge, and that tiny
//  radius catches a bright line of light. Our cube has perfectly sharp 90
//  degree corners, which reads as folded paper.
//
//  We cannot change the cube mesh, but we can fake the thing that matters: a
//  slightly larger DARK cube behind the panel (reads as the shadowed recess a
//  panel sits in) and a thin BRIGHT strip along its top edge (reads as the
//  chamfer catching light). Worth doing on large panels the camera gets close
//  to; not worth the draw calls on small props.
void Scene::drawPanel(const Mat4& parent, const Vec3& pos, const Vec3& size,
    const Vec3& color, const Vec3& rotDeg)
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

// ---- Contact darkening -----------------------------------------------------
//  Where an object meets the floor, dirt collects and ambient light cannot
//  reach. A flat, very dark, very slightly oversized patch under an object is
//  a cheap stand-in for ambient occlusion and does most of what a real contact
//  shadow does: it glues the object to the ground.
void Scene::drawContactPatch(const Mat4& parent, const Vec3& centre, float sizeX, float sizeZ)
{
    drawCube(parent * place(Vec3(centre.x, 0.009f, centre.z), Vec3(sizeX, 0.014f, sizeZ)),
        Vec3(0.085f, 0.088f, 0.095f));
}




// ============================================================================
//  SECTION 2 - SCENE DIMENSIONS
//
//  Every number the scene depends on lives here, with a name. Nothing below
//  should contain an unexplained magic number for a major dimension.
//
//  The building is modelled on a modern underground "smart parking" service
//  structure: a wide central service aisle with the hydraulic lift in the
//  middle, and marked parking bays down both side walls.
// ============================================================================

// ---- Building shell --------------------------------------------------------
static const float GARAGE_W = 28.0f;   // interior width  (X): -14.0 .. +14.0
static const float GARAGE_D = 26.0f;   // interior depth  (Z): -13.0 .. +13.0
static const float GARAGE_H = 7.0f;   // interior height (Y):   0.0 ..  +7.0
static const float WALL_T = 0.4f;   // wall thickness
static const float SHELL_W = GARAGE_W + 2.0f * WALL_T;   // 28.8
static const float SHELL_D = GARAGE_D + 2.0f * WALL_T;   // 26.8
static const float WALL_X = GARAGE_W * 0.5f;             // 14.0 inner face
static const float WALL_Z = GARAGE_D * 0.5f;             // 13.0 inner face

// ---- Entrance and sliding door ---------------------------------------------
static const float ENTRY_Z = 13.2f;  // plane of the entrance frame
static const float DOOR_W = 13.0f;  // clear opening width
static const float DOOR_H = 4.9f;   // clear opening height (leaves room for the roll)
static const int   DOOR_PANELS = 5;
static const float DOOR_PANEL_H = DOOR_H / static_cast<float>(DOOR_PANELS); // 1.24
static const float DOOR_Z = ENTRY_Z - 0.32f;  // panels sit just inside the frame

// ---- Scissor lift -----------------------------------------------------------
//  A scissor lift is two X-shaped arm pairs stacked on top of each other.
//  If one arm is LIFT_ARM_LEN long and stands at angle theta above the
//  horizontal, that stage contributes  LIFT_ARM_LEN * sin(theta)  of height.
//  Two stages, so the platform rises by  2 * LIFT_ARM_LEN * sin(theta).
//  See scissorAngle() in SECTION 6 - that one function is the whole mechanism.
static const float LIFT_Z = -2.00f;  // the lift sits behind the middle
static const float LIFT_ARM_LEN = 3.30f;  // pivot-to-pivot length of one arm
static const float LIFT_ARM_W = 0.20f;  // arm thickness across X
static const float LIFT_ARM_T = 0.32f;  // arm depth (its "tall" dimension)
static const float LIFT_BASE_TOP = 0.22f;  // top of the base frame
static const float LIFT_REST_H = 0.58f;  // platform lift when fully lowered
static const float LIFT_PLAT_T = 0.18f;  // blue platform beam thickness
static const float LIFT_PAD_T = 0.10f;  // yellow runway pad thickness
static const float LIFT_PAD_L = 5.20f;  // runway pad length along Z
static const float LIFT_TRACK = 0.98f;  // half distance between the two runways
static const float LIFT_MAX_H = 4.00f;  // travel of the animation parameter

// ---- The automobile (Cybertruck-style angular pickup, nose along -Z) ---------
// The body is deliberately built from faceted cube panels.  The large wheels,
// straight belt line, single front/rear light bars, steep windscreen, peaked
// roof and sloping tonneau make the silhouette read as a Cybertruck while
// preserving the assignment rule: only the existing primitive meshes are used.
static const float CAR_LENGTH = 5.68f;    // production-like overall proportion
static const float CAR_WIDTH = 2.20f;     // stainless body width; tyres sit only slightly wider
static const float CAR_FWHEEL_R = 0.495f; // 35-inch-class visual proportion
static const float CAR_FWHEEL_W = 0.390f;
static const float CAR_RWHEEL_R = 0.500f;
static const float CAR_RWHEEL_W = 0.400f;
static const float CAR_FAXLE_Z = -1.73f;  // long production-style wheelbase
static const float CAR_RAXLE_Z = 1.73f;
static const float CAR_FTRACK = 1.010f;   // wheel-centre half-track along project X axis
static const float CAR_RTRACK = 1.010f;
static const float CAR_WHEEL_OUTSET = 0.035f; // tyres remain tucked close to the flush arch cladding

// ---- Parking bays along the two side walls -----------------------------------
static const float PARK_X = 10.60f;  // centre line of a parked car
static const float PARK_BAY_W = 3.10f;   // bay width  (across the aisle, along Z)
static const float PARK_BAY_D = 5.60f;   // bay depth  (into the wall, along X)
static const float PARK_Z_A = -7.00f;  // first bay on each side
static const float PARK_Z_B = 4.20f;  // second bay on each side

// ---- Robotic inspection arm ---------------------------------------------------
static const float ARM_X = -4.60f;  // where the arm is bolted to the floor
static const float ARM_Z = -2.20f;
static const float ARM_SHOULDER_Y = 1.05f;  // height of joint 1
static const float ARM_LOWER_LEN = 4.50f;  // long-reach shoulder link: clears the Cybertruck and reaches its far side
static const float ARM_UPPER_LEN = 4.00f;  // long-reach forearm: full serviced-vehicle footprint coverage

// ---- Ceiling fittings ----------------------------------------------------------
static const float CEIL_Y = GARAGE_H;          // underside of the ceiling slab
static const float LAMP_Y = 6.55f;
static const float FAN_Y = 6.30f;


// ============================================================================
//  SECTION 3 - COLOUR PALETTE
//
//  Colour is a UNIFORM, never a vertex attribute. That is the whole reason a
//  single 24-vertex cube can become a concrete wall, a rusted drum and a
//  painted floor line without ever touching the vertex data.
//
//  THE THREE RULES EVERY COLOUR BELOW OBEYS
//  ----------------------------------------
//   1. No channel above 0.85 on anything that is not a light. Real paint under
//      real light essentially never pins a channel at 1.0 - only emitters do.
//   2. No channel below 0.04. Nothing in a lit room is pure black; even fresh
//      tyre rubber bounces a little light.
//   3. Nothing fully saturated. Every colour here has been pulled roughly a
//      quarter of the way toward its own average, which is what turns a
//      poster-flat primary into a pigment. Compare the old C_NEON
//      (1.00, 0.40, 0.06) with the value below: the hue is the same, but the
//      spread between the channels is much smaller, and that difference alone
//      is most of what separates "rendered" from "photographed".
//
//  Specular response is a shading-stage concern, so the numbers here are
//  albedo only. The comments record the intended specular/shininess for each
//  material so the lighting pass has them ready.
// ============================================================================

// ---- Building --------------------------------------------------------------
static const Vec3 C_FLOOR(0.190f, 0.190f, 0.200f);  // worn concrete   spec .18  shine 12
static const Vec3 C_FLOOR_BAY(0.150f, 0.152f, 0.162f);  // darker painted bay
static const Vec3 C_FLOOR_STAIN(0.098f, 0.096f, 0.094f);  // oil staining near the lift
static const Vec3 C_WALL(0.240f, 0.250f, 0.270f);  // matte industrial paint
static const Vec3 C_WALL_DARK(0.150f, 0.158f, 0.172f);
static const Vec3 C_CEILING(0.205f, 0.212f, 0.228f);
static const Vec3 C_BEAM(0.165f, 0.170f, 0.182f);
static const Vec3 C_COLUMN(0.215f, 0.222f, 0.238f);
static const Vec3 C_STEEL(0.520f, 0.540f, 0.570f);  // brushed steel   spec .70  shine 64
static const Vec3 C_GALV(0.545f, 0.560f, 0.570f);  // galvanised      spec .45  shine 32
static const Vec3 C_CHROME(0.700f, 0.715f, 0.740f);  // chrome trim     spec 1.0  shine 220
static const Vec3 C_RUST(0.340f, 0.200f, 0.130f);
static const Vec3 C_PLASTIC(0.300f, 0.320f, 0.340f);  // matte plastic   spec .30  shine 40
static const Vec3 C_WOOD(0.420f, 0.330f, 0.225f);
static const Vec3 C_CARDBOARD(0.440f, 0.360f, 0.265f);
static const Vec3 C_RUBBER(0.090f, 0.090f, 0.095f);

// ---- Lights and paint markings ---------------------------------------------
// Emissive surfaces are the ONE place a channel is allowed to reach 1.0,
// because they are light sources rather than reflectors.
static const Vec3 C_NEON(0.980f, 0.545f, 0.225f);   // neon tube, emissive
static const Vec3 C_NEON_CYAN(0.380f, 0.860f, 0.930f);   // diagnostic / scanner cyan
static const Vec3 C_PARK_LINE(0.720f, 0.450f, 0.120f);   // faded painted line
static const Vec3 C_WHITE_LINE(0.700f, 0.705f, 0.715f);  // scuffed white paint
static const Vec3 C_TUBE(0.980f, 0.940f, 0.860f);  // fluorescent tube, emissive
static const Vec3 C_HAZARD_Y(0.740f, 0.620f, 0.140f);  // safety yellow - muted
static const Vec3 C_HAZARD_K(0.110f, 0.112f, 0.118f);

// ---- Sliding door ----------------------------------------------------------
static const Vec3 C_DOOR(0.515f, 0.530f, 0.545f);  // galvanised panel
static const Vec3 C_DOOR_RIB(0.400f, 0.415f, 0.430f);

// ---- Scissor lift (blue frame + yellow pads, as in the reference photo) -----
static const Vec3 C_LIFT_BLUE(0.145f, 0.275f, 0.545f);
static const Vec3 C_LIFT_BLUE_D(0.095f, 0.185f, 0.375f);
static const Vec3 C_LIFT_YELLOW(0.740f, 0.615f, 0.145f);
static const Vec3 C_LIFT_DARK(0.155f, 0.158f, 0.168f);

// ---- Car -------------------------------------------------------------------
// Only the panel colour is passed in to drawCar(). The carbon-fibre parts,
// glass, lights and wheels are the same on every car, which is exactly how a
// real model is re-used: one mesh, one material swap.
static const Vec3 C_CAR_ORANGE(0.680f, 0.300f, 0.115f);  // gloss   spec .85 shine 128
static const Vec3 C_CAR_WHITE(0.720f, 0.725f, 0.730f);
static const Vec3 C_CAR_BLUE(0.145f, 0.260f, 0.480f);
static const Vec3 C_CAR_RED(0.520f, 0.135f, 0.115f);
static const Vec3 C_CAR_GUNMETAL(0.280f, 0.292f, 0.315f);  // matte   spec .25 shine 24
static const Vec3 C_CAR_SILVER(0.560f, 0.575f, 0.595f);
static const Vec3 C_CAR_SAND(0.545f, 0.480f, 0.365f);

static const Vec3 C_CARBON(0.105f, 0.108f, 0.118f);  // splitter, diffuser, skirts
static const Vec3 C_GLASS(0.070f, 0.085f, 0.105f);  // deep neutral tint; reflections carry the sky/garage
static const Vec3 C_TIRE(0.058f, 0.059f, 0.064f);  // tread - never mathematically black
static const Vec3 C_TIRE_WALL(0.074f, 0.075f, 0.080f);  // subtle sidewall lift
static const Vec3 C_RIM(0.105f, 0.110f, 0.120f);  // near-black production aero cover
static const Vec3 C_RIM_LIP(0.640f, 0.655f, 0.680f);
static const Vec3 C_BRAKE_DISC(0.480f, 0.470f, 0.455f);  // scored cast iron
static const Vec3 C_HEADLIGHT(0.960f, 0.930f, 0.860f);  // emissive
static const Vec3 C_TAILLIGHT(0.870f, 0.185f, 0.140f);  // emissive

// ---- Robot arm and scanner --------------------------------------------------
static const Vec3 C_ROBOT(0.615f, 0.628f, 0.650f);
static const Vec3 C_ROBOT_DARK(0.215f, 0.222f, 0.240f);
static const Vec3 C_JOINT(0.720f, 0.400f, 0.165f);
static const Vec3 C_SCANNER(0.185f, 0.192f, 0.208f);

// ---- Fittings ----------------------------------------------------------------
static const Vec3 C_LAMP_HOUSING(0.235f, 0.243f, 0.258f);
static const Vec3 C_LAMP_LIGHT(0.960f, 0.935f, 0.880f);  // emissive

static const Vec3 C_CABINET(0.540f, 0.190f, 0.150f);
static const Vec3 C_CABINET_DRAWER(0.600f, 0.255f, 0.205f);

static const Vec3 C_SHELF(0.360f, 0.372f, 0.395f);
static const Vec3 C_SHELF_DARK(0.245f, 0.255f, 0.272f);

// ---- Repeated equipment -------------------------------------------------------
// Four muted bin colours that cycle through the racking. None of them is a
// primary; all four sit within a narrow value range so the racking reads as one
// object rather than as confetti.
static const Vec3 C_BIN[4] = {
    Vec3(0.290f, 0.375f, 0.480f),   // dusty blue
    Vec3(0.470f, 0.415f, 0.230f),   // ochre
    Vec3(0.330f, 0.410f, 0.330f),   // sage
    Vec3(0.450f, 0.330f, 0.295f)    // clay
};
static const Vec3 C_DRUM[4] = {
    Vec3(0.230f, 0.330f, 0.420f),   // blue drum
    Vec3(0.480f, 0.410f, 0.170f),   // yellow drum, well faded
    Vec3(0.260f, 0.365f, 0.285f),   // green drum
    Vec3(0.400f, 0.250f, 0.175f)    // rusted
};
static const Vec3 C_EXTING(0.560f, 0.145f, 0.120f);
static const Vec3 C_JERRY(0.330f, 0.390f, 0.310f);

// ---- Exterior ------------------------------------------------------------------
static const Vec3 C_APRON(0.480f, 0.470f, 0.445f);  // sunlit concrete, warm
static const Vec3 C_APRON_DARK(0.400f, 0.392f, 0.372f);
static const Vec3 C_ASPHALT(0.265f, 0.265f, 0.272f);
static const Vec3 C_ASPHALT_PATCH(0.220f, 0.222f, 0.230f);
static const Vec3 C_KERB(0.560f, 0.552f, 0.528f);
static const Vec3 C_PERIM_WALL(0.505f, 0.492f, 0.462f);
static const Vec3 C_BUILDING[3] = {
    Vec3(0.455f, 0.462f, 0.485f),
    Vec3(0.505f, 0.500f, 0.498f),
    Vec3(0.420f, 0.432f, 0.458f)
};


// ============================================================================
//  SECTION 4 - THE GARAGE SHELL
//
//  Floor, walls, ceiling, beams, neon columns, strip lights, painted bay
//  markings and the entrance frame. Every single piece below is the same unit
//  cube with a different scale, position and colour.
// ============================================================================

// ---------------------------------------------------------------------------
//  One parking bay: the painted floor markings a car parks inside.
//  Local origin = the centre of the bay, on the floor.
//  Local +X points INTO the wall (away from the aisle), local +-Z are the
//  two neighbouring bays. The caller supplies the rotation.
// ---------------------------------------------------------------------------
void Scene::drawParkingBay(const Mat4& parent)
{
    const float halfW = PARK_BAY_W * 0.5f;    // 1.55  (across, along Z)
    const float halfD = PARK_BAY_D * 0.5f;    // 2.80  (into the wall, along X)
    const float y = 0.018f;                 // a hair above the floor
    const float t = 0.020f;                 // paint thickness

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


void Scene::drawGarage(const Mat4& parent)
{
    // ---- Floor ------------------------------------------------------------
    // A cube squashed to 0.3 units tall. Its centre sits at y = -0.15 so its
    // TOP surface lands exactly on y = 0, which is our ground plane.
    drawCube(parent * place(Vec3(0.0f, -0.15f, 0.0f), Vec3(SHELL_W, 0.30f, SHELL_D)), C_FLOOR, 0.0f, 0.0f, 0.88f, MAT_CONCRETE);

    // Darker painted service bay under the lift.
    drawCube(parent * place(Vec3(0.0f, 0.012f, LIFT_Z), Vec3(8.4f, 0.02f, 9.0f)), C_FLOOR_BAY, 0.0f, 0.0f, 0.78f, MAT_PAINT);

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
        Vec3(SHELL_W, GARAGE_H, WALL_T)), C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);

    // Recessed dark panels give the flat wall some built scale.
    for (int i = 0; i < 4; ++i)
    {
        float x = -9.0f + static_cast<float>(i) * 6.0f;
        drawCube(parent * place(Vec3(x, 1.90f, -WALL_Z + 0.06f), Vec3(4.60f, 2.80f, 0.08f)), C_WALL_DARK);
    }

    // ---- Left and right walls ---------------------------------------------
    drawCube(parent * place(Vec3(-(WALL_X + WALL_T * 0.5f), GARAGE_H * 0.5f, 0.0f),
        Vec3(WALL_T, GARAGE_H, SHELL_D)), C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);
    drawCube(parent * place(Vec3((WALL_X + WALL_T * 0.5f), GARAGE_H * 0.5f, 0.0f),
        Vec3(WALL_T, GARAGE_H, SHELL_D)), C_WALL, 0.0f, 0.0f, 0.82f, MAT_WALL);

    // NOTE: the structural columns used to be drawn here as plain boxes. They
    // are now drawSupportColumn() in SECTION 18, because a real column has a
    // base plate, anchor bolts and a hazard band, and because it appears eight
    // times - which makes it equipment, not part of the shell.

    // ---- Contact darkening where the walls meet the floor ------------------
    // A thin dark band along the bottom of every wall. This is the cheapest
    // possible stand-in for ambient occlusion: in a real room the corner where
    // two surfaces meet receives far less bounced light than the open wall, and
    // putting that darkening in by hand is most of what the effect buys you.
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

    // NOTE: lighting is now 14 x drawCeilingFixture() from SECTION 18, hung on
    // chain drops at a spacing that deliberately does NOT match the beams.

    // ---- Entrance frame ---------------------------------------------------
    // Two side posts plus a deep header that doubles as the door housing.
    const float postW = (SHELL_W - DOOR_W) * 0.5f;              // 7.9
    const float postX = DOOR_W * 0.5f + postW * 0.5f;           // 10.45

    drawCube(parent * place(Vec3(-postX, GARAGE_H * 0.5f, ENTRY_Z), Vec3(postW, GARAGE_H, 0.50f)), C_WALL_DARK);
    drawCube(parent * place(Vec3(postX, GARAGE_H * 0.5f, ENTRY_Z), Vec3(postW, GARAGE_H, 0.50f)), C_WALL_DARK);

    // Lintel beam over the opening, and an open hood rather than a closed box:
    // the drum and the rolled-up panels are meant to be visible from inside.
    drawCube(parent * place(Vec3(0.0f, GARAGE_H - 0.22f, ENTRY_Z - 0.10f),
        Vec3(DOOR_W + 1.2f, 0.44f, 0.70f)), C_WALL_DARK);
    drawCube(parent * place(Vec3(0.0f, GARAGE_H - 0.52f, ENTRY_Z + 0.16f),
        Vec3(DOOR_W + 1.2f, 0.60f, 0.22f)), C_WALL_DARK);
    // Two cheek plates the drum shaft passes between.
    for (int s2 = 0; s2 < 2; ++s2)
    {
        float sx = (s2 == 0) ? -1.0f : 1.0f;
        drawCube(parent * place(Vec3(sx * (DOOR_W * 0.5f + 0.42f), DOOR_H + 0.85f, ENTRY_Z - 0.20f),
            Vec3(0.12f, 1.30f, 0.80f)), C_GALV * 0.70f);
    }

    // Amber safety strip under the header and guide rails either side.
    drawCube(parent * place(Vec3(0.0f, DOOR_H + 0.10f, ENTRY_Z - 0.78f), Vec3(DOOR_W - 0.4f, 0.09f, 0.06f)), C_NEON, 1.0f);
    // (the guide rails the door runs in are part of drawGarageDoor)
}


// ============================================================================
//  SECTION 5 - ROLLER SHUTTER DOOR AND ITS MECHANISM
//
//  The door is OPEN by default now, and an open roller door is not simply an
//  absent door: the panels are still there, curled into a roll above the
//  opening, and the machinery that put them there is on show. Modelling that
//  is what stops the entrance reading as a hole cut in a wall.
//
//  Built in three parts:
//      - the five panels, which slide up and then curl into the drum
//      - the two vertical guide rails, with a roller wheel per panel edge
//      - the chain drive: shaft, sprocket and motor box in the top corner
//
//  `doorOffset` still does exactly what it always did, and it is still the
//  only thing that moves:
//
//      doorOffset = 0.0        fully closed, all five panels across the opening
//      doorOffset = DOOR_H     fully open, all five curled into the drum
//
//  The one new idea is that a panel which has travelled past the top of the
//  opening stops going up and starts going ROUND. That is what a real roller
//  door does, and it costs one if-statement.
// ============================================================================

void Scene::drawGarageDoor(const Mat4& parent, float doorOffset)
{
    // ---- The two guide rails ----------------------------------------------
    // These do not move. A panel edge runs up the inside of each one.
    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        float x = sx * (DOOR_W * 0.5f + 0.16f);

        // C-section rail: a back plate plus two lips, so it reads as a channel.
        drawCube(parent * place(Vec3(x, DOOR_H * 0.5f + 0.20f, DOOR_Z), Vec3(0.230f, DOOR_H + 0.40f, 0.075f)), C_GALV);
        drawCube(parent * place(Vec3(x - sx * 0.090f, DOOR_H * 0.5f + 0.20f, DOOR_Z + 0.075f), Vec3(0.050f, DOOR_H + 0.40f, 0.190f)), C_GALV * 0.86f);
        drawCube(parent * place(Vec3(x - sx * 0.090f, DOOR_H * 0.5f + 0.20f, DOOR_Z - 0.075f), Vec3(0.050f, DOOR_H + 0.40f, 0.190f)), C_GALV * 0.86f);

        // Wall brackets every 1.24 m, each with two bolts. Detail tier.
        for (int i = 0; i < 6; ++i)
        {
            float y = 0.45f + static_cast<float>(i) * 1.15f;
            drawCube(parent * place(Vec3(x + sx * 0.130f, y, DOOR_Z), Vec3(0.090f, 0.140f, 0.230f)), C_GALV);
            drawCylinder(parent * place(Vec3(x + sx * 0.130f, y + 0.045f, DOOR_Z), Vec3(0.045f, 0.035f, 0.045f)), C_ROBOT_DARK);
            drawCylinder(parent * place(Vec3(x + sx * 0.130f, y - 0.045f, DOOR_Z), Vec3(0.045f, 0.035f, 0.045f)), C_ROBOT_DARK);
        }
    }

    // ---- The drum housing the panels roll into -----------------------------
    const float DRUM_Y = DOOR_H + 0.95f;                 // centre of the roll
    const float DRUM_R = 0.52f;

    drawCylS(parent * translate(0.0f, DRUM_Y, DOOR_Z) * rotateZ(90.0f)
        * scale(0.180f, DOOR_W + 0.50f, 0.180f), C_STEEL);                 // the shaft

    // End plates and the bearing blocks the shaft turns in.
    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        drawCylS(parent * translate(sx * (DOOR_W * 0.5f + 0.12f), DRUM_Y, DOOR_Z) * rotateZ(90.0f)
            * scale(DRUM_R * 2.05f, 0.060f, DRUM_R * 2.05f), C_GALV);
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
            // Still in the opening: a flat panel running in the guide rails.
            panel = parent * translate(0.0f, travel, DOOR_Z);
        }
        else
        {
            // Past the top, so it is winding onto the drum. Turn the remaining
            // travel into an angle around the shaft: arc length over radius.
            //
            //      angle = over / DRUM_R          (radians)
            //
            // One panel is DOOR_PANEL_H long, so each panel takes up
            // DOOR_PANEL_H / DRUM_R = 2 radians = about 115 degrees of the
            // drum. Five panels therefore wrap around roughly one and a half
            // times, which is exactly what a real shutter does.
            float ang = (over / DRUM_R) * 180.0f / PI_F;
            if (ang > 560.0f) ang = 560.0f;          // the drum is full

            // Each full turn winds on top of the last, so the radius has to
            // grow by a bit more than one panel thickness per revolution or
            // the second layer would sit inside the first.
            float r = DRUM_R + (ang / 360.0f) * 0.095f;

            // The drum shaft lies along X, so the panels wind in the YZ plane -
            // that means rotating about the X axis, not the Z axis. Getting
            // this wrong sends the 13 m wide panels swinging diagonally across
            // the opening instead of curling neatly over the lintel.
            //
            //   rotateX(ang)           swing round to this point on the drum
            //   translate(0, -r, 0)    step out to the winding radius
            //   rotateX(90)            lay the panel's LENGTH along the
            //                          circumference and its THICKNESS radially
            //
            // The panel's width stays on X throughout, because rotateX never
            // touches the X axis.
            panel = parent * translate(0.0f, DRUM_Y, DOOR_Z)
                * rotateX(ang)
                * translate(0.0f, -r, 0.0f)
                * rotateX(90.0f);
        }

        // ---- The panel itself, identical whether flat or curled -------------
        Vec3 face = (i == 0) ? C_DOOR * 0.94f : C_DOOR;   // bottom panel is scuffed

        drawCube(panel * scale(DOOR_W, DOOR_PANEL_H * 0.96f, 0.070f), face);

        // Three pressed ribs across each panel.
        for (int r = 0; r < 3; ++r)
            drawCube(panel * translate(0.0f, (static_cast<float>(r) - 1.0f) * 0.310f, 0.042f)
                * scale(DOOR_W - 0.10f, 0.070f, 0.022f), C_DOOR_RIB);

        // The hinge line between this panel and the one below it.
        drawCube(panel * translate(0.0f, -DOOR_PANEL_H * 0.48f, 0.030f)
            * scale(DOOR_W - 0.06f, 0.040f, 0.055f), C_DOOR_RIB * 0.80f);
        for (int h = 0; h < 5; ++h)
            drawCube(panel * translate((static_cast<float>(h) - 2.0f) * 2.55f, -DOOR_PANEL_H * 0.48f, 0.058f)
                * scale(0.230f, 0.075f, 0.050f), C_GALV);

        // Roller wheels: one at each edge, riding in the guide rail.
        for (int s = 0; s < 2; ++s)
        {
            float sx = (s == 0) ? -1.0f : 1.0f;
            drawCylinder(panel * translate(sx * (DOOR_W * 0.5f + 0.055f), -DOOR_PANEL_H * 0.30f, 0.0f)
                * rotateZ(90.0f) * scale(0.140f, 0.060f, 0.140f), C_ROBOT_DARK);
            drawCube(panel * translate(sx * (DOOR_W * 0.5f + 0.020f), -DOOR_PANEL_H * 0.30f, 0.0f)
                * scale(0.075f, 0.075f, 0.075f), C_GALV);
        }

        if (i == 3)
        {
            // Vision slots in the fourth panel, as most shutter doors have.
            for (int w = 0; w < 5; ++w)
                drawCube(panel * translate((static_cast<float>(w) - 2.0f) * 2.45f, 0.150f, -0.040f)
                    * scale(1.55f, 0.360f, 0.020f), C_GLASS);
        }

        if (i == 0)
        {
            // Bottom panel: rubber weather seal and a hazard band.
            drawCube(panel * translate(0.0f, -DOOR_PANEL_H * 0.50f, 0.0f)
                * scale(DOOR_W, 0.075f, 0.110f), C_RUBBER);

            for (int b = 0; b < 12; ++b)
                drawCube(panel * translate((static_cast<float>(b) - 5.5f) * 1.05f, -0.290f, 0.045f)
                    * rotateZ(38.0f) * scale(0.560f, 0.190f, 0.020f),
                    (b & 1) ? C_HAZARD_Y : C_HAZARD_K);

            // Lift handle.
            drawCylS(panel * translate(0.0f, -0.120f, 0.070f) * rotateZ(90.0f)
                * scale(0.050f, 0.620f, 0.050f), C_GALV);
        }
    }

    // ---- Chain drive and motor, top right corner ---------------------------
    const float MX = DOOR_W * 0.5f + 0.42f;

    drawCylS(parent * translate(MX, DRUM_Y, DOOR_Z - 0.16f) * rotateZ(90.0f)
        * scale(0.360f, 0.070f, 0.360f), C_ROBOT_DARK);                       // sprocket
    for (int i = 0; i < 16; ++i)                                                // sprocket teeth
        drawCube(parent * translate(MX, DRUM_Y, DOOR_Z - 0.16f) * rotateX(static_cast<float>(i) * 22.5f)
            * translate(0.0f, 0.195f, 0.0f) * scale(0.080f, 0.050f, 0.045f), C_ROBOT_DARK);

    // The chain hanging down to the motor, as a column of small links.
    for (int i = 0; i < 9; ++i)
        drawCube(parent * place(Vec3(MX, DRUM_Y - 0.28f - static_cast<float>(i) * 0.175f, DOOR_Z - 0.34f),
            Vec3(0.045f, 0.130f, 0.045f)), C_ROBOT_DARK);

    // Motor and gearbox bolted to the wall.
    drawCube(parent * place(Vec3(MX, DRUM_Y - 1.90f, DOOR_Z - 0.30f), Vec3(0.420f, 0.480f, 0.420f)), C_LIFT_BLUE_D);
    drawCylS(parent * translate(MX, DRUM_Y - 1.90f, DOOR_Z - 0.06f) * rotateX(90.0f)
        * scale(0.300f, 0.240f, 0.300f), C_LIFT_BLUE_D * 1.15f);
    for (int i = 0; i < 4; ++i)
        drawCylinder(parent * place(Vec3(MX + ((i & 1) ? 0.16f : -0.16f), DRUM_Y - 1.90f + ((i & 2) ? 0.19f : -0.19f), DOOR_Z + 0.08f),
            Vec3(0.055f, 0.040f, 0.055f)), C_CHROME);
    // Conduit from the motor to a control box beside the opening.
    drawCube(parent * place(Vec3(MX, DRUM_Y - 2.85f, DOOR_Z - 0.30f), Vec3(0.055f, 1.45f, 0.055f)), C_GALV);
    drawCube(parent * place(Vec3(MX, DRUM_Y - 3.70f, DOOR_Z - 0.30f), Vec3(0.230f, 0.330f, 0.140f)), C_GALV);
    drawCube(parent * place(Vec3(MX - 0.125f, DRUM_Y - 3.66f, DOOR_Z - 0.30f), Vec3(0.020f, 0.075f, 0.075f)), C_NEON_CYAN, 0.9f);
}


// ============================================================================
//  SECTION 19 - THE DAYLIT EXTERIOR
//
//  With the door open, whatever is beyond it is on screen. If nothing is out
//  there the opening reads as a flat bright rectangle, which does more damage
//  to the illusion than a closed door would.
//
//  So the ground continues outside, and there is depth behind it: apron, then
//  asphalt, then a kerb, then a perimeter wall, then a hazed skyline. None of
//  it is complicated - it is the same cube, scaled flat and laid down - but it
//  gives the eye something to focus through, which is what sells the opening.
//
//  Built from the threshold outwards so the distances stay readable.
// ============================================================================

void Scene::drawExterior(const Mat4& parent)
{
    const float TH = WALL_Z + WALL_T;      // z of the threshold (13.4)

    // ---- 1. The sunlit concrete apron, 14 m deep ---------------------------
    // Noticeably lighter and warmer than the interior floor: this is the single
    // most important value relationship in the whole shot.
    drawCube(parent * place(Vec3(0.0f, -0.145f, TH + 7.0f), Vec3(30.0f, 0.30f, 14.0f)), C_APRON, 0.0f, 0.0f, 0.86f, MAT_CONCRETE);

    // Expansion joints across it, every 3.5 m, plus two lengthways.
    for (int i = 0; i < 4; ++i)
        drawCube(parent * place(Vec3(0.0f, 0.007f, TH + 1.75f + static_cast<float>(i) * 3.5f),
            Vec3(30.0f, 0.016f, 0.055f)), C_APRON_DARK);
    drawCube(parent * place(Vec3(-7.5f, 0.007f, TH + 7.0f), Vec3(0.055f, 0.016f, 14.0f)), C_APRON_DARK);
    drawCube(parent * place(Vec3(7.5f, 0.007f, TH + 7.0f), Vec3(0.055f, 0.016f, 14.0f)), C_APRON_DARK);

    // Patches of staining and old repairs, so it is not a clean slab.
    for (int i = 0; i < 7; ++i)
    {
        float x = jitter(i * 5 + 1, 11.0f);
        float z = TH + 2.0f + hash01(i * 5 + 2) * 11.0f;
        float w = 1.1f + hash01(i * 5 + 3) * 2.2f;
        drawCube(parent * place(Vec3(x, 0.006f, z), Vec3(w, 0.014f, w * 0.72f),
            Vec3(0.0f, jitter(i * 5 + 4, 40.0f), 0.0f)), C_APRON_DARK * (0.90f + hash01(i * 5) * 0.12f));
    }

    // ---- 2. Approach markings, faded -------------------------------------
    // A stop line and three arrows pointing at the door.
    drawCube(parent * place(Vec3(0.0f, 0.012f, TH + 2.30f), Vec3(12.0f, 0.020f, 0.190f)), C_WHITE_LINE * 0.88f);
    for (int i = 0; i < 3; ++i)
    {
        float x = (static_cast<float>(i) - 1.0f) * 3.4f;
        float z = TH + 5.2f;
        drawCube(parent * place(Vec3(x, 0.012f, z), Vec3(0.230f, 0.020f, 1.55f)), C_WHITE_LINE * 0.80f);
        // The head: two bars swept BACK from the tip, not forward past it.
        drawCube(parent * place(Vec3(x - 0.255f, 0.012f, z - 0.500f), Vec3(0.220f, 0.020f, 0.760f), Vec3(0.0f, 40.0f, 0.0f)), C_WHITE_LINE * 0.80f);
        drawCube(parent * place(Vec3(x + 0.255f, 0.012f, z - 0.500f), Vec3(0.220f, 0.020f, 0.760f), Vec3(0.0f, -40.0f, 0.0f)), C_WHITE_LINE * 0.80f);
    }
    // Hatched no-parking zone right at the threshold.
    for (int i = 0; i < 9; ++i)
        drawCube(parent * place(Vec3(-5.6f + static_cast<float>(i) * 1.4f, 0.011f, TH + 1.05f),
            Vec3(0.130f, 0.018f, 1.65f), Vec3(0.0f, 34.0f, 0.0f)), C_WHITE_LINE * 0.72f);

    // ---- 3. Kerb and drainage along the far edge of the apron -------------
    drawCube(parent * place(Vec3(0.0f, 0.075f, TH + 14.0f), Vec3(30.0f, 0.150f, 0.300f)), C_KERB);
    for (int i = 0; i < 5; ++i)
    {
        float x = -10.0f + static_cast<float>(i) * 5.0f;
        drawCube(parent * place(Vec3(x, 0.020f, TH + 13.55f), Vec3(0.780f, 0.040f, 0.420f)), C_GALV * 0.80f);
        for (int b = 0; b < 5; ++b)
            drawCube(parent * place(Vec3(x - 0.30f + static_cast<float>(b) * 0.15f, 0.034f, TH + 13.55f),
                Vec3(0.055f, 0.030f, 0.420f)), C_GALV);
    }

    // ---- 4. Asphalt road beyond the kerb -----------------------------------
    drawCube(parent * place(Vec3(0.0f, -0.16f, TH + 22.5f), Vec3(46.0f, 0.30f, 17.0f)), C_ASPHALT, 0.0f, 0.0f, 0.97f, MAT_ASPHALT);
    for (int i = 0; i < 6; ++i)
    {
        float x = jitter(i * 9 + 1, 16.0f);
        float z = TH + 16.0f + hash01(i * 9 + 2) * 13.0f;
        drawCube(parent * place(Vec3(x, 0.004f, z), Vec3(2.2f + hash01(i * 9) * 2.5f, 0.012f, 1.5f + hash01(i * 9 + 3) * 2.0f),
            Vec3(0.0f, jitter(i * 9 + 4, 30.0f), 0.0f)), C_ASPHALT_PATCH);
    }
    // Lane line down the road.
    for (int i = 0; i < 7; ++i)
        drawCube(parent * place(Vec3(-16.0f + static_cast<float>(i) * 5.4f, 0.010f, TH + 22.0f),
            Vec3(2.40f, 0.018f, 0.180f)), C_WHITE_LINE * 0.70f);

    // ---- 5. Perimeter wall, 15 m out ---------------------------------------
    drawCube(parent * place(Vec3(0.0f, 1.00f, TH + 31.5f), Vec3(48.0f, 2.00f, 0.300f)), C_PERIM_WALL, 0.0f, 0.0f, 0.88f, MAT_CONCRETE);
    drawCube(parent * place(Vec3(0.0f, 2.06f, TH + 31.5f), Vec3(48.0f, 0.120f, 0.420f)), C_PERIM_WALL * 1.10f); // coping
    for (int i = 0; i < 13; ++i)                                                                                  // posts
        drawCube(parent * place(Vec3(-24.0f + static_cast<float>(i) * 4.0f, 1.12f, TH + 31.5f),
            Vec3(0.420f, 2.24f, 0.460f)), C_PERIM_WALL * 1.06f);

    // ---- 6. Distant skyline -------------------------------------------------
    // Nine plain boxes at varying heights and distances. Each one further back
    // is nudged toward the sky colour, which is what reads as haze and is what
    // makes 40 m look like 400.
    const Vec3 SKY(0.560f, 0.610f, 0.680f);
    for (int i = 0; i < 9; ++i)
    {
        float x = -26.0f + static_cast<float>(i) * 6.5f + jitter(i * 11, 2.0f);
        float depth = 42.0f + hash01(i * 11 + 1) * 34.0f;
        float h = 6.0f + hash01(i * 11 + 2) * 13.0f;
        float w = 5.5f + hash01(i * 11 + 3) * 5.0f;

        // Haze: blend the building toward the sky in proportion to distance.
        float t = (depth - 42.0f) / 34.0f * 0.55f + 0.22f;
        Vec3 base = C_BUILDING[i % 3];
        Vec3 c = base * (1.0f - t) + SKY * t;

        drawCube(parent * place(Vec3(x, h * 0.5f, TH + depth), Vec3(w, h, w * 0.85f)), c);
        // A roof plant box, so the silhouette is not a plain rectangle.
        drawCube(parent * place(Vec3(x + w * 0.22f, h + 0.55f, TH + depth), Vec3(w * 0.34f, 1.10f, w * 0.34f)), c * 1.04f);
        // Window bands, barely visible through the haze but they break the mass.
        for (int r = 0; r < 4; ++r)
            drawCube(parent * place(Vec3(x, h * (0.25f + static_cast<float>(r) * 0.18f), TH + depth - w * 0.43f),
                Vec3(w * 0.86f, 0.30f, 0.06f)), c * 0.90f);
    }

    // ---- 7. Light columns along the road ------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        float x = -15.0f + static_cast<float>(i) * 10.0f;
        Mat4 pole = parent * translate(x, 0.0f, TH + 30.2f);
        drawCylS(pole * place(Vec3(0.0f, 2.70f, 0.0f), Vec3(0.180f, 5.40f, 0.180f)), C_PERIM_WALL * 0.80f);
        drawCylS(pole * translate(0.0f, 5.30f, -0.55f) * rotateX(72.0f) * scale(0.140f, 1.30f, 0.140f), C_PERIM_WALL * 0.80f);
        drawCube(pole * place(Vec3(0.0f, 5.66f, -1.15f), Vec3(0.520f, 0.140f, 0.900f)), C_GALV * 0.85f);
    }
}


// ============================================================================
//  SECTION 6 - DOUBLE-SCISSOR HYDRAULIC LIFT
//
//  Modelled on a real workshop scissor lift: a bolted base frame, TWO stacked
//  X-shaped arm pairs on each side, a hydraulic ram inside each stack, and a
//  blue platform carrying two yellow runway pads with drive-on ramps.
//
//  THE WHOLE MECHANISM IS ONE PIECE OF TRIGONOMETRY
//  ------------------------------------------------
//  An arm of length L standing at angle theta above the horizontal lifts its
//  top pivot by  L * sin(theta)  and its two pivots sit  L * cos(theta) apart.
//  Two stages are stacked, so:
//
//        platform rise = 2 * L * sin(theta)
//        arm span      =     L * cos(theta)
//
//  We are given the rise we want (LIFT_REST_H + liftHeight) and solve for the
//  angle. That single angle then drives every arm, pin and ram in the lift:
//
//        theta = asin( rise / (2 * L) )
//
//  So the lift folds flat and opens up correctly for ANY liftHeight, which is
//  exactly what the animation stage will need. Nothing else has to change.
//
//  `liftHeight` is the animation parameter, 0 .. LIFT_MAX_H.
// ============================================================================

// Solve the scissor geometry for a given lift height. Returns RADIANS.
static float scissorAngle(float liftHeight)
{
    float h = liftHeight;
    if (h < 0.0f)               h = 0.0f;
    if (h > LIFT_MAX_H)         h = LIFT_MAX_H;

    // Total rise the two stages must produce between them.
    float rise = LIFT_REST_H + h;

    // rise = 2 * L * sin(theta)   ->   sin(theta) = rise / (2L)
    float s = rise / (2.0f * LIFT_ARM_LEN);
    if (s > 0.985f) s = 0.985f;        // never let the arms go fully vertical
    if (s < 0.0f)   s = 0.0f;

    return asinf(s);
}


float Scene::liftTopY(float liftHeight)
{
    // Base frame + both scissor stages + platform beam + yellow pad.
    // Because 2 * L * sin(theta) is exactly (LIFT_REST_H + liftHeight) by the
    // way scissorAngle() was solved, this reduces to a clean straight line:
    //
    //      liftTopY(h) = 0.22 + 0.58 + h + 0.18 + 0.10  =  1.08 + h
    //
    // The car is placed on exactly this value, so raising the lift later
    // carries the car up with it and drawCar() never has to be touched.
    float rise = 2.0f * LIFT_ARM_LEN * sinf(scissorAngle(liftHeight));
    return LIFT_BASE_TOP + rise + LIFT_PLAT_T + LIFT_PAD_T;
}


// ---------------------------------------------------------------------------
//  One scissor stack (there is one under each runway, at x = +-LIFT_TRACK).
//  `x` is where the stack sits, `theta` comes from scissorAngle().
// ---------------------------------------------------------------------------
static void drawScissorStack(const Mat4& lift, float x, float theta)
{
    using namespace Scene;

    const float thetaDeg = theta * 180.0f / PI_F;
    const float rise = LIFT_ARM_LEN * sinf(theta);   // height gained per stage
    const float span = LIFT_ARM_LEN * cosf(theta);   // pivot-to-pivot along Z
    const float halfSpan = span * 0.5f;

    // The two arms of one X are offset slightly in X so they visibly cross
    // each other instead of sitting in the same plane and z-fighting.
    const float armOff = 0.12f;

    for (int stage = 0; stage < 2; ++stage)
    {
        // Height of the two BOTTOM pivots of this stage.
        float yBot = LIFT_BASE_TOP + static_cast<float>(stage) * rise;
        float yMid = yBot + rise * 0.5f;      // where the two arms cross

        // ---- Arm A: bottom pivot at -Z, rising towards +Z -------------------
        // A cube LIFT_ARM_LEN long in Z, rotated about the X axis. rotateX(-t)
        // lifts its +Z end, so the arm leans forward exactly like the real one.
        drawCube(lift * place(Vec3(x - armOff, yMid, 0.0f),
            Vec3(LIFT_ARM_W, LIFT_ARM_T, LIFT_ARM_LEN),
            Vec3(-thetaDeg, 0.0f, 0.0f)), C_LIFT_BLUE);

        // ---- Arm B: bottom pivot at +Z, rising towards -Z -------------------
        // The mirror image: the SAME cube with the opposite rotation. That is
        // what makes the X.
        drawCube(lift * place(Vec3(x + armOff, yMid, 0.0f),
            Vec3(LIFT_ARM_W, LIFT_ARM_T, LIFT_ARM_LEN),
            Vec3(thetaDeg, 0.0f, 0.0f)), C_LIFT_BLUE);

        // ---- Centre pin where the two arms cross ----------------------------
        // A cylinder laid on its side (rotateZ(90) puts its axis along X), so
        // it reads as a real steel pin passing through both arms.
        drawCylinder(lift * translate(x, yMid, 0.0f) * rotateZ(90.0f)
            * scale(0.17f, 0.46f, 0.17f), C_LIFT_DARK);
        drawCylinder(lift * translate(x, yMid, 0.0f) * rotateZ(90.0f)
            * scale(0.09f, 0.52f, 0.09f), C_CHROME);

        // ---- The four pivot pins of this stage ------------------------------
        const float pinZ[2] = { -halfSpan, halfSpan };
        const float pinY[2] = { yBot,      yBot + rise };
        for (int a = 0; a < 2; ++a)
        {
            for (int b = 0; b < 2; ++b)
            {
                drawCylinder(lift * translate(x, pinY[a], pinZ[b]) * rotateZ(90.0f)
                    * scale(0.15f, 0.54f, 0.15f), C_LIFT_DARK);
                // Bearing collar flanges - the fitted housing a pin actually
                // rides in, rather than a bare rod poking out of the arm.
                drawCylinder(lift * translate(x - 0.19f, pinY[a], pinZ[b]) * rotateZ(90.0f)
                    * scale(0.195f, 0.075f, 0.195f), C_STEEL);
                drawCylinder(lift * translate(x + 0.19f, pinY[a], pinZ[b]) * rotateZ(90.0f)
                    * scale(0.195f, 0.075f, 0.195f), C_STEEL);
            }
        }

        // ---- Sliding rollers --------------------------------------------------
        // On a real lift one bottom pivot is fixed and the other runs in a
        // track. The roller is the give-away detail that sells the mechanism.
        drawCylinder(lift * translate(x, yBot + 0.02f, halfSpan) * rotateZ(90.0f)
            * scale(0.20f, 0.30f, 0.20f), C_LIFT_YELLOW);

        // ---- Connecting plate between the two stages ---------------------------
        if (stage == 0)
        {
            drawCube(lift * place(Vec3(x, yBot + rise, -halfSpan), Vec3(0.40f, 0.16f, 0.46f)), C_LIFT_BLUE_D);
            drawCube(lift * place(Vec3(x, yBot + rise, halfSpan), Vec3(0.40f, 0.16f, 0.46f)), C_LIFT_BLUE_D);
        }
    }

    // ---- Hydraulic ram, lying inside the lower X ---------------------------
    // The ram points the same way as arm A. A cylinder's own axis is +Y, so to
    // lay it along a direction that is theta above horizontal we rotate about
    // X by (90 - theta): that swings +Y down to the horizontal and then back
    // up by theta.
    {
        const float d = 90.0f - thetaDeg;
        const float sy = sinf(theta), cz = cosf(theta);

        // Bottom pivot of arm A in the lift's frame.
        const float bx = x, by = LIFT_BASE_TOP, bz = -halfSpan;

        // Helper: a point a fraction `f` of the way up arm A.
        // (position = pivot + f * L * (0, sin theta, cos theta))
        auto along = [&](float f) {
            return Vec3(bx, by + f * LIFT_ARM_LEN * sy, bz + f * LIFT_ARM_LEN * cz);
            };

        // Polished rod - thin, bright, runs most of the way up.
        drawCylinder(lift * translate(along(0.52f)) * rotateX(d)
            * scale(0.105f, LIFT_ARM_LEN * 0.84f, 0.105f), C_CHROME);

        // Cylinder barrel - fat and dark, covering the lower half.
        drawCylinder(lift * translate(along(0.34f)) * rotateX(d)
            * scale(0.215f, LIFT_ARM_LEN * 0.52f, 0.215f), C_LIFT_DARK);

        // End caps and the hose fitting.
        drawCylinder(lift * translate(along(0.09f)) * rotateX(d)
            * scale(0.25f, 0.14f, 0.25f), C_LIFT_BLUE_D);
        drawCylinder(lift * translate(along(0.60f)) * rotateX(d)
            * scale(0.23f, 0.12f, 0.23f), C_LIFT_BLUE_D);
        drawCube(lift * place(Vec3(bx, by + 0.30f, bz + 0.18f), Vec3(0.09f, 0.42f, 0.09f)), C_LIFT_DARK);
    }
}


void Scene::drawHydraulicLift(const Mat4& parent, float liftHeight)
{
    // Everything in this function is drawn relative to `lift`, so moving the
    // whole machine is a one-line change.
    Mat4 lift = parent * translate(0.0f, 0.0f, LIFT_Z);

    const float theta = scissorAngle(liftHeight);
    const float rise = LIFT_ARM_LEN * sinf(theta);

    // ---- Base frame bolted to the floor ------------------------------------
    for (int s = 0; s < 2; ++s)
    {
        float x = (s == 0) ? -LIFT_TRACK : LIFT_TRACK;

        // Long base rail under each scissor stack.
        drawCube(lift * place(Vec3(x, LIFT_BASE_TOP * 0.5f, 0.0f),
            Vec3(0.66f, LIFT_BASE_TOP, LIFT_ARM_LEN + 0.60f)), C_LIFT_BLUE);

        // Thin chrome bevel along the top outer edge, so the rail reads as a
        // finished pressed-steel channel instead of a raw box.
        drawCube(lift * place(Vec3(x + 0.335f, LIFT_BASE_TOP - 0.012f, 0.0f),
            Vec3(0.020f, 0.020f, LIFT_ARM_LEN + 0.58f)), C_CHROME * 0.85f);

        // Row of bolt heads securing the rail to the floor.
        for (int i = 0; i < 5; ++i)
        {
            float z = -0.42f * LIFT_ARM_LEN + static_cast<float>(i) * (0.84f * LIFT_ARM_LEN / 4.0f);
            drawCylinder(lift * translate(x, LIFT_BASE_TOP + 0.008f, z) * scale(0.045f, 0.020f, 0.045f), C_CHROME * 0.70f);
        }

        // Dark slide track the rolling pivot runs in.
        drawCube(lift * place(Vec3(x, LIFT_BASE_TOP + 0.015f, 0.55f),
            Vec3(0.44f, 0.04f, LIFT_ARM_LEN * 0.72f)), C_LIFT_DARK);

        // Four anchor feet, each a proper 4-bolt anchor plate.
        for (int i = 0; i < 2; ++i)
        {
            float z = (i == 0) ? -(LIFT_ARM_LEN * 0.5f) : (LIFT_ARM_LEN * 0.5f);
            drawCube(lift * place(Vec3(x, 0.035f, z), Vec3(0.86f, 0.07f, 0.52f)), C_LIFT_BLUE_D);
            for (int cx = -1; cx <= 1; cx += 2)
                for (int cz = -1; cz <= 1; cz += 2)
                    drawCylinder(lift * translate(x + 0.33f * static_cast<float>(cx), 0.075f, z + 0.19f * static_cast<float>(cz))
                        * scale(0.055f, 0.045f, 0.055f), C_CHROME);
        }
    }

    // Two cross members tying the left and right base rails together.
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP * 0.5f, -1.80f), Vec3(2.60f, 0.18f, 0.34f)), C_LIFT_BLUE);
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP * 0.5f, 1.80f), Vec3(2.60f, 0.18f, 0.34f)), C_LIFT_BLUE);
    // Bevel strip along the top edge of each cross member.
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP - 0.010f, -1.80f), Vec3(2.55f, 0.018f, 0.020f)), C_CHROME * 0.80f);
    drawCube(lift * place(Vec3(0.0f, LIFT_BASE_TOP - 0.010f, 1.80f), Vec3(2.55f, 0.018f, 0.020f)), C_CHROME * 0.80f);

    // ---- The two scissor stacks --------------------------------------------
    // ONE function, called twice with a mirrored x. That is the whole point of
    // building with transforms: the left and right stacks are the same code.
    drawScissorStack(lift, -LIFT_TRACK, theta);
    drawScissorStack(lift, LIFT_TRACK, theta);

    // ---- The lifting platform -----------------------------------------------
    const float platY = LIFT_BASE_TOP + 2.0f * rise;      // underside of the beam
    const float padY = platY + LIFT_PLAT_T;              // top of the blue beam
    const float topY = padY + LIFT_PAD_T;               // the surface wheels rest on

    // Cross beams under the platform, linking the two runways.
    drawCube(lift * place(Vec3(0.0f, platY + LIFT_PLAT_T * 0.5f, -1.55f), Vec3(2.70f, 0.16f, 0.34f)), C_LIFT_BLUE_D);
    drawCube(lift * place(Vec3(0.0f, platY + LIFT_PLAT_T * 0.5f, 1.55f), Vec3(2.70f, 0.16f, 0.34f)), C_LIFT_BLUE_D);

    for (int s = 0; s < 2; ++s)
    {
        float sx = (s == 0) ? -1.0f : 1.0f;
        float x = sx * LIFT_TRACK;

        // Blue structural beam.
        drawCube(lift * place(Vec3(x, platY + LIFT_PLAT_T * 0.5f, 0.0f),
            Vec3(0.88f, LIFT_PLAT_T, LIFT_PAD_L)), C_LIFT_BLUE);

        // Yellow runway pad on top of it - the surface the tyres stand on.
        drawCube(lift * place(Vec3(x, padY + LIFT_PAD_T * 0.5f, 0.0f),
            Vec3(1.00f, LIFT_PAD_T, LIFT_PAD_L)), C_LIFT_YELLOW);

        // Raised lip along the outer edge so a wheel cannot roll off.
        drawCube(lift * place(Vec3(x + sx * 0.46f, topY + 0.045f, 0.0f),
            Vec3(0.09f, 0.09f, LIFT_PAD_L)), C_LIFT_YELLOW);
        // Thin chrome trim along the inner edge, finishing the pad the same
        // way the outer lip does.
        drawCube(lift * place(Vec3(x - sx * 0.49f, topY - 0.006f, 0.0f),
            Vec3(0.020f, 0.020f, LIFT_PAD_L)), C_CHROME * 0.80f);

        // Drive-on ramps, hinged at each end and sloping back down to the floor.
        // rotateX(+9) drops the +Z end, rotateX(-9) drops the -Z end.
        drawCube(lift * place(Vec3(x, topY - 0.06f, LIFT_PAD_L * 0.5f + 0.52f),
            Vec3(1.00f, 0.08f, 1.10f), Vec3(9.0f, 0.0f, 0.0f)), C_LIFT_YELLOW);
        drawCube(lift * place(Vec3(x, topY - 0.06f, -(LIFT_PAD_L * 0.5f + 0.52f)),
            Vec3(1.00f, 0.08f, 1.10f), Vec3(-9.0f, 0.0f, 0.0f)), C_LIFT_YELLOW);

        // Dark grip strips across the pad - set nearly flush so they read as
        // pressed diamond-plate texture rather than separate floating bars.
        for (int i = 0; i < 5; ++i)
        {
            float z = -1.80f + static_cast<float>(i) * 0.90f;
            drawCube(lift * place(Vec3(x, topY + 0.006f, z), Vec3(0.90f, 0.012f, 0.10f)), C_LIFT_DARK);
        }
    }

}


// ============================================================================
//  SECTION 7 - WHEEL  (the one place where a cube would be dishonest)
//
//  A wheel is the only shape in this project a cube cannot fake, so it is the
//  only one that uses the second primitive - and it uses the 48-segment SMOOTH
//  version, because a faceted tyre is the fastest way to make a car look like
//  a toy. Everything on it is still built by transforms in one local frame:
//
//      parent * translate(localPos) * rotateZ(90)
//
//  rotateZ(90) tips the cylinder over so its axis (local +Y) lies along the
//  world X axis - which is exactly how a real axle is oriented on a car.
//
//  A real wheel is not one disc. It is a tread band, a sidewall at a smaller
//  radius, a rim inside that, a brake disc visible THROUGH the rim, a caliper
//  clamped over the disc, and five lug nuts. Building those as concentric
//  cylinders at slightly different radii is what gives the wheel depth instead
//  of reading as a painted circle.
//
//  `outward` is +1 or -1 and says which side of the car this wheel is on, so
//  the brake disc and caliper sit on the INBOARD side where they belong.
// ============================================================================

void Scene::drawWheel(const Mat4& parent, const Vec3& localPos, float radius, float width, float outward)
{
    Mat4 hub = parent * translate(localPos) * rotateZ(90.0f);
    const float D = radius * 2.0f;
    const float s = outward;

    // Heavy all-terrain tyre: broad barrel, rounded sidewall layers and visible
    // tread blocks.  The added width/radius in SECTION 2 gives the truck the
    // planted stance that was missing in the previous render.
    // Rounded shoulder approximation: four overlapping smooth barrels with
    // progressively smaller radii as they approach the sidewall.  This keeps
    // the assignment primitive-based while avoiding the old hard 90-degree
    // cylinder edge that made the tyre look like a hockey puck.
    drawCylS(hub * scale(D, width * 0.56f, D),
             C_TIRE, 0.0f, 0.0f, 0.96f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.992f, width * 0.68f, D * 0.992f),
             C_TIRE * 1.02f, 0.0f, 0.0f, 0.95f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.978f, width * 0.78f, D * 0.978f),
             C_TIRE_WALL, 0.0f, 0.0f, 0.94f, MAT_TIRE);
    drawCylS(hub * scale(D * 0.952f, width * 0.86f, D * 0.952f),
             C_TIRE_WALL * 1.03f, 0.0f, 0.0f, 0.93f, MAT_TIRE);
    drawCylS(hub * translate(0.0f, s * width * 0.445f, 0.0f)
        * scale(D * 0.900f, width * 0.030f, D * 0.900f),
        C_TIRE_WALL * 1.08f, 0.0f, 0.0f, 0.90f, MAT_TIRE);

    // Sixteen low-profile tread lugs around the circumference.  They are kept
    // shallow so the tyre reads as real rubber rather than a gear wheel.
    for (int i = 0; i < 16; ++i)
    {
        Mat4 lug = hub * rotateY(static_cast<float>(i) * 22.5f)
            * translate(radius * 0.965f, 0.0f, 0.0f);
        drawCube(lug * scale(radius * 0.105f, width * 0.62f, radius * 0.145f),
                 C_TIRE * 0.82f, 0.0f, 0.0f, 0.98f, MAT_TIRE);
    }

    // Production-style wheel face: a shallow, nearly flush black aero cover.
    // The cover hides most of the deep rim/brake geometry so the wheel reads as
    // a flat polygonal assembly rather than a conventional sports-car wheel.
    drawCylS(hub * translate(0.0f, s * width * 0.478f, 0.0f)
        * scale(D * 0.790f, width * 0.032f, D * 0.790f),
        C_RIM, 0.0f, 0.12f, 0.48f, MAT_CARBON);
    drawCylS(hub * translate(0.0f, s * width * 0.497f, 0.0f)
        * scale(D * 0.690f, width * 0.018f, D * 0.690f),
        C_CARBON * 0.78f, 0.0f, 0.08f, 0.55f, MAT_CARBON);

    // Seven broad angular cover blades.  Their very small depth is intentional:
    // in side view they merge into one flat black polygonal aero cover.
    for (int i = 0; i < 7; ++i)
    {
        Mat4 blade = hub * translate(0.0f, s * width * 0.510f, 0.0f)
            * rotateY(static_cast<float>(i) * (360.0f / 7.0f));
        drawCube(blade * translate(D * 0.205f, 0.0f, 0.0f)
            * rotateY(8.0f)
            * scale(D * 0.255f, width * 0.012f, D * 0.105f),
            C_RIM * 0.82f, 0.0f, 0.10f, 0.50f, MAT_CARBON);
    }

    // Small flush centre cap; lug hardware is intentionally suppressed.
    drawCylS(hub * translate(0.0f, s * width * 0.520f, 0.0f)
        * scale(D * 0.205f, width * 0.018f, D * 0.205f),
        C_RIM * 0.72f, 0.0f, 0.10f, 0.44f, MAT_CARBON);

    // Brake disc remains physically present inboard but is mostly occluded by
    // the production aero cover, matching the reference vehicle.
    drawCylS(hub * translate(0.0f, -s * width * 0.300f, 0.0f)
        * scale(D * 0.555f, width * 0.045f, D * 0.555f),
        C_BRAKE_DISC * 0.62f, 0.0f, 0.76f, 0.46f, MAT_BRAKE);
}


// ============================================================================
//  SECTION 8 - CYBERTRUCK-STYLE ANGULAR PICKUP
//
//  Every vehicle in the garage is an instance of this SAME primitive-built
//  model.  No imported mesh is used.  The silhouette is intentionally faceted:
//  vertical front/rear slabs, long straight sides, steep glass, a roof peak,
//  sloping tonneau, polygonal black wheel arches, and single light bars.
//
//  The local origin is on the ground between the axles; the nose points -Z.
//  Different cars are still created by parent translation/rotation and a subtle
//  metallic tint, so all parked/lift vehicles share exactly the same geometry.
// ============================================================================



namespace
{
    struct TruckMeshBatch
    {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int count = 0;
    };

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
    static bool gTruckMeshReady = false;

    static void truckPushVertex(std::vector<float>& out, const Vec3& p, const Vec3& n)
    {
        out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
        out.push_back(n.x); out.push_back(n.y); out.push_back(n.z);
    }

    static void truckPushTriN(std::vector<float>& out,
                              const Vec3& a, const Vec3& b, const Vec3& c,
                              const Vec3& n)
    {
        truckPushVertex(out, a, n);
        truckPushVertex(out, b, n);
        truckPushVertex(out, c, n);
    }

    static void truckPushQuadN(std::vector<float>& out,
                               const Vec3& a, const Vec3& b,
                               const Vec3& c, const Vec3& d,
                               const Vec3& n)
    {
        truckPushTriN(out, a, b, c, n);
        truckPushTriN(out, a, c, d, n);
    }

    static Vec3 truckFaceNormal(const Vec3& a, const Vec3& b, const Vec3& c,
                                const Vec3& preferredDirection)
    {
        Vec3 n = normalize(cross(b - a, c - a));
        if (dot(n, preferredDirection) < 0.0f) n = -n;
        return n;
    }

    static void truckPushQuadAuto(std::vector<float>& out,
                                  const Vec3& a, const Vec3& b,
                                  const Vec3& c, const Vec3& d,
                                  const Vec3& preferredDirection)
    {
        Vec3 n = truckFaceNormal(a, b, c, preferredDirection);
        truckPushQuadN(out, a, b, c, d, n);
    }

    static void truckPushTriAuto(std::vector<float>& out,
                                 const Vec3& a, const Vec3& b, const Vec3& c,
                                 const Vec3& preferredDirection)
    {
        Vec3 n = truckFaceNormal(a, b, c, preferredDirection);
        truckPushTriN(out, a, b, c, n);
    }

    // Closed cuboid made from six quads.  Used for the physical front/rear
    // bumper blocks instead of the old single flat strip.
    static void truckPushBox(std::vector<float>& out,
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

    // The real Cybertruck arch is not circular when seen from the side.  Its
    // visible upper flare is a five-segment polygon: front lower chamfer,
    // front upper chamfer, flat crown, rear upper chamfer, rear lower chamfer.
    // This helper builds that ring as a TRUE 3D sealed extrusion: outward face,
    // inboard face, outer/inner edge walls and both end caps.
    static void truckPushFiveFacetFender(std::vector<float>& out,
                                         int side,
                                         float axleZ,
                                         float wheelRadius)
    {
        const float sx = static_cast<float>(side);
        // Keep the cladding almost flush with the stainless side skin.  The
        // older 136 mm extrusion looked aftermarket-heavy; production cladding
        // is a much thinner geometric lip.
        const float xInner = sx * 1.047f;
        const float xOuter = sx * 1.098f;
        const Vec3 nSide(sx, 0.0f, 0.0f);

        struct YZ { float y; float z; };

        // Six vertices produce exactly FIVE straight visible segments.
        const YZ outer[6] = {
            { wheelRadius + 0.020f, -0.648f },
            { wheelRadius + 0.318f, -0.548f },
            { wheelRadius + 0.548f, -0.320f },
            { wheelRadius + 0.548f,  0.320f },
            { wheelRadius + 0.318f,  0.548f },
            { wheelRadius + 0.020f,  0.648f }
        };
        const YZ inner[6] = {
            { wheelRadius + 0.018f, -0.615f },
            { wheelRadius + 0.292f, -0.520f },
            { wheelRadius + 0.520f, -0.302f },
            { wheelRadius + 0.520f,  0.302f },
            { wheelRadius + 0.292f,  0.520f },
            { wheelRadius + 0.018f,  0.615f }
        };

        for (int i = 0; i < 5; ++i)
        {
            Vec3 oo0(xOuter, outer[i].y,     axleZ + outer[i].z);
            Vec3 oo1(xOuter, outer[i + 1].y, axleZ + outer[i + 1].z);
            Vec3 oi1(xOuter, inner[i + 1].y, axleZ + inner[i + 1].z);
            Vec3 oi0(xOuter, inner[i].y,     axleZ + inner[i].z);

            Vec3 io0(xInner, outer[i].y,     axleZ + outer[i].z);
            Vec3 io1(xInner, outer[i + 1].y, axleZ + outer[i + 1].z);
            Vec3 ii1(xInner, inner[i + 1].y, axleZ + inner[i + 1].z);
            Vec3 ii0(xInner, inner[i].y,     axleZ + inner[i].z);

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

        Vec3 ioR(xInner, outer[5].y, axleZ + outer[5].z);
        Vec3 iiR(xInner, inner[5].y, axleZ + inner[5].z);
        Vec3 oiR(xOuter, inner[5].y, axleZ + inner[5].z);
        Vec3 ooR(xOuter, outer[5].y, axleZ + outer[5].z);
        truckPushQuadAuto(out, ioR, iiR, oiR, ooR, Vec3(0.0f, 0.0f, 1.0f));
    }

    static void truckUpload(TruckMeshBatch& mesh, const std::vector<float>& data)
    {
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                     data.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        mesh.count = static_cast<int>(data.size() / 6);
    }

    static void buildCybertruckShellMesh()
    {
        if (gTruckMeshReady) return;

        std::vector<float> body;
        std::vector<float> glass;
        std::vector<float> darkTop;
        std::vector<float> trim;
        std::vector<float> fenders;
        std::vector<float> bumpers;
        std::vector<float> frontLight;
        std::vector<float> rearLight;
        std::vector<float> mirrorHousing;
        std::vector<float> mirrorGlass;

        // Project vehicle axes: X = width, Y = up, Z = length, nose toward -Z.
        // The proportions below are deliberately wider/taller/heavier than V2.
        const float xBody  = 1.055f;
        // The greenhouse is almost flush with the stainless shell.  Keeping
        // only a ~25-35 mm step avoids the deeply recessed "toy window" look.
        const float xGlass = 1.025f;
        const float xWind  = 1.010f;

        const float zFrontBottom = -2.700f;
        const float zFrontTop    = -2.625f;
        const float zCowl        = -1.405f;
        // The roof peak belongs near the B-pillar, not over the front seats.
        // Moving it rearward lengthens and shallows the single windscreen slope.
        const float zRoofApex    =  0.090f;
        const float zRoofRear    =  0.785f;
        const float zRearGlass   =  1.075f;
        const float zTailTop     =  2.575f;
        const float zTailBottom  =  2.690f;

        const float yBottom      = 0.300f;
        const float yFrontTop    = 0.980f;
        const float yCowl        = 1.090f;
        const float ySill        = 1.105f;
        const float yRoofApex    = 1.720f;
        const float yRoofRear    = 1.655f;
        const float yRearGlass   = 1.225f;
        const float yTailTop     = 1.290f;
        const float yTailBottom  = 0.405f;

        // -----------------------------------------------------------------
        // STAINLESS BODY - broad clean planes, not stair-stepped cubes.
        // -----------------------------------------------------------------
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float x = sx * xBody;
            const Vec3 nSide(sx, 0.0f, 0.0f);

            Vec3 A(x, yBottom, zFrontBottom);
            Vec3 B(x, yBottom, zTailBottom);
            Vec3 C(x, yTailBottom, zTailBottom);
            Vec3 D(x, yTailTop, zTailTop);
            Vec3 E(x, yRearGlass, zRearGlass);
            Vec3 F(x, ySill, zCowl);
            Vec3 G(x, yFrontTop, zFrontTop);

            // One continuous lower side sheet with a straight belt line.
            truckPushTriN(body, A, B, C, nSide);
            truckPushTriN(body, A, C, D, nSide);
            truckPushTriN(body, A, D, E, nSide);
            truckPushTriN(body, A, E, F, nSide);
            truckPushTriN(body, A, F, G, nSide);

            // Rear stainless sail / vault shoulder.
            Vec3 sailTop(x, yRoofRear, zRoofRear);
            truckPushTriN(body, E, sailTop, D, nSide);

            // Under-window shoulder bevel: outer body folds inward toward glass.
            truckPushQuadAuto(body,
                Vec3(sx * xBody,  ySill - 0.012f, zCowl + 0.020f),
                Vec3(sx * xBody,  yRearGlass + 0.020f, zRearGlass),
                Vec3(sx * xGlass, yRearGlass + 0.055f, zRearGlass - 0.015f),
                Vec3(sx * xGlass, ySill + 0.052f, zCowl + 0.030f),
                Vec3(sx, 0.65f, 0.0f));

            // Thin stainless A-pillar bridge closes the windshield/side gap.
            truckPushQuadAuto(body,
                Vec3(sx * xBody, yCowl, zCowl),
                Vec3(sx * xBody, yRoofApex - 0.020f, zRoofApex),
                Vec3(sx * xWind, yRoofApex, zRoofApex),
                Vec3(sx * xWind, yCowl + 0.020f, zCowl),
                Vec3(sx, 0.35f, -0.25f));
        }

        // Hood / front folded planes.
        truckPushQuadAuto(body,
            Vec3(-xBody, yFrontTop, zFrontTop),
            Vec3( xBody, yFrontTop, zFrontTop),
            Vec3( xBody, yCowl,     zCowl),
            Vec3(-xBody, yCowl,     zCowl),
            Vec3(0.0f, 1.0f, 0.0f));

        truckPushQuadAuto(body,
            Vec3(-xBody, 0.390f, zFrontBottom),
            Vec3( xBody, 0.390f, zFrontBottom),
            Vec3( xBody, yFrontTop, zFrontTop),
            Vec3(-xBody, yFrontTop, zFrontTop),
            Vec3(0.0f, 0.08f, -1.0f));

        // Short angled front cheeks make the nose boxy instead of paper-thin.
        const float xFace = 0.915f;
        truckPushQuadAuto(body,
            Vec3(-xFace, 0.390f, zFrontBottom - 0.004f),
            Vec3(-xBody, 0.390f, zFrontBottom),
            Vec3(-xBody, yFrontTop, zFrontTop),
            Vec3(-xFace, yFrontTop, zFrontTop - 0.025f),
            Vec3(-1.0f, 0.0f, -0.4f));
        truckPushQuadAuto(body,
            Vec3( xBody, 0.390f, zFrontBottom),
            Vec3( xFace, 0.390f, zFrontBottom - 0.004f),
            Vec3( xFace, yFrontTop, zFrontTop - 0.025f),
            Vec3( xBody, yFrontTop, zFrontTop),
            Vec3(1.0f, 0.0f, -0.4f));

        // Tailgate: broad vertical stainless panel with a slight rake.
        truckPushQuadAuto(body,
            Vec3(-xBody, yTailBottom, zTailBottom),
            Vec3(-xBody, yTailTop,    zTailTop),
            Vec3( xBody, yTailTop,    zTailTop),
            Vec3( xBody, yTailBottom, zTailBottom),
            Vec3(0.0f, 0.0f, 1.0f));

        // -----------------------------------------------------------------
        // GLASS - inset dark panes with real folded boundaries.
        // -----------------------------------------------------------------
        truckPushQuadAuto(glass,
            Vec3(-xWind, yCowl + 0.018f, zCowl + 0.012f),
            Vec3( xWind, yCowl + 0.018f, zCowl + 0.012f),
            Vec3( xWind, yRoofApex, zRoofApex),
            Vec3(-xWind, yRoofApex, zRoofApex),
            Vec3(0.0f, 1.0f, -0.55f));

        const float zB = 0.115f;
        const float yBTop = 1.690f;
        const float yBLow = 1.145f;
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float x = sx * xGlass;
            const Vec3 nSide(sx, 0.0f, 0.0f);

            truckPushQuadN(glass,
                Vec3(x, ySill + 0.060f, zCowl + 0.060f),
                Vec3(x, yRoofApex - 0.040f, zRoofApex + 0.050f),
                Vec3(x, yBTop, zB - 0.030f),
                Vec3(x, yBLow, zB - 0.030f),
                nSide);

            truckPushQuadN(glass,
                Vec3(x, yBLow, zB + 0.040f),
                Vec3(x, yBTop, zB + 0.040f),
                Vec3(x, yRoofRear - 0.035f, zRoofRear - 0.040f),
                Vec3(x, yRearGlass + 0.055f, zRearGlass - 0.050f),
                nSide);
        }

        // Dark panoramic roof + covered vault/tonneau.
        truckPushQuadAuto(darkTop,
            Vec3(-xWind, yRoofApex, zRoofApex),
            Vec3( xWind, yRoofApex, zRoofApex),
            Vec3( xWind, yRoofRear, zRoofRear),
            Vec3(-xWind, yRoofRear, zRoofRear),
            Vec3(0.0f, 1.0f, 0.0f));
        truckPushQuadAuto(darkTop,
            Vec3(-1.018f, yRoofRear - 0.006f, zRoofRear),
            Vec3( 1.018f, yRoofRear - 0.006f, zRoofRear),
            Vec3( 1.018f, yTailTop + 0.014f, zTailTop),
            Vec3(-1.018f, yTailTop + 0.014f, zTailTop),
            Vec3(0.0f, 1.0f, 0.0f));

        // -----------------------------------------------------------------
        // Flush black window trim / seals.
        // -----------------------------------------------------------------
        const float xTrim = 1.036f;
        const float trimHalf = 0.014f;
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float x = sx * xTrim;
            const Vec3 nSide(sx, 0.0f, 0.0f);

            truckPushQuadN(trim,
                Vec3(x, ySill + 0.035f, zCowl + 0.040f),
                Vec3(x, ySill + 0.078f, zCowl + 0.040f),
                Vec3(x, yRearGlass + 0.080f, zRearGlass - 0.030f),
                Vec3(x, yRearGlass + 0.035f, zRearGlass - 0.030f),
                nSide);

            truckPushQuadN(trim,
                Vec3(x, yBLow - 0.010f, zB - trimHalf),
                Vec3(x, yBTop + 0.010f, zB - trimHalf),
                Vec3(x, yBTop + 0.010f, zB + trimHalf),
                Vec3(x, yBLow - 0.010f, zB + trimHalf),
                nSide);

            truckPushQuadN(trim,
                Vec3(x, yRoofApex - 0.020f, zRoofApex + 0.030f),
                Vec3(x, yRoofApex + 0.020f, zRoofApex + 0.030f),
                Vec3(x, yRoofRear + 0.020f, zRoofRear),
                Vec3(x, yRoofRear - 0.025f, zRoofRear),
                nSide);
        }

        // -----------------------------------------------------------------
        // TRUE 3D FIVE-FACET FENDER FLARES - four sealed extrusions.
        // -----------------------------------------------------------------
        truckPushFiveFacetFender(fenders, -1, CAR_FAXLE_Z, CAR_FWHEEL_R);
        truckPushFiveFacetFender(fenders,  1, CAR_FAXLE_Z, CAR_FWHEEL_R);
        truckPushFiveFacetFender(fenders, -1, CAR_RAXLE_Z, CAR_RWHEEL_R);
        truckPushFiveFacetFender(fenders,  1, CAR_RAXLE_Z, CAR_RWHEEL_R);

        // -----------------------------------------------------------------
        // BUMPERS - full 3D blocks.  The rear block projects behind and wider
        // than the tailgate, which was missing from the old flat-strip model.
        // -----------------------------------------------------------------
        truckPushBox(bumpers, -1.105f, 1.105f, 0.165f, 0.395f, -2.820f, -2.625f);
        truckPushBox(bumpers, -1.120f, 1.120f, 0.155f, 0.420f,  2.605f,  2.855f);

        // Full-width front white and rear red light bars.
        truckPushQuadN(frontLight,
            Vec3(-0.980f, 0.950f, -2.634f), Vec3(0.980f, 0.950f, -2.634f),
            Vec3( 0.980f, 0.992f, -2.629f), Vec3(-0.980f, 0.992f, -2.629f),
            Vec3(0.0f, 0.0f, -1.0f));
        truckPushQuadN(rearLight,
            Vec3(-0.985f, 1.238f, 2.695f), Vec3(-0.985f, 1.282f, 2.690f),
            Vec3( 0.985f, 1.282f, 2.690f), Vec3( 0.985f, 1.238f, 2.695f),
            Vec3(0.0f, 0.0f, 1.0f));

        // Angular triangular production mirrors at the base of the A-pillars.
        // Each is a shallow triangular prism rather than a rectangular cube.
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = static_cast<float>(side);
            const float xIn  = sx * 1.045f;
            const float xOut = sx * 1.245f;
            const Vec3 outN(sx, 0.0f, 0.0f);

            Vec3 aIn (xIn,  1.145f, -1.285f);
            Vec3 bIn (xIn,  1.315f, -1.145f);
            Vec3 cIn (xIn,  1.155f, -0.985f);
            Vec3 aOut(xOut, 1.145f, -1.285f);
            Vec3 bOut(xOut, 1.315f, -1.145f);
            Vec3 cOut(xOut, 1.155f, -0.985f);

            truckPushTriN(mirrorHousing, aOut, bOut, cOut, outN);
            truckPushTriN(mirrorHousing, cIn, bIn, aIn, -outN);
            truckPushQuadAuto(mirrorHousing, aIn, aOut, bOut, bIn, Vec3(0.0f, 0.55f, -0.55f));
            truckPushQuadAuto(mirrorHousing, bIn, bOut, cOut, cIn, Vec3(0.0f, 0.45f, 0.45f));
            truckPushQuadAuto(mirrorHousing, cIn, cOut, aOut, aIn, Vec3(0.0f, -1.0f, 0.0f));

            // Slightly inset reflective triangular face avoids z-fighting.
            const float xFace = sx * 1.247f;
            truckPushTriN(mirrorGlass,
                Vec3(xFace, 1.165f, -1.250f),
                Vec3(xFace, 1.292f, -1.145f),
                Vec3(xFace, 1.170f, -1.020f), outN);
        }

        truckUpload(gTruckBodyMesh, body);
        truckUpload(gTruckGlassMesh, glass);
        truckUpload(gTruckDarkTopMesh, darkTop);
        truckUpload(gTruckTrimMesh, trim);
        truckUpload(gTruckFenderMesh, fenders);
        truckUpload(gTruckBumperMesh, bumpers);
        truckUpload(gTruckFrontLightMesh, frontLight);
        truckUpload(gTruckRearLightMesh, rearLight);
        truckUpload(gTruckMirrorHousingMesh, mirrorHousing);
        truckUpload(gTruckMirrorGlassMesh, mirrorGlass);
        gTruckMeshReady = true;
    }

    static void drawTruckMeshBatch(const TruckMeshBatch& mesh,
                                   const Mat4& model,
                                   const Vec3& color,
                                   float emissive = 0.0f,
                                   float metallic = 0.0f,
                                   float roughness = 0.65f,
                                   int materialType = -1)
    {
        Vec3 shownColor;
        if (!teachingPrepareColor(color, shownColor)) return;

        Mat3 nrm = normalMatrix(model);
        glUniformMatrix4fv(gLocModel, 1, GL_FALSE, model.m);
        glUniformMatrix3fv(gLocNormalMat, 1, GL_FALSE, nrm.m);
        uploadMaterialUniforms(shownColor, emissive, metallic, roughness, materialType);
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.count);
        glBindVertexArray(0);
    }
}

void Scene::drawCar(const Mat4& parent, const Vec3& bodyColor)
{
    buildCybertruckShellMesh();

    // Body colour remains almost neutral stainless even when drawCar() receives
    // the old per-car tint.  This keeps every instance recognisably Cybertruck.
    const Vec3 stainless(0.505f, 0.525f, 0.550f);
    const Vec3 body = stainless * 0.990f + bodyColor * 0.010f;
    const Vec3 glass(0.022f, 0.032f, 0.045f);
    const Vec3 tonneau(0.045f, 0.052f, 0.062f);
    const Vec3 trim(0.030f, 0.032f, 0.036f);

    // Metallic/roughness values tuned for bare brushed stainless and glossy
    // neutral glass.  Lower albedo prevents the body from self-illuminating.
    drawTruckMeshBatch(gTruckBodyMesh,       parent, body,    0.0f, 0.96f, 0.30f, MAT_STAINLESS);
    drawTruckMeshBatch(gTruckGlassMesh,      parent, glass,   0.0f, 0.02f, 0.055f, MAT_GLASS);
    drawTruckMeshBatch(gTruckDarkTopMesh,    parent, tonneau, 0.0f, 0.08f, 0.30f, MAT_CARBON);
    drawTruckMeshBatch(gTruckTrimMesh,       parent, trim,    0.0f, 0.01f, 0.78f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckFenderMesh,     parent, trim,    0.0f, 0.01f, 0.80f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckBumperMesh,     parent, trim,    0.0f, 0.01f, 0.84f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckMirrorHousingMesh, parent, trim, 0.0f, 0.01f, 0.66f, MAT_PLASTIC);
    drawTruckMeshBatch(gTruckMirrorGlassMesh,   parent, glass * 1.15f, 0.0f, 0.02f, 0.045f, MAT_GLASS);
    drawTruckMeshBatch(gTruckFrontLightMesh, parent, Vec3(1.0f, 0.975f, 0.900f), 1.0f, 0.0f, 0.10f, MAT_PAINT);
    drawTruckMeshBatch(gTruckRearLightMesh,  parent, Vec3(0.940f, 0.060f, 0.045f), 1.0f, 0.0f, 0.10f, MAT_PAINT);

    // Boxy rocker cladding ties the lower body into the fender flare depth.
    drawCube(parent * place(Vec3(-1.070f, 0.438f,  0.000f), Vec3(0.055f, 0.145f, 2.210f)),
             trim, 0.0f, 0.0f, 0.82f);
    drawCube(parent * place(Vec3( 1.070f, 0.438f,  0.000f), Vec3(0.055f, 0.145f, 2.210f)),
             trim, 0.0f, 0.0f, 0.82f);

    // Very thin door shut-lines and flush handle recesses; no floating trim strips.
    for (int side = -1; side <= 1; side += 2)
    {
        const float sx = static_cast<float>(side);
        drawCube(parent * place(Vec3(sx * 1.057f, 0.792f, -0.345f), Vec3(0.006f, 0.500f, 0.008f)),
                 trim * 0.56f, 0.0f, 0.0f, 0.90f, MAT_PLASTIC);
        drawCube(parent * place(Vec3(sx * 1.057f, 0.792f,  0.750f), Vec3(0.006f, 0.500f, 0.008f)),
                 trim * 0.56f, 0.0f, 0.0f, 0.90f, MAT_PLASTIC);
        drawCube(parent * place(Vec3(sx * 1.058f, 0.952f, -0.145f), Vec3(0.007f, 0.018f, 0.170f)),
                 trim * 0.58f, 0.0f, 0.0f, 0.82f, MAT_PLASTIC);
        drawCube(parent * place(Vec3(sx * 1.058f, 0.952f,  0.925f), Vec3(0.007f, 0.018f, 0.170f)),
                 trim * 0.58f, 0.0f, 0.0f, 0.82f, MAT_PLASTIC);
    }


    // ---------------------------------------------------------------------
    // Production-side details: front door leading seams, subtle folded-edge
    // highlight strips and compact side mirrors.  The strips are deliberately
    // millimetre-thin: they simulate the real truck's tiny edge radii rather
    // than adding decorative chrome mouldings that the Cybertruck does not have.
    // ---------------------------------------------------------------------
    auto drawSideFold = [&](float sx, float y0, float z0, float y1, float z1,
                            float thickness, const Vec3& c)
    {
        const float dy = y1 - y0;
        const float dz = z1 - z0;
        const float len = std::sqrt(dy * dy + dz * dz);
        const float angleX = std::atan2(-dy, dz) * 180.0f / PI_F;
        drawCube(parent * place(Vec3(sx * 1.058f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f),
            Vec3(thickness, thickness, len), Vec3(angleX, 0.0f, 0.0f)),
            c, 0.0f, 0.94f, 0.18f, MAT_STAINLESS);
    };

    for (int side = -1; side <= 1; side += 2)
    {
        const float sx = static_cast<float>(side);

        // Leading edge of the front door, just behind the front quarter panel.
        drawCube(parent * place(Vec3(sx * 1.057f, 0.795f, -1.115f),
            Vec3(0.006f, 0.545f, 0.008f)), trim * 0.52f,
            0.0f, 0.0f, 0.92f, MAT_PLASTIC);

        // Millimetre-scale fold catches only; these should appear as specular
        // edge response, not as decorative chrome strips.
        drawSideFold(sx, 0.980f, -2.625f, 1.090f, -1.405f, 0.006f, body * 1.025f);
        drawSideFold(sx, 1.720f,  0.090f, 1.655f,  0.785f, 0.006f, body * 1.030f);
        drawSideFold(sx, 1.655f,  0.785f, 1.290f,  2.575f, 0.006f, body * 1.020f);
    }

    // IMPORTANT: width in this project is X, not Z.  The requested outward tyre
    // offset is therefore applied as +/-X.  This moves the heavy tyres out under
    // the newly extruded fenders instead of leaving them tucked under the body.
    const float fTrack = CAR_FTRACK + CAR_WHEEL_OUTSET;
    const float rTrack = CAR_RTRACK + CAR_WHEEL_OUTSET;
    drawWheel(parent, Vec3(-fTrack, CAR_FWHEEL_R, CAR_FAXLE_Z), CAR_FWHEEL_R, CAR_FWHEEL_W,  1.0f);
    drawWheel(parent, Vec3( fTrack, CAR_FWHEEL_R, CAR_FAXLE_Z), CAR_FWHEEL_R, CAR_FWHEEL_W, -1.0f);
    drawWheel(parent, Vec3(-rTrack, CAR_RWHEEL_R, CAR_RAXLE_Z), CAR_RWHEEL_R, CAR_RWHEEL_W,  1.0f);
    drawWheel(parent, Vec3( rTrack, CAR_RWHEEL_R, CAR_RAXLE_Z), CAR_RWHEEL_R, CAR_RWHEEL_W, -1.0f);
}



// ============================================================================
//  SECTION 9 - SCANNER HEAD
//
//  Drawn in whatever frame it is handed. The robot arm hands it a frame that
//  has already been un-rotated, so the scanner always hangs level - like a
//  real gimbal-mounted sensor.
// ============================================================================

void Scene::drawScanner(const Mat4& parent)
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


// ============================================================================
//  SECTION 10 - TWO-JOINT ROBOTIC INSPECTION ARM
//
//  This is the clearest example of HIERARCHICAL TRANSFORMATION in the project.
//
//      base
//        |
//        +-- joint 1  (rotateZ by joint1Angle)
//               |
//               +-- lower arm  (length ARM_LOWER_LEN)
//                      |
//                      +-- joint 2  (rotateZ by joint2Angle)
//                             |
//                             +-- upper arm  (length ARM_UPPER_LEN)
//                                    |
//                                    +-- scanner
//
//  The key trick: each arm segment is drawn with its PIVOT at the bottom.
//  We build the segment's matrix as
//        jointFrame * translate(0, length/2, 0) * scale(w, length, d)
//  so the cube's centre sits half a length above the joint - which means the
//  joint, not the middle of the segment, is what rotates.
//
//  Change joint1Angle / joint2Angle and the whole chain below moves with it.
//  That is exactly what the animation stage will do.
// ============================================================================

// ============================================================================
//  SECTION 10 - FULLY CONTROLLABLE ROBOT ARM + QUICK-CHANGE TOOLS
//
//  Degrees of freedom:
//    baseYaw    : rotate the entire upper robot sideways around world/local Y
//    joint1     : shoulder pitch (Z)
//    joint2     : elbow pitch (Z)
//    wristPitch : end-effector tilt about Z; clamped to -90..+90 = full 180-degree top-to-bottom sweep
//    wristRoll  : tool roll (Y)
//
//  toolType: 0 scanner, 1 impact driver, 2 gripper, 3 polisher.
// ============================================================================

static void drawRobotToolHeadImpl(const Mat4& parent, int toolType)
{
    // Common quick-change coupler.
    Scene::drawCylS(parent * translate(0.0f, -0.08f, 0.0f) * scale(0.25f, 0.14f, 0.25f), C_JOINT);

    if (toolType == 1) // impact driver / drill
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.34f, 0.0f), Vec3(0.42f, 0.42f, 0.34f)), C_ROBOT);
        Scene::drawCylS(parent * translate(0.0f, -0.63f, 0.0f) * scale(0.14f, 0.30f, 0.14f), C_ROBOT_DARK);
        Scene::drawCylS(parent * translate(0.0f, -0.88f, 0.0f) * scale(0.055f, 0.22f, 0.055f), C_CHROME);
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.31f, 0.19f), Vec3(0.25f, 0.10f, 0.05f)), C_NEON_CYAN, 0.55f);
    }
    else if (toolType == 2) // two-finger gripper
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.30f, 0.0f), Vec3(0.48f, 0.24f, 0.38f)), C_ROBOT_DARK);
        for (int s = -1; s <= 1; s += 2)
        {
            float x = 0.24f * static_cast<float>(s);
            Scene::drawCube(parent * Scene::place(Vec3(x, -0.56f, 0.0f), Vec3(0.10f, 0.50f, 0.12f),
                Vec3(0.0f, 0.0f, -12.0f * static_cast<float>(s))), C_CHROME);
            Scene::drawCube(parent * Scene::place(Vec3(x + 0.055f * static_cast<float>(s), -0.81f, 0.0f),
                Vec3(0.16f, 0.12f, 0.18f)), C_RUBBER);
        }
    }
    else if (toolType == 3) // rotary polisher
    {
        Scene::drawCube(parent * Scene::place(Vec3(0.0f, -0.30f, 0.0f), Vec3(0.42f, 0.34f, 0.38f)), C_ROBOT);
        Scene::drawCylS(parent * translate(0.0f, -0.55f, 0.0f) * scale(0.13f, 0.22f, 0.13f), C_ROBOT_DARK);
        Scene::drawCylS(parent * translate(0.0f, -0.72f, 0.0f) * scale(0.58f, 0.08f, 0.58f), Vec3(0.62f, 0.62f, 0.58f));
    }
    else // scanner / inspection light head
    {
        Scene::drawScanner(parent);
    }
}

void Scene::drawRobotToolStation(const Mat4& parent, int selectedTool)
{
    // Sideways tool-changing table beside the arm.
    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), 2.65f, 1.25f);
    drawCube(parent * place(Vec3(0.0f, 0.78f, 0.0f), Vec3(2.60f, 0.12f, 1.10f)), C_STEEL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            drawCube(parent * place(Vec3(1.05f * sx, 0.38f, 0.38f * sz), Vec3(0.12f, 0.76f, 0.12f)), C_ROBOT_DARK);

    // Four dock sockets. The selected tool is absent from the rack because it
    // is visually attached to the robot wrist.
    for (int i = 0; i < 4; ++i)
    {
        float x = -0.90f + static_cast<float>(i) * 0.60f;
        drawCylS(parent * translate(x, 0.89f, 0.0f) * scale(0.27f, 0.06f, 0.27f),
            (i == selectedTool) ? C_NEON_CYAN : C_ROBOT_DARK, (i == selectedTool) ? 0.55f : 0.0f);
        if (i != selectedTool)
        {
            Mat4 dock = parent * translate(x, 1.32f, 0.0f) * scale(0.62f, 0.62f, 0.62f);
            drawRobotToolHeadImpl(dock, i);
        }
    }

    // Back board and simple status lamps.
    drawCube(parent * place(Vec3(0.0f, 1.45f, 0.48f), Vec3(2.55f, 1.20f, 0.08f)), C_ROBOT_DARK);
    for (int i = 0; i < 4; ++i)
    {
        float x = -0.90f + static_cast<float>(i) * 0.60f;
        drawCube(parent * place(Vec3(x, 1.82f, 0.53f), Vec3(0.18f, 0.08f, 0.04f)),
            (i == selectedTool) ? C_NEON : C_STEEL, (i == selectedTool) ? 0.7f : 0.0f);
    }
}

void Scene::drawRobotArm(const Mat4& parent, float baseYaw, float joint1Angle, float joint2Angle,
    float wristPitch, float wristRoll, int toolType)
{
    // IMPORTANT MECHANICAL RULE:
    // The rectangular pedestal is bolted to the floor and NEVER yaws. Only the
    // circular turntable at the top, the shoulder yoke, and the complete arm
    // above it rotate around Y.  This matches a real industrial robot base.

    // ---- FIXED FOUNDATION ------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 0.09f, 0.0f), Vec3(1.70f, 0.18f, 1.70f)), C_ROBOT_DARK);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
        {
            Vec3 bp(0.68f * static_cast<float>(sx), 0.0f, 0.68f * static_cast<float>(sz));
            drawCylinder(parent * translate(bp) * scale(0.095f, 0.20f, 0.095f), C_STEEL);      // bolt shank
            drawCylinder(parent * translate(bp.x, 0.205f, bp.z) * scale(0.115f, 0.045f, 0.115f), C_CHROME);  // hex head
            drawSphere(parent * place(Vec3(bp.x, 0.235f, bp.z), Vec3(0.045f, 0.030f, 0.045f)), C_CHROME * 1.15f); // highlight dome
        }

    drawCube(parent * place(Vec3(0.0f, 0.48f, 0.0f), Vec3(1.05f, 0.62f, 1.05f)), C_ROBOT);
    drawCube(parent * place(Vec3(0.0f, 0.55f, 0.54f), Vec3(0.72f, 0.10f, 0.05f)), C_NEON, 0.9f);

    // ---- ROTATING TURNTABLE + EVERYTHING ABOVE IT -----------------------
    // The round bearing is centred on the same vertical axis, so yaw does not
    // translate it; it rotates the yoke/arm that is physically bolted to it.
    Mat4 rotatingBase = parent * rotateY(baseYaw);
    drawCylS(rotatingBase * place(Vec3(0.0f, 0.86f, 0.0f), Vec3(1.00f, 0.18f, 1.00f)), C_ROBOT_DARK);
    drawCube(rotatingBase * place(Vec3(0.0f, 1.00f, 0.0f), Vec3(0.55f, 0.30f, 0.66f)), C_ROBOT);

    // Shoulder pitch.
    Mat4 j1 = rotatingBase * translate(0.0f, ARM_SHOULDER_Y, 0.0f) * rotateZ(joint1Angle);
    drawCylS(j1 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.48f, 0.70f, 0.48f),
        Vec3(90.0f, 0.0f, 0.0f)), C_JOINT);
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
    drawCylS(j1 * place(Vec3(0.0f, 0.16f, -0.20f), Vec3(0.075f, 0.10f, 0.075f)), C_CHROME * 0.70f);
    drawCylS(j1 * place(Vec3(0.0f, ARM_LOWER_LEN - 0.16f, -0.20f), Vec3(0.075f, 0.10f, 0.075f)), C_CHROME * 0.70f);

    // Elbow pitch.
    Mat4 j2 = j1 * translate(0.0f, ARM_LOWER_LEN, 0.0f) * rotateZ(joint2Angle);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.42f, 0.60f, 0.42f),
        Vec3(90.0f, 0.0f, 0.0f)), C_JOINT);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, 0.20f), Vec3(0.465f, 0.06f, 0.465f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawCylS(j2 * place(Vec3(0.0f, 0.0f, -0.20f), Vec3(0.465f, 0.06f, 0.465f), Vec3(90.0f, 0.0f, 0.0f)), C_ROBOT_DARK);
    drawSphere(j2 * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.47f, 0.47f, 0.47f)), C_JOINT * 0.94f);
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.5f, 0.0f), Vec3(0.26f, ARM_UPPER_LEN, 0.26f)), C_ROBOT);
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.55f, 0.0f), Vec3(0.30f, 0.09f, 0.30f)), C_ROBOT_DARK);
    // Matching cable conduit continuing up the forearm from the shoulder run.
    drawCube(j2 * place(Vec3(0.0f, ARM_UPPER_LEN * 0.5f, -0.17f), Vec3(0.08f, ARM_UPPER_LEN - 0.26f, 0.08f)), C_ROBOT_DARK);
    drawCylS(j2 * place(Vec3(0.0f, 0.14f, -0.17f), Vec3(0.060f, 0.09f, 0.060f)), C_CHROME * 0.70f);

    // User-controlled wrist.  Main.cpp clamps wristPitch to -90..+90, so the
    // complete scanner/tool head has a true 180-degree top-to-bottom hinge sweep.
    // There is deliberately no automatic leveling: the operator controls where
    // the head points while the long-reach shoulder/forearm cover the whole truck.
    Mat4 tip = j2 * translate(0.0f, ARM_UPPER_LEN, 0.0f);
    drawSphere(tip * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.38f, 0.38f, 0.38f)), C_JOINT);
    drawCube(tip * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.28f, 0.20f, 0.28f)), C_JOINT * 0.82f);
    Mat4 wrist = tip * rotateZ(wristPitch) * rotateY(wristRoll);
    drawCylS(wrist * translate(0.0f, -0.08f, 0.0f) * scale(0.31f, 0.13f, 0.31f), C_JOINT);
    drawRobotToolHeadImpl(wrist * translate(0.0f, -0.16f, 0.0f), toolType);
}
// ============================================================================
//  SECTION 12 - OVERHEAD LAMP FIXTURE
//
//  Thin dark housing + a bright emissive panel just underneath it, hung from
//  the ceiling by two rods.
// ============================================================================

void Scene::drawLamp(const Mat4& parent)
{
    float rodLen = CEIL_Y - LAMP_Y;   // reach from the fixture up to the ceiling

    drawCube(parent * place(Vec3(-0.95f, rodLen * 0.5f, 0.0f), Vec3(0.05f, rodLen, 0.05f)), C_STEEL * 0.7f);
    drawCube(parent * place(Vec3(0.95f, rodLen * 0.5f, 0.0f), Vec3(0.05f, rodLen, 0.05f)), C_STEEL * 0.7f);

    // Housing.
    drawCube(parent * place(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.70f, 0.18f, 0.56f)), C_LAMP_HOUSING);

    // End caps.
    drawCube(parent * place(Vec3(-1.38f, -0.02f, 0.0f), Vec3(0.10f, 0.26f, 0.56f)), C_LAMP_HOUSING);
    drawCube(parent * place(Vec3(1.38f, -0.02f, 0.0f), Vec3(0.10f, 0.26f, 0.56f)), C_LAMP_HOUSING);

    // The glowing surface. `emissive = 1.0` makes the shader skip lighting.
    drawCube(parent * place(Vec3(0.0f, -0.11f, 0.0f), Vec3(2.46f, 0.05f, 0.42f)), C_LAMP_LIGHT, 1.0f);
}


// ============================================================================
//  SECTION 13 - NEON SIGN:  "SMART AUTO SERVICE"
//
//  No text-rendering library is used. Instead each letter is stored as a
//  5-wide x 7-tall pattern of characters, and every '#' becomes one small
//  glowing cube. So the sign is literally spelled out in cubes - which fits
//  the whole philosophy of the project.
// ============================================================================

// Each glyph is 7 strings of 5 characters. Row 0 is the TOP row.
static const char* GLYPH_SPACE[7] = { ".....", ".....", ".....", ".....", ".....", ".....", "....." };
static const char* GLYPH_S[7] = { ".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###." };
static const char* GLYPH_M[7] = { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" };
static const char* GLYPH_A[7] = { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
static const char* GLYPH_R[7] = { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" };
static const char* GLYPH_T[7] = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." };
static const char* GLYPH_U[7] = { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." };
static const char* GLYPH_O[7] = { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." };
static const char* GLYPH_E[7] = { "#####", "#....", "#....", "####.", "#....", "#....", "#####" };
static const char* GLYPH_V[7] = { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." };
static const char* GLYPH_I[7] = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" };
static const char* GLYPH_C[7] = { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." };

// Returns the 7-row pattern for one character (only the letters we need).
static const char* const* glyphFor(char c)
{
    switch (c)
    {
    case 'S': return GLYPH_S;
    case 'M': return GLYPH_M;
    case 'A': return GLYPH_A;
    case 'R': return GLYPH_R;
    case 'T': return GLYPH_T;
    case 'U': return GLYPH_U;
    case 'O': return GLYPH_O;
    case 'E': return GLYPH_E;
    case 'V': return GLYPH_V;
    case 'I': return GLYPH_I;
    case 'C': return GLYPH_C;
    default:  return GLYPH_SPACE;
    }
}


// Draws `text` as a grid of small cubes in the parent's XY plane.
static void drawCubeText(const Mat4& parent, const char* text,
    float cell, float zFront, const Vec3& color)
{
    const int   GW = 5;          // glyph width in cells
    const int   GH = 7;          // glyph height in cells
    const float advance = static_cast<float>(GW + 1) * cell;   // 1 blank column between letters

    int   count = static_cast<int>(std::strlen(text));
    float totalW = static_cast<float>(count) * advance - cell;
    float startX = -totalW * 0.5f + cell * 0.5f;
    float topY = (static_cast<float>(GH) * cell) * 0.5f - cell * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        const char* const* g = glyphFor(text[i]);
        float baseX = startX + static_cast<float>(i) * advance;

        for (int row = 0; row < GH; ++row)
        {
            for (int col = 0; col < GW; ++col)
            {
                if (g[row][col] != '#') continue;

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


void Scene::drawNeonSign(const Mat4& parent)
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


// ============================================================================
//  SECTION 14 - TOOL CABINET
// ============================================================================

void Scene::drawToolCabinet(const Mat4& parent)
{
    // ---- Casters -----------------------------------------------------------
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            drawCylinder(parent * place(Vec3(1.10f * static_cast<float>(sx), 0.10f, 0.36f * static_cast<float>(sz)),
                Vec3(0.20f, 0.12f, 0.20f), Vec3(0.0f, 0.0f, 90.0f)), C_ROBOT_DARK);

    // ---- Main body ---------------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 0.98f, 0.0f), Vec3(2.70f, 1.76f, 1.05f)), C_CABINET);

    // ---- Work-top ----------------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 1.92f, 0.02f), Vec3(2.90f, 0.10f, 1.15f)), C_SHELF);
    drawCube(parent * place(Vec3(0.0f, 1.975f, 0.60f), Vec3(2.90f, 0.012f, 0.020f)), C_CHROME * 0.85f);

    // ---- Four drawers, each a flat cube on the front face ------------------
    for (int i = 0; i < 4; ++i)
    {
        float y = 0.38f + static_cast<float>(i) * 0.42f;

        drawCube(parent * place(Vec3(0.0f, y, 0.55f), Vec3(2.46f, 0.34f, 0.06f)), C_CABINET_DRAWER);

        // Drawer handle.
        drawCube(parent * place(Vec3(0.0f, y + 0.10f, 0.60f), Vec3(1.25f, 0.06f, 0.05f)), C_RIM);
    }

    // ---- Pegboard above the cabinet ----------------------------------------
    drawCube(parent * place(Vec3(0.0f, 2.62f, -0.48f), Vec3(2.70f, 1.30f, 0.06f)), C_WALL_DARK);

    // A few "tools" hanging on it (thin cubes of different sizes).
    drawCube(parent * place(Vec3(-0.90f, 2.70f, -0.42f), Vec3(0.09f, 0.62f, 0.05f)), C_STEEL);
    drawCube(parent * place(Vec3(-0.50f, 2.80f, -0.42f), Vec3(0.09f, 0.42f, 0.05f)), C_STEEL);
    drawCube(parent * place(Vec3(-0.12f, 2.62f, -0.42f), Vec3(0.14f, 0.80f, 0.05f)), C_LIFT_YELLOW);
    drawCube(parent * place(Vec3(0.35f, 2.75f, -0.42f), Vec3(0.09f, 0.52f, 0.05f)), C_STEEL);
    drawCube(parent * place(Vec3(0.85f, 2.68f, -0.42f), Vec3(0.24f, 0.24f, 0.05f)), C_CABINET_DRAWER);

    // Small glowing label strip on the work-top edge.
    drawCube(parent * place(Vec3(0.0f, 1.96f, 0.58f), Vec3(1.60f, 0.06f, 0.04f)), C_NEON, 0.9f);
}


// ============================================================================
//  SECTION 15 - SHELVING UNIT
// ============================================================================

void Scene::drawShelf(const Mat4& parent)
{
    // ---- Two vertical side supports ----------------------------------------
    drawCube(parent * place(Vec3(-1.34f, 1.28f, 0.0f), Vec3(0.12f, 2.56f, 1.00f)), C_SHELF_DARK);
    drawCube(parent * place(Vec3(1.34f, 1.28f, 0.0f), Vec3(0.12f, 2.56f, 1.00f)), C_SHELF_DARK);

    // ---- Back braces --------------------------------------------------------
    drawCube(parent * place(Vec3(0.0f, 0.90f, -0.46f), Vec3(2.80f, 0.07f, 0.07f)), C_SHELF_DARK);
    drawCube(parent * place(Vec3(0.0f, 1.66f, -0.46f), Vec3(2.80f, 0.07f, 0.07f)), C_SHELF_DARK);

    // ---- Three shelf levels + the top ---------------------------------------
    const float levelY[4] = { 0.45f, 1.25f, 2.05f, 2.54f };
    for (int i = 0; i < 4; ++i)
        drawCube(parent * place(Vec3(0.0f, levelY[i], 0.0f), Vec3(2.80f, 0.08f, 1.00f)), C_SHELF);

    // ---- Boxes / cans stored on the shelves ---------------------------------
    // A small table so the props are easy to read and easy to change.
    struct Item { float x, level, w, h, d, r, g, b; };
    static const Item items[] =
    {
        { -0.95f, 0.0f, 0.62f, 0.44f, 0.60f, 0.72f, 0.52f, 0.24f },
        { -0.18f, 0.0f, 0.50f, 0.56f, 0.52f, 0.30f, 0.52f, 0.36f },
        {  0.72f, 0.0f, 0.78f, 0.34f, 0.62f, 0.55f, 0.56f, 0.62f },

        { -0.85f, 1.0f, 0.70f, 0.50f, 0.58f, 0.24f, 0.40f, 0.64f },
        {  0.10f, 1.0f, 0.44f, 0.62f, 0.44f, 0.80f, 0.74f, 0.26f },
        {  0.90f, 1.0f, 0.58f, 0.38f, 0.56f, 0.66f, 0.28f, 0.26f },

        { -0.70f, 2.0f, 0.90f, 0.42f, 0.64f, 0.40f, 0.42f, 0.48f },
        {  0.55f, 2.0f, 0.66f, 0.54f, 0.54f, 0.22f, 0.58f, 0.58f },
    };

    for (const Item& it : items)
    {
        int   li = static_cast<int>(it.level);
        float y = levelY[li] + 0.04f + it.h * 0.5f;
        drawCube(parent * place(Vec3(it.x, y, 0.0f), Vec3(it.w, it.h, it.d)),
            Vec3(it.r, it.g, it.b));
    }

    // ---- Feet ---------------------------------------------------------------
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            drawCube(parent * place(Vec3(1.34f * static_cast<float>(sx), 0.03f, 0.40f * static_cast<float>(sz)),
                Vec3(0.24f, 0.06f, 0.24f)), C_ROBOT_DARK);
}


// ============================================================================
//  SECTION 16 - SMALL PROPS
//  Tyre stack, two oil drums and a few crates. They cost almost nothing but
//  they make the bay feel occupied instead of empty.
// ============================================================================

void Scene::drawProps(const Mat4& parent)
{
    // ---- Stack of spare tyres ----------------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        float y = 0.15f + static_cast<float>(i) * 0.28f;
        drawCylinder(parent * place(Vec3(-6.90f, y, 4.20f), Vec3(1.30f, 0.28f, 1.30f)), C_TIRE);
        drawCylinder(parent * place(Vec3(-6.90f, y, 4.20f), Vec3(0.70f, 0.30f, 0.70f)), C_SHELF_DARK);
    }

    // ---- Two oil drums ------------------------------------------------------
    for (int i = 0; i < 2; ++i)
    {
        float z = -6.40f + static_cast<float>(i) * 1.15f;
        drawCylinder(parent * place(Vec3(-7.05f, 0.58f, z), Vec3(0.95f, 1.16f, 0.95f)), C_LIFT_YELLOW);
        drawCylinder(parent * place(Vec3(-7.05f, 1.18f, z), Vec3(1.02f, 0.08f, 1.02f)), C_ROBOT_DARK);
        drawCube(parent * place(Vec3(-7.05f, 0.70f, z + 0.48f), Vec3(0.44f, 0.30f, 0.05f)), C_ROBOT_DARK);
    }

    // ---- A few crates against the right wall --------------------------------
    drawCube(parent * place(Vec3(7.90f, 0.35f, 9.90f), Vec3(1.10f, 0.70f, 1.00f)), Vec3(0.52f, 0.42f, 0.28f));
    drawCube(parent * place(Vec3(7.90f, 0.92f, 9.90f), Vec3(0.86f, 0.44f, 0.80f), Vec3(0.0f, 14.0f, 0.0f)),
        Vec3(0.60f, 0.48f, 0.32f));
    drawCube(parent * place(Vec3(6.70f, 0.28f, 9.55f), Vec3(0.80f, 0.56f, 0.76f), Vec3(0.0f, -22.0f, 0.0f)),
        Vec3(0.46f, 0.38f, 0.26f));

    // ---- Small tool trolley near the lift ------------------------------------
    drawCube(parent * place(Vec3(2.95f, 0.62f, 2.60f), Vec3(0.80f, 0.06f, 0.60f)), C_SHELF);
    drawCube(parent * place(Vec3(2.95f, 0.32f, 2.60f), Vec3(0.76f, 0.06f, 0.56f)), C_SHELF);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            drawCube(parent * place(Vec3(2.95f + 0.34f * static_cast<float>(sx), 0.34f,
                2.60f + 0.24f * static_cast<float>(sz)),
                Vec3(0.05f, 0.62f, 0.05f)), C_SHELF_DARK);
    drawCube(parent * place(Vec3(2.88f, 0.70f, 2.60f), Vec3(0.30f, 0.10f, 0.14f)), C_CABINET);
    drawCube(parent * place(Vec3(3.16f, 0.68f, 2.52f), Vec3(0.16f, 0.06f, 0.30f)), C_STEEL);
}


// ============================================================================
//  SECTION 18 - THE REPEATED EQUIPMENT LIBRARY
//
//  Everything in this section is something a real service garage holds in
//  QUANTITY. There are no one-off hero props: if an object type would appear
//  fewer than three times, it does not belong here.
//
//  Every entry is ONE function, written once and called many times with a
//  different parent matrix. That is the same principle as the car - one model,
//  many placements - applied to the twenty-odd kinds of equipment that make a
//  workshop look inhabited rather than staged.
//
//  Functions that get repeated take an `index`. The index feeds the hash in
//  SECTION 1b, which gives that instance its own slight colour, angle and
//  scale. Without it, twenty drums are twenty clones; with it, they are a
//  stack of drums.
//
//  Every object larger than about half a metre carries at least three features
//  under 5 cm - bolt heads, seams, handles, feet, labels. That detail tier is
//  what the eye resolves when it gets close, and without it even correctly
//  proportioned geometry reads as a diagram.
// ============================================================================

// ---------------------------------------------------------------------------
//  2. OIL DRUM (55 gallon)  -  12 to 16 of them, in rows and on pallets
//     Origin on the floor. Ribs, rolled rims and a bung are what make a
//     cylinder read as a drum rather than as a tube.
// ---------------------------------------------------------------------------
void Scene::drawOilDrum(const Mat4& parent, int index)
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

    // Detail tier: two bung plugs and a paper label.
    drawCylinder(parent * place(Vec3(R * 0.46f, H + 0.012f, 0.0f), Vec3(0.085f, 0.030f, 0.085f)), C_GALV);
    drawCylinder(parent * place(Vec3(-R * 0.40f, H + 0.010f, R * 0.22f), Vec3(0.050f, 0.024f, 0.050f)), C_GALV);
    drawCube(parent * place(Vec3(0.0f, H * 0.56f, R * 0.99f), Vec3(0.34f, 0.24f, 0.012f)), Vec3(0.62f, 0.60f, 0.555f));

    // Where it has sat on a wet floor, the bottom 5 cm is always darker.
    drawCylS(parent * place(Vec3(0.0f, 0.028f, 0.0f), Vec3(R * 1.015f, 0.055f, R * 1.015f)), body * 0.52f);
}


// ---------------------------------------------------------------------------
//  4. ROLLER TOOL CHEST  -  5 to 6 along the walls and beside the bays
// ---------------------------------------------------------------------------
void Scene::drawToolChest(const Mat4& parent, int index)
{
    const float W = 1.05f, H = 0.95f, D = 0.52f;
    Vec3 body = tintVary(C_CABINET, index, 0.05f);

    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), W + 0.18f, D + 0.18f);

    // Four casters, then the body sits on them.
    for (int i = 0; i < 4; ++i)
    {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        drawCylinder(parent * translate(sx * (W * 0.40f), 0.055f, sz * (D * 0.33f)) * rotateZ(90.0f)
            * scale(0.105f, 0.045f, 0.105f), C_RUBBER);
        drawCube(parent * place(Vec3(sx * (W * 0.40f), 0.125f, sz * (D * 0.33f)), Vec3(0.070f, 0.075f, 0.070f)), C_GALV);
    }

    // Carcass, with the chamfer treatment because the camera gets close to it.
    drawPanel(parent, Vec3(0.0f, 0.165f + H * 0.5f, 0.0f), Vec3(W, H, D), body);

    // Work top, overhanging slightly as a real one does.
    drawCube(parent * place(Vec3(0.0f, 0.165f + H + 0.022f, 0.0f), Vec3(W + 0.055f, 0.045f, D + 0.045f)), C_STEEL);

    // Six drawer fronts with handles and the seam between each one.
    for (int i = 0; i < 6; ++i)
    {
        float y = 0.255f + static_cast<float>(i) * 0.132f;
        drawCube(parent * place(Vec3(0.0f, y, D * 0.5f + 0.010f), Vec3(W * 0.93f, 0.112f, 0.022f)), body * 1.12f);
        drawCube(parent * place(Vec3(0.0f, y + 0.066f, D * 0.5f + 0.012f), Vec3(W * 0.93f, 0.008f, 0.024f)), body * 0.45f);
        drawCylinder(parent * translate(0.0f, y - 0.018f, D * 0.5f + 0.032f) * rotateZ(90.0f)
            * scale(0.020f, W * 0.45f, 0.020f), C_CHROME);
        // Small white label on the drawer face.
        drawCube(parent * place(Vec3(-W * 0.34f, y + 0.020f, D * 0.5f + 0.024f), Vec3(0.150f, 0.040f, 0.008f)),
            Vec3(0.640f, 0.635f, 0.615f));
    }
}


// ---------------------------------------------------------------------------
//  5. PALLET RACKING BAY  -  4 bays along the back wall, full height
//     `levels` decks per bay. Origin on the floor at the bay centre.
// ---------------------------------------------------------------------------
void Scene::drawRackingBay(const Mat4& parent, int levels, int index)
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
                Vec3(a, 0.0f, 0.0f)), C_LIFT_BLUE_D);
        }

        // Base plate + four floor bolts. Detail tier.
        drawCube(parent * place(Vec3(sx * W * 0.5f, 0.014f, 0.0f), Vec3(0.230f, 0.028f, D * 0.92f)), C_GALV);
        for (int b = 0; b < 4; ++b)
            drawCylinder(parent * place(Vec3(sx * W * 0.5f + ((b & 1) ? 0.070f : -0.070f), 0.036f,
                ((b & 2) ? 0.36f : -0.36f) * D), Vec3(0.034f, 0.026f, 0.034f)), C_ROBOT_DARK);
    }

    // Beams and deck boards for each level.
    for (int L = 0; L < levels; ++L)
    {
        float y = 0.45f + static_cast<float>(L) * LEVEL_H;

        drawCube(parent * place(Vec3(0.0f, y, -D * 0.38f), Vec3(W, 0.105f, 0.060f)), C_JOINT);
        drawCube(parent * place(Vec3(0.0f, y, D * 0.38f), Vec3(W, 0.105f, 0.060f)), C_JOINT);

        for (int b = 0; b < 3; ++b)
            drawCube(parent * place(Vec3(0.0f, y + 0.062f, (static_cast<float>(b) - 1.0f) * D * 0.31f),
                Vec3(W - 0.14f, 0.028f, D * 0.28f)), tintVary(C_WOOD, index * 9 + L * 3 + b, 0.06f));
    }
}


// ---------------------------------------------------------------------------
//  8. FIRE EXTINGUISHER  -  5 of them, wall mounted at intervals
//     Origin at the floor directly below; the unit hangs at chest height.
// ---------------------------------------------------------------------------
void Scene::drawFireExtinguisher(const Mat4& parent)
{
    const float Y = 1.15f;                     // height of the bracket

    drawCube(parent * place(Vec3(0.0f, Y, -0.085f), Vec3(0.200f, 0.300f, 0.030f)), C_GALV);       // wall bracket
    drawCube(parent * place(Vec3(0.0f, Y, -0.050f), Vec3(0.145f, 0.060f, 0.045f)), C_ROBOT_DARK); // strap

    drawCylS(parent * place(Vec3(0.0f, Y, 0.0f), Vec3(0.185f, 0.440f, 0.185f)), C_EXTING);       // body
    drawSphere(parent * place(Vec3(0.0f, Y + 0.215f, 0.0f), Vec3(0.185f, 0.115f, 0.185f)), C_EXTING); // rounded shoulder
    drawSphere(parent * place(Vec3(0.0f, Y - 0.215f, 0.0f), Vec3(0.185f, 0.085f, 0.185f)), C_EXTING * 0.90f); // rounded base
    drawCylS(parent * place(Vec3(0.0f, Y - 0.250f, 0.0f), Vec3(0.185f, 0.030f, 0.185f)), C_EXTING * 0.80f);
    drawCylS(parent * place(Vec3(0.0f, Y + 0.268f, 0.0f), Vec3(0.070f, 0.075f, 0.070f)), C_GALV);       // neck

    drawCube(parent * place(Vec3(0.0f, Y + 0.322f, 0.0f), Vec3(0.145f, 0.048f, 0.060f)), C_ROBOT_DARK); // handle
    drawCylinder(parent * place(Vec3(0.075f, Y + 0.300f, 0.0f), Vec3(0.055f, 0.022f, 0.055f)), C_CHROME); // gauge
    // Hose looped down the side: four short cylinders.
    for (int i = 0; i < 4; ++i)
        drawCylinder(parent * translate(0.115f, Y + 0.20f - static_cast<float>(i) * 0.105f, 0.0f)
            * rotateZ(static_cast<float>(i) * 14.0f) * scale(0.032f, 0.120f, 0.032f), C_ROBOT_DARK);
    drawCube(parent * place(Vec3(0.0f, Y + 0.060f, 0.098f), Vec3(0.130f, 0.170f, 0.010f)), Vec3(0.640f, 0.630f, 0.600f)); // label
}


// ---------------------------------------------------------------------------
//  11. CEILING LIGHT FIXTURE  -  12 to 16, in rows that are deliberately NOT
//      on the beam grid. Two emissive tubes in a housing on two chain drops.
// ---------------------------------------------------------------------------
void Scene::drawCeilingFixture(const Mat4& parent, float dropLen)
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
        drawCylS(parent * translate(0.0f, -0.062f, sz) * rotateZ(90.0f) * scale(0.062f, 1.50f, 0.062f), C_TUBE, 1.0f);
        // End caps.
        drawCube(parent * place(Vec3(-0.775f, -0.062f, sz), Vec3(0.045f, 0.080f, 0.080f)), C_LAMP_HOUSING);
        drawCube(parent * place(Vec3(0.775f, -0.062f, sz), Vec3(0.045f, 0.080f, 0.080f)), C_LAMP_HOUSING);
    }
}


// ---------------------------------------------------------------------------
//  13. DRAIN PAN STACK  -  6 pans, stacked with a small offset each
// ---------------------------------------------------------------------------
void Scene::drawDrainPanStack(const Mat4& parent, int count, int index)
{
    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), 0.95f, 0.95f);

    for (int i = 0; i < count; ++i)
    {
        int seed = index * 17 + i;
        Mat4 p = parent * translate(jitter(seed, 0.030f), 0.055f + static_cast<float>(i) * 0.062f, jitter(seed + 1, 0.030f))
            * rotateY(jitter(seed + 2, 18.0f));
        drawCylS(p * scale(0.780f, 0.085f, 0.780f), tintVary(C_PLASTIC, seed, 0.06f));
        drawCylS(p * translate(0.0f, 0.020f, 0.0f) * scale(0.690f, 0.070f, 0.690f), C_ROBOT_DARK);
        // Pour spout.
        drawCube(p * place(Vec3(0.365f, 0.030f, 0.0f), Vec3(0.110f, 0.055f, 0.140f)), tintVary(C_PLASTIC, seed, 0.06f));
    }
}

// ---------------------------------------------------------------------------
//  16. WORK STOOL  -  4, beside the benches and bays
// ---------------------------------------------------------------------------
void Scene::drawWorkStool(const Mat4& parent, int index)
{
    Mat4 s = parent * rotateY(jitter(index, 45.0f));

    drawContactPatch(s, Vec3(0.0f, 0.0f, 0.0f), 0.60f, 0.60f);

    drawCylS(s * place(Vec3(0.0f, 0.480f, 0.0f), Vec3(0.380f, 0.075f, 0.380f)), C_RUBBER * 1.25f);  // seat
    drawCylS(s * place(Vec3(0.0f, 0.430f, 0.0f), Vec3(0.180f, 0.040f, 0.180f)), C_GALV);            // seat plate
    drawCylS(s * place(Vec3(0.0f, 0.235f, 0.0f), Vec3(0.070f, 0.400f, 0.070f)), C_CHROME);          // gas post

    // Five leg spokes at 72 degrees, each ending in a caster.
    for (int i = 0; i < 5; ++i)
    {
        Mat4 leg = s * rotateY(static_cast<float>(i) * 72.0f);
        drawCube(leg * place(Vec3(0.145f, 0.055f, 0.0f), Vec3(0.270f, 0.040f, 0.048f)), C_ROBOT_DARK);
        drawCylinder(leg * translate(0.270f, 0.030f, 0.0f) * rotateZ(90.0f) * scale(0.060f, 0.028f, 0.060f), C_RUBBER);
    }
}


// ---------------------------------------------------------------------------
//  18. CARDBOARD BOX STACK  -  4 stacks of 3, in corners and on the racking
// ---------------------------------------------------------------------------
void Scene::drawBoxStack(const Mat4& parent, int count, int index)
{
    float y = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        int   seed = index * 23 + i;
        float w = 0.520f + jitter(seed, 0.090f);
        float h = 0.330f + jitter(seed + 1, 0.060f);
        float d = 0.420f + jitter(seed + 2, 0.070f);

        Mat4 b = parent * translate(jitter(seed + 3, 0.075f), y + h * 0.5f, jitter(seed + 4, 0.075f))
            * rotateY(jitter(seed + 5, 14.0f));

        drawCube(b * scale(w, h, d), tintVary(C_CARDBOARD, seed, 0.07f));
        // Packing tape down the seam, and a shipping label.
        drawCube(b * translate(0.0f, h * 0.5f + 0.004f, 0.0f) * scale(0.070f, 0.008f, d * 1.01f), Vec3(0.560f, 0.520f, 0.430f));
        drawCube(b * translate(0.0f, 0.0f, d * 0.5f + 0.004f) * scale(w * 0.42f, h * 0.34f, 0.006f), Vec3(0.650f, 0.640f, 0.615f));
        y += h;
    }
}


// ---------------------------------------------------------------------------
//  20. WORKBENCH MODULE  -  3 along the walls, each with a pegboard above
// ---------------------------------------------------------------------------
void Scene::drawWorkbench(const Mat4& parent, int index)
{
    const float W = 2.35f, D = 0.78f, TOP_Y = 0.90f;

    drawContactPatch(parent, Vec3(0.0f, 0.0f, 0.0f), W + 0.20f, D + 0.20f);

    // Four box legs with levelling feet - detail tier.
    for (int i = 0; i < 4; ++i)
    {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        drawCube(parent * place(Vec3(sx * (W * 0.45f), TOP_Y * 0.5f + 0.030f, sz * (D * 0.35f)),
            Vec3(0.080f, TOP_Y - 0.030f, 0.080f)), C_SHELF_DARK);
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
                -D * 0.5f + 0.052f), Vec3(0.022f, 0.022f, 0.006f)), C_HAZARD_K);

    // 16 hanging tools: alternating thin cylinders (spanners, screwdrivers)
    // and small boxes (pliers, sockets), at loosely varying heights.
    for (int i = 0; i < 16; ++i)
    {
        int   seed = index * 41 + i;
        float x = (static_cast<float>(i) - 7.5f) * 0.142f;
        float y = TOP_Y + 0.62f + jitter(seed, 0.30f) + ((i & 1) ? 0.26f : 0.0f);
        float z = -D * 0.5f + 0.080f;

        if (i % 3 == 0)
            drawCube(parent * place(Vec3(x, y, z), Vec3(0.060f, 0.185f, 0.030f),
                Vec3(0.0f, 0.0f, jitter(seed + 1, 8.0f))), C_ROBOT_DARK);
        else
            drawCylS(parent * translate(x, y, z) * rotateZ(jitter(seed + 2, 7.0f))
                * scale(0.030f, 0.255f, 0.030f), (i & 1) ? C_CHROME : C_STEEL);
    }
}


// ---------------------------------------------------------------------------
//  21. STRUCTURAL SUPPORT COLUMN  -  8 of them on the structural grid
//      Base plate, four anchor bolts, a hazard band and a neon strip.
// ---------------------------------------------------------------------------
void Scene::drawSupportColumn(const Mat4& parent, float height, float neonSide)
{
    drawCube(parent * place(Vec3(0.0f, height * 0.5f, 0.0f), Vec3(0.70f, height, 0.90f)), C_COLUMN);

    // Cast base plate and its four anchor bolts.
    drawCube(parent * place(Vec3(0.0f, 0.030f, 0.0f), Vec3(0.98f, 0.060f, 1.18f)), C_GALV);
    drawCube(parent * place(Vec3(0.0f, 0.062f, 0.0f), Vec3(0.98f, 0.006f, 1.18f)), C_STEEL * 0.85f);
    for (int i = 0; i < 4; ++i)
        drawCylinder(parent * place(Vec3((i & 1) ? 0.400f : -0.400f, 0.078f, (i & 2) ? 0.480f : -0.480f),
            Vec3(0.070f, 0.040f, 0.070f)), C_ROBOT_DARK);

    // Hazard band around the bottom: alternating yellow and black blocks.
    for (int i = 0; i < 6; ++i)
    {
        Vec3 c = (i & 1) ? C_HAZARD_Y : C_HAZARD_K;
        drawCube(parent * place(Vec3(0.0f, 0.34f + static_cast<float>(i) * 0.075f, 0.455f),
            Vec3(0.72f, 0.078f, 0.014f), Vec3(38.0f, 0.0f, 0.0f)), c);
    }

    // Cap plate where the column meets the beam.
    drawCube(parent * place(Vec3(0.0f, height - 0.040f, 0.0f), Vec3(0.86f, 0.080f, 1.06f)), C_GALV);
    drawCube(parent * place(Vec3(0.0f, height - 0.006f, 0.0f), Vec3(0.86f, 0.006f, 1.06f)), C_STEEL * 0.85f);

    // The neon strip down the aisle-facing face.
    drawCube(parent * place(Vec3(neonSide * 0.365f, height * 0.52f, 0.0f), Vec3(0.035f, height * 0.72f, 0.155f)), C_NEON, 1.0f);
}


// ---------------------------------------------------------------------------
//  23. SPRINKLER HEAD  -  9 to 12 on the ceiling grid
// ---------------------------------------------------------------------------
void Scene::drawSprinkler(const Mat4& parent)
{
    drawCylinder(parent * place(Vec3(0.0f, -0.070f, 0.0f), Vec3(0.055f, 0.140f, 0.055f)), C_CABINET * 0.9f);
    drawCylinder(parent * place(Vec3(0.0f, -0.155f, 0.0f), Vec3(0.090f, 0.035f, 0.090f)), C_CHROME); // deflector
    drawCube(parent * place(Vec3(0.0f, -0.135f, 0.0f), Vec3(0.022f, 0.050f, 0.022f)), C_TAILLIGHT);  // glass bulb
}


// ---------------------------------------------------------------------------
//  24. WHEEL CHOCK  -  4 pairs, at the wheels of the parked cars
//      A wedge faked from two rotated boxes.
// ---------------------------------------------------------------------------
void Scene::drawWheelChock(const Mat4& parent, int index)
{
    Mat4 c = parent * rotateY(jitter(index, 10.0f));
    drawCube(c * place(Vec3(0.0f, 0.055f, 0.0f), Vec3(0.230f, 0.110f, 0.300f)), C_HAZARD_Y);
    drawCube(c * place(Vec3(0.0f, 0.130f, -0.055f), Vec3(0.230f, 0.180f, 0.210f), Vec3(31.0f, 0.0f, 0.0f)), C_HAZARD_Y);
    drawCube(c * place(Vec3(0.0f, 0.062f, 0.130f), Vec3(0.235f, 0.050f, 0.050f)), C_HAZARD_K);
}


// ---------------------------------------------------------------------------
//  25. CONDUIT RUN  -  4 runs along the walls, with a clamp every 1.5 m
//      Runs along the parent's local Z.
// ---------------------------------------------------------------------------
void Scene::drawConduitRun(const Mat4& parent, float length)
{
    // Two parallel pipes: one electrical conduit, one compressed-air line.
    drawCylS(parent * translate(0.0f, 0.0f, 0.0f) * rotateX(90.0f) * scale(0.075f, length, 0.075f), C_GALV);
    drawCylS(parent * translate(0.0f, -0.165f, 0.0f) * rotateX(90.0f) * scale(0.055f, length, 0.055f), C_STEEL);

    int clamps = static_cast<int>(length / 1.5f);
    for (int i = 0; i <= clamps; ++i)
    {
        float z = -length * 0.5f + static_cast<float>(i) * 1.5f;
        drawCube(parent * place(Vec3(-0.055f, -0.082f, z), Vec3(0.045f, 0.290f, 0.060f)), C_GALV);  // backing strap
        drawCube(parent * place(Vec3(0.0f, 0.0f, z), Vec3(0.115f, 0.115f, 0.045f)), C_GALV);  // clamp
        drawCube(parent * place(Vec3(0.0f, -0.165f, z), Vec3(0.090f, 0.090f, 0.040f)), C_GALV);
    }
    // A junction box a third of the way along.
    drawCube(parent * place(Vec3(-0.030f, 0.0f, -length * 0.18f), Vec3(0.150f, 0.210f, 0.170f)), C_GALV);
}


// ---------------------------------------------------------------------------
//  26. PIPE RUN  -  overhead services, 3 runs across the ceiling
// ---------------------------------------------------------------------------
void Scene::drawPipeRun(const Mat4& parent, float length, int index)
{
    drawCylS(parent * rotateX(90.0f) * scale(0.115f, length, 0.115f), tintVary(C_GALV, index, 0.05f));
    drawCylS(parent * translate(0.0f, -0.195f, 0.0f) * rotateX(90.0f) * scale(0.070f, length, 0.070f), C_CABINET * 0.85f);

    int hangers = static_cast<int>(length / 2.2f);
    for (int i = 0; i <= hangers; ++i)
    {
        float z = -length * 0.5f + static_cast<float>(i) * 2.2f;
        drawCube(parent * place(Vec3(0.0f, 0.230f, z), Vec3(0.035f, 0.400f, 0.035f)), C_ROBOT_DARK);
        drawCube(parent * place(Vec3(0.0f, 0.020f, z), Vec3(0.170f, 0.045f, 0.045f)), C_GALV);
        drawCube(parent * place(Vec3(0.0f, -0.195f, z), Vec3(0.130f, 0.040f, 0.040f)), C_GALV);
    }
}


// ============================================================================
//  SECTION 17 - THE COMPLETE SCENE
//
//  This is the only function Main.cpp calls. Read it top to bottom and you
//  know exactly what is in the building and where.
//
//  THREE PRINCIPLES RUN THROUGH THE WHOLE LAYOUT
//  ---------------------------------------------
//  1. ONE MODEL, MANY PLACEMENTS. Nine cars, three lifts, eight tyre stacks,
//     sixteen drums, fourteen light fixtures - and one function each. Nothing
//     below duplicates geometry; it only duplicates transforms.
//
//  2. NOTHING IS ON A PERFECT GRID. Every repeated item gets messy(index),
//     which hashes its index into a small offset, yaw and scale. Two of the
//     parked cars are deliberately crooked in their bays. The ceiling fixture
//     rows are deliberately off the beam spacing. Perfect alignment is the
//     single loudest "this is computer generated" signal there is.
//
//  3. THE CENTRAL AISLE STAYS CLEAR. Everything is pushed to the walls or
//     hung from the ceiling, so the car can still drive in, the lift still
//     works, and the daylight coming through the open door still has a clean
//     run across the floor.
// ============================================================================

void Scene::drawScene(float doorOffset,
    float liftHeight,
    float robotBaseYaw,
    float joint1Angle,
    float joint2Angle,
    float wristPitch,
    float wristRoll,
    int robotTool,
    float fanAngle)
{
    // The world root. Everything is drawn relative to this.
    Mat4 world = mat4Identity();

    // ---- 1. The building and what is outside it ----------------------------
    drawGarage(world);
    drawExterior(world);
    drawGarageDoor(world, doorOffset);

    // ---- 2. Eight structural columns, four down each side --------------------
    // The neon strip has to face the aisle, so the side tells the function
    // which way to put it: -1 on the right wall, +1 on the left.
    const float COL_Z[4] = { -9.5f, -4.0f, 1.5f, 7.0f };
    for (int i = 0; i < 4; ++i)
    {
        drawSupportColumn(world * translate(-13.30f, 0.0f, COL_Z[i]), GARAGE_H, 1.0f);
        drawSupportColumn(world * translate(13.30f, 0.0f, COL_Z[i]), GARAGE_H, -1.0f);
    }

    // ---- 3. Neon sign on the back wall ---------------------------------------
    drawNeonSign(world * translate(0.0f, 5.15f, -12.88f));

    // ---- 4. Three service bays -----------------------------------------------
    // The SAME lift function three times. The main one carries the car; the
    // other two are working bays, one part-raised and one down.
    drawHydraulicLift(world, liftHeight);
    drawHydraulicLift(world * translate(-8.60f, 0.0f, 5.20f), 2.40f);
    drawHydraulicLift(world * translate(-8.60f, 0.0f, -4.60f), 0.00f);

    // ---- 5. The car under service, standing on the lift runways --------------
    // liftTopY() returns the surface the wheels stand on, so when the lift is
    // animated later the car rises with it and this line never changes.
    drawCar(world * translate(0.0f, liftTopY(liftHeight), LIFT_Z), C_CAR_ORANGE);

    // ---- 6. Robotic inspection arm ---------------------------------------------
    drawRobotArm(world * translate(ARM_X, 0.0f, ARM_Z), robotBaseYaw, joint1Angle, joint2Angle,
        wristPitch, wristRoll, robotTool);
    drawRobotToolStation(world * translate(ARM_X - 2.75f, 0.0f, ARM_Z + 0.35f), robotTool);

    // ---- 7. Two parked cars inside, both deliberately untidy --------------------
    // A car parked by a human is never square in its bay. messy() gives each
    // one a few centimetres of offset and a few degrees of yaw.
    drawParkingBay(world * translate(PARK_X, 0.0f, PARK_Z_A));
    drawParkingBay(world * translate(PARK_X, 0.0f, PARK_Z_B));

    drawCar(world * translate(PARK_X, 0.0f, PARK_Z_A) * messy(101, 0.16f, 5.0f, 0.0f) * rotateY(90.0f), C_CAR_WHITE);
    drawCar(world * translate(PARK_X, 0.0f, PARK_Z_B) * messy(207, 0.16f, 5.0f, 0.0f) * rotateY(90.0f), C_CAR_GUNMETAL);

    // Wheel chocks at their wheels - 4 pairs.
    for (int i = 0; i < 2; ++i)
    {
        float z = (i == 0) ? PARK_Z_A : PARK_Z_B;
        drawWheelChock(world * translate(PARK_X - 1.35f, 0.0f, z - 0.95f) * rotateY(90.0f), i * 2);
        drawWheelChock(world * translate(PARK_X - 1.35f, 0.0f, z + 0.95f) * rotateY(90.0f), i * 2 + 1);
        drawWheelChock(world * translate(PARK_X + 1.60f, 0.0f, z - 0.95f) * rotateY(-90.0f), i * 2 + 4);
        drawWheelChock(world * translate(PARK_X + 1.60f, 0.0f, z + 0.95f) * rotateY(-90.0f), i * 2 + 5);
    }

    // ---- 8. Four cars parked outside on the apron -------------------------------
    // The same model again. They cost nothing and they do two jobs: they give
    // the exterior a sense of scale, and they give the eye something to focus
    // on through the open door instead of a bright empty rectangle.
    const float AP = WALL_Z + WALL_T + 5.60f;
    drawCar(world * translate(-9.80f, 0.0f, AP) * messy(301, 0.22f, 7.0f, 0.0f) * rotateY(180.0f), C_CAR_SILVER);
    drawCar(world * translate(-6.10f, 0.0f, AP + 0.35f) * messy(302, 0.22f, 7.0f, 0.0f) * rotateY(180.0f), C_CAR_BLUE);
    drawCar(world * translate(7.90f, 0.0f, AP - 0.30f) * messy(303, 0.22f, 7.0f, 0.0f) * rotateY(180.0f), C_CAR_RED);
    drawCar(world * translate(11.40f, 0.0f, AP) * messy(304, 0.22f, 7.0f, 0.0f) * rotateY(180.0f), C_CAR_SAND);

    // ---- 9. Ceiling: fixtures, fans, reels, extraction, sprinklers, pipes ---------
    // 14 light fixtures in two rows. The beams are at 3.5 m spacing, so the
    // fixtures are put at 3.1 m spacing and offset half a metre - close enough
    // to look deliberate, off enough that nothing lines up.
    for (int i = 0; i < 7; ++i)
    {
        float z = -10.1f + static_cast<float>(i) * 3.10f;
        drawCeilingFixture(world * translate(-6.40f + jitter(i * 3, 0.10f), CEIL_Y - 0.62f, z), 0.62f);
        drawCeilingFixture(world * translate(6.40f + jitter(i * 3 + 1, 0.10f), CEIL_Y - 0.62f, z + 0.55f), 0.62f);
    }

    // Three overhead pipe runs.
    drawPipeRun(world * translate(-11.60f, CEIL_Y - 0.55f, 0.0f), 22.0f, 1);
    drawPipeRun(world * translate(11.60f, CEIL_Y - 0.55f, 0.0f), 22.0f, 2);
    drawPipeRun(world * translate(0.0f, CEIL_Y - 0.48f, -11.60f) * rotateY(90.0f), 24.0f, 3);

    // ---- 10. Back wall: four racking bays and the benches ------------------------
    drawRackingBay(world * translate(-11.00f, 0.0f, -12.25f), 3, 1);
    drawRackingBay(world * translate(-7.70f, 0.0f, -12.25f), 3, 2);
    drawRackingBay(world * translate(7.70f, 0.0f, -12.25f), 3, 3);
    drawRackingBay(world * translate(11.00f, 0.0f, -12.25f), 2, 4);

    // Boxes and drums stored up on the other two racking bays.
    drawBoxStack(world * translate(7.10f, 0.525f, -12.25f), 2, 11);
    drawBoxStack(world * translate(8.35f, 0.525f, -12.25f), 3, 12);
    drawBoxStack(world * translate(7.70f, 1.845f, -12.25f), 2, 13);
    for (int i = 0; i < 3; ++i)
        drawOilDrum(world * translate(10.20f + static_cast<float>(i) * 0.78f, 0.525f, -12.25f), 40 + i);

    drawWorkbench(world * translate(-3.00f, 0.0f, -12.10f), 1);
    drawWorkbench(world * translate(2.70f, 0.0f, -12.10f), 2);
    drawWorkbench(world * translate(12.72f, 0.0f, -1.60f) * rotateY(-90.0f), 3);

    // ---- 13. Oil drums: twelve --------------------------------------------------
    // Three in a row against the left wall, two by the right wall, three stored
    // up on the racking (drawn in step 10), and a loose cluster in the back
    // corner where somebody has clearly just rolled them off a pallet.
    for (int i = 0; i < 3; ++i)
        drawOilDrum(world * translate(-12.85f, 0.0f, -8.00f + static_cast<float>(i) * 1.30f)
            * messy(i + 50, 0.08f, 30.0f, 0.03f), i + 50);
    drawOilDrum(world * translate(12.85f, 0.0f, 11.90f) * messy(60, 0.08f, 30.0f, 0.03f), 60);
    drawOilDrum(world * translate(12.85f, 0.0f, 12.60f) * messy(61, 0.06f, 30.0f, 0.03f), 61);

    const float DRUM_XZ[4][2] = { { -8.80f, -10.70f }, { -7.50f, -11.00f },
                                  { -8.50f,  -9.40f }, { -7.10f,  -9.70f } };
    for (int i = 0; i < 4; ++i)
        drawOilDrum(world * translate(DRUM_XZ[i][0], 0.0f, DRUM_XZ[i][1])
            * messy(i + 70, 0.05f, 40.0f, 0.03f), i + 70);

    // ---- 17. Fire extinguishers: five, wall mounted ---------------------------------
    // Four of them are bracketed to the structural columns, which is where a
    // real workshop puts them, and one is on the back wall.
    drawFireExtinguisher(world * translate(-12.78f, 0.0f, -4.00f) * rotateY(90.0f));
    drawFireExtinguisher(world * translate(-12.78f, 0.0f, 1.50f) * rotateY(90.0f));
    drawFireExtinguisher(world * translate(12.78f, 0.0f, -9.50f) * rotateY(-90.0f));
    drawFireExtinguisher(world * translate(12.78f, 0.0f, 7.00f) * rotateY(-90.0f));
    drawFireExtinguisher(world * translate(-1.40f, 0.0f, -12.72f));

    // ---- 18. Conduit runs: four, along the walls at about 3 m ------------------------
    drawConduitRun(world * translate(-13.80f, 3.05f, -3.00f), 18.0f);
    drawConduitRun(world * translate(13.80f, 3.05f, -3.00f), 18.0f);
    drawConduitRun(world * translate(-6.00f, 3.25f, -12.85f) * rotateY(90.0f), 13.0f);
    drawConduitRun(world * translate(6.00f, 3.25f, -12.85f) * rotateY(90.0f), 13.0f);

    drawWorkStool(world * translate(-3.40f, 0.0f, -10.90f), 1);
    drawWorkStool(world * translate(3.60f, 0.0f, -10.80f), 2);
    drawWorkStool(world * translate(11.70f, 0.0f, -1.60f), 3);
    drawWorkStool(world * translate(-5.60f, 0.0f, 3.40f), 4);

    drawBoxStack(world * translate(-12.20f, 0.0f, 12.10f), 3, 1);
    drawBoxStack(world * translate(-11.10f, 0.0f, 11.60f), 3, 2);
    drawBoxStack(world * translate(5.50f, 0.0f, 12.30f), 3, 3);
    drawBoxStack(world * translate(6.60f, 0.0f, 11.80f), 3, 4);

    // ---- 22. Two shelving units and the tool cabinet ------------------------------------
    drawShelf(world * translate(-9.20f, 0.0f, 11.90f));
    drawShelf(world * translate(-6.30f, 0.0f, 11.90f));
    drawToolCabinet(world * translate(-5.50f, 0.0f, -9.30f) * rotateY(6.0f));

    // ---- 23. Oil staining on the floor around the lift -----------------------------------
    // Flat, very dark, slightly irregular patches. Cheap, and it does more for
    // "this floor has been worked on" than any amount of extra hardware.
    for (int i = 0; i < 9; ++i)
    {
        float x = jitter(i * 4 + 1, 2.60f);
        float z = LIFT_Z + jitter(i * 4 + 2, 3.10f);
        float w = 0.55f + hash01(i * 4 + 3) * 1.30f;
        drawCube(world * place(Vec3(x, 0.014f, z), Vec3(w, 0.014f, w * 0.78f),
            Vec3(0.0f, jitter(i * 4, 45.0f), 0.0f)), C_FLOOR_STAIN);
    }
}

