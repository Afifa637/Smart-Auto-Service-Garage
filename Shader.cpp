// ============================================================================
//  Shader.cpp - production rendering pipeline for the Smart Garage
// ============================================================================
//  OpenGL 3.3 core, self-contained GLSL strings (no working-directory issues).
//
//  Scene shader features:
//    * Cook-Torrance GGX PBR (default) + legacy per-vertex Gouraud comparison
//    * ANISOTROPIC GGX for brushed stainless - the highlight stretches along
//      the grain direction exactly like real rolled/brushed steel
//    * CLEAR-COAT lobe - a second, much smoother specular layer on top of the
//      base layer.  Polishing raises it, which is what makes a polished panel
//      visibly wetter and sharper than an unpolished one.
//    * split-sum environment BRDF (Karis analytic DFG) + multi-scatter energy
//      compensation, so metals actually receive environment energy instead of
//      rendering near-black
//    * directional daylight, 8 ceiling point lights, dynamic robot spotlight
//    * PCF directional shadow map
//    * procedural albedo/normal/roughness/AO material maps
//    * HDR output + bloom bright-pass
//
//  Post pipeline:
//    * separable Gaussian bloom (half resolution -> wider, cheaper, smoother)
//    * ACES-like filmic tone mapping
//    * vignette, subtle filmic contrast grade and fine sensor grain
//    * gamma correction
// ============================================================================

#include <glad/glad.h>
#include <iostream>
#include <string>
#include "Shader.h"

static const char* kSceneVertexShader = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;
uniform mat3 uNormalMatrix;

uniform vec3 uObjectColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uCameraPos;

uniform vec3  uDirLightDirection;
uniform vec3  uDirLightColor;
uniform float uDirLightIntensity;

uniform int   uPointLightCount;
uniform vec3  uPointLightPos[8];
uniform vec3  uPointLightColor[8];
uniform float uPointLightIntensity[8];

uniform vec3  uSpotPos;
uniform vec3  uSpotDir;
uniform vec3  uSpotColor;
uniform float uSpotIntensity;
uniform float uSpotInnerCos;
uniform float uSpotOuterCos;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vBrushDir;
out vec4 vFragPosLightSpace;
out vec4 vFragPosSpotSpace;
out vec3 vGouraudLighting;

uniform mat4 uSpotLightSpaceMatrix;

float specTerm(vec3 N, vec3 L, vec3 V, float roughness)
{
    vec3 H = normalize(L + V);
    float shininess = mix(180.0, 10.0, clamp(roughness, 0.04, 1.0));
    return pow(max(dot(N, H), 0.0), shininess);
}

vec3 vertexLighting(vec3 P, vec3 N)
{
    vec3 V = normalize(uCameraPos - P);
    float rough = clamp(uRoughness, 0.04, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);
    vec3 F0 = mix(vec3(0.045), max(uObjectColor, vec3(0.06)), metal);

    vec3 result = vec3(0.038); // deliberately restrained ambient for readable light separation

    vec3 Ld = normalize(-uDirLightDirection);
    float ndl = max(dot(N, Ld), 0.0);
    float sp = specTerm(N, Ld, V, rough);
    result += uDirLightColor * uDirLightIntensity *
              (ndl * (1.0 - 0.35 * metal) + sp * mix(0.12, 0.75, metal));

    for (int i = 0; i < 8; ++i)
    {
        if (i >= uPointLightCount) break;
        vec3 toL = uPointLightPos[i] - P;
        float d = length(toL);
        vec3 L = toL / max(d, 0.001);
        float att = 1.0 / (1.0 + 0.075 * d + 0.018 * d * d);
        float nd = max(dot(N, L), 0.0);
        float s = specTerm(N, L, V, rough);
        result += uPointLightColor[i] * uPointLightIntensity[i] * att *
                  (nd * (1.0 - 0.30 * metal) + s * mix(0.08, 0.48, metal));
    }

    vec3 toS = uSpotPos - P;
    float sd = length(toS);
    vec3 SL = toS / max(sd, 0.001);
    float cone = dot(normalize(-SL), normalize(uSpotDir));
    float coneW = clamp((cone - uSpotOuterCos) /
                        max(uSpotInnerCos - uSpotOuterCos, 0.001), 0.0, 1.0);
    float satt = coneW / (1.0 + 0.11 * sd + 0.035 * sd * sd);
    float snd = max(dot(N, SL), 0.0);
    float ss = specTerm(N, SL, V, rough);
    result += uSpotColor * uSpotIntensity * satt *
              (snd * (1.0 - 0.30 * metal) + ss * mix(0.10, 0.55, metal));

    return result;
}

void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vec3 N = normalize(uNormalMatrix * aNormal);

    vWorldPos = world.xyz;
    vLocalPos = aPos;
    vNormal = N;

    // The object's own +Z axis carried into world space.  For the vehicle this
    // is the longitudinal (nose-to-tail) direction, which is the direction a
    // rolled stainless panel is actually grained in.  Tangents transform with
    // the model matrix, NOT with the inverse-transpose normal matrix.
    vBrushDir = mat3(uModel) * vec3(0.0, 0.0, 1.0);

    vFragPosLightSpace = uLightSpaceMatrix * world;
    vFragPosSpotSpace  = uSpotLightSpaceMatrix * world;
    vGouraudLighting = vertexLighting(world.xyz, N);

    gl_Position = uProjection * uView * world;
}
)GLSL";

static const char* kSceneFragmentShader = R"GLSL(
#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vLocalPos;
in vec3 vBrushDir;
in vec4 vFragPosLightSpace;
in vec4 vFragPosSpotSpace;
in vec3 vGouraudLighting;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform vec3  uObjectColor;
uniform float uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform int   uMaterialType;
uniform vec3  uCameraPos;

