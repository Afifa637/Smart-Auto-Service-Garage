SMARTGARAGE - CYBERTRUCK + COLLISION-SAFE FINAL BUILD
=====================================================

V6 - CYBERTRUCK REALISM AND RENDERING UPGRADE (LATEST)
------------------------------------------------------
Full write-up, with measurements and known limitations:
    V6_REALISM_AND_RENDERING.md

Headline changes:

 RENDERING
 - Split-sum environment BRDF added.  The stainless body is metallic 0.97 and
   so has no diffuse response at all; without this term it received almost no
   environment energy and rendered as a near-black silhouette.
 - HDR environment probe, 128px RGB16F.  Ceiling strips sit above 1.0 so they
   survive tone mapping as real bright streaks on the panels.
 - Anisotropic GGX for brushed stainless, plus a clear-coat lobe for painted
   and composite surfaces, and multi-scatter energy compensation.
 - Real 4x MSAA.  The old build set GLFW_SAMPLES but rendered into a
   single-sample HDR target, so multisampling was never actually used.
 - Half-resolution bloom, 5x5 PCF shadows, ACES tone map, split-tone grade,
   vignette and fine sensor grain.

 THE VEHICLE
 - Body re-measured against Reference/03_reference_side.png.  The hood and
   windscreen now form one straight line (slopes 0.319 / 0.325) instead of a
   flat hood meeting a steep screen (0.09 / 0.42).
 - Roof apex, flat-roof length, body width and the sail panel all corrected.
 - Stamped fold ribs along every body line, so the folds catch a specular
   highlight instead of vanishing.
 - Wheel-arch cladding widened from 28 mm to 95 mm, seven-spoke aero cover,
   recessed light channels with real lenses, sloping vault cover.

 POLISHING NOW DOES SOMETHING
 - The side skin is its own mesh batch with its own surface finish, so the
   flanks really do drop from 0.40 roughness to 0.16 while the hood, roof and
   tailgate keep the factory finish.  Same mesh, two finishes, same frame.

 THE ROBOT
 - All 17 service poses re-solved against each tool's real working face.  The
   impact driver was reaching 211 mm INSIDE the wheel; the polishing pad was
   stopping 225 mm short of the panel.  Both now touch.
 - Arm shaded as matte powder coat.  It was being classified as brushed
   stainless, which gave it a mirror finish and a camouflage grain.
 - Scanner head parks level looking down instead of pitched -74 degrees.

 v6.1 - COLLISION AUDIT + THE TWO MISSING SYLLABUS TOPICS
 - A sweep that replays every service pose AND every transition found the tool
   passing through the bodywork three different ways:
     * the panel stand-off had the wrong SIGN, putting the tool 12 mm inside
       the skin (the robot works from the more-negative side of x = -1.030)
     * a wipe was one long interpolation; joint angles interpolate linearly but
       the tool tip swings through an ARC, which dipped 83 mm into the body at
       the midpoint of a sweep.  Each wipe is now three shorter segments and the
       worst point of any pass is 2.2 mm CLEAR of the panel.
     * manual control passed "allowVehicleServiceContact = true", which did not
       mean "may approach" - it meant the forearm and tool were not tested
       against the vehicle AT ALL.  There are now two volumes: an approach
       volume the tool may enter, and a vehicle SOLID that nothing may enter.
 - The collision test now uses the tool that is actually fitted (the impact
   driver is 1.15 m, not the 0.95 m stub the test assumed - which is exactly
   how it went through a wheel unnoticed).
 - PROJECTION: BACKSPACE switches perspective <-> orthographic.  The routine
   existed but was only ever used for the shadow map, where it is invisible.
 - TEXTURES: a real 4-layer GL_TEXTURE_2D_ARRAY (concrete, plaster, asphalt,
   steel tread plate) with mipmaps, GL_REPEAT and 8x anisotropic filtering,
   sampled by TRIPLANAR projection.  Before this there was no texture object,
   no UV and no mip chain anywhere in the material path - all four samplers in
   the program were the shadow map, the cubemap and the two post buffers.
   F6 now toggles the whole material system.
 - Also fixed: the polisher's rotation markers hung 48 mm below the pad in mid
   air, and the polish stage ran only two of its three authored passes.

 v6.2 - THE FRONT OF THE VEHICLE, AND THE WORLD OUTSIDE
 - THE FRONT is now built in layers like the real one, instead of one flat
   stainless slab:  stainless hood lip overhanging -> full-width light blade in
   its channel -> DARK GREY fascia raked back under the lip -> black bumper.
   Rendering the fascia as bare steel is what made the nose read as a blank
   wall.
 - The light blade was present but SWITCHED OFF (vehicleLights defaulted to
   false, so it drew at 6% emissive).  On the real vehicle it is a daytime
   running light: it is now on by default, spans the full width instead of 87%,
   sits just behind the hood lip, and wraps around each front corner.
 - Two driving lamps added low in the bumper.  There were none at all before.
 - OUTSIDE: the nine distant tower blocks and the four parked cars are gone.
   The forecourt is narrowed from 30 m to 20 m and walled on both sides, so the
   doorway frames a contained yard instead of an open field.
 - A through road now carries LIVE TRAFFIC across the opening - one vehicle
   looping left to right, wheels rolling at the rate the distance demands.
   PAGE UP stops/starts it.
 - A graded sky backdrop stands behind the wall, so the opening is daylight
   rather than a black rectangle.

 v6.3 - PROPORTIONS, GRIME, AND LIGHT THAT STOPS AT SURFACES
 - Two proportion errors found by MEASURING the side reference, not by eye:
     * the hood and windscreen slopes were INVERTED.  The real vehicle's hood
       is the steeper of the two (0.394 vs 0.311); the project had 0.319 vs
       0.366, leaving the cowl 144 mm too low - a long shallow hood with a
       tall pinched cabin behind it.  Corrected to 0.395 / 0.292.
     * the wheels were 18% OVERSIZE (R = 0.495 against a true 0.445 for
       35-inch tyres).  Corrected to 0.450.  That moved every lug 45 mm down,
       so all five wheel poses were re-solved - socket tip now lands within
       0.4 mm of the nut face.
 - DIRT is no longer geometry.  It was six flat boxes floating 4 mm off the
   door skin, which is exactly how it read.  It is now shaded ON the surface:
   collects low and thins with height, blotchy and vertically streaked, and
   crucially it DROPS THE METALLIC TERM - dust is a dielectric, so it kills the
   reflection underneath instead of tinting it.  Without that step it just
   looks like grey paint.
 - THE LAMP NO LONGER SHINES THROUGH THINGS.  The inspection light had no
   occlusion at all - its beam went straight through the vehicle and the lift.
   There is now a SECOND SHADOW MAP rendered from the lamp itself.
 - The travelling highlight after polishing is GONE.  A bright bar crawling
   across a finished panel is not something a polished surface does; it was a
   second scan line in a different colour.  The polish now shows as what it
   physically is - the flank drops from 0.40 roughness to 0.16.
 - THE SCAN IS NOW AN INSPECTION.  The head flies the whole length of the
   vehicle first - nose, cowl, roof, bed, hovering 620 mm above the skin and
   looking down - and only then drops to the flank where the dirt is.

 v9 - ROBOT MOTION, TOOL RACK AND COMPLETE SCANNING
 MOTION - three faults that compounded:
   * angles were interpolated RAW, so 170 deg -> -170 deg (a 20 deg step) was
     run the long way round as -340.  Every rotation now takes the SHORTEST
     signed path.
   * each joint ran at its own fixed rate, so the one with least to do finished
     first and the arm moved in stages.  All five joints now share ONE timeline.
   * motion started and stopped abruptly.  The timeline is now EASED with a
     smoothstep, and speedScale per call site sets fast travel / slow docking /
     slow scanning.
 TOOL RACK:
   * the 2.55 x 1.20 m vertical BACKBOARD is gone.  It forced every tool change
     to be approached from the side - which is what drove the long base
     rotations - and stood exactly where the wrist wanted to descend.
   * tools were positioned by their CENTRE, so the longer heads sat halfway
     inside the tabletop.  Each tool now declares how far its lowest part hangs
     below its frame and stands ON its dock.
   * tools were drawn at 0.62 scale on the rack and 1.0 on the robot, so a head
     changed size the moment it was picked up.  Both are 1.0 now.
   * 2 x 2 layout with a dock pedestal under each tool.
 TOOL CHANGE is now PERFORMED, not swapped: lower onto the dock, pause,
   release, lift the BARE wrist, hold 0.55 s with the bare connector and all
   four tools visible on the table, then across, down, latch and lift.
 SCANNING now covers SIX REGIONS - front/hood, windscreen, roof, upper side,
   lower side, rear side/bed - each entered and left through a staging pose,
   lens held 0.24 m off the surface, with a coverage percentage in the status.
 COLLISION: a sweep replays every pose AND every transition against the car,
   both wheels, the lift, the table and the pedestal, testing link 1, link 2
   and the fitted tool.  It found and fixed: the top->side move clipping the
   roof by 97 mm, band-to-band traverses dragging 78 mm through the door, and
   rack<->work moves clipping a wheel.  Final: nothing deeper than 3 mm
   anywhere, which is the intended sponge/pad contact.

 NEW MOTION AND INTERACTION
 - ;  work lights    '  vehicle lights    /  neon sign    END  hazard beacon
 - 9 / 0  fan speed (the fan previously never turned at all)
 - HOME   drive the vehicle out of the bay and back, wheels rolling at the
          rate the distance travelled demands
 - F12    hands-free showcase orbit for presenting    M  lighting presets
 - F      save a screenshot     V  geometry + render report
 - BACKSPACE  perspective <-> orthographic projection
 - F6     material textures on/off (sampled array + procedural detail)

