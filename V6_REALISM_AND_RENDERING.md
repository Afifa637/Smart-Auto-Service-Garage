# SmartGarage v6 — Cybertruck realism and rendering upgrade

Written for: the person presenting this project, and the examiner reading the code.

This document records what was wrong with v5, what was changed, and why. Every
number quoted below was measured either off `Reference/03_reference_side.png`
(scaled with the published 5.683 m overall length) or out of the project's own
geometry.

---

## 1. The defects that were found

### 1.1 The vehicle rendered almost black

This was the single biggest problem, and it is visible in
`Reference/01_before_project.png`.

The Cybertruck body is bare stainless steel, so it is declared `metallic = 0.97`.
A metal has **no diffuse response at all** — every photon it shows the camera is
either a direct specular highlight or a reflection of the environment. The
environment probe was a 64×64 `RGB8` cubemap whose brightest pixel was about
0.25 in linear light, and the shader had **no split-sum environment BRDF**, so
the specular environment term was weighted down to roughly 0.09.

The result: a hero vehicle with a handful of highlights on an otherwise black
silhouette.

### 1.2 The silhouette was not a Cybertruck

The defining feature of the real vehicle is that the hood and the windscreen form
**one almost unbroken straight line** from the front bumper to the roof apex.

| | hood slope | windscreen slope |
|---|---|---|
| v5 | 0.09 (nearly flat) | 0.42 (steep) |
| real / v6 | 0.319 | 0.325 |

v5 therefore read as a conventional angular SUV with a steep glasshouse bolted
on. The roof apex was also 20 cm too far back and 7 cm too low, and the flat
section of roof was more than twice as long as it should be.

### 1.3 "Polishing" did nothing physical

`polishAmount` drew **one extra thin decal cube** on the flank. The body's
roughness never changed, so a polished panel and an unpolished one shaded
identically.

### 1.4 MSAA was dead code

`glfwWindowHint(GLFW_SAMPLES, 4)` and `glEnable(GL_MULTISAMPLE)` were both set,
but the scene was rendered into a **single-sample** HDR framebuffer and only the
tone-mapped result was blitted to the window. The window's multisample buffer
was never used. A 5-tap contrast blur in the post shader was standing in for it.

### 1.5 The brushed-metal grain ran the wrong way, in the wrong space

The grain was modulated **along** the panel (`sin(lp.z * 290.0)`) when brushed
steel is scratched along its length and therefore varies **across** it. It was
also read in each object's **local** coordinates, so the streak density scaled
with the object: an 8 m robot arm built from a unit cube received the same 50
streaks as a 2 cm bolt head. That is what produced the camouflage blotching on
the arm.

### 1.6 The robot never touched what it was working on

The safety model in `robotSafetyPoints()` treats every tool as a nominal 0.95 m
stub on the wrist. None of the tools are 0.95 m:

| tool | real working face, from the wrist | error |
|---|---|---|
| impact driver socket tip | 1.15 m | **+0.20 m — drove through the wheel** |
| polishing pad face | 0.9225 m | −0.03 m |
| cleaning sponge face | 1.175 m | +0.23 m |

Combined with a blanket 0.18 m "stand-off" that had never been measured, and a
side skin that moved inboard when the body was corrected to its real 2.06 m
width, the measured results were:

* impact driver: **211 mm inside** the wheel face
* polishing pad: **225 mm away** from the panel
* cleaning sponge: **164 mm away** from the panel

### 1.7 Smaller defects

* Tinted glass was blended with a flat 42 % reflection regardless of viewing
  angle, so the windows were as bright as the steel around them and the glazing
  disappeared. Real glass reflects about 4 % head-on.
* The wheel-arch cladding band was **28 mm wide** — from any normal distance it
  read as a pencil outline, not as the heavy black trim on the real vehicle.
* The aero cover was shaded as carbon composite, which put a woven-fabric
  pattern on the wheel face.
* The scanner head finished the service sequence pitched −74°, lying on its side
  aimed back across the workshop.
* 14 ceiling fixtures made the ceiling read as a grid of lamps.
* `resolveMaterial()` silently snapped any stainless roughness above 0.34 down to
  0.25, making a matte as-rolled finish impossible to request.
