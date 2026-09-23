#ifndef SHADER_H
#define SHADER_H

// ============================================================================
//  Shader.h  -  Compiles and links the GLSL program used by the whole scene.
// ============================================================================
//
//  The shader SOURCE CODE is stored directly inside Shader.cpp as C++ string
//  literals instead of in separate .vert / .frag files.
//
//  Why? Because loading shaders from disk depends on the "working directory",
//  and in Visual Studio the working directory is not always the folder you
//  expect. Embedding the source removes an entire class of "it runs from the
//  Debug folder but not by double-clicking the .exe" problems.
//
//  For this laboratory the shader is intentionally simple:
//    - one directional light (a fixed direction, like sunlight)
//    - one ambient term
//    - one object colour supplied as a uniform
//    - one "emissive" switch for objects that glow (neon sign, lamps, scanner)
//
//  Phong / Gouraud switching can be added later inside the same file without
//  touching any of the geometry code.
// ============================================================================

// Builds the program. Returns the OpenGL program id, or 0 on failure.
unsigned int createSceneShaderProgram();

#endif // SHADER_H
