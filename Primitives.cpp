// The unit cube and the unit cylinder
#include <glad/glad.h>
#include <vector>
#include <cmath>

#include "Primitives.h"
#include "Math3D.h"


// ============================================================================
//                         THE UNIT CUBE  (the heart of the project)
// ============================================================================
//
//  WHY DO THE COORDINATES GO FROM -0.5 TO +0.5 ?
//  ---------------------------------------------
//  Because that makes the cube exactly 1 unit wide, 1 unit tall and 1 unit
//  deep, and it puts the CENTRE of the cube at its own local origin (0,0,0).
//
//  That choice makes every later transformation easy to reason about:
//
//      * scale(4.2f, 0.5f, 1.9f)  ->  the box becomes 4.2 long, 0.5 tall,
//                                     1.9 wide, still centred on its origin.
//      * translate(0, 1.5f, 0)    ->  the box's CENTRE moves to y = 1.5.
//      * rotateX(25)              ->  the box tips about its own centre,
//                                     not about some far-away corner.
//
//  If the cube ran from 0 to 1 instead, scaling would also move the object,
//  and every position in the scene would need a correction term.
//
//
//  WHY 24 VERTICES INSTEAD OF 8 ?
//  ------------------------------
//  A cube has only 8 corners, but each corner is shared by 3 faces that point
//  in 3 different directions. A vertex can only carry ONE normal, so a shared
//  corner would have to lie about which way its face points and the lighting
//  would look smeared and round.
//
//  So we store each face separately:  6 faces x 4 corners = 24 vertices.
//  Every vertex then carries the correct flat normal for its own face.
//
//
//  THE LAYOUT OF EACH LINE
//  -----------------------
//      x      y      z        nx    ny    nz
//      \-- position --/       \-- face normal --/
//
//  NOTE: the colour is NOT stored in the vertex data. It is sent to the shader
//  as a uniform (uObjectColor). That is deliberate: if the colour were baked
//  into the vertices we would need a separate vertex array for every colour,
//  which is exactly what we are trying to avoid. One cube, many colours.
// ============================================================================

static const float kCubeVertices[] =
{
    //  x      y      z        nx     ny     nz

    // ---- BACK face  (z = -0.5, points towards -Z) --------------------------
     0.5f, -0.5f, -0.5f,    0.0f,  0.0f, -1.0f,   //  0  bottom-left  (seen from behind)
    -0.5f, -0.5f, -0.5f,    0.0f,  0.0f, -1.0f,   //  1  bottom-right
    -0.5f,  0.5f, -0.5f,    0.0f,  0.0f, -1.0f,   //  2  top-right
     0.5f,  0.5f, -0.5f,    0.0f,  0.0f, -1.0f,   //  3  top-left

     // ---- FRONT face (z = +0.5, points towards +Z) --------------------------
     -0.5f, -0.5f,  0.5f,    0.0f,  0.0f,  1.0f,   //  4  bottom-left
      0.5f, -0.5f,  0.5f,    0.0f,  0.0f,  1.0f,   //  5  bottom-right
      0.5f,  0.5f,  0.5f,    0.0f,  0.0f,  1.0f,   //  6  top-right
     -0.5f,  0.5f,  0.5f,    0.0f,  0.0f,  1.0f,   //  7  top-left

     // ---- LEFT face  (x = -0.5, points towards -X) --------------------------
     -0.5f, -0.5f, -0.5f,   -1.0f,  0.0f,  0.0f,   //  8
     -0.5f, -0.5f,  0.5f,   -1.0f,  0.0f,  0.0f,   //  9
     -0.5f,  0.5f,  0.5f,   -1.0f,  0.0f,  0.0f,   // 10
     -0.5f,  0.5f, -0.5f,   -1.0f,  0.0f,  0.0f,   // 11

     // ---- RIGHT face (x = +0.5, points towards +X) --------------------------
      0.5f, -0.5f,  0.5f,    1.0f,  0.0f,  0.0f,   // 12
      0.5f, -0.5f, -0.5f,    1.0f,  0.0f,  0.0f,   // 13
      0.5f,  0.5f, -0.5f,    1.0f,  0.0f,  0.0f,   // 14
      0.5f,  0.5f,  0.5f,    1.0f,  0.0f,  0.0f,   // 15

      // ---- BOTTOM face (y = -0.5, points towards -Y) -------------------------
      -0.5f, -0.5f, -0.5f,    0.0f, -1.0f,  0.0f,   // 16
       0.5f, -0.5f, -0.5f,    0.0f, -1.0f,  0.0f,   // 17
       0.5f, -0.5f,  0.5f,    0.0f, -1.0f,  0.0f,   // 18
      -0.5f, -0.5f,  0.5f,    0.0f, -1.0f,  0.0f,   // 19

      // ---- TOP face   (y = +0.5, points towards +Y) --------------------------
      -0.5f,  0.5f,  0.5f,    0.0f,  1.0f,  0.0f,   // 20
       0.5f,  0.5f,  0.5f,    0.0f,  1.0f,  0.0f,   // 21
       0.5f,  0.5f, -0.5f,    0.0f,  1.0f,  0.0f,   // 22
      -0.5f,  0.5f, -0.5f,    0.0f,  1.0f,  0.0f,   // 23
};