* Bloom ran 8 full-resolution passes with a 9-tap kernel: expensive, and the
  glow radius was too small to see.

---

## 2. What was changed

### 2.1 Shading (`Shader.cpp`)

**Split-sum environment BRDF.** Karis' analytic fit to the DFG lookup table:

```glsl
vec2 envDFG(float rough, float ndv)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}
```

The specular IBL term becomes `prefiltered * (F0 * dfg.x + dfg.y)`. This is the
piece whose absence made the metal black.

**Multi-scatter energy compensation.** Single-scatter GGX loses energy as
roughness rises, which is why rough metals go dull and grey. Both the analytic
and the image-based specular are scaled by
`1 + F0 * (1/(dfg.x + dfg.y) - 1)`.

**Anisotropic GGX** (Burley) for brushed stainless. The specular lobe is
stretched along the grain, which is what gives real bodywork its long smeared
highlight instead of a round one. The grain direction is the object's own +Z
axis carried into world space by the model matrix — note that tangents transform
with the model matrix, not with the inverse-transpose normal matrix.

**A clear-coat lobe**, driven by the *material* rather than by polish. Bare
metal gains no lacquer from being polished, so only paint, plastic and composite
declare a coat.

**Horizon occlusion** on the reflection, so bumped surfaces stop glowing along
their edges.

**Correct Fresnel for glass**: `mix(interior, env, F)` with Schlick's `F`, so a
window is dark head-on and mirrored at grazing angles.

**Derivative-based anti-aliasing of the procedural grain.** A sine of angular
frequency *k* is adequately sampled only while one pixel spans less than half its
period, i.e. while `fwidth < π/k`. Each band is faded out as it approaches *its
own* Nyquist limit:

```glsl
float coarseFade = 1.0 - smoothstep(0.35 * (PI / kStreak), PI / kStreak, w);
```

This keeps the flanks clean at a distance without blurring the grain away when
the camera is close.

**5×5 PCF** instead of 3×3 for the directional shadow.

### 2.2 The surface-finish system (`SceneObjects`)

A new piece of render state that sits beside the material:

```cpp
Scene::setSurfaceFinish(polish, clearCoat, anisotropy, materialTypeOverride);
Scene::clearSurfaceFinish();
```

The **material** says what a surface is made of; the **finish** says how it has
been worked. Polishing does three measurable things to a BRDF, and all three are
applied per draw call:

1. cuts the RMS micro-roughness (0.40 as-rolled → 0.16 polished)
2. reduces the directionality of the grain
3. raises specular reflectance slightly, because the dull light-scattering oxide
   layer is cut away

The truck's **side skin is its own mesh batch**, so the flanks can be drawn
inside a finish block while the hood, roof and tailgate keep their factory
finish — one mesh, two finishes, in the same frame. That is the requested
before/after, and it is a real material response rather than a painted-on
rectangle.

The same system makes the robot arm uniformly matte powder-coat without editing
forty individual draw calls.

### 2.3 Geometry (`buildCybertruckShellMesh`)

A **master profile** declared once, because the hood, A-pillar, roof rail, sail
panel and glazing all have to meet exactly:

```cpp
static float ctRoofLineY(float z);   // the upper silhouette as one function
```

| | v5 | v6 | reference |
|---|---|---|---|
| roof apex z | +0.295 | **+0.110** | +0.10 |
| roof apex y | 1.720 | **1.790** | 1.791 (published height) |
| body half width | 1.055 | **1.030** | 2.03 m across the doors |
| flat roof length | 0.57 m | **0.41 m** | ~0.4 m |
| arch cladding band | 0.028 m | **0.095 m** | heavy black trim |

The flank is emitted as six convex sub-panels (lower body, front wing, B-pillar,
roof rail ×2, sail) so a plain triangle fan is exact everywhere, with the
daylight opening cut out of it.

**Fold ribs.** Every folded steel panel has a small radius at its edge. Without
one, a facet junction is a mathematically perfect corner that light slides over
and the body lines disappear. `truckPushFoldRib()` emits two narrow strips
meeting at a crest just proud of the two faces, so each fold catches a specular
line — the wedge spine, the roof shoulder, the sail line, the rocker, the belt
crease and the vertical nose and tailgate corners.

