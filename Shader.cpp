
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
uniform vec3  uPointLightPos[10];
uniform vec3  uPointLightColor[10];
uniform float uPointLightIntensity[10];

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
out vec3 vGouraudDiffuse;    // ambient + diffuse light at the vertex
out vec3 vGouraudSpecular;   // specular light at the vertex

uniform mat4 uSpotLightSpaceMatrix;

void phongReflection(vec3 P, vec3 N, vec3 V, float rough, float metal,
                     float dirShadow, float spotShadowF,
                     out vec3 diffuse, out vec3 specular)
{
    float n = mix(180.0, 10.0, clamp(rough, 0.04, 1.0));   // shininess exponent
    diffuse  = vec3(0.038);                                  // ambient term ka*La
    specular = vec3(0.0);

    // Directional light
    vec3 L = normalize(-uDirLightDirection);
    vec3 R = reflect(-L, N);
    float ndl = max(dot(N, L), 0.0);
    vec3 Ldir = uDirLightColor * uDirLightIntensity * (1.0 - dirShadow);
    diffuse  += Ldir * ndl * (1.0 - 0.35 * metal);
    specular += Ldir * (ndl > 0.0 ? pow(max(dot(R, V), 0.0), n) : 0.0) * mix(0.12, 0.75, metal);

    // Point lights (ceiling fixtures + vehicle headlights)
    for (int i = 0; i < 10; ++i)
    {
        if (i >= uPointLightCount) break;
        vec3 toL = uPointLightPos[i] - P;
        float d = length(toL);
        vec3 Lp = toL / max(d, 0.001);
        vec3 Rp = reflect(-Lp, N);
        float att = 1.0 / (1.0 + 0.075 * d + 0.018 * d * d);
        float nd = max(dot(N, Lp), 0.0);
        vec3 Lc = uPointLightColor[i] * uPointLightIntensity[i] * att;
        diffuse  += Lc * nd * (1.0 - 0.30 * metal);
        specular += Lc * (nd > 0.0 ? pow(max(dot(Rp, V), 0.0), n) : 0.0) * mix(0.08, 0.48, metal);
    }

    // Spotlight on the robot scanner
    vec3 toS = uSpotPos - P;
    float sd = length(toS);
    vec3 SL = toS / max(sd, 0.001);
    vec3 RS = reflect(-SL, N);
    float cone = dot(normalize(-SL), normalize(uSpotDir));
    float coneW = clamp((cone - uSpotOuterCos) /
                        max(uSpotInnerCos - uSpotOuterCos, 0.001), 0.0, 1.0);
    float satt = coneW / (1.0 + 0.11 * sd + 0.035 * sd * sd) * (1.0 - spotShadowF);
    float snd = max(dot(N, SL), 0.0);
    vec3 Ls = uSpotColor * uSpotIntensity * satt;
    diffuse  += Ls * snd * (1.0 - 0.30 * metal);
    specular += Ls * (snd > 0.0 ? pow(max(dot(RS, V), 0.0), n) : 0.0) * mix(0.10, 0.55, metal);
}

void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vec3 N = normalize(uNormalMatrix * aNormal);

    vWorldPos = world.xyz;
    vLocalPos = aPos;
    vNormal = N;

    vBrushDir = mat3(uModel) * vec3(0.0, 0.0, 1.0);

    vFragPosLightSpace = uLightSpaceMatrix * world;
    vFragPosSpotSpace  = uSpotLightSpaceMatrix * world;
    // GOURAUD: the Phong reflection model evaluated once per VERTEX; the
    // rasteriser then interpolates these colours across the triangle.
    phongReflection(world.xyz, N, normalize(uCameraPos - world.xyz),
                    uRoughness, clamp(uMetallic, 0.0, 1.0), 0.0, 0.0,
                    vGouraudDiffuse, vGouraudSpecular);

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
in vec3 vGouraudDiffuse;
in vec3 vGouraudSpecular;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform vec3  uObjectColor;
uniform float uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform int   uMaterialType;
uniform vec3  uCameraPos;

uniform float uPolish;
uniform float uClearCoat;
uniform float uAnisotropy;

uniform float uDirt;