PREVIOUS RELEASES
-----------------

AUTOMATIC SURFACE SERVICE UPDATE (LATEST)
-----------------------------------------
- The main Cybertruck remains on the existing lift; it does not drive in/out.
- ENTER now runs a simple predefined scan -> sponge pickup -> 3-pass clean ->
  rotary polish -> robot home -> lift down -> door open demonstration.
- R resets the complete automatic service and camera.
- No AI, computer vision, runtime IK or rigid-body physics was added.
- Added visible dirt removal, polish/reflection improvement, scanner sweep,
  warning/completion indicators, soft blue sponge and visibly rotating pad.
- Work-stool cushion is now rounded blue padded vinyl/foam.
- Exterior road is darker realistic asphalt with aggregate/patch/crack detail.

See AUTOMATIC_SERVICE_GUIDE.txt for the sequence and viva explanation.

CYBERTRUCK REALISM V3 (LATEST)
------------------------------
This ZIP includes the latest requested visual pass:
- roof apex moved rearward to the B-pillar region and windshield made longer/shallower
- side glazing made nearly flush with the stainless shell
- wheel-arch cladding made thinner, polygonal and less protruding
- triangular-prism side mirrors with reflective faces
- much subtler door seams/handles and micro fold highlights
- flatter near-black polygonal aero-cover wheels
- Cook-Torrance GGX metallic/roughness PBR as the default realistic shading path
- darker exposure, lower ambient/daylight/ceiling-light levels and restrained bloom
- darker reflection probe with horizon/ceiling/floor contrast so individual lights read clearly

