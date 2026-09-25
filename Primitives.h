#ifndef PRIMITIVES_H
#define PRIMITIVES_H

// ============================================================================
//  Primitives.h  -  Reusable primitive meshes for the whole project.
// ============================================================================
//
//  Most garage equipment - walls, lift, robot arm, cabinet, shelves, props
//  and all wheel/accessory details - is drawn from this reusable primitive
//  library. The Cybertruck's large folded stainless shell is the one deliberate
//  exception: SceneObjects.cpp generates its flat triangle/quad panels in code
//  so the production-style silhouette and hard fold normals are accurate.
//
//  Reusable meshes provided here:
//
//      1. A UNIT CUBE     : 1 x 1 x 1, centred on its own origin.
//      2. A UNIT CYLINDER : radius 0.5, height 1.0, centred on its own origin,
//                           uploaded at 28- and 48-segment detail levels.
//      3. A UNIT UV SPHERE: radius 0.5, centred on its own origin, used only
//                           where a truly rounded joint/bulb/knob is useful.
//
//  We never write a second vertex array for "car body" or "garage wall".
//  Instead we upload the same cube once to the GPU and then draw it many
//  times with a different MODEL MATRIX each time. That model matrix is what
//  turns the cube into a wall, a bumper, a drawer or a fan blade.
//
//      ONE UNIT CUBE  ->  model transformation  ->  many different objects
//
// ============================================================================

namespace Primitives
{
    // Uploads the cube and cylinder to the GPU. Call once after GLAD loads.
    void init();

    // Frees the VAOs / VBOs / EBOs. Call once before the program exits.
    void destroy();

    // Issue one draw call for the unit cube (36 indices = 12 triangles).
    void drawCubeMesh();

    // Issue one draw call for the unit cylinder (28 segments).
    // Used for small hardware - bolts, pins, casters - where the extra
    // triangles of the smooth mesh would be invisible.
    void drawCylinderMesh();

    // Issue one draw call for the HIGH-DETAIL unit cylinder (48 segments).
    // Identical shape and identical local frame, just finer around the barrel.
    // Used wherever the silhouette must read as genuinely round: tyres, oil
    // drums, hoses, pipes, robot arm segments, the fan hub.
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