// ============================================================================
//                              THE CUBE INDICES
// ============================================================================
//
//  WHY TRIANGLES?
//  --------------
//  Modern GPUs can only rasterise triangles. A triangle is the simplest shape
//  that is guaranteed to be FLAT - any three points always lie in one plane.
//  Four points might not, so squares are not safe to draw directly.
//
//  WHY TWO TRIANGLES PER FACE?
//  ---------------------------
//  A square face has 4 corners. Cutting it along one diagonal gives two
//  triangles that together cover exactly the same area:
//
//        3 --------- 2          3 --------- 2
//        |           |          | \         |
//        |   face    |   =>     |   \   B   |      A = (0, 1, 2)
//        |           |          |  A  \     |      B = (2, 3, 0)
//        0 --------- 1          0 ------\-- 1
//
//  6 faces x 2 triangles = 12 triangles for the whole cube.
//  12 triangles x 3 corners = 36 indices.
//
//  WHAT IS AN INDEX?
//  -----------------
//  An index is just the row number of a vertex in the array above. Instead of
//  repeating the same x/y/z/nx/ny/nz six times for a corner that two triangles
//  share, we list the vertex once and then refer to it by number. This is
//  called INDEXED DRAWING and it is why we use glDrawElements, not glDrawArrays.
//
//  WINDING: every triangle below is listed counter-clockwise when you look at
//  that face from OUTSIDE the cube. That is the standard OpenGL convention for
//  "this side is the front".
// ============================================================================

static const unsigned int kCubeIndices[] =
{
    // back face
     0,  1,  2,      2,  3,  0,

     // front face
      4,  5,  6,      6,  7,  4,

      // left face
       8,  9, 10,     10, 11,  8,

       // right face
        12, 13, 14,     14, 15, 12,

        // bottom face
         16, 17, 18,     18, 19, 16,

         // top face
          20, 21, 22,     22, 23, 20
};


// ============================================================================
//                            THE UNIT CYLINDER
// ============================================================================
//
//  A cube cannot honestly pretend to be a wheel, so we add one more primitive.
//  It is still built programmatically - there is no model file anywhere.
//
//  The cylinder is approximated by a ring of flat quads. With 28 segments the
//  silhouette already looks round at normal viewing distance.
//
//  Dimensions are chosen to match the cube's philosophy:
//      radius 0.5, height 1.0, centred at the local origin, axis along +Y.
//  So scale(0.7f, 0.25f, 0.7f) gives a disc 0.7 across and 0.25 thick.
//
//  Because the axis points along Y, a wheel (whose axle points along X) is
//  made by rotating the cylinder 90 degrees about the Z axis first.
// ============================================================================

static const int kCylinderSegments = 28;   // standard mesh - small hardware
static const int kSmoothSegments = 48;   // high-detail mesh - tyres, drums, pipes