See CYBERTRUCK_REALISM_V3_CHANGES.txt for the exact values changed.

THIS VERSION INCLUDES
---------------------
- Teaching/construction visualization modes retained.
- Full object set from the fuller SmartGarage object project retained.
- EVERY vehicle now uses the same Cybertruck-style angular pickup model.
- Cybertruck shell is generated programmatically from flat triangle/quad panels; no imported vehicle mesh is used.
- Wheels, mirrors, rockers, seams and service details remain primitive-built.
- Robot square pedestal is fixed to the floor.
- ONLY the circular top turntable and the arm above it yaw sideways.
- Shoulder and forearm links are extended for full serviced-Cybertruck reach, including the far/right side.
- Shoulder, elbow, wrist pitch and wrist roll remain manually controllable.
- Wrist/head pitch is a true 180-degree top-to-bottom sweep (-90 to +90 degrees).
- Scanner / driver / gripper / polisher quick-change tools remain available.
- Lift can stop at any safe intermediate height.
- Garage door can stop at any intermediate position.
- Collision/safety envelopes prevent robot/lift motion from passing through nearby machinery, the serviced Cybertruck, the floor, ceiling, robot pedestal and tool station.

BUILD
-----
1. Extract the ZIP completely.
2. Open YoutubeOpenGL.slnx in Visual Studio.
3. Select x64 + Debug (or Release).
4. Use Build > Rebuild Solution.
5. Run the newly built project. Do not launch an older EXE from another folder.