// ---- SURFACE FINISH (per draw call) ------------------------------------
// uPolish      0 = as manufactured, 1 = machine polished.  It lowers base
//              roughness, lifts the clear coat and slightly brightens F0.
// uClearCoat   strength of the second smooth specular layer.
// uAnisotropy  0 = isotropic, 1 = fully grained along vBrushDir.
uniform float uPolish;
uniform float uClearCoat;
uniform float uAnisotropy;

// Road grime lying ON the surface, 0..1.  This used to be a handful of flat
// grey boxes floating a few millimetres off the door skin, which read exactly
// as what it was - discs stuck on the car.  Dirt is not an object; it is a
// thin dielectric film that changes what the surface underneath does with
// light, and that is what this models.
uniform float uDirt;

uniform int uShadingMode;       // 0 = Cook-Torrance PBR, 1 = Gouraud
uniform int uTexturesEnabled;
uniform int uNormalMapsEnabled;
uniform int uShadowsEnabled;
uniform int uReflectionsEnabled;

uniform float uIblIntensity;    // global multiplier on image-based lighting

uniform vec3  uDirLightDirection;
uniform vec3  uDirLightColor;
uniform float uDirLightIntensity;

uniform int   uPointLightCount;
uniform vec3  uPointLightPos[8];
uniform vec3  uPointLightColor[8];
uniform float uPointLightIntensity[8];

uniform vec3  uSpotPos;
uniform vec3  uSpotDir;
uniform vec3  uSpotColor;
uniform float uSpotIntensity;
uniform float uSpotInnerCos;
uniform float uSpotOuterCos;

uniform sampler2D uShadowMap;
uniform sampler2D uSpotShadowMap;
uniform samplerCube uEnvironmentMap;

// ---- REAL TEXTURE MAPPING -------------------------------------------------
//  A four-layer GL_TEXTURE_2D_ARRAY of tiling material maps.
//    RGB = albedo detail multiplier, packed as value/2 so 0..1 covers 0..2
//    A   = roughness, stored directly
//  uTextureLayer selects the layer for the current draw, or -1 for none.
uniform sampler2DArray uMaterialTextures;
uniform int   uTextureLayer;
uniform float uTextureScale;

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
//  SPOT SHADOWS
//
//  The robot's inspection lamp had no occlusion of any kind, so its beam lit
//  whatever lay along the cone - straight through the vehicle, the lift and
//  anything else in the way.  A light that passes through solid bodywork is
//  one of the most obvious tells there is.
//
//  This is a second shadow map, rendered from the lamp through a perspective
//  frustum matched to the cone angle, so the beam is stopped by the first
//  thing it actually hits.
// ---------------------------------------------------------------------------
float spotShadow(vec3 N, vec3 L)
{
    if (uShadowsEnabled == 0) return 0.0;
    vec3 proj = vFragPosSpotSpace.xyz / max(vFragPosSpotSpace.w, 0.0001);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0 ||
        proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float bias = max(0.0022 * (1.0 - dot(N, L)), 0.00055);
    vec2 texel = 1.0 / vec2(textureSize(uSpotShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            shadow += (proj.z - bias > texture(uSpotShadowMap, proj.xy + vec2(x,y) * texel).r)
                      ? 1.0 : 0.0;
    return shadow / 9.0;
}

float hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0,0,0));
    float n100 = hash31(i + vec3(1,0,0));
    float n010 = hash31(i + vec3(0,1,0));
    float n110 = hash31(i + vec3(1,1,0));
    float n001 = hash31(i + vec3(0,0,1));
    float n101 = hash31(i + vec3(1,0,1));
    float n011 = hash31(i + vec3(0,1,1));
    float n111 = hash31(i + vec3(1,1,1));
    float n00 = mix(n000,n100,f.x);
    float n10 = mix(n010,n110,f.x);
    float n01 = mix(n001,n101,f.x);
    float n11 = mix(n011,n111,f.x);
    return mix(mix(n00,n10,f.y), mix(n01,n11,f.y), f.z);
}

float fbm(vec3 p)
{
    float a = 0.5;
    float s = 0.0;
    for (int i=0;i<4;++i) { s += a * valueNoise3(p); p *= 2.03; a *= 0.5; }
    return s;
}

// Build an orthonormal frame whose tangent follows the grain direction
// projected onto the shading plane.
void brushFrame(vec3 N, out vec3 T, out vec3 B)
{
    vec3 g = vBrushDir;
    if (dot(g, g) < 1e-6) g = vec3(0.0, 0.0, 1.0);
    g = normalize(g);
    T = g - N * dot(N, g);
    if (dot(T, T) < 1e-5)          // grain parallel to the normal: pick any tangent
        T = abs(N.y) < 0.95 ? cross(N, vec3(0,1,0)) : cross(N, vec3(1,0,0));
    T = normalize(T);
    B = normalize(cross(N, T));
}

// ---------------------------------------------------------------------------
//  TRIPLANAR PROJECTION
//
//  The mesh format in this project is position + normal only - there is not a
//  single UV coordinate in any vertex buffer, because every object is a
//  transformed unit cube or cylinder and a shared UV set would stretch wildly
//  between them.
//
//  Triplanar mapping solves exactly that case: the surface is textured three
//  times, once projected down each world axis, and the three results are
//  blended by how strongly the normal faces that axis.  The UVs are therefore
//  DERIVED from world position rather than stored, texel density is uniform on
//  every surface regardless of how its cube was scaled, and nothing has to be
//  unwrapped.  The cost is three fetches instead of one.
//
//  The blend weights are sharpened with a power so that flat faces take almost
//  all of their colour from a single projection and only the corners mix.
// ---------------------------------------------------------------------------
vec4 triplanarSample(float layer, vec3 worldPos, vec3 n, float scale)
{
    vec3 w = abs(normalize(n));
    w = pow(w, vec3(6.0));
    w /= max(w.x + w.y + w.z, 1e-4);

    vec4 cx = texture(uMaterialTextures, vec3(worldPos.zy * scale, layer));
    vec4 cy = texture(uMaterialTextures, vec3(worldPos.xz * scale, layer));
    vec4 cz = texture(uMaterialTextures, vec3(worldPos.xy * scale, layer));

    return cx * w.x + cy * w.y + cz * w.z;
}

