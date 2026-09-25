# SmartGarage Cybertruck Realism V3

This revision applies the requested Cybertruck-proportion, material and lighting corrections while preserving the garage, lift, robot controls, collision logic and teaching modes.

## 1. Cybertruck geometry

- Roof apex moved rearward to approximately the B-pillar region. The windshield is now a longer, shallower continuous plane instead of a short steep wedge.
- Stainless side and windshield glazing are nearly flush with the body shell; the previous deep window recess was reduced to a small manufactured step.
- Wheel-arch cladding is thinner, more geometric and much less proud of the stainless body.
- Side mirrors are now actual shallow triangular-prism meshes at the A-pillar/window base, with reflective triangular faces.
- Door shut-lines and handle recesses are narrower and less contrast-heavy.
- Hood/roof/sail edge catches are millimetre-scale so they behave as specular fold highlights rather than decorative trim.
- Wheels use shallow, near-black polygonal aero-cover styling and suppress most visible brake/lug depth from the exterior.

## 2. Stainless, glass and PBR shading

The realistic default path now uses a metallic/roughness Cook-Torrance GGX BRDF:

- GGX normal-distribution function
- Smith geometry masking/shadowing
- Schlick Fresnel
- metallic/dielectric energy split
- roughness-aware cubemap reflection sampling

The stainless body is highly metallic with moderate brushed roughness. The glass is a dark neutral tint with strong environment reflection rather than a matte-black surface. Procedural brushed-metal variation remains subtle so the body planes stay clean.

`F5` switches between the default **Cook-Torrance PBR** path and the legacy **Gouraud** coursework comparison mode.

## 3. Lighting and environment

The scene is deliberately darker and more contrast-controlled so individual sources are visible:

- exposure reduced from the previous bright setting
- directional daylight reduced and warmed slightly
- ceiling point-light intensity reduced
- ambient/bounce contribution reduced
- robot scanner remains strong enough to read as a distinct cyan source
- bloom contribution and bright-pass threshold reduced
- environment cubemap darkened overall, with controlled ceiling strips, floor darkness and horizon bands for stainless reflections

This prevents the garage from looking uniformly illuminated and gives the Cybertruck panels visible sky/ground/horizon response.

## 4. Runtime controls

| Key | Function |
|---|---|
| F5 | Cook-Torrance PBR / Gouraud |
| F6 | Material maps on/off |
| F7 | Normal/bump maps on/off |
| F8 | Shadows on/off |
| F9 | Cubemap reflections on/off |
| F10 | Bloom on/off |
| L | Wireframe |
| 1 | Full scene |
| 2 | Cybertruck teaching view |
| N/B | Next/previous construction stage |

## 5. Build

Open `YoutubeOpenGL.slnx`, select **x64 / Debug** or **x64 / Release**, then **Build > Rebuild Solution**. Run the newly built executable rather than an older binary from another directory.

The C++ sources were syntax-checked after this V3 pass. The final OpenGL/GLSL runtime should still be tested on Windows/Visual Studio because the bundled GLFW library is the Windows `.lib`.