CAMERA
------
W/S            forward/back
A/D            left/right
SPACE/CTRL     up/down (E/Q also work)
SHIFT          faster movement
Mouse          look
Scroll         zoom
TAB            capture/release mouse
R              reset camera/current teaching view
ESC            quit

TEACHING MODES
--------------
1              full scene
2              isolated Cybertruck construction
3              isolated scissor lift construction
4              isolated robot construction
5              unit cube
6              28-segment cylinder
7              48-segment smooth cylinder
N/B            next/previous construction stage
L              wireframe toggle
V              print primitive vertex/index/triangle information

REALISTIC RENDERING CONTROLS
----------------------------
F5             Cook-Torrance PBR (per-fragment) / Gouraud (per-vertex) shading
F6             procedural albedo/roughness/AO material maps on/off
F7             procedural normal/bump maps on/off
F8             PCF directional shadows on/off
F9             cubemap environment reflections on/off
F10            HDR bloom on/off

Default = Cook-Torrance PBR + material maps + normal maps + shadows + reflections + restrained bloom.

CYBERTRUCK MODEL
----------------
Every call to Scene::drawCar() now renders the same Cybertruck-style model.
The different scene vehicles are therefore all Cybertrucks.

Key programmatically modelled features:
- near-vertical stainless front fascia
- single full-width front light bar
- black lower front bumper
- long straight lower body/chassis
- sharply faceted hood
- steep raked windshield
- high roof peak
- sloping rear glass/tonneau wedge
- straight side belt line
- flush handles
- compact angular mirrors
- vertical tailgate
- single full-width red rear light bar
- black lower rear bumper
- polygonal/hexagonal-looking wheel arch cladding
- four large detailed cylinder-based wheels

The model uses NO imported vehicle mesh. The large stainless body planes are
programmatically generated as flat-shaded triangles/quads so the fold lines catch
light correctly; the detailed wheels and accessories are built from the reusable
primitive library.

ROBOT - FIXED BASE + ROTATING TOP
---------------------------------
The following parts NEVER yaw:
- floor plate
- four anchor bolts
- square lower pedestal
- square/status section on the fixed pedestal

The circular top turntable is the yaw bearing. Everything mechanically mounted
above that bearing rotates with it.

T/G            rotate TOP circular turntable left/right
U/J            shoulder up/down
I/K            elbow up/down
Y/H            head tilt top/bottom through a full 180-degree sweep (-90..+90)
LEFT/RIGHT     wrist roll

The default robot starts parked sideways so the lift has a clear vertical path.
The arm links are lengthened to 4.50 + 4.00 scene units, giving enough radial
reach to move the wrist/tool across the complete central Cybertruck footprint,
including the right-hand side and front/rear corners.

SERVICE-CONTACT COLLISION RULE:
The heavy shoulder link is still blocked from entering the serviced Cybertruck.
During manual robot positioning, the forearm and wrist/tool are allowed into the
vehicle service envelope so the tool can reach the far/right body surface instead
of being stopped by the old broad-phase vehicle box. When the lift itself moves,
the entire robot becomes a hard obstacle again, so the vehicle cannot rise through
a parked service arm.

ROBOT QUICK-CHANGE TOOLS
------------------------
F1             scanner / inspection light
F2             impact driver / drill
F3             two-finger gripper
F4             rotary polisher

The selected head is shown on the wrist and its dock appears empty/active on
the side tool station.

SCISSOR LIFT
------------
P              automatic lower/raise
[              manually LOWER while held
]              manually RAISE while held
\              STOP at exact current height

Release [ or ] and the lift stays where it is.
The serviced Cybertruck remains parented to the platform.