uniform int uShadingMode;       // 0 = Cook-Torrance PBR, 1 = Gouraud, 2 = Phong
uniform int uTexturesEnabled;
uniform int uNormalMapsEnabled;
uniform int uShadowsEnabled;
uniform int uReflectionsEnabled;

uniform float uIblIntensity;    // global multiplier on image-based lighting

uniform vec3  uDirLightDirection;
uniform vec3  uDirLightColor;
uniform float uDirLightIntensity;

uniform int   uPointLightCount;
uniform vec3  uPointLightPos[10];
uniform vec3  uPointLightColor[10];
uniform float uPointLightIntensity[10];

uniform vec3  uSpotPos;
uniform vec3  uSpotDir;
uniform vec3  uSpotColor;
uniform float uSpotIntensity;
uniform float uSpotInnerCos;
uniform float uSpotOuterCos;

uniform sampler2D uShadowMap;
uniform sampler2D uSpotShadowMap;
uniform samplerCube uEnvironmentMap;

uniform sampler2DArray uMaterialTextures;
uniform int   uTextureLayer;
uniform float uTextureScale;

const float PI = 3.14159265359;

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
        float pol = clamp(uPolish, 0.0, 1.0);

        vec3 bT, bB;
        brushFrame(normalize(vNormal), bT, bB);
        float across = dot(vWorldPos, bB);

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
    else if (uMaterialType == 21) // woven upholstery fabric (work-stool cushion)
    {
        vec3 q = wp * 420.0;
        float warp = 0.5 + 0.5*sin(q.x + q.y*0.7);
        float weft = 0.5 + 0.5*sin(q.z - q.y*0.7);
        float weave = warp * weft;
        float fuzz = valueNoise3(wp * 90.0);
        float wv = fwidth(wp.x + wp.z) + 1e-6;
        float weaveFade = 1.0 - smoothstep(0.35 * (PI / 420.0), PI / 420.0, wv);
        m.albedo *= 0.88 + 0.14*weave*weaveFade + 0.10*fuzz;
        m.roughness = 0.97;
        m.ao = 0.93 + 0.07*weave;
        m.bump = vec3((weave-0.5)*0.05*weaveFade + (fuzz-0.5)*0.03);
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
        float weave = 0.5 + 0.5*sin((wp.x+wp.z)*115.0)*sin((wp.x-wp.z)*115.0);
        float wv = fwidth(wp.x + wp.z) + 1e-6;
        float weaveFade = 1.0 - smoothstep(0.35 * (PI / 115.0), PI / 115.0, wv);
        m.albedo *= 0.90 + 0.12*weave*weaveFade;
        m.roughness = 0.40 + 0.16*weave*weaveFade;
        m.bump = vec3((weave-0.5)*0.020*weaveFade);
    }

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
    for (int x=-2;x<=2;++x)
        for (int y=-2;y<=2;++y)
        {
            float pcfDepth = texture(uShadowMap, proj.xy + vec2(x,y)*texel).r;
            shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    return shadow / 25.0;
}

vec3 environmentColor(vec3 R, float rough)
{
    float lod = sqrt(clamp(rough, 0.0, 1.0)) * 6.0;
    return textureLod(uEnvironmentMap, normalize(R), lod).rgb;
}

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

    for (int i=0;i<10;++i)
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

    if (uReflectionsEnabled != 0)
    {
        vec3 R = reflect(-V, N);

        float horizon = clamp(1.0 + 1.6 * dot(R, geoN), 0.0, 1.0);
        horizon *= horizon;

        vec2 dfg = envDFG(rough, ndv);
        vec3 prefiltered = environmentColor(R, rough);
        vec3 specIBL = prefiltered * (F0 * dfg.x + dfg.y) * horizon;

        // Multi-scatter compensation again, so rough metal keeps its energy.
        specIBL *= 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 1e-3) - 1.0);

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