struct MaterialMaps
{
    vec3 albedo;
    float roughness;
    float ao;
    vec3 bump;
};

MaterialMaps materialMaps(vec3 base, float roughBase)
{
    MaterialMaps m;
    m.albedo = base;
    m.roughness = clamp(roughBase, 0.04, 1.0);
    m.ao = 1.0;
    m.bump = vec3(0.0);

    if (uTexturesEnabled == 0) return m;

    vec3 wp = vWorldPos;
    vec3 lp = vLocalPos;

    if (uMaterialType == 1) // worn concrete floor / apron
    {
        float n = fbm(wp * 2.8);
        float pores = valueNoise3(wp * 28.0);
        float stain = smoothstep(0.72, 0.93, fbm(vec3(wp.x*0.45, wp.y*3.0, wp.z*0.45)+7.0));
        m.albedo *= 0.78 + 0.32*n;
        m.albedo *= mix(1.0, 0.64, stain * 0.35);
        m.roughness = clamp(0.78 + 0.18*(1.0-n), 0.0, 1.0);
        m.ao = 0.90 + 0.10*n;
        m.bump = vec3((pores-0.5)*0.12, 0.0, (valueNoise3(wp*28.0+19.0)-0.5)*0.12);
    }
    else if (uMaterialType == 2) // painted wall/plaster
    {
        float n = fbm(wp * 4.0);
        m.albedo *= 0.92 + 0.12*n;
        m.roughness = 0.76 + 0.12*n;
        m.bump = vec3((n-0.5)*0.045, 0.0, (valueNoise3(wp*11.0)-0.5)*0.045);
    }
    else if (uMaterialType == 3 || uMaterialType == 13) // brushed steel / stainless
    {
        // ---------------------------------------------------------------
        //  BRUSHED / ROLLED STAINLESS
        //
        //  The scratches run ALONG the panel (the object's local Z), so the
        //  brightness must vary ACROSS the grain and stay constant along it.
        //  The previous version had this exactly backwards - it modulated
        //  along Z at a 22 mm wavelength, which produced ring stripes and a
        //  great deal of sampling noise rather than a brushed look.
        //
        //  `across` is the coordinate perpendicular to the grain.  Every
        //  streak term is a function of it alone.
        // ---------------------------------------------------------------
        float pol = clamp(uPolish, 0.0, 1.0);

        // `across` is the world-space coordinate perpendicular to the grain,
        // measured in metres.  Because it comes from the shading frame rather
        // than from object-local space, a 3 cm streak pitch is 3 cm on the
        // truck, on the robot arm and on a bolt head alike.
        vec3 bT, bB;
        brushFrame(normalize(vNormal), bT, bB);
        float across = dot(vWorldPos, bB);

        // Derivative-based anti-aliasing.
        //
        //  A sine of angular frequency k is adequately sampled only while one
        //  pixel spans less than half its period, i.e. while fwidth < PI/k.
        //  Past that it folds into low-frequency speckle.  Each band is
        //  therefore faded out as it approaches ITS OWN Nyquist limit, which
        //  is what keeps the flanks clean at a distance without blurring the
        //  grain away when the camera is close.
        float w = fwidth(across) + 1e-6;
        const float kStreak = 210.0;
        const float kFine   = 860.0;
        float coarseFade = 1.0 - smoothstep(0.35 * (PI / kStreak), PI / kStreak, w);
        float fineFade   = 1.0 - smoothstep(0.35 * (PI / kFine),   PI / kFine,   w);

        float streak = 0.5 + 0.5*sin(across * kStreak + sin(across * 31.0) * 1.7);
        float fine   = 0.5 + 0.5*sin(across * kFine + 1.7);
        float blotch = valueNoise3(wp * 1.6);

        float streakAmp = mix(0.080, 0.030, pol) * coarseFade;
        float fineAmp   = mix(0.030, 0.012, pol) * fineFade;

        // Polished steel is genuinely a little brighter and more uniform: the
        // diffusing micro-scratches that scatter light sideways are cut away.
        float brushed = 0.90 + streakAmp*(streak-0.5)*2.0
                             + fineAmp*(fine-0.5)*2.0
                             + 0.045*(blotch-0.5);
        m.albedo *= brushed * mix(1.0, 1.04, pol);

        // Roughness varies across the grain too - that is what gives brushed
        // metal its characteristic smeared, directional highlight.
        m.roughness = clamp(roughBase
                            + (0.085*(streak-0.5) + 0.030*(blotch-0.5)) * mix(1.0, 0.45, pol) * coarseFade,
                            0.030, 0.55);

        // The micro-normal is tilted only ACROSS the grain, never along it.
        m.bump = vec3(0.0, (streak-0.5)*0.014*coarseFade, 0.0) * mix(1.0, 0.45, pol);
    }
    else if (uMaterialType == 4) // galvanised metal
    {
        float n = fbm(wp * 7.0);
        float speck = valueNoise3(wp * 34.0);
        m.albedo *= 0.88 + 0.16*n + 0.05*(speck-0.5);
        m.roughness = 0.43 + 0.18*n;
        m.bump = vec3((speck-0.5)*0.05, (n-0.5)*0.04, 0.0);
    }
    else if (uMaterialType == 6) // plastic
    {
        float n = valueNoise3(wp * 24.0);
        m.albedo *= 0.94 + 0.08*n;
        m.roughness = clamp(roughBase + (n-0.5)*0.08, 0.25, 0.88);
        m.bump = vec3((n-0.5)*0.025);
    }
    else if (uMaterialType == 7) // wood
    {
        float grain = 0.5 + 0.5*sin((wp.x + 0.22*sin(wp.z*4.0))*18.0 + fbm(wp*2.0)*7.0);
        float pores = valueNoise3(wp*35.0);
        m.albedo *= vec3(0.78 + 0.38*grain, 0.72 + 0.28*grain, 0.64 + 0.18*grain);
        m.roughness = 0.62 + 0.24*(1.0-grain);
        m.bump = vec3((grain-0.5)*0.05, 0.0, (pores-0.5)*0.03);
    }
    else if (uMaterialType == 8) // cardboard
    {
        float fiber = 0.5 + 0.5*sin((wp.x+wp.z)*95.0 + valueNoise3(wp*12.0)*5.0);
        float n = valueNoise3(wp*18.0);
        m.albedo *= 0.88 + 0.10*n + 0.035*fiber;
        m.roughness = 0.86 + 0.09*n;
        m.bump = vec3((fiber-0.5)*0.025, (n-0.5)*0.025, 0.0);
    }
    else if (uMaterialType == 9 || uMaterialType == 12) // rubber / tyre
    {
        float n = valueNoise3(wp * 45.0);
        float micro = 0.5 + 0.5*sin((lp.y+lp.z)*145.0);
        m.albedo *= 0.82 + 0.20*n;
        m.roughness = 0.88 + 0.10*n;
        m.bump = vec3((n-0.5)*0.055, (micro-0.5)*0.045, (valueNoise3(wp*53.0)-0.5)*0.055);
    }
    else if (uMaterialType == 10) // powder-coated / painted metal
    {
        float n = valueNoise3(wp * 26.0);
        m.albedo *= 0.94 + 0.09*n;
        m.roughness = clamp(roughBase + (n-0.5)*0.12, 0.22, 0.82);
        m.bump = vec3((n-0.5)*0.018);
    }
    else if (uMaterialType == 14) // brake iron
    {
        float radial = 0.5 + 0.5*sin(length(lp.xz)*240.0);
        float n = valueNoise3(wp*30.0);
        m.albedo *= 0.80 + 0.18*n + 0.04*radial;
        m.roughness = 0.48 + 0.22*n;
        m.bump = vec3((radial-0.5)*0.03, 0.0, (n-0.5)*0.03);
    }
    else if (uMaterialType == 15) // asphalt
    {
        float coarse = fbm(wp * 5.5);
        float grit = valueNoise3(wp * 44.0);
        m.albedo *= 0.68 + 0.38*coarse + 0.08*(grit-0.5);
        m.roughness = 0.90 + 0.08*grit;
        m.bump = vec3((grit-0.5)*0.11, 0.0, (valueNoise3(wp*47.0+31.0)-0.5)*0.11);
    }
    else if (uMaterialType == 16) // rust / oxidised metal
    {
        float n = fbm(wp * 8.0);
        float pits = valueNoise3(wp*38.0);
        m.albedo *= vec3(0.80+0.35*n, 0.68+0.20*n, 0.58+0.12*n);
        m.roughness = 0.78 + 0.18*pits;
        m.bump = vec3((pits-0.5)*0.09, (n-0.5)*0.07, 0.0);
    }
    else if (uMaterialType == 17) // floor paint / safety marking
    {
        float chips = smoothstep(0.78,0.94,valueNoise3(wp*16.0));
        m.albedo *= mix(1.0, 0.58, chips*0.65);
        m.roughness = 0.62 + 0.20*chips;
    }
    else if (uMaterialType == 18) // carbon / dark composite
    {
        // In world metres, so a 2x2 twill reads at its real 8 mm pitch on a
        // tonneau cover and on a wheel face alike.  Read in object-local space
        // it inherited each object's scale, which turned small parts into
        // fine fabric mesh and large ones into a wire grid.
        float weave = 0.5 + 0.5*sin((wp.x+wp.z)*115.0)*sin((wp.x-wp.z)*115.0);
        float wv = fwidth(wp.x + wp.z) + 1e-6;
        float weaveFade = 1.0 - smoothstep(0.35 * (PI / 115.0), PI / 115.0, wv);
        m.albedo *= 0.90 + 0.12*weave*weaveFade;
        m.roughness = 0.40 + 0.16*weave*weaveFade;
        m.bump = vec3((weave-0.5)*0.020*weaveFade);
    }

    // -----------------------------------------------------------------
    //  SAMPLED TEXTURE PASS
    //
    //  Where a real tiling map exists for this material it is applied on top
    //  of the arithmetic detail above: the texture supplies the large- and
    //  mid-scale structure that noise functions are bad at (aggregate, crack
    //  patterns, the raised bars of a tread plate) while the procedural term
    //  keeps the very fine grain alive at close range.
    // -----------------------------------------------------------------
    if (uTextureLayer >= 0)
    {
        vec4 t = triplanarSample(float(uTextureLayer), vWorldPos,
                                 normalize(vNormal), uTextureScale);
        m.albedo *= t.rgb * 2.0;                       // unpack the 0..2 detail
        m.roughness = clamp(mix(m.roughness, t.a, 0.70), 0.04, 1.0);
        m.ao *= 0.88 + 0.12 * dot(t.rgb * 2.0, vec3(0.3333));
    }

    return m;
}