Also added: recessed light channels with lenses sunk into them (a real light bar
is not a glowing decal), a sloping vault cover that follows the sail line, an
underbody floor pan, bed-rail tie-downs that ride the tapering rail, and a
seven-spoke aero cover with enough tonal separation to read at a distance.

### 2.4 Rendering pipeline (`Main.cpp`)

* **HDR environment probe**, 128², `RGB16F`. Ceiling strips sit at 5.2 and
  daylight at ~3.0, well above 1.0, so they survive tone mapping as genuine
  bright streaks on the panels. Wall-mounted amber strips were added because a
  vertical body panel reflects almost horizontally — without high contrast at
  eye level, polishing a flank has nothing to mirror and makes no visible
  difference.
* **Real 4× MSAA**: a multisampled HDR target with MRT, resolved with
  `glBlitFramebuffer` (one blit per attachment). Falls back cleanly to the
  software edge filter if the driver refuses.
* **Half-resolution bloom**: a linear blit downsamples the bright pass (a free
  2×2 box filter that also removes single-pixel fireflies), then six ping-pong
  passes. Wider and smoother than the old eight full-resolution passes, for
  about a fifth of the fill cost.
* **ACES filmic tone map** plus a split-tone grade, vignette and fine sensor
  grain.
* **Three lighting rigs** — Workshop, Studio (a three-point product rig built
  around the vehicle) and Night — each a complete, self-contained description
  including the exposure it was balanced for.

### 2.5 Robot poses re-solved

All seventeen service poses were re-solved offline against the **actual** tool
geometry, preserving each pose's tool direction and sweep height and changing
only the reach:

* impact driver: socket tip now lands on the outer face of each lug nut to
  within 3 mm. As a nut seats it draws 31 mm inboard, so the socket closes over
  it — which is what a real impact driver does.
* polishing pad and cleaning sponge: working face now 12 mm off the skin. Twelve
  millimetres is deliberate — it is the thickness of the working film a real
  polisher or sponge rides on, so the tool contacts the panel without
  interpenetrating it.
* `POSE_HOME` wrist pitch set to +74°, which exactly cancels the two arm joints
  so the inspection head parks hanging level and looking straight down.

---

## 3. New interaction and motion

The ceiling fan was previously a constant — it never turned. These are all new:

| key | effect |
|---|---|
| `;` | overhead work lights on / off (changes the **light**, not just the fixtures) |
| `'` | vehicle head and tail light bars on / off |
| `/` | neon sign on / off |
| `END` | hazard beacon flash |
| `9` / `0` | ceiling fan slower / faster, 0 = stopped |
| `HOME` | drive the vehicle out of the bay and back, wheels rolling |
| `F11` | 4× MSAA on / off |
| `F12` | showcase orbit — hands-free turntable around the vehicle |
| `M` | cycle lighting preset |
| `,` / `.` | environment lighting intensity |
| `F` | save a screenshot |
| `V` | geometry + full render report |

The drive-out animation is worth pointing at in a viva: the wheel spin is
**derived from the distance actually travelled**, so a wheel of radius *r*
turning through angle *A* has rolled exactly *rA* along the floor. That is the
difference between wheels that turn and wheels that look like they are driving.
The whole vehicle moves because one number is applied to its parent matrix —
body, glazing, cladding, wheels and service decals all travel with it, which is
the point of building the scene as a transform hierarchy.

Command line, for repeatable stills:

```
YoutubeOpenGL.exe --capture shot.bmp --orbit 215 --radius 8.2 --height 1.5 \
                  --lift 1.1 --polish 1 --preset 1 --size 1600 1000 --fov 40
```

---

## 4. Known limitations

Stated honestly, because an examiner will find them anyway.

* The wheelbase is 3.46 m against the real 3.63 m. The axle positions were left
  alone deliberately: seventeen robot poses are calibrated against them, and
  moving the wheels 8.5 cm would have thrown the whole service sequence out for
  a 4.7 % proportional gain.
* `robotSafetyPoints()` still models every tool as a 0.95 m stub. The authored
  poses now account for each tool's real length, but the *collision* test does
  not. Giving it a per-tool reach would be the correct next step; it was not done
  here because it changes which poses the safety system rejects, and that risks
  the demo stalling mid-sequence.
