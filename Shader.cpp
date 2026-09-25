// ============================================================================
//  Shader.cpp - production rendering pipeline for the Smart Garage
// ============================================================================
//  OpenGL 3.3 core, self-contained GLSL strings (no working-directory issues).
//
//  Scene shader features:
//    * Cook-Torrance GGX PBR (default) + legacy per-vertex Gouraud comparison
//    * directional daylight, 8 ceiling point lights, dynamic robot spotlight
//    * PCF directional shadow map
//    * procedural albedo/normal/roughness/AO material maps
//    * roughness-aware cubemap reflections + Schlick Fresnel for stainless/glass
//    * HDR output + bloom bright-pass
//
//  Post pipeline:
//    * separable Gaussian bloom
//    * ACES-like filmic tone mapping
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
out vec4 vFragPosLightSpace;
out vec3 vGouraudLighting;

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
    vFragPosLightSpace = uLightSpaceMatrix * world;
    vGouraudLighting = vertexLighting(world.xyz, N);

    gl_Position = uProjection * uView * world;
}
)GLSL";

static const char* kSceneFragmentShader = R"GLSL(
#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vLocalPos;
in vec4 vFragPosLightSpace;
in vec3 vGouraudLighting;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform vec3  uObjectColor;
uniform float uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform int   uMaterialType;
uniform vec3  uCameraPos;