// Same classical Phong reflection model as the vertex shader (see there);
// duplicated because GLSL stages cannot share functions.
void phongReflection(vec3 P, vec3 N, vec3 V, float rough, float metal,
                     float dirShadow, float spotShadowF,
                     out vec3 diffuse, out vec3 specular)
{
    float n = mix(180.0, 10.0, clamp(rough, 0.04, 1.0));   // shininess exponent
    diffuse  = vec3(0.038);                                  // ambient term ka*La
    specular = vec3(0.0);

    vec3 L = normalize(-uDirLightDirection);
    vec3 R = reflect(-L, N);
    float ndl = max(dot(N, L), 0.0);
    vec3 Ldir = uDirLightColor * uDirLightIntensity * (1.0 - dirShadow);
    diffuse  += Ldir * ndl * (1.0 - 0.35 * metal);
    specular += Ldir * (ndl > 0.0 ? pow(max(dot(R, V), 0.0), n) : 0.0) * mix(0.12, 0.75, metal);

    for (int i = 0; i < 10; ++i)
    {
        if (i >= uPointLightCount) break;
        vec3 toL = uPointLightPos[i] - P;
        float d = length(toL);
        vec3 Lp = toL / max(d, 0.001);
        vec3 Rp = reflect(-Lp, N);
        float att = 1.0 / (1.0 + 0.075 * d + 0.018 * d * d);
        float nd = max(dot(N, Lp), 0.0);
        vec3 Lc = uPointLightColor[i] * uPointLightIntensity[i] * att;
        diffuse  += Lc * nd * (1.0 - 0.30 * metal);
        specular += Lc * (nd > 0.0 ? pow(max(dot(Rp, V), 0.0), n) : 0.0) * mix(0.08, 0.48, metal);
    }

    vec3 toS = uSpotPos - P;
    float sd = length(toS);
    vec3 SL = toS / max(sd, 0.001);
    vec3 RS = reflect(-SL, N);
    float cone = dot(normalize(-SL), normalize(uSpotDir));
    float coneW = clamp((cone - uSpotOuterCos) /
                        max(uSpotInnerCos - uSpotOuterCos, 0.001), 0.0, 1.0);
    float satt = coneW / (1.0 + 0.11 * sd + 0.035 * sd * sd) * (1.0 - spotShadowF);
    float snd = max(dot(N, SL), 0.0);
    vec3 Ls = uSpotColor * uSpotIntensity * satt;
    diffuse  += Ls * snd * (1.0 - 0.30 * metal);
    specular += Ls * (snd > 0.0 ? pow(max(dot(RS, V), 0.0), n) : 0.0) * mix(0.10, 0.55, metal);
}