vec3 mappedNormal(vec3 baseN, vec3 bump)
{
    if (uNormalMapsEnabled == 0) return normalize(baseN);
    // World-space micro-normal approximation.  It deliberately stays subtle;
    // geometry normals still define the actual manufactured form.
    vec3 T = normalize(abs(baseN.y) < 0.95 ? cross(baseN, vec3(0,1,0))
                                           : cross(baseN, vec3(1,0,0)));
    vec3 B = normalize(cross(baseN, T));
    return normalize(baseN + T*bump.x + B*bump.z + vec3(0.0, bump.y, 0.0));
}

float shadowPCF(vec3 N, vec3 L)
{
    if (uShadowsEnabled == 0) return 0.0;
    vec3 proj = vFragPosLightSpace.xyz / max(vFragPosLightSpace.w, 0.0001);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    float bias = max(0.0015 * (1.0 - dot(N,L)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    // 5x5 PCF with a small rotation-free Poisson-ish spread.  The wider kernel
    // gives the lift columns and the vehicle a soft contact-hardening edge
    // instead of the hard stair-stepped 3x3 result.
    for (int x=-2;x<=2;++x)
        for (int y=-2;y<=2;++y)
        {
            float pcfDepth = texture(uShadowMap, proj.xy + vec2(x,y)*texel).r;
            shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    return shadow / 25.0;
}

// ---------------------------------------------------------------------------
//  ENVIRONMENT / IMAGE-BASED LIGHTING
// ---------------------------------------------------------------------------
vec3 environmentColor(vec3 R, float rough)
{
    // The cubemap has generated mip levels, so rough surfaces sample a blurrier
    // reflection.  This is not a full prefiltered HDR IBL pipeline, but it keeps
    // the same physically meaningful roughness control and is OpenGL-3.3 safe.
    float lod = sqrt(clamp(rough, 0.0, 1.0)) * 6.0;
    return textureLod(uEnvironmentMap, normalize(R), lod).rgb;
}

// Karis' analytic fit to the split-sum DFG lookup table.  This is the single
// most important missing piece in the old shader: without it a metal receives
// almost no environment energy and renders black no matter how bright the
// probe is.  x = scale on F0, y = bias.
vec2 envDFG(float rough, float ndv)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

float distributionGGX(vec3 N, vec3 H, float rough)
{
    float a = max(rough * rough, 0.0016);
    float a2 = a * a;
    float ndh = max(dot(N,H), 0.0);
    float ndh2 = ndh * ndh;
    float d = ndh2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}

// Anisotropic GGX (Burley).  `at` is the roughness along the grain, `ab`
// across it.  A brushed panel has at >> ab, which smears the highlight into
// the long soft streak you see on real stainless bodywork.
float distributionGGXAniso(float ndh, float tdh, float bdh, float at, float ab)
{
    float a2 = at * ab;
    vec3 v = vec3(ab * tdh, at * bdh, a2 * ndh);
    float v2 = dot(v, v);
    float w2 = a2 / max(v2, 1e-8);
    return a2 * w2 * w2 / PI;
}

float geometrySchlickGGX(float ndv, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return ndv / max(ndv * (1.0 - k) + k, 0.000001);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float rough)
{
    return geometrySchlickGGX(max(dot(N,V),0.0), rough) *
           geometrySchlickGGX(max(dot(N,L),0.0), rough);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// One analytic light evaluated with the full layered BRDF:
//   base layer   : (an)isotropic GGX, metal or dielectric
//   coat layer   : smooth isotropic GGX with a fixed 0.04 IOR response
vec3 shadeLight(vec3 albedo, vec3 N, vec3 V, vec3 L, vec3 radiance,
                float metal, float rough, float aniso, float coat)
{
    float ndl = max(dot(N,L), 0.0);
    float ndv = max(dot(N,V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.040), max(albedo, vec3(0.040)), metal);
    vec3 F = fresnelSchlick(max(dot(H,V), 0.0), F0);
    float G = geometrySmith(N,V,L,rough);

    float D;
    if (aniso > 0.01)
    {
        vec3 T, B;
        brushFrame(N, T, B);
        float a = max(rough * rough, 0.0016);
        // Keep the average roughness constant while stretching the lobe.
        float stretch = mix(1.0, 3.2, clamp(aniso, 0.0, 1.0));
        float at = clamp(a * stretch, 0.0016, 1.0);
        float ab = clamp(a / stretch, 0.0016, 1.0);
        D = distributionGGXAniso(max(dot(N,H),0.0), dot(T,H), dot(B,H), at, ab);
    }
    else
    {
        D = distributionGGX(N,H,rough);
    }

    vec3 specular = (D * G * F) / max(4.0 * ndv * ndl, 0.001);

    // Multi-scatter energy compensation.  Single-scatter GGX loses a lot of
    // energy at high roughness, which is why rough metals look dull and grey.
    vec2 dfg = envDFG(rough, ndv);
    specular *= 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 1e-3) - 1.0);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metal);
    vec3 diffuse = kD * albedo / PI;

    vec3 result = (diffuse + specular) * radiance * ndl;

    if (coat > 0.001)
    {
        float ccRough = mix(0.085, 0.018, clamp(uPolish, 0.0, 1.0));
        float Dc = distributionGGX(N, H, ccRough);
        float Gc = geometrySmith(N, V, L, ccRough);
        float Fc = 0.04 + 0.96 * pow(clamp(1.0 - max(dot(H,V),0.0), 0.0, 1.0), 5.0);
        float spec = (Dc * Gc * Fc) / max(4.0 * ndv * ndl, 0.001);
        // The coat absorbs part of what reaches the base layer.
        result *= (1.0 - coat * Fc);
        result += vec3(spec) * coat * radiance * ndl;
    }

    return result;
}

vec3 pbrLighting(vec3 albedo, vec3 N, vec3 geoN, float metal, float rough,
                 float ao, float aniso, float coat)
{
    vec3 V = normalize(uCameraPos - vWorldPos);
    rough = clamp(rough, 0.035, 1.0);
    vec3 F0 = mix(vec3(0.040), max(albedo, vec3(0.040)), metal);
    float ndv = max(dot(N,V), 0.0);

    // Low room bounce keeps shadow detail visible without flattening the scene.
    vec3 result = albedo * (1.0 - metal) * vec3(0.030,0.032,0.036) * ao;

    vec3 Ld = normalize(-uDirLightDirection);
    float shadow = shadowPCF(N,Ld);
    result += shadeLight(albedo,N,V,Ld,
        uDirLightColor*uDirLightIntensity*(1.0-shadow), metal,rough,aniso,coat);

    for (int i=0;i<8;++i)
    {
        if (i >= uPointLightCount) break;
        vec3 toL = uPointLightPos[i]-vWorldPos;
        float d = length(toL);
        vec3 L = toL/max(d,0.001);
        float att = 1.0/(1.0+0.10*d+0.026*d*d);
        vec3 radiance = uPointLightColor[i]*uPointLightIntensity[i]*att;
        result += shadeLight(albedo,N,V,L,radiance,metal,rough,aniso,coat);
    }

    vec3 toS = uSpotPos-vWorldPos;
    float sd = length(toS);
    vec3 SL = toS/max(sd,0.001);
    float cone = dot(normalize(-SL),normalize(uSpotDir));
    float coneW = clamp((cone-uSpotOuterCos)/max(uSpotInnerCos-uSpotOuterCos,0.001),0.0,1.0);
    coneW *= coneW;                                  // soft-edged cone falloff
    float satt = coneW/(1.0+0.11*sd+0.040*sd*sd);
    satt *= (1.0 - spotShadow(N, SL));
    result += shadeLight(albedo,N,V,SL,uSpotColor*uSpotIntensity*satt,metal,rough,aniso,coat);

    // -----------------------------------------------------------------
    //  IMAGE-BASED LIGHTING - split-sum approximation
    // -----------------------------------------------------------------
    if (uReflectionsEnabled != 0)
    {
        vec3 R = reflect(-V, N);

        // Horizon occlusion: a reflection ray that has been bent below the
        // real geometric surface by the micro-normal must not pick up the
        // environment, otherwise bumped surfaces glow along their edges.
        float horizon = clamp(1.0 + 1.6 * dot(R, geoN), 0.0, 1.0);
        horizon *= horizon;

        vec2 dfg = envDFG(rough, ndv);
        vec3 prefiltered = environmentColor(R, rough);
        vec3 specIBL = prefiltered * (F0 * dfg.x + dfg.y) * horizon;

        // Multi-scatter compensation again, so rough metal keeps its energy.
        specIBL *= 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 1e-3) - 1.0);

        // Crude diffuse irradiance: the blurriest mip around the normal.
        // It is deliberately weighted well below the specular term.  A full
        // cosine-convolved irradiance map would be darker than this single
        // blurry sample, and at full strength it flattens the whole room into
        // an evenly lit grey box with no shadow separation left.
        vec3 irradiance = environmentColor(N, 1.0);
        vec3 kD = (1.0 - (F0 * dfg.x + dfg.y)) * (1.0 - metal);
        vec3 diffIBL = irradiance * albedo * kD * 0.42;

        result += (specIBL + diffIBL) * ao * uIblIntensity;

        if (coat > 0.001)
        {
            // The coat adds its own sharp, almost mirror-like reflection.
            float ccRough = mix(0.085, 0.018, clamp(uPolish, 0.0, 1.0));
            float Fc = 0.04 + 0.96 * pow(clamp(1.0 - ndv, 0.0, 1.0), 5.0);
            result *= (1.0 - coat * Fc * 0.5);
            result += environmentColor(R, ccRough) * Fc * coat * horizon * uIblIntensity;
        }
    }

    return result;
}