// ---- OpenGL object handles -------------------------------------------------
static unsigned int gCubeVAO = 0, gCubeVBO = 0, gCubeEBO = 0;
static unsigned int gCylVAO = 0, gCylVBO = 0, gCylEBO = 0;
static int          gCylIndexCount = 0;
static int          gCylVertexCount = 0;
static unsigned int gCylSVAO = 0, gCylSVBO = 0, gCylSEBO = 0;
static int          gCylSIndexCount = 0;
static int          gCylSVertexCount = 0;
static unsigned int gSphereVAO = 0, gSphereVBO = 0, gSphereEBO = 0;
static int          gSphereIndexCount = 0;
static int          gSphereVertexCount = 0;
static int          gDrawCalls = 0;


// Helper: push one vertex (position + normal) into a float buffer.
static void pushVertex(std::vector<float>& out,
    float px, float py, float pz,
    float nx, float ny, float nz)
{
    out.push_back(px); out.push_back(py); out.push_back(pz);
    out.push_back(nx); out.push_back(ny); out.push_back(nz);
}


// ----------------------------------------------------------------------------
//  buildCylinder - generates the cylinder vertices/indices on the CPU.
// ----------------------------------------------------------------------------
static void buildCylinder(std::vector<float>& verts, std::vector<unsigned int>& idx, int segments)
{
    const int   N = segments;
    const float R = 0.5f;
    const float halfH = 0.5f;

    // ---- 1. The curved side --------------------------------------------
    // For every segment boundary we store a bottom and a top vertex.
    // We go to N (inclusive) so the last pair duplicates the first pair;
    // that avoids a visible seam.
    for (int i = 0; i <= N; ++i)
    {
        float a = (2.0f * PI_F * static_cast<float>(i)) / static_cast<float>(N);
        float cs = std::cos(a);
        float sn = std::sin(a);

        // A side normal points straight outwards from the axis.
        pushVertex(verts, R * cs, -halfH, R * sn, cs, 0.0f, sn);   // bottom
        pushVertex(verts, R * cs, halfH, R * sn, cs, 0.0f, sn);   // top
    }

    for (int i = 0; i < N; ++i)
    {
        unsigned int b0 = static_cast<unsigned int>(i * 2);
        unsigned int t0 = b0 + 1;
        unsigned int b1 = b0 + 2;
        unsigned int t1 = b0 + 3;

        idx.push_back(b0); idx.push_back(b1); idx.push_back(t1);
        idx.push_back(t1); idx.push_back(t0); idx.push_back(b0);
    }

    // ---- 2. The top cap (a triangle fan around a centre vertex) ---------
    unsigned int topCentre = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, 0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f);

    unsigned int topRingStart = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i <= N; ++i)
    {
        float a = (2.0f * PI_F * static_cast<float>(i)) / static_cast<float>(N);
        pushVertex(verts, R * std::cos(a), halfH, R * std::sin(a), 0.0f, 1.0f, 0.0f);
    }
    for (int i = 0; i < N; ++i)
    {
        idx.push_back(topCentre);
        idx.push_back(topRingStart + static_cast<unsigned int>(i) + 1);
        idx.push_back(topRingStart + static_cast<unsigned int>(i));
    }

    // ---- 3. The bottom cap ---------------------------------------------
    unsigned int botCentre = static_cast<unsigned int>(verts.size() / 6);
    pushVertex(verts, 0.0f, -halfH, 0.0f, 0.0f, -1.0f, 0.0f);

    unsigned int botRingStart = static_cast<unsigned int>(verts.size() / 6);
    for (int i = 0; i <= N; ++i)
    {
        float a = (2.0f * PI_F * static_cast<float>(i)) / static_cast<float>(N);
        pushVertex(verts, R * std::cos(a), -halfH, R * std::sin(a), 0.0f, -1.0f, 0.0f);
    }
    for (int i = 0; i < N; ++i)
    {
        idx.push_back(botCentre);
        idx.push_back(botRingStart + static_cast<unsigned int>(i));
        idx.push_back(botRingStart + static_cast<unsigned int>(i) + 1);
    }
}