void main()
{
    float metal = clamp(uMetallic,0.0,1.0);
    float polish = clamp(uPolish, 0.0, 1.0);

    MaterialMaps maps = materialMaps(uObjectColor,uRoughness);

    maps.roughness = mix(maps.roughness, maps.roughness * 0.34 + 0.030, polish);

    float coat  = clamp(uClearCoat, 0.0, 1.0);
    float aniso = clamp(uAnisotropy, 0.0, 1.0) * mix(1.0, 0.42, polish);

    maps.albedo *= mix(1.0, 1.05, polish);

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
    if (uShadingMode == 1 || uShadingMode == 2)
    {
        vec3 lightDiffuse, lightSpecular;
        if (uShadingMode == 1)
        {
            // GOURAUD: use the colours computed at the three vertices, already
            // interpolated by the rasteriser (alpha*C1 + beta*C2 + gamma*C3).
            lightDiffuse  = vGouraudDiffuse;
            lightSpecular = vGouraudSpecular;
        }
        else
        {
            // PHONG: interpolated normal, same reflection model per FRAGMENT,
            // so highlights and light pools smaller than a triangle survive.
            vec3 Vf = normalize(uCameraPos - vWorldPos);
            vec3 Ld0 = normalize(-uDirLightDirection);
            vec3 SL0 = normalize(uSpotPos - vWorldPos);
            phongReflection(vWorldPos, N, Vf, maps.roughness, metal,
                            shadowPCF(N, Ld0), spotShadow(N, SL0),
                            lightDiffuse, lightSpecular);
        }
        // I = kd*(ambient + diffuse light) + ks-weighted specular light; metals tint their highlight.
        color = maps.albedo * lightDiffuse * maps.ao + lightSpecular * mix(vec3(1.0), maps.albedo, metal);
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

static const char* kRayTraceFragmentShader = R"GLSL(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D uItems;
uniform sampler2D uChunks;
uniform int   uChunkCount;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec3  uLightPos;
uniform float uLightPower;
uniform int   uShadows;

const float INF = 1e30;

vec4 itemTexel(int item, int k)
{
    int t = item * 4 + k;
    return texelFetch(uItems, ivec2(t & 4095, t >> 12), 0);
}

vec4 chunkTexel(int c, int k)
{
    int t = c * 2 + k;
    return texelFetch(uChunks, ivec2(t & 1023, t >> 10), 0);
}

bool hitItem(int i, vec3 o, vec3 d, float tmax, out float tHit, out vec3 nHit)
{
    vec4 r0 = itemTexel(i, 0);
    vec4 r1 = itemTexel(i, 1);
    vec4 r2 = itemTexel(i, 2);
    int type = int(itemTexel(i, 3).w + 0.5) & 7;
    tHit = INF;
    nHit = vec3(0.0, 1.0, 0.0);

    if (type == 3)
    {
        vec3 e1 = r1.xyz - r0.xyz;
        vec3 e2 = r2.xyz - r0.xyz;
        vec3 p = cross(d, e2);
        float det = dot(e1, p);
        if (abs(det) < 1e-9) return false;
        float inv = 1.0 / det;
        vec3 sv = o - r0.xyz;
        float u = dot(sv, p) * inv;
        if (u < 0.0 || u > 1.0) return false;
        vec3 q = cross(sv, e1);
        float v = dot(d, q) * inv;
        if (v < 0.0 || u + v > 1.0) return false;
        float t = dot(e2, q) * inv;
        if (t < 1e-4 || t > tmax) return false;
        tHit = t;
        nHit = normalize(cross(e1, e2));
        return true;
    }

    vec3 oo = vec3(dot(r0.xyz, o) + r0.w, dot(r1.xyz, o) + r1.w, dot(r2.xyz, o) + r2.w);
    vec3 dd = vec3(dot(r0.xyz, d), dot(r1.xyz, d), dot(r2.xyz, d));
    vec3 nl = vec3(0.0, 1.0, 0.0);
    float t = INF;

    if (type == 0)                     // unit cube, slab method
    {
        vec3 sd = vec3(abs(dd.x) < 1e-9 ? 1e-9 : dd.x, abs(dd.y) < 1e-9 ? 1e-9 : dd.y, abs(dd.z) < 1e-9 ? 1e-9 : dd.z);
        vec3 inv = 1.0 / sd;
        vec3 t0 = (vec3(-0.5) - oo) * inv;
        vec3 t1 = (vec3(0.5) - oo) * inv;
        vec3 tmn = min(t0, t1);
        vec3 tmx = max(t0, t1);
        float tn = max(max(tmn.x, tmn.y), tmn.z);
        float tf = min(min(tmx.x, tmx.y), tmx.z);
        if (tn > tf || tf < 1e-4) return false;
        if (tn > 1e-4)
        {
            t = tn;
            if (tmn.x >= tmn.y && tmn.x >= tmn.z) nl = vec3(-sign(sd.x), 0.0, 0.0);
            else if (tmn.y >= tmn.z)             nl = vec3(0.0, -sign(sd.y), 0.0);
            else                                  nl = vec3(0.0, 0.0, -sign(sd.z));
        }
        else
        {
            t = tf;
            if (tmx.x <= tmx.y && tmx.x <= tmx.z) nl = vec3(sign(sd.x), 0.0, 0.0);
            else if (tmx.y <= tmx.z)             nl = vec3(0.0, sign(sd.y), 0.0);
            else                                  nl = vec3(0.0, 0.0, sign(sd.z));
        }
    }
    else if (type == 1)                // unit cylinder, radius 0.5, axis y, height 1
    {
        float a = dd.x * dd.x + dd.z * dd.z;
        float b = oo.x * dd.x + oo.z * dd.z;
        float c = oo.x * oo.x + oo.z * oo.z - 0.25;
        if (a > 1e-10)
        {
            float disc = b * b - a * c;
            if (disc > 0.0)
            {
                float sq = sqrt(disc);
                float ta = (-b - sq) / a;
                float tb = (-b + sq) / a;
                if (ta > 1e-4 && abs(oo.y + ta * dd.y) <= 0.5) { t = ta; nl = vec3(oo.x + ta * dd.x, 0.0, oo.z + ta * dd.z); }
                else if (tb > 1e-4 && abs(oo.y + tb * dd.y) <= 0.5) { t = tb; nl = vec3(oo.x + tb * dd.x, 0.0, oo.z + tb * dd.z); }
            }
        }
        if (abs(dd.y) > 1e-10)
        {
            for (int k = 0; k < 2; ++k)
            {
                float sy = (k == 0) ? -0.5 : 0.5;
                float tc = (sy - oo.y) / dd.y;
                if (tc > 1e-4 && tc < t)
                {
                    float x = oo.x + tc * dd.x;
                    float z = oo.z + tc * dd.z;
                    if (x * x + z * z <= 0.25) { t = tc; nl = vec3(0.0, sy * 2.0, 0.0); }
                }
            }
        }
        if (t >= INF) return false;
    }
    else                               // sphere / ellipsoid, radius 0.5
    {
        float a = dot(dd, dd);
        float b = dot(oo, dd);
        float c = dot(oo, oo) - 0.25;
        float disc = b * b - a * c;
        if (disc < 0.0) return false;
        float sq = sqrt(disc);
        float ta = (-b - sq) / a;
        float tb = (-b + sq) / a;
        t = (ta > 1e-4) ? ta : tb;
        if (t < 1e-4) return false;
        nl = oo + t * dd;
    }

    if (t > tmax) return false;
    tHit = t;
    nHit = normalize(nl.x * r0.xyz + nl.y * r1.xyz + nl.z * r2.xyz);   // transpose of the inverse
    return true;
}

// NEAREST hit: every candidate shrinks tBest, so a farther surface can never
// replace a nearer one.
bool traceClosest(vec3 o, vec3 d, out float tBest, out vec3 nBest, out int itemBest)
{
    tBest = INF;
    nBest = vec3(0.0, 1.0, 0.0);
    itemBest = -1;
    for (int c = 0; c < uChunkCount; ++c)
    {
        vec4 s = chunkTexel(c, 0);
        vec4 info = chunkTexel(c, 1);
        vec3 oc = s.xyz - o;
        float tca = dot(oc, d);
        float d2 = dot(oc, oc) - tca * tca;
        if (d2 > s.w * s.w || tca + s.w < 0.0 || tca - s.w > tBest) continue;   // bounding sphere
        int first = int(info.x + 0.5);
        int cnt = int(info.y + 0.5);
        for (int k = 0; k < cnt; ++k)
        {
            float t; vec3 n;
            if (hitItem(first + k, o, d, tBest, t, n)) { tBest = t; nBest = n; itemBest = first + k; }
        }
    }
    return itemBest >= 0;
}

// ANY hit closer than tmax (the distance to the light): blockers beyond the
// light are never considered.
bool occluded(vec3 o, vec3 d, float tmax)
{
    for (int c = 0; c < uChunkCount; ++c)
    {
        vec4 s = chunkTexel(c, 0);
        vec4 info = chunkTexel(c, 1);
        vec3 oc = s.xyz - o;
        float tca = dot(oc, d);
        float d2 = dot(oc, oc) - tca * tca;
        if (d2 > s.w * s.w || tca + s.w < 0.0 || tca - s.w > tmax) continue;
        int first = int(info.x + 0.5);
        int cnt = int(info.y + 0.5);
        for (int k = 0; k < cnt; ++k)
        {
            float t; vec3 n;
            if (hitItem(first + k, o, d, tmax, t, n)) return true;
        }
    }
    return false;
}

void main()
{
    vec2 ndc = TexCoords * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + ndc.x * uAspect * uTanHalfFov * uCamRight + ndc.y * uTanHalfFov * uCamUp);

    float tObj; vec3 nObj; int item;
    bool hitObj = traceClosest(uCamPos, dir, tObj, nObj, item);
    float tFloor = (dir.y < -1e-4) ? (-uCamPos.y / dir.y) : INF;
    if (tFloor > 40.0) tFloor = INF;

    vec3 base; vec3 n; float t; bool emissive = false;
    if (hitObj && tObj < tFloor)
    {
        t = tObj; n = nObj;
        vec4 tex3 = itemTexel(item, 3);
        base = tex3.rgb;
        emissive = (int(tex3.w + 0.5) & 8) != 0;
    }
    else if (tFloor < INF)
    {
        t = tFloor; n = vec3(0.0, 1.0, 0.0);
        vec3 fp = uCamPos + dir * t;
        vec2 g = abs(fract(fp.xz) - 0.5);
        float line = smoothstep(0.47, 0.49, max(g.x, g.y)) * (1.0 - smoothstep(5.0, 13.0, t));
        base = mix(vec3(0.34, 0.35, 0.37), vec3(0.16, 0.17, 0.18), line);
    }
    else
    {
        float v = clamp(0.5 + 0.5 * dir.y, 0.0, 1.0);
        FragColor = vec4(mix(vec3(0.015, 0.02, 0.03), vec3(0.05, 0.07, 0.10), v), 1.0);
        return;
    }

    vec3 p = uCamPos + dir * t;
    if (dot(n, dir) > 0.0) n = -n;
    if (emissive) { FragColor = vec4(base * 1.3, 1.0); return; }

    vec3 Lv = uLightPos - p;
    float dist = length(Lv);
    vec3 L = Lv / dist;
    float ndl = max(dot(n, L), 0.0);

    // Shadow ray: from the hit point (offset along the normal to avoid
    // self-intersection) toward the light; only blockers nearer than the light count.
    float vis = 1.0;
    if (uShadows != 0 && ndl > 0.0)
        vis = occluded(p + n * 0.012, L, dist - 0.03) ? 0.0 : 1.0;

    float att = uLightPower / (1.0 + 0.10 * dist + 0.04 * dist * dist);
    vec3 V = -dir;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(n, H), 0.0), 48.0) * 0.30;

    vec3 ambient = base * (0.085 + 0.06 * n.y + 0.05 * max(dot(n, V), 0.0));
    vec3 direct = base * ndl * att * vis + vec3(spec * att * vis);
    vec3 col = ambient + direct;

    // simple exposure curve and a faint distance fade so the far floor recedes
    col = vec3(1.0) - exp(-col * 1.35);
    col *= 1.0 - clamp((t - 12.0) / 40.0, 0.0, 0.5);
    FragColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// The physical monitor's screen: samples the ray-traced texture on a quad in