void main()
{
    float metal = clamp(uMetallic,0.0,1.0);
    float polish = clamp(uPolish, 0.0, 1.0);

    MaterialMaps maps = materialMaps(uObjectColor,uRoughness);

    // -----------------------------------------------------------------
    //  SURFACE FINISH RESOLVE
    //
    //  Polishing a panel does three measurable things to its BRDF:
    //    1. it cuts the RMS micro-roughness - a tighter, brighter highlight
    //       and a sharper environment reflection
    //    2. it reduces the directionality of the grain, because the long
    //       parallel scratches that caused it are lapped away
    //    3. it raises specular reflectance slightly, because the dull
    //       light-scattering oxide layer goes with them
    //
    //  What it does NOT do is add a lacquer.  A clear coat belongs to the
    //  MATERIAL - paint, plastic, composite - not to how hard it was buffed.
    //  Driving a coat from polish is what made the stainless read as white
    //  plastic rather than as satin metal.
    //
    //  All of this is applied per draw call, which is what lets the polished
    //  flanks and the as-rolled hood share one mesh and one frame.
    // -----------------------------------------------------------------
    maps.roughness = mix(maps.roughness, maps.roughness * 0.34 + 0.030, polish);

    // A bare metal gains no lacquer from being polished, so the coat follows
    // the MATERIAL, not the polish.  Only painted and composite surfaces,
    // which declare a coat of their own, get the second specular layer.
    float coat  = clamp(uClearCoat, 0.0, 1.0);
    float aniso = clamp(uAnisotropy, 0.0, 1.0) * mix(1.0, 0.42, polish);

    // Polishing a metal physically raises its specular reflectance: the dull,
    // light-scattering oxide and the work-hardened surface layer are cut away
    // and what is left is cleaner, denser metal.  A few percent is realistic
    // and it is what stops a polished panel merely looking SHARPER while
    // staying the same brightness.
    maps.albedo *= mix(1.0, 1.05, polish);

    // -----------------------------------------------------------------
    //  ROAD GRIME
    //
    //  Three things make dust and dried spray read correctly:
    //
    //    1. it collects LOW and thins out as it climbs the panel, because it
    //       is thrown up off the road by the wheels
    //    2. it is blotchy at the decimetre scale and streaked vertically at
    //       the centimetre scale, from rain running down through it
    //    3. it is a DIELECTRIC.  A dirty metal panel stops behaving like
    //       metal where the film is thick - that is why grime kills a
    //       reflection rather than just tinting it.  Dropping the metallic
    //       term is what sells it; without that it looks like grey paint.
    //
    //  The mask is built in the VEHICLE'S OWN frame, so the patch stays on
    //  the panel when the car is raised or driven out.
    // -----------------------------------------------------------------
    float dirt = clamp(uDirt, 0.0, 1.0);
    if (dirt > 0.002)
    {
        vec3 lp = vLocalPos;

        // The serviced flank only: this is the panel the sequence inspects.
        float side  = smoothstep(-0.55, -0.95, lp.x);
        // Thrown up from the road, so it thins out with height.
        float low   = 1.0 - smoothstep(0.48, 1.26, lp.y);
        // Heaviest behind each wheel, lighter across the doors.
        float along = smoothstep(-2.30, -1.55, lp.z) * (1.0 - smoothstep(1.35, 2.10, lp.z));

        float blotch = fbm(vWorldPos * 2.6);
        float streak = fbm(vec3(vWorldPos.x * 1.8, vWorldPos.y * 0.45, vWorldPos.z * 11.0));
        float grit   = valueNoise3(vWorldPos * 34.0);

        float mask = side * low * along;
        mask *= 0.35 + 0.95 * blotch;
        mask *= 0.65 + 0.60 * streak;
        mask = clamp(mask * dirt * 2.25, 0.0, 1.0);

        vec3 grime = vec3(0.205, 0.183, 0.158) * (0.80 + 0.40 * grit);

        maps.albedo = mix(maps.albedo, grime, mask * 0.88);
        maps.roughness = mix(maps.roughness, 0.94, mask);
        metal = mix(metal, 0.0, mask * 0.92);
        aniso *= (1.0 - mask);
        coat  *= (1.0 - mask);
        maps.ao *= 1.0 - 0.18 * mask;
        maps.bump += vec3((grit - 0.5) * 0.05 * mask);
    }

    vec3 geoN = normalize(vNormal);
    vec3 N = mappedNormal(geoN, maps.bump);

    vec3 color;
    if (uShadingMode == 1)
    {
        color = maps.albedo * vGouraudLighting * maps.ao;
        if (uReflectionsEnabled != 0 && (metal > 0.18 || uMaterialType == 11 || uMaterialType == 13))
        {
            vec3 V = normalize(uCameraPos-vWorldPos);
            vec3 R = reflect(-V,N);
            float fres = pow(1.0-max(dot(N,V),0.0),5.0);
            color += environmentColor(R,maps.roughness) * mix(0.08,0.70,metal)
                   * (0.30+0.70*fres) * uIblIntensity;
        }
    }
    else
    {
        color = pbrLighting(maps.albedo,N,geoN,metal,maps.roughness,maps.ao,aniso,coat);
    }

    // -----------------------------------------------------------------
    //  TINTED LAMINATED GLASS
    //
    //  A sheet of glass reflects only about 4% of what hits it head on, and
    //  approaches a mirror only at grazing angles.  The old code blended in a
    //  flat 42% reflection regardless of angle, which made every window as
    //  bright as the stainless around it - the glazing simply disappeared.
    //  Schlick's term alone gives the correct behaviour: dark, legible
    //  windows face on, and bright mirrored ones at the edges of the body.
    // -----------------------------------------------------------------
    if (uMaterialType == 11)
    {
        vec3 V = normalize(uCameraPos-vWorldPos);
        float ndv = max(dot(N,V),0.0);
        float F = clamp(0.04 + 0.96*pow(1.0-ndv,5.0), 0.0, 1.0);

        vec3 env = environmentColor(reflect(-V,N), maps.roughness*0.35) * uIblIntensity;

        // What is behind the glass: a dark cabin that still catches a little
        // of the room, so the pane never becomes a flat black hole.
        vec3 interior = maps.albedo * (0.55 + 0.45 * environmentColor(N, 1.0) * 0.5);

        color = mix(interior, env, F);

        // A single sharp specular glint off the outer surface.
        vec3 Ld = normalize(-uDirLightDirection);
        vec3 H = normalize(V + Ld);
        float glint = pow(max(dot(N,H),0.0), 480.0);
        color += uDirLightColor * uDirLightIntensity * glint * 2.4 * (0.04 + 0.96*F);
    }

    vec3 emissive = maps.albedo * uEmissive * 4.2;
    color += emissive;

    FragColor = vec4(max(color,vec3(0.0)),1.0);
    float brightness = dot(color,vec3(0.2126,0.7152,0.0722));
    BrightColor = vec4((brightness>1.18 || uEmissive>0.35) ? color : vec3(0.0),1.0);
}
)GLSL";

