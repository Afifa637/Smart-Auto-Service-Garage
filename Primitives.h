#ifndef PRIMITIVES_H
#define PRIMITIVES_H

namespace Primitives
{
    // Uploads the cube and cylinder to the GPU. Call once after GLAD loads.
    void init();

    // Frees the VAOs / VBOs / EBOs. Call once before the program exits.
    void destroy();

    // Issue one draw call for the unit cube (36 indices = 12 triangles).
    void drawCubeMesh();

    void drawCylinderMesh();

    void drawSmoothCylinderMesh();

    // UV sphere: radius 0.5, centred at origin. Used for rounded joints, bulbs,
    // knobs and other details where a cylinder/cube is visually wrong.
    void drawSphereMesh();

    // ---- Numbers used by the on-screen / console teaching view -------------
    int  cubeVertexCount();
    int  cubeIndexCount();
    int  cubeTriangleCount();
    int  cylinderVertexCount();
    int  cylinderIndexCount();
    int  cylinderTriangleCount();
    int  cylinderSegments();
    int  smoothCylinderVertexCount();
    int  smoothCylinderTriangleCount();
    int  smoothCylinderSegments();
    int  sphereVertexCount();
    int  sphereIndexCount();
    int  sphereTriangleCount();

    // How many times we called drawCubeMesh/drawCylinderMesh this frame.
    int  drawCallsThisFrame();
    void resetDrawCallCounter();
}

#endif // PRIMITIVES_H