// the garage (rendered into the same HDR targets as the rest of the scene).
static const char* kMonitorVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
out vec2 vUV;
out float vFront;
void main()
{
    vUV = aPos.xy + 0.5;
    vFront = aNormal.z;
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)GLSL";

static const char* kMonitorFragmentShader = R"GLSL(
#version 330 core
in vec2 vUV;
in float vFront;
layout(location=0) out vec4 FragColor;
layout(location=1) out vec4 BrightColor;
uniform sampler2D uTex;
uniform float uGain;
void main()
{
    if (vFront < 0.9) discard;
    vec3 c = texture(uTex, vUV).rgb;
    float scan = 0.94 + 0.06 * sin(vUV.y * 900.0);
    c = c * scan * uGain;
    FragColor = vec4(c, 1.0);
    float br = dot(c, vec3(0.2126, 0.7152, 0.0722));
    BrightColor = vec4(br > 1.18 ? c : vec3(0.0), 1.0);
}
)GLSL";

// Enlarged inspection view: the texture drawn on top of the finished frame.
static const char* kBlitFragmentShader = R"GLSL(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D uTex;
void main()
{
    vec3 c = texture(uTex, TexCoords).rgb;
    vec2 e = min(TexCoords, 1.0 - TexCoords);
    float edge = smoothstep(0.006, 0.0, min(e.x * 1.6, e.y));   // thin cyan frame
    FragColor = vec4(mix(c, vec3(0.15, 0.75, 0.9), edge), 1.0);
}
)GLSL";

unsigned int createRayTraceShaderProgram(){ return linkProgram(kFullscreenVertexShader,kRayTraceFragmentShader,"raytrace"); }
unsigned int createMonitorShaderProgram(){ return linkProgram(kMonitorVertexShader,kMonitorFragmentShader,"monitor"); }
unsigned int createBlitShaderProgram(){ return linkProgram(kFullscreenVertexShader,kBlitFragmentShader,"blit"); }