COLLISION BEHAVIOUR:
If the robot is over the vehicle/lift and you try to raise the lift into it,
the lift stops before intersection. Move/swing the robot clear, then continue.
The collision-aware safe lift range is 0.0 to 3.8 units to preserve overhead
clearance for the taller Cybertruck.

GARAGE DOOR
-----------
O              automatic open/close
Z              manually CLOSE while held
X              manually OPEN while held
C              STOP at exact current position

Release Z/X and the door remains at that intermediate position.
Its movement is constrained to the existing guide/roll path and cannot travel
outside the valid 0.0..4.9 range.

COLLISION / SAFETY SYSTEM
-------------------------
This coursework project does not use a full rigid-body physics engine. Instead,
it now contains a conservative collision layer designed for the actual moving
objects in this garage:

- robot links are treated as thick line-segment/capsule safety envelopes
- machinery/vehicles are represented by conservative AABB safety envelopes
- every proposed robot joint movement is checked before it is committed
- the robot cannot drive through its fixed pedestal
- the robot cannot drive through the central lift/Cybertruck in full-scene mode
- the robot cannot drive through the side tool station
- nearby secondary lift structures are protected
- robot endpoints are restricted above the floor and below the ceiling
- lift movement is checked against the current robot pose
- invalid movement is rejected before visual intersection

When a requested movement is blocked, the window title shows:
  SAFETY STOP: collision prevented

In isolated teaching modes, invisible scene objects do not block the isolated
model; self/base and room safety rules still apply where relevant.

IMPORTANT
---------
Some primitive panels intentionally overlap inside one STATIC composite object.
That is construction geometry, not moving-object penetration. Collision safety
is applied to the moving mechanisms and their surrounding equipment.


REALISM V2 UPDATE
-----------------
- Cybertruck body rebuilt from many small rotated/scaled cube panels instead of large boxy slabs.
- Windshield is a tapered 7-strip sloped surface; hood is a 6-strip shallow wedge.
- Roof/tonneau uses 8 overlapping tapered panels to follow the Cybertruck rear wedge.
- Side windows use multiple narrow panels to create the polygonal glass outline.
- Wheel arches are now 7 short tangent segments per wheel (no X-shaped cladding).
- Wheels use dark aero-cover styling instead of exposed sports-car spokes.
- Added UV sphere primitive (24 stacks x 36 slices) for realistic rounded robot joints, wheel caps and extinguisher domes.
- Teaching mode: press 8 to isolate the UV sphere primitive.

============================================================
PHASE-1 CYBERTRUCK REALISM REBUILD
============================================================
This revision intentionally changes ONLY the Cybertruck geometry and the
matching car-teaching thresholds. Other garage objects/mechanics remain as in
REALISM_V2.

Key surface fix:
- Hood is now ONE continuous rotated plane, not six stair-step strips.
- Windshield is ONE continuous glass plane with overlapping seals/A-pillars.
- Roof is ONE near-flat plane.
- Rear tonneau is ONE long sloping plane with the correct downward rear slope.
- Lower side body is ONE uninterrupted stainless skin per side.
- Side glazing remains subdivided only where needed to trace the polygonal
  window silhouette; black pillars cover the strip boundaries.
- Wheel arches use 11 short tangent cladding segments per wheel, preventing
  the previous crossed/jagged geometry.
- Neighbouring outer parts overlap inward so cube end-faces are hidden.

This layout is specifically intended to give the later lighting pass large,
clean normals on the hood, glass, roof and rear wedge instead of a staircase of
separate lighting facets.

============================================================
FINAL CYBERTRUCK SHAPE / GAP FIX
============================================================
This final revision focuses on the reference-image issues reported after the
Phase-1 rebuild.

- Side glass moved inward so it sits inside the stainless body instead of
  floating outside the door skin.
- Added connected stainless shoulder, roof-side and rear sail geometry to close
  the old triangular side gaps completely.
- Replaced stepped side-window strips with two large masked glass panes per side.
- Removed the long floating belt-line strip; only genuine recessed shut-lines
  and small flush-handle indications remain.
