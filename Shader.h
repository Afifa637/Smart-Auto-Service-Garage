#ifndef SHADER_H
#define SHADER_H

// Scene lighting/material shader (Cook-Torrance PBR + Gouraud switch, procedural material
// maps, reflections, HDR bright-pass and directional shadow sampling).
unsigned int createSceneShaderProgram();

// Depth-only shader for the directional-light shadow map.
unsigned int createDepthShaderProgram();

// Fullscreen post-processing shaders used for Gaussian bloom and final
// tone-mapping/gamma correction.
unsigned int createBlurShaderProgram();
unsigned int createPostShaderProgram();

// Ray-traced inspection monitor: tracing pass, in-world screen, enlarged overlay.
unsigned int createRayTraceShaderProgram();
unsigned int createMonitorShaderProgram();
unsigned int createBlitShaderProgram();

#endif // SHADER_H