// ----------------------------------------------------------------------------
//  buildSphere - UV sphere, radius 0.5, centered on origin.
// ----------------------------------------------------------------------------
static void buildSphere(std::vector<float>& verts, std::vector<unsigned int>& idx,
    int stacks = 24, int slices = 36)
{
    const float R = 0.5f;
    for (int y = 0; y <= stacks; ++y)
    {
        float v = static_cast<float>(y) / static_cast<float>(stacks);
        float phi = PI_F * v;                    // 0 .. pi
        float sy = std::cos(phi);
        float ring = std::sin(phi);

        for (int x = 0; x <= slices; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(slices);
            float theta = 2.0f * PI_F * u;       // 0 .. 2pi
            float nx = ring * std::cos(theta);
            float ny = sy;
            float nz = ring * std::sin(theta);
            pushVertex(verts, R * nx, R * ny, R * nz, nx, ny, nz);
        }
    }

    const int stride = slices + 1;
    for (int y = 0; y < stacks; ++y)
    {
        for (int x = 0; x < slices; ++x)
        {
            unsigned int a = static_cast<unsigned int>(y * stride + x);
            unsigned int b = static_cast<unsigned int>((y + 1) * stride + x);
            unsigned int c = a + 1;
            unsigned int d = b + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(c); idx.push_back(b); idx.push_back(d);
        }
    }
}

// ----------------------------------------------------------------------------
//  Primitives::init - upload both meshes to the GPU exactly once.
// ----------------------------------------------------------------------------
void Primitives::init()
{
    // ======================= CUBE ==========================================
    glGenVertexArrays(1, &gCubeVAO);
    glGenBuffers(1, &gCubeVBO);
    glGenBuffers(1, &gCubeEBO);

    glBindVertexArray(gCubeVAO);

    // VBO = the raw vertex data (positions + normals)
    glBindBuffer(GL_ARRAY_BUFFER, gCubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);

    // EBO = the index list that says which vertices form which triangles
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gCubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STATIC_DRAW);

    // Tell OpenGL how to read one vertex: 6 floats, position first then normal.
    // location 0 -> position (3 floats, offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // location 1 -> normal   (3 floats, offset 3 floats)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // ===================== CYLINDER ========================================
    std::vector<float>        cylVerts;
    std::vector<unsigned int> cylIdx;
    buildCylinder(cylVerts, cylIdx, kCylinderSegments);

    gCylVertexCount = static_cast<int>(cylVerts.size() / 6);
    gCylIndexCount = static_cast<int>(cylIdx.size());

    glGenVertexArrays(1, &gCylVAO);
    glGenBuffers(1, &gCylVBO);
    glGenBuffers(1, &gCylEBO);

    glBindVertexArray(gCylVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gCylVBO);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cylVerts.size() * sizeof(float)),
        cylVerts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gCylEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cylIdx.size() * sizeof(unsigned int)),
        cylIdx.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // ---- 3. The HIGH-DETAIL smooth cylinder ---------------------------------
    // Same generator, 48 segments instead of 28. The side normals already point
    // radially outwards (cs, 0, sn), which is what makes the lighting sweep
    // smoothly around the barrel instead of stepping from facet to facet; going
    // from 28 to 48 removes the last visible straight edges on the silhouette.
    // Used for tyres, oil drums, hoses, pipes and the robot arm segments -
    // anything the camera gets close to that must read as genuinely round.
    std::vector<float>        cylSVerts;
    std::vector<unsigned int> cylSIdx;
    buildCylinder(cylSVerts, cylSIdx, kSmoothSegments);

    gCylSVertexCount = static_cast<int>(cylSVerts.size() / 6);
    gCylSIndexCount = static_cast<int>(cylSIdx.size());

    glGenVertexArrays(1, &gCylSVAO);
    glGenBuffers(1, &gCylSVBO);
    glGenBuffers(1, &gCylSEBO);

    glBindVertexArray(gCylSVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gCylSVBO);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cylSVerts.size() * sizeof(float)),
        cylSVerts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gCylSEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cylSIdx.size() * sizeof(unsigned int)),
        cylSIdx.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // ====================== UV SPHERE ======================================
    std::vector<float> sphereVerts;
    std::vector<unsigned int> sphereIdx;
    buildSphere(sphereVerts, sphereIdx);
    gSphereVertexCount = static_cast<int>(sphereVerts.size() / 6);
    gSphereIndexCount = static_cast<int>(sphereIdx.size());

    glGenVertexArrays(1, &gSphereVAO);
    glGenBuffers(1, &gSphereVBO);
    glGenBuffers(1, &gSphereEBO);
    glBindVertexArray(gSphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gSphereVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sphereVerts.size() * sizeof(float)), sphereVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gSphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sphereIdx.size() * sizeof(unsigned int)), sphereIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}