static const char* kDepthVertexShader = R"GLSL(
#version 330 core
layout (location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;
void main(){ gl_Position = uLightSpaceMatrix * uModel * vec4(aPos,1.0); }
)GLSL";

static const char* kDepthFragmentShader = R"GLSL(
#version 330 core
void main() { }
)GLSL";

static const char* kFullscreenVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTex;
out vec2 TexCoords;
void main(){ TexCoords=aTex; gl_Position=vec4(aPos,0.0,1.0); }
)GLSL";

static const char* kBlurFragmentShader = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D uImage;
uniform int uHorizontal;
uniform float uRadiusScale;   // widens the kernel without extra taps

// Linear-sampled 9-tap Gaussian.  Because the bloom chain now runs at half
// resolution, the effective world-space radius is twice what it used to be
// for exactly a quarter of the fill cost.
void main()
{
    vec2 texel = 1.0/vec2(textureSize(uImage,0));
    float w0=0.227027, w1=0.1945946, w2=0.1216216, w3=0.054054, w4=0.016216;
    vec3 result=texture(uImage,TexCoords).rgb*w0;
    vec2 axis=((uHorizontal!=0)?vec2(texel.x,0.0):vec2(0.0,texel.y)) * max(uRadiusScale,0.1);
    result += texture(uImage,TexCoords+axis*1.0).rgb*w1;
    result += texture(uImage,TexCoords-axis*1.0).rgb*w1;
    result += texture(uImage,TexCoords+axis*2.0).rgb*w2;
    result += texture(uImage,TexCoords-axis*2.0).rgb*w2;
    result += texture(uImage,TexCoords+axis*3.0).rgb*w3;
    result += texture(uImage,TexCoords-axis*3.0).rgb*w3;
    result += texture(uImage,TexCoords+axis*4.0).rgb*w4;
    result += texture(uImage,TexCoords-axis*4.0).rgb*w4;
    FragColor=vec4(result,1.0);
}
)GLSL";

