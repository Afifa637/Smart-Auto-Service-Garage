// ============================================================================
//  Shader.cpp  -  GLSL source + compile/link helpers
// ============================================================================

#include <glad/glad.h>      // MUST come before any other OpenGL-related header
#include <iostream>

#include "Shader.h"


// ----------------------------------------------------------------------------
//  VERTEX SHADER
// ----------------------------------------------------------------------------
//  Runs once per vertex. Its job is to take a vertex of our unit cube
//  (a number between -0.5 and +0.5) and work out where it lands on screen.
//
//  The famous line is:
//
//      gl_Position = projection * view * model * vec4(aPos, 1.0);
//
//  Read right to left:
//      model      - places the cube in the world (translate * rotate * scale)
//      view       - moves the world so the camera sits at the origin
//      projection - applies perspective so far things look smaller
// ----------------------------------------------------------------------------
static const char* kVertexShaderSource = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPos;      // vertex position from cubeVertices[]
layout (location = 1) in vec3 aNormal;   // which way this face points

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;              // inverse-transpose of uModel's 3x3

out vec3 vNormal;
out vec3 vWorldPos;

void main()
{
    vec4 worldPosition = uModel * vec4(aPos, 1.0);

    vWorldPos = worldPosition.xyz;
    vNormal   = normalize(uNormalMatrix * aNormal);

    gl_Position = uProjection * uView * worldPosition;
}
)GLSL";


// ----------------------------------------------------------------------------
//  FRAGMENT SHADER
// ----------------------------------------------------------------------------
//  Runs once per pixel covered by a triangle and decides its final colour.
//
//  We use a very small lighting model on purpose:
//      final = objectColour * (ambient + diffuse)
//
//  "ambient" is a constant so nothing is ever pure black.
//  "diffuse" is brighter on faces that point towards the light.
//  This is enough to make the cubes read as solid 3D boxes instead of
//  flat silhouettes, without being the full Phong model we implement later.
// ----------------------------------------------------------------------------
static const char* kFragmentShaderSource = R"GLSL(
#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;

out vec4 FragColor;

uniform vec3  uObjectColor;
uniform float uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3  uCameraPos;

void main()
{
    vec3 N = normalize(vNormal);

    // Key + fill directions keep the garage readable but still create the broad,
    // hard reflections that sell the Cybertruck's folded stainless surfaces.
    vec3 L1 = normalize(vec3(-0.35, 1.0, 0.45));
    vec3 L2 = normalize(vec3( 0.45, 0.30,-1.00));
    vec3 V  = normalize(uCameraPos - vWorldPos);
    vec3 H1 = normalize(L1 + V);
    vec3 H2 = normalize(L2 + V);

    float ndl1 = max(dot(N, L1), 0.0);
    float ndl2 = max(dot(N, L2), 0.0);

    float rough = clamp(uRoughness, 0.04, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);

    // A compact Blinn-Phong material model.  It is intentionally simple enough
    // for the coursework project, but unlike the old diffuse-only shader it can
    // distinguish brushed steel, glossy glass, painted metal and rubber.
    float shininess = mix(150.0, 12.0, rough);
    float spec1 = pow(max(dot(N, H1), 0.0), shininess);
    float spec2 = pow(max(dot(N, H2), 0.0), shininess * 0.65);

    vec3 F0 = mix(vec3(0.045), max(uObjectColor, vec3(0.08)), metal);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 specColor = mix(F0, vec3(1.0), fresnel * 0.45);

    // Metals contribute less diffuse energy; rubber/matte plastics remain mostly
    // diffuse.  This keeps the stainless body bright without making every object
    // in the garage look chrome-plated.
    vec3 diffuseColor = uObjectColor * (1.0 - 0.58 * metal);
    vec3 diffuse = diffuseColor * (0.30 + 0.67 * ndl1 + 0.15 * ndl2);

    float specStrength = mix(0.18, 1.05, metal) * mix(1.0, 0.35, rough);
    vec3 specular = specColor * (spec1 + 0.32 * spec2) * specStrength;

    // Very subtle high-frequency longitudinal variation approximates the cold
    // brushed finish without textures.  It only activates on strongly metallic
    // materials, so walls, tyres and painted props are unaffected.
    float brush = 1.0 + metal * 0.026 * sin(vWorldPos.z * 115.0 + vWorldPos.y * 17.0);
    vec3 shaded = diffuse * brush + specular;

    // Emissive light bars are self-lit and retain their clean white/red colour.
    vec3 result = mix(shaded, uObjectColor, clamp(uEmissive, 0.0, 1.0));
    FragColor = vec4(result, 1.0);
}
)GLSL";


// ----------------------------------------------------------------------------
//  compileStage - compiles one shader stage and reports errors clearly.
// ----------------------------------------------------------------------------
static unsigned int compileStage(GLenum type, const char* source, const char* label)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::cout << "[Shader] " << label << " compilation FAILED:\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}


// ----------------------------------------------------------------------------
//  createSceneShaderProgram - compile both stages and link them together.
// ----------------------------------------------------------------------------
unsigned int createSceneShaderProgram()
{
    unsigned int vs = compileStage(GL_VERTEX_SHADER, kVertexShaderSource, "vertex shader");
    if (vs == 0) return 0;

    unsigned int fs = compileStage(GL_FRAGMENT_SHADER, kFragmentShaderSource, "fragment shader");
    if (fs == 0)
    {
        glDeleteShader(vs);
        return 0;
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[1024];
        glGetProgramInfoLog(program, 1024, nullptr, log);
        std::cout << "[Shader] program linking FAILED:\n" << log << std::endl;
        glDeleteProgram(program);
        program = 0;
    }

    // The individual shader objects are baked into the program now.
    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}