uniform int uShadingMode;       // 0 = Cook-Torrance PBR, 1 = Gouraud
uniform int uTexturesEnabled;
uniform int uNormalMapsEnabled;
uniform int uShadowsEnabled;
uniform int uReflectionsEnabled;

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
uniform samplerCube uEnvironmentMap;

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
        float grain = 0.5 + 0.5*sin((lp.z * 290.0) + sin(lp.y*37.0)*2.0);
        float fine  = 0.5 + 0.5*sin(lp.z * 1030.0 + lp.y * 71.0);
        float n = valueNoise3(vec3(lp.z*150.0, lp.y*16.0, lp.x*11.0));
        float brushed = 0.86 + 0.10*grain + 0.035*fine + 0.035*n;
        m.albedo *= brushed;
        m.roughness = clamp(roughBase + 0.08*(grain-0.5) + 0.04*(n-0.5), 0.10, 0.55);
        m.bump = vec3(0.0, (grain-0.5)*0.018, (fine-0.5)*0.035);
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
        float weave = 0.5 + 0.5*sin((lp.x+lp.z)*145.0)*sin((lp.x-lp.z)*145.0);
        m.albedo *= 0.86 + 0.16*weave;
        m.roughness = 0.40 + 0.18*weave;
        m.bump = vec3((weave-0.5)*0.025);
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
    for (int x=-1;x<=1;++x)
        for (int y=-1;y<=1;++y)
        {
            float pcfDepth = texture(uShadowMap, proj.xy + vec2(x,y)*texel).r;
            shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

const float PI = 3.14159265359;

vec3 environmentColor(vec3 R, float rough)
{
    // The cubemap has generated mip levels, so rough surfaces sample a blurrier
    // reflection.  This is not a full prefiltered HDR IBL pipeline, but it keeps
    // the same physically meaningful roughness control and is OpenGL-3.3 safe.
    float lod = clamp(rough, 0.0, 1.0) * 5.0;
    return textureLod(uEnvironmentMap, normalize(R), lod).rgb;
}

vec3 environmentColor(vec3 R)
{
    return environmentColor(R, 0.0);
}

float distributionGGX(vec3 N, vec3 H, float rough)
{
    float a = max(rough * rough, 0.025);
    float a2 = a * a;
    float ndh = max(dot(N,H), 0.0);
    float ndh2 = ndh * ndh;
    float d = ndh2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.000001);
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

vec3 cookTorranceLight(vec3 albedo, vec3 N, vec3 V, vec3 L,
                       vec3 radiance, float metal, float rough)
{
    vec3 H = normalize(V + L);
    float ndl = max(dot(N,L), 0.0);
    float ndv = max(dot(N,V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return vec3(0.0);

    vec3 F0 = mix(vec3(0.040), max(albedo, vec3(0.040)), metal);
    vec3 F = fresnelSchlick(max(dot(H,V), 0.0), F0);
    float D = distributionGGX(N,H,rough);
    float G = geometrySmith(N,V,L,rough);
    vec3 specular = (D * G * F) / max(4.0 * ndv * ndl, 0.001);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metal);
    vec3 diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * ndl;
}

vec3 pbrLighting(vec3 albedo, vec3 N, float metal, float rough, float ao)
{
    vec3 V = normalize(uCameraPos - vWorldPos);
    rough = clamp(rough, 0.045, 1.0);
    vec3 F0 = mix(vec3(0.040), max(albedo, vec3(0.040)), metal);

    // Low room bounce keeps shadow detail visible without flattening the scene.
    vec3 result = albedo * (1.0 - metal) * vec3(0.032,0.034,0.038) * ao;

    vec3 Ld = normalize(-uDirLightDirection);
    float shadow = shadowPCF(N,Ld);
    result += cookTorranceLight(albedo,N,V,Ld,
        uDirLightColor*uDirLightIntensity*(1.0-shadow), metal,rough);

    for (int i=0;i<8;++i)
    {
        if (i >= uPointLightCount) break;
        vec3 toL = uPointLightPos[i]-vWorldPos;
        float d = length(toL);
        vec3 L = toL/max(d,0.001);
        float att = 1.0/(1.0+0.10*d+0.026*d*d);
        vec3 radiance = uPointLightColor[i]*uPointLightIntensity[i]*att;
        result += cookTorranceLight(albedo,N,V,L,radiance,metal,rough);
    }

    vec3 toS = uSpotPos-vWorldPos;
    float sd = length(toS);
    vec3 SL = toS/max(sd,0.001);
    float cone = dot(normalize(-SL),normalize(uSpotDir));
    float coneW = clamp((cone-uSpotOuterCos)/max(uSpotInnerCos-uSpotOuterCos,0.001),0.0,1.0);
    float satt = coneW/(1.0+0.11*sd+0.040*sd*sd);
    result += cookTorranceLight(albedo,N,V,SL,uSpotColor*uSpotIntensity*satt,metal,rough);

    if (uReflectionsEnabled != 0)
    {
        float ndv = max(dot(N,V),0.0);
        vec3 F = fresnelSchlick(ndv,F0);
        vec3 R = reflect(-V,N);
        vec3 envSpec = environmentColor(R,rough);

        // Specular IBL is strongest for metals and grazing angles.  A small
        // diffuse environment term gives paint/plastic believable room bounce.
        float specWeight = mix(0.26,0.92,metal) * mix(1.0,0.38,rough);
        result += envSpec * F * specWeight;
        if (metal < 0.95)
            result += environmentColor(N,1.0) * albedo * (1.0-metal) * 0.055 * ao;
    }

    return result;
}

void main()
{
    float metal = clamp(uMetallic,0.0,1.0);
    MaterialMaps maps = materialMaps(uObjectColor,uRoughness);
    vec3 N = mappedNormal(normalize(vNormal), maps.bump);

    vec3 color;
    if (uShadingMode == 1)
    {
        color = maps.albedo * vGouraudLighting * maps.ao;
        if (uReflectionsEnabled != 0 && (metal > 0.18 || uMaterialType == 11 || uMaterialType == 13))
        {
            vec3 V = normalize(uCameraPos-vWorldPos);
            vec3 R = reflect(-V,N);
            float fres = pow(1.0-max(dot(N,V),0.0),5.0);
            color += environmentColor(R,maps.roughness) * mix(0.08,0.48,metal) * (0.30+0.70*fres);
        }
    }
    else
    {
        color = pbrLighting(maps.albedo,N,metal,maps.roughness,maps.ao);
    }

    // Opaque-tinted glass approximation: very dark transmission plus strong,
    // sharp environment reflection.  This avoids the old matte-black polygon look.
    if (uMaterialType == 11)
    {
        vec3 V = normalize(uCameraPos-vWorldPos);
        float ndv = max(dot(N,V),0.0);
        float f = 0.04 + 0.96*pow(1.0-ndv,5.0);
        vec3 env = environmentColor(reflect(-V,N), maps.roughness*0.55);
        color = maps.albedo*0.07 + env*(0.46 + 0.46*f);
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
void main()
{
    vec2 texel = 1.0/vec2(textureSize(uImage,0));
    float w0=0.227027, w1=0.1945946, w2=0.1216216, w3=0.054054, w4=0.016216;
    vec3 result=texture(uImage,TexCoords).rgb*w0;
    vec2 axis=(uHorizontal!=0)?vec2(texel.x,0.0):vec2(0.0,texel.y);
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
uniform int uBloomEnabled;
uniform float uExposure;
uniform vec2 uInvResolution;

vec3 acesFilm(vec3 x)
{
    const float a=2.51;
    const float b=0.03;
    const float c=2.43;
    const float d=0.59;
    const float e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);
}

void main()
{
    vec3 hdr=texture(uScene,TexCoords).rgb;

    // Lightweight post AA. Rendering goes through an HDR FBO, so the default
    // window MSAA buffer no longer resolves the scene for us. This contrast-
    // aware 5-tap blend removes the most obvious staircase edges without
    // smearing material texture detail.
    vec3 n=texture(uScene,TexCoords+vec2(0.0,uInvResolution.y)).rgb;
    vec3 s=texture(uScene,TexCoords-vec2(0.0,uInvResolution.y)).rgb;
    vec3 e=texture(uScene,TexCoords+vec2(uInvResolution.x,0.0)).rgb;
    vec3 w=texture(uScene,TexCoords-vec2(uInvResolution.x,0.0)).rgb;
    float lc=dot(hdr,vec3(0.2126,0.7152,0.0722));
    float lmin=min(lc,min(min(dot(n,vec3(0.2126,0.7152,0.0722)),dot(s,vec3(0.2126,0.7152,0.0722))),min(dot(e,vec3(0.2126,0.7152,0.0722)),dot(w,vec3(0.2126,0.7152,0.0722)))));
    float lmax=max(lc,max(max(dot(n,vec3(0.2126,0.7152,0.0722)),dot(s,vec3(0.2126,0.7152,0.0722))),max(dot(e,vec3(0.2126,0.7152,0.0722)),dot(w,vec3(0.2126,0.7152,0.0722)))));
    float edge=smoothstep(0.07,0.28,lmax-lmin);
    hdr=mix(hdr,(hdr+n+s+e+w)/5.0,edge*0.28);

    if(uBloomEnabled!=0) hdr += texture(uBloom,TexCoords).rgb*0.10;
    hdr *= max(uExposure,0.01);
    vec3 mapped=acesFilm(hdr);
    mapped=pow(mapped,vec3(1.0/2.2));
    FragColor=vec4(mapped,1.0);
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