* The diffuse IBL is a single blurry cubemap sample weighted to 0.42, not a
  cosine-convolved irradiance map. At full strength a single blurry sample
  flattens the room into an evenly lit grey box.
* The environment probe is static. It does not update when the garage door opens
  or the lights are switched off, so reflections do not track those changes.

---

## 5. v6.1 — collision audit and the two missing syllabus topics

### 5.1 The tool was passing through the bodywork

A sweep was written that replays every pose and every transition in the service
sequence and reports the deepest intrusion of the fitted tool's solid into the
vehicle. It found three separate faults.

**Fault 1 — the stand-off had the wrong sign.** The panel is at `x = -1.030`
and the robot works from outboard of it, which is the **more negative** side. A
stand-off therefore has to be subtracted. It was being added, which put every
tool face 12 mm *inside* the skin.

**Fault 2 — the sweep bulge.** A wipe was one long interpolation between two end
poses. Joint angles interpolate linearly; the tool tip does not — it swings
through an arc. Measured at the midpoint of a full-length sweep, that arc dipped
**83 mm into the body**. This is what put the tool head inside the panel on
screen.

Each wipe is now three shorter segments. Sag falls with roughly the square of
the segment count (83 / 3² ≈ 9 mm), so with a 12 mm stand-off the tool stays
outboard for the whole pass:

```
PATH_LINE_HIGH   polisher  worst = -2.19 mm   stays outboard
PATH_LINE_MID    polisher  worst = -2.28 mm   stays outboard
PATH_LINE_LOW    polisher  worst = -2.39 mm   stays outboard
PATH_CLEAN_HIGH  sponge    worst = -2.61 mm   stays outboard
PATH_CLEAN_MID   sponge    worst = -2.37 mm   stays outboard
PATH_CLEAN_LOW   sponge    worst = -2.25 mm   stays outboard
```

Every waypoint is the exact solution of a 4-DOF IK problem. With the tool axis
horizontal the pitch at the tool is exactly 90°, which collapses to "aim the arm
plane at the target, then a plain two-link solve for the wrist".

**Fault 3 — manual control had the vehicle test switched off.** `robotPoseSafe`
took a flag `allowVehicleServiceContact`, and when it was true the forearm and
tool were **not tested against the vehicle at all**. Manual keyboard control
passes `true`, so hand-driving the arm pushed the tool straight through the
body — which is what the reported screenshot shows.

There are now two volumes rather than one flag:

* **approach volume** — the tool may enter it during service; the forearm never
  may.
* **vehicle solid** — the stainless shell and the tyres. *Nothing* may ever be
  inside it, service contact or not.

And the collision test now uses the **tool that is actually fitted**:

| tool | reach from wrist |
|---|---|
| impact driver, socket tip | 1.150 m |
| cleaning sponge, seam ring | 1.149 m |
| gripper fingers | 1.030 m |
| polishing pad + marker | 0.932 m |
| scanner lens face | 0.930 m |

It previously assumed a single nominal 0.95 m stub for all of them, which is
precisely how a 1.15 m tool went unnoticed through a wheel.

Two smaller fixes found on the way: the polisher's three rotation markers were
hanging 48 mm *below* the pad in mid-air (they were the part of the tool that
reached the panel first), and the polish stage only ever ran two of its three
authored passes — the LOW pass existed but was never executed.

### 5.2 The two syllabus topics that were genuinely missing

An audit against the required concepts found everything well covered except two.

**Projection.** The scene was only ever drawn through a perspective frustum. The
`orthographic()` routine existed but was used solely to build the shadow map,
where it is invisible. **BACKSPACE** now switches the view projection. The two
are matched at the subject's distance so the vehicle stays the same size and
only the projection changes: under perspective the parallel runway edges
converge to a vanishing point; under orthographic they stay parallel and all
foreshortening disappears, which is why orthographic is what engineering
elevations are drawn in.

**Texture mapping.** This was the weakest area by a distance. Every surface was
textured *arithmetically* in the fragment shader — there was no texture object,
no UV coordinate, no sampling and no mip chain anywhere in the material path.
All four samplers in the program were the shadow map, the environment cubemap
and the two post-process buffers.