void Primitives::destroy()
{
    if (gCubeEBO) glDeleteBuffers(1, &gCubeEBO);
    if (gCubeVBO) glDeleteBuffers(1, &gCubeVBO);
    if (gCubeVAO) glDeleteVertexArrays(1, &gCubeVAO);

    if (gCylEBO) glDeleteBuffers(1, &gCylEBO);
    if (gCylVBO) glDeleteBuffers(1, &gCylVBO);
    if (gCylVAO) glDeleteVertexArrays(1, &gCylVAO);

    if (gCylSEBO) glDeleteBuffers(1, &gCylSEBO);
    if (gCylSVBO) glDeleteBuffers(1, &gCylSVBO);
    if (gCylSVAO) glDeleteVertexArrays(1, &gCylSVAO);
    if (gSphereEBO) glDeleteBuffers(1, &gSphereEBO);
    if (gSphereVBO) glDeleteBuffers(1, &gSphereVBO);
    if (gSphereVAO) glDeleteVertexArrays(1, &gSphereVAO);

    gCubeVAO = gCubeVBO = gCubeEBO = 0;
    gCylVAO = gCylVBO = gCylEBO = 0;
    gCylSVAO = gCylSVBO = gCylSEBO = 0;
    gSphereVAO = gSphereVBO = gSphereEBO = 0;
}


void Primitives::drawCubeMesh()
{
    glBindVertexArray(gCubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    ++gDrawCalls;
}


void Primitives::drawSmoothCylinderMesh()
{
    glBindVertexArray(gCylSVAO);
    glDrawElements(GL_TRIANGLES, gCylSIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    ++gDrawCalls;
}


void Primitives::drawSphereMesh()
{
    glBindVertexArray(gSphereVAO);
    glDrawElements(GL_TRIANGLES, gSphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    ++gDrawCalls;
}


void Primitives::drawCylinderMesh()
{
    glBindVertexArray(gCylVAO);
    glDrawElements(GL_TRIANGLES, gCylIndexCount, GL_UNSIGNED_INT, 0);
    ++gDrawCalls;
}


int  Primitives::cubeVertexCount() { return 24; }
int  Primitives::cubeIndexCount() { return 36; }
int  Primitives::cubeTriangleCount() { return 12; }
int  Primitives::cylinderVertexCount() { return gCylVertexCount; }
int  Primitives::cylinderIndexCount() { return gCylIndexCount; }
int  Primitives::cylinderTriangleCount() { return gCylIndexCount / 3; }
int  Primitives::smoothCylinderVertexCount() { return gCylSVertexCount; }
int  Primitives::smoothCylinderTriangleCount() { return gCylSIndexCount / 3; }
int  Primitives::smoothCylinderSegments() { return kSmoothSegments; }
int  Primitives::sphereVertexCount() { return gSphereVertexCount; }
int  Primitives::sphereIndexCount() { return gSphereIndexCount; }
int  Primitives::sphereTriangleCount() { return gSphereIndexCount / 3; }
int  Primitives::cylinderSegments() { return kCylinderSegments; }

int  Primitives::drawCallsThisFrame() { return gDrawCalls; }
void Primitives::resetDrawCallCounter() { gDrawCalls = 0; }
