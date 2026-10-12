// The unit cube and the unit cylinder
#include <glad/glad.h>
#include <vector>
#include <cmath>

#include "Primitives.h"
#include "Math3D.h"

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

static void buildCylinder(std::vector<float>& verts, std::vector<unsigned int>& idx, int segments)
{
    const int   N = segments;
    const float R = 0.5f;
    const float halfH = 0.5f;

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