There is now a real one: a **`GL_TEXTURE_2D_ARRAY`** with four tiling layers
(worn concrete, painted plaster, asphalt, steel tread plate), a full mip chain,
`GL_REPEAT` wrapping, trilinear filtering and 8× anisotropic filtering.

* An **array, not an atlas** — atlas tiles bleed into each other at coarse mip
  levels and cannot use `GL_REPEAT`. Each layer here wraps independently.
* **Packed**: RGB is an albedo detail multiplier (stored as value/2 so the 0–1
  texel range covers 0–2), alpha is roughness. One fetch returns both maps.
* **Seamless**: the generator's noise is periodic — every lookup wraps its
  lattice indices modulo the tile size.
* Deliberately **mid- and fine-frequency only**. Anything low-frequency repeats
  every 2.9 m and the eye catches it immediately; the large-scale variation is
  left to the non-tiling procedural noise.

The UVs are **derived, not stored**, by triplanar projection: the surface is
textured three times, once projected down each world axis, and the results are
blended by how strongly the normal faces that axis. This is the right technique
here because the mesh format is position + normal only — every object is a
transformed unit cube or cylinder, and a shared UV set would stretch wildly
between a bolt head and a wall. Triplanar gives uniform texel density on every
surface regardless of how its cube was scaled, for three fetches instead of one.

`F6` now toggles the whole material system — sampled textures and procedural
detail together — so the switch demonstrates something real.

### 5.3 Concept coverage after this pass

| concept | where to point during the demo |
|---|---|
| Shading | `F5` — Cook-Torrance GGX vs per-vertex Gouraud, same scene |
| Lighting | `M` — workshop / three-point studio / night rigs; `;` kills the bays |
| Illumination | `F9` — split-sum IBL off/on; `,` `.` scale the probe |
| **Projection** | **`BACKSPACE` — perspective vs orthographic** |
| **Textures** | **`F6` — 4-layer texture array, triplanar, mipmapped, anisotropic** |
| Effects | `F8` shadows, `F10` bloom, `F11` MSAA, ACES + vignette + grain |
| Interaction | `;` `'` `/` `END` `9` `0` `HOME`, full arm and camera control |
| Animation | `ENTER` service sequence, door, lift, fan, drive-out with rolling wheels |
| Presentation | `F12` showcase orbit, `F` screenshot, `V` render report |

---

## 6. v6.2 — the front of the vehicle, and the world outside

### 6.1 The front was one flat slab

Compared against the reference photograph, the front was wrong in three ways
at once, and they compounded.

**The fascia was bare stainless.** On the real vehicle only the **hood** is
stainless. Below its leading edge sits the light blade, and below that a **dark
grey fascia panel** raked back under the hood's overhang, finishing in the
black bumper. Rendering that panel as bright bare steel is what made the whole
front read as a featureless wall — a bright slab with nothing on it.

The front is now built in layers, which is how the real one is assembled:

```
y 0.805            hood leading edge, stainless, overhangs the fascia
y 0.735 .. 0.801   full-width light blade, sunk in its channel
y 0.420 .. 0.735   dark grey fascia, raked back 38 mm under the lip
y 0.185 .. 0.420   black bumper, projecting forward
```

**The light bar was switched off.** It existed in the mesh, but
`vehicleLights` defaulted to `false`, so it was drawn at 6% emissive — invisible.
On the real vehicle that blade is a daytime running light; it is on whenever the
car is awake. It now defaults on, spans essentially the full width instead of
87%, sits just behind the hood lip instead of 40 mm back in its recess, and
**wraps a short way around each front corner**, which is what makes it read as a
blade rather than a decal.

**There were no headlights at all.** Two driving-lamp clusters have been added
low in the bumper at roughly a quarter and three quarters of the width, each in
its own recess, matching the reference.

### 6.2 The forecourt

The exterior was a 30 m open plaza backed by a row of nine distant tower blocks.
Through a 13 m doorway that read as a bright empty field with a skyline floating
in it: the scale was wrong, and the buildings pulled the eye straight out of the
workshop.

* **Buildings removed.**
* **The four parked cars removed.** They filled the doorway with static clutter.
* **The space is narrowed and enclosed** — 20 m wide instead of 30 m, walled on
  both sides with piers, and a back wall behind the road. Everything the camera
  can see from inside is now within 20 m.