- Hood angle reduced to a shallow ~3.2 degrees.
- Front fascia is almost vertical; corner turn-in reduced to ~6 degrees.
- Rear quarter and tailgate are bright stainless. Black/dark material is now
  limited to the tonneau top, glass, seals, arch cladding and low bumpers.
- Rear bumper height reduced so the tailgate no longer reads as a black rear.
- Wheel-arch tangent pieces are larger and overlap internally, eliminating
  detached/floating-looking arch segments.
- Roof width and side framing were increased/bridged so oblique camera views do
  not reveal holes between roof, glass and side body.

Build/run instructions and all garage/robot/lift controls are unchanged.

============================================================
FINAL REAL-CYBERTRUCK GEOMETRY FIX (2026-09-23)
============================================================
This build replaces the remaining toy-like / flat areas of the previous car.

BODY / PROPORTIONS
------------------
- Longer, wider, taller and heavier Cybertruck-like proportions.
- Broad continuous stainless side sheets, shallow hood, steep windshield,
  peaked roof, rear sail and sloping covered tonneau.
- Added folded under-window shoulder geometry so the glass is inset into the
  steel body instead of appearing pasted onto a flat side panel.

FIVE-FACET 3D FENDER FLARES
---------------------------
- The upper wheel arch is now EXACTLY five straight side-view segments.
- Each flare is generated as a sealed 3D extrusion using quads:
  outward ring face, inboard face, outer edge wall, inner edge wall and end caps.
- Flares physically protrude beyond the stainless side body.

BUMPER FIX
----------
- Rear bumper is no longer a flat strip.
- It is a full six-face 3D block that projects rearward and is wider than the
  tailgate. The front bumper is also a matching physical 3D block.

WHEELS / STANCE
---------------
- Larger radius and width.
- Wheel centres are pushed outward under the flares.
- NOTE: this project's WIDTH axis is X, so the requested outward offset is
  correctly applied on +/-X (not Z, which is vehicle length in this project).
- Added low-profile all-terrain tread blocks plus dark aero-cover wheels.

MATERIALS / LIGHTING
--------------------
- Shader upgraded from diffuse-only lighting to a compact material-aware
  diffuse + specular model.
- Stainless body uses high metallic response and moderate brushed roughness.
- Tinted glass uses a dark glossy response.
- Tyres are very rough; wheel metal/brake hardware use metallic highlights.
- Front full-width white light bar and rear full-width red light bar are emissive.
- Camera position is now supplied to the shader for view-dependent reflections.

SOURCE VALIDATION
-----------------
The C++ sources were syntax-checked together with:
  g++ -std=c++20 -I Libraries/include -fsyntax-only Main.cpp Shader.cpp Primitives.cpp SceneObjects.cpp
No C++ syntax errors were reported.

CLEAN BUILD
-----------
Old .vs, obj, pdb and other stale build-output folders were removed from this
ZIP deliberately. Open YoutubeOpenGL.slnx and use Build > Rebuild Solution so
Visual Studio compiles the corrected source rather than running an old binary.


REALISM PIPELINE
----------------
- 2048x2048 directional depth shadow map with 3x3 PCF filtering
- 8 representative ceiling point lights aligned to the visible fixture rows
- dynamic cyan robot/scanner spotlight calculated from the live arm pose
- self-contained cubemap reflection probe for stainless steel and tinted glass
- procedural material maps for concrete, wall paint, steel, galvanised metal,
  chrome, plastic, wood, cardboard, rubber, tyre, stainless, brake iron, asphalt,
  rust, painted metal, floor markings and carbon/composite surfaces
- procedural micro-normal/bump detail and roughness/AO variation
- HDR two-target render pass, Gaussian bloom, ACES-like tone mapping, gamma correction
- lightweight post anti-aliasing for the HDR framebuffer path
- Cook-Torrance PBR default with Gouraud retained for coursework comparison

WHY PROCEDURAL TEXTURES?
------------------------
The project deliberately generates its material texture information in GLSL instead
of depending on external JPG/PNG paths. This keeps the submitted ZIP self-contained
and still provides separate albedo, normal/bump, roughness and AO behaviour for all
major material families. The reflection probe is a real OpenGL cubemap generated at
startup, so stainless/glass reflections also require no external HDR asset.
