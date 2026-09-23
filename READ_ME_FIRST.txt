SMARTGARAGE - CYBERTRUCK + COLLISION-SAFE FINAL BUILD
=====================================================

THIS VERSION INCLUDES
---------------------
- Teaching/construction visualization modes retained.
- Full object set from the fuller SmartGarage object project retained.
- EVERY vehicle now uses the same Cybertruck-style angular pickup model.
- Cybertruck body is still built only from the existing cube/cylinder primitives.
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

CYBERTRUCK MODEL
----------------
Every call to Scene::drawCar() now renders the same Cybertruck-style model.
The different scene vehicles are therefore all Cybertrucks.

Key primitive-built features:
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

The model uses NO imported vehicle mesh.

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