* **A through road with live traffic.** One vehicle loops across the opening,
  left to right. It is the same `drawCar()` as everything else, yawed 90°, and
  its wheel spin is derived from the distance covered — same rule as the
  serviced car, so it reads as driving rather than sliding. `PAGE UP` stops and
  starts it.
* **A sky backdrop.** With the towers gone there was nothing behind the wall but
  the clear colour, so the doorway framed a black rectangle while the apron in
  front of it was brightly sunlit. It is now a banded emissive backdrop graded
  from a warm pale horizon to a cooler zenith, with the gradient compressed into
  the few metres actually visible through the opening.

One subtlety worth recording: the sky was first drawn as `MAT_PAINT`, whose
worn-paint branch speckles the albedo with chips. Correct for a painted floor,
very wrong for a sky — it appeared as dirt scattered across the horizon. A
`MAT_SKY` with no procedural detail and no sampled texture fixed it.

---

## 7. v6.3 — proportions, grime, and light that stops at surfaces

### 7.1 Two measured proportion errors

Both were found by measuring `Reference/03_reference_side.png` against the
model rather than by eye.

**The hood and windscreen slopes were inverted.**

| | hood | windscreen |
|---|---|---|
| reference | 0.394 | 0.311 |
| project (before) | 0.319 | 0.366 |

On the real vehicle the hood is the *steeper* of the two. The project had it
the other way round, which left the cowl **144 mm too low** — a long shallow
hood with a tall pinched cabin sitting behind it. The cowl is now at
`y = 1.370, z = -1.330`, giving slopes of 0.395 and 0.292.

**The wheels were 18% oversize.** 35-inch tyres are 0.889 m across, so the
radius is 0.445. Measured off the reference they came out at 0.421. The project
was using **0.495**. Corrected to 0.450 / 0.455.

Dropping the tyre moved the hub, and every lug with it, 45 mm down, so all five
wheel-service poses were re-solved. The earlier solver only slid along the tool
axis; this one solves the full target, and lands the socket tip within **0.4 mm**
of the nut face on all five.

### 7.2 Dirt was geometry; it is now a surface

The grime was six flat boxes floating 4 mm off the door skin. From any angle
they read as exactly that — grey discs stuck on the car.

It is now shaded *on* the surface. Three things make dust and dried spray read
correctly, and the third is the one that matters most:

1. it collects **low** and thins as it climbs the panel, because the wheels
   throw it up off the road;
2. it is blotchy at the decimetre scale and **streaked vertically** at the
   centimetre scale, from rain running down through it;
3. it is a **dielectric**. A dirty metal panel stops behaving like metal where
   the film is thick — that is why grime kills a reflection rather than tinting
   it. Dropping the metallic term is what sells it. Without that step it looks
   like grey paint.

The mask is built in the vehicle's own frame, so the patch stays on the panel
when the car is raised or driven out.

### 7.3 The lamp shone through solid bodywork

The robot's inspection lamp had **no occlusion of any kind**. Its beam lit
whatever lay along the cone — straight through the vehicle, the lift, and
anything else in the way.

There is now a **second shadow map**, rendered from the lamp through a
perspective frustum matched to the cone angle, so the beam is stopped by the
first thing it actually hits. Verified by pointing the lamp at the near flank
and photographing the far side: no light reaches it.

### 7.4 Effects that were announcing themselves

The travelling highlight that swept the panel after polishing has been
**removed outright**. A bright bar crawling across a finished surface is not
something a polished panel does — it was a second scan line wearing a different
colour. The result of polishing is now the surface itself: the flank drops from
0.40 roughness to 0.16, and that reads without being announced.

The scanner beam stays, because a line-projecting laser scanner really does put
a bright line on what it is measuring.

### 7.5 The scan is now an inspection

Scanning went straight to the dirty panel, which gave away that the fault was
known in advance. The head now flies the **whole length of the vehicle** first —
nose, cowl, roof, bed — hovering 620 mm above the skin and looking straight down
at it, and only then drops to the flank where the contamination is.

Each station is an exact IK solution for "put the lens *here*, pitched *this* far
from vertical". All four are solved on the same elbow branch deliberately: both
elbow-up and elbow-down reach these points, but switching branch part-way
through swings the whole arm across the vehicle.