static const char* kPostFragmentShader = R"GLSL(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int   uBloomEnabled;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2  uInvResolution;
uniform float uTime;
uniform int   uEdgeAA;        // 1 only when hardware MSAA is unavailable
uniform float uVignette;
uniform float uGrain;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

vec3 acesFilm(vec3 x)
{
    const float a=2.51;
    const float b=0.03;
    const float c=2.43;
    const float d=0.59;
    const float e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);
}

// Gentle S-curve applied in log-ish space.  It deepens the blacks and rolls
// the highlights without the muddy mid-tones a plain pow() contrast gives.
vec3 filmicGrade(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 s = c * c * (3.0 - 2.0 * c);     // smoothstep contrast
    c = mix(c, s, 0.28);
    // Slight cool shadow / warm highlight split-tone: reads as photographic.
    float l = dot(c, LUMA);
    vec3 shadowTint    = vec3(0.96, 0.985, 1.045);
    vec3 highlightTint = vec3(1.035, 1.005, 0.965);
    c *= mix(shadowTint, highlightTint, smoothstep(0.18, 0.85, l));
    return clamp(c, 0.0, 1.0);
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec3 hdr=texture(uScene,TexCoords).rgb;

    // Fallback post AA, used only when a multisampled HDR target could not be
    // created.  With hardware MSAA resolving the scene this stays off, so the
    // material micro-detail is never smeared.
    if (uEdgeAA != 0)
    {
        vec3 n=texture(uScene,TexCoords+vec2(0.0,uInvResolution.y)).rgb;
        vec3 s=texture(uScene,TexCoords-vec2(0.0,uInvResolution.y)).rgb;
        vec3 e=texture(uScene,TexCoords+vec2(uInvResolution.x,0.0)).rgb;
        vec3 w=texture(uScene,TexCoords-vec2(uInvResolution.x,0.0)).rgb;
        float lc=dot(hdr,LUMA);
        float ln=dot(n,LUMA), ls=dot(s,LUMA), le=dot(e,LUMA), lw=dot(w,LUMA);
        float lmin=min(lc,min(min(ln,ls),min(le,lw)));
        float lmax=max(lc,max(max(ln,ls),max(le,lw)));
        float edge=smoothstep(0.07,0.28,lmax-lmin);
        hdr=mix(hdr,(hdr+n+s+e+w)/5.0,edge*0.28);
    }

    if(uBloomEnabled!=0) hdr += texture(uBloom,TexCoords).rgb * uBloomStrength;

    hdr *= max(uExposure,0.01);
    vec3 mapped=acesFilm(hdr);
    mapped=filmicGrade(mapped);
    mapped=pow(mapped,vec3(1.0/2.2));

    // Lens vignette - keeps the eye on the vehicle in the middle of frame.
    if (uVignette > 0.001)
    {
        vec2 d = TexCoords - 0.5;
        float v = 1.0 - uVignette * dot(d, d) * 1.9;
        mapped *= clamp(v, 0.0, 1.0);
    }

    // Very fine sensor grain.  At this amplitude it is not visible as noise,
    // it just breaks up the smooth gradients that give away a synthetic image.
    if (uGrain > 0.0001)
    {
        float g = hash12(TexCoords * vec2(1920.0, 1080.0) + fract(uTime) * 137.0);
        mapped += (g - 0.5) * uGrain;
    }

    FragColor=vec4(clamp(mapped,0.0,1.0),1.0);
}
)GLSL";