---

## 8. v9 — robot motion, tool rack and complete scanning

### 8.1 Three faults in the motion core, which compounded

**Angles were interpolated raw.** Moving the base from 170° to −170° is a 20°
step, but subtracting the two gives −340, so the arm span the long way round.
Every rotation now takes the **shortest signed path**, wrapped into ±180°.

**Each joint ran at its own fixed rate**, so whichever had the least to do
finished first and the rest carried on alone. The arm moved in stages —
straighten, then swing, then tilt. All five joints now share **one timeline**,
sized from whichever has furthest to travel, so the links fold and the wrist
turns together.

**Motion started and stopped abruptly**, at full speed from rest. The timeline
is **eased with a smoothstep**, so the arm accelerates out of one pose and
settles into the next. `speedScale` at each call site then sets the character:
fast for open travel, slow for a tool connection or a scanning pass.

### 8.2 The tool rack

The station carried a **2.55 × 1.20 m vertical backboard** behind the docks. It
was pure obstruction: it forced every tool change to be approached from the
side, which is what drove the long base rotations, and it put a wall exactly
where the wrist wanted to descend. **Removed.** The station is now an open table
the robot reaches straight down into.

**Tools were buried in the tabletop.** They were positioned by their geometric
centre, so the longer heads sat halfway inside it. Each tool now declares how
far its lowest part hangs below its own frame, and stands on its dock:

| tool | drop below frame |
|---|---|
| scanner (lens disc) | 0.770 |
| impact driver (socket tip) | 0.990 |
| gripper (finger pads) | 0.870 |
| polisher (pad + marker) | 0.772 |

**Tools were drawn at 0.62 scale on the rack and 1.0 on the robot**, so a head
changed size the instant it was picked up. Both are now 1.0, and the table was
widened to suit. Layout is a 2 × 2 grid with a dock pedestal under each.

### 8.3 The exchange is now performed, not swapped

The old code set `gRobotTool` to a new value and the head simply became a
different object in mid-air. The full exchange is now visible:

```
rack safe -> above the fitted tool's dock -> lower onto it -> pause
-> RELEASE (the head stays on the dock) -> bare wrist lifts
-> hold 0.55 s: bare connector on the robot, all four tools on the table
-> across to the next dock -> lower -> pause -> LATCH -> lift away
```

The dwell is the proof. Every dock pose is an analytic IK solution for "wrist
straight above this dock, tool axis vertical" — only possible because the
backboard is gone.

### 8.4 Six inspection regions

Scanning was one long interpolation along a single path: it covered very little
of the vehicle and risked sweeping the arm across it. The body is now divided
into **front/hood, windscreen, roof, upper side, lower side, rear side/bed**,
each entered and left through a staging pose. The lens is held **0.24 m off the
surface** throughout, so only the light reaches the paint. Coverage is reported
as a percentage in the status line.

### 8.5 Collision verification

A sweep replays every pose and every transition of the whole sequence against
the vehicle, both wheels, the lift, the table and the pedestal — testing link 1,
link 2 and the fitted tool, not just the head.

The first run reported a 0.9 m intrusion over the bonnet, which turned out to be
the *test's* fault: it modelled the car as an AABB, and a box over a wedge
reports all the empty space above the sloping hood as solid. With the real
silhouette, three genuine faults remained and were fixed:

* **top group → side group clipped the roof by 97 mm** — interpolating between
  "above the car" and "beside the car" cuts the corner. Now routed through an
  outboard waypoint at roof height, so the arm moves clear before it descends.
* **band-to-band traverses dragged the tool 78 mm through the door skin** —
  now stepped 0.35 m off the paint in between.
* **rack ↔ work traversals clipped a wheel by ~20 mm** — now routed through the
  same retract.

Final state: **no intrusion anywhere deeper than 3 mm**, which is the intended
contact of the sponge and polishing pad on their 12 mm stand-off, and **zero
lift or pedestal violations**.

### 8.6 Shorter routes

`SERVICE_CLEAN` used to swing out to yaw 180, back to 0, and only then approach
the panel — two full base rotations that achieved nothing. It now goes from the
rack to the panel through a single safe retract. The same applies to polishing.