static unsigned int compileStage(GLenum type, const char* source, const char* label)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader,1,&source,nullptr);
    glCompileShader(shader);
    int success=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        char log[4096]; glGetShaderInfoLog(shader,4096,nullptr,log);
        std::cout << "[Shader] " << label << " compilation FAILED:\n" << log << std::endl;
        glDeleteShader(shader); return 0;
    }
    return shader;
}

static unsigned int linkProgram(const char* vsSource, const char* fsSource,
                                const char* label)
{
    unsigned int vs=compileStage(GL_VERTEX_SHADER,vsSource,(std::string(label)+" vertex").c_str());
    if(!vs) return 0;
    unsigned int fs=compileStage(GL_FRAGMENT_SHADER,fsSource,(std::string(label)+" fragment").c_str());
    if(!fs){ glDeleteShader(vs); return 0; }
    unsigned int p=glCreateProgram();
    glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    int success=0; glGetProgramiv(p,GL_LINK_STATUS,&success);
    if(!success)
    {
        char log[4096]; glGetProgramInfoLog(p,4096,nullptr,log);
        std::cout << "[Shader] " << label << " link FAILED:\n" << log << std::endl;
        glDeleteProgram(p); p=0;
    }
    glDeleteShader(vs); glDeleteShader(fs); return p;
}

unsigned int createSceneShaderProgram(){ return linkProgram(kSceneVertexShader,kSceneFragmentShader,"scene"); }
unsigned int createDepthShaderProgram(){ return linkProgram(kDepthVertexShader,kDepthFragmentShader,"depth"); }
unsigned int createBlurShaderProgram(){ return linkProgram(kFullscreenVertexShader,kBlurFragmentShader,"blur"); }
unsigned int createPostShaderProgram(){ return linkProgram(kFullscreenVertexShader,kPostFragmentShader,"post"); }
