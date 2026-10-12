# v10 — the arrival sequence, the corrected shape, and the scene clean-up

Written for: the presenter, and anyone reading the diff.

This round did four things: it gave the vehicle a proper arrival, it corrected
the body profile against the reference photograph, it removed several objects
that read as floating debris, and it closed two real safety gaps in the robot
sequence.

---

## 1. The vehicle now arrives under its own power

The scene used to open with the vehicle already parked on the lift, and the
only vehicle motion was a drive-*out* at the end. It now opens with the
vehicle **outside on the apron, facing a closed door**, and `ENTER` runs:

```
door opens  →  pause  →  drives in  →  climbs the ramp  →  parks on the lift
            →  door closes  →  lift raises  →  the service as before
```

Two new states carry it: `SERVICE_DOOR_OPEN_IN` and `SERVICE_DRIVE_IN`.

**One parameter drives the whole thing.** `gSwitches.carDrive` runs 1 → 0, and
the scene derives from it: the vehicle's world position, its height on the
ramp, its nose-up attitude, and its wheel rotation. Nothing is animated
separately, so the vehicle cannot be at a height its position does not justify,
and the wheels cannot skid — the wheel angle comes from distance covered, not
from elapsed time.

## 2. The approach ramp

The lift platform rests **1.08 m** above the slab, so a vehicle has to climb to
reach it. `Scene::drawLiftApproachRamp()` builds a 4.60 m ramp at **13.2°**,
with guide kerbs, supporting legs, a toe plate and hazard banding.

Two details matter:

* **It is bolted to the floor, not to the platform.** The lift's own short
  drive-on wedges used to be parented to the platform, so once the lift was up
  they hung in mid-air a metre off the ground. Those are gone.
* **The vehicle's height comes from its AXLES, not its body centre.** A 5.68 m
  vehicle driving onto a 4.60 m ramp has one axle on the slope and one still on
  the slab for most of the climb. Positioning the body from its centre puts the
  leading tyres straight through the deck. Sampling the ramp surface under each
  axle gives the ride height (their mean) and the pitch (their difference over
  the wheelbase) for free, and the tyres stay on the ramp by construction.

## 3. The body profile, re-measured

The silhouette was re-measured off `Reference/03_reference_side.png` on a pixel
grid, using the roof apex (1.791 m) as the vertical scale. Three errors came
out of it, and between them they were why the vehicle read as an angular coupe:

| What | Was | Now | Why it mattered |
|---|---|---|---|
| Roof peak, as a fraction of length | 52 % | **40 %** | the peak sat at the midpoint, which lengthened the windscreen, shortened the rear and destroyed the forward-biased wedge |
| Nose top height | 0.805 m | **0.951 m** | the real front is blunt and tall; at 0.805 the model had a drooping snout |
| Rear | 30° fastback, then a level bed rail | **one continuous 9° line** apex → tailgate | the real vehicle's bed rail is not horizontal, it is part of the wedge |

The front is also **concave, not convex**: the hood rises at 14° and the
windscreen at 28°, so the cowl sits *below* the nose-to-apex chord. The model
had this inverted, which put a brow over the hood.

`CT_ROOF_R` is now placed **on** the apex→tail line, so the two rear segments
of `ctRoofLineY()` are collinear and the silhouette is a single straight run.

The two bright driving-lamp clusters low in the bumper are gone. The real
vehicle's entire front lighting is the single full-width blade, and at that
brightness the pair read unmistakably as a conventional car's headlights.

## 4. Objects removed

| Removed | Why |
|---|---|
| The passing car on the road outside | the main vehicle now arrives from the road; the second car was redundant and pulled attention off the arrival |
| Three ceiling air hoses on reels | they read as black tubes dangling in the middle of the room |
| Apron chevrons, hatching and six "repair patch" slabs | at 14–20 mm proud of the slab they read as loose metal plates lying on the concrete, and the vehicle now drives across that exact strip |
| The cyan scan line projected on the flank | it was pinned to the vehicle's local frame at a fixed height, so it stayed on the near flank while the scanner head was over the roof |
| The raised yellow index bars on the turntable | they stood 12 mm proud, and a tyre now drives across that plate |

### The curve geometry moved rather than going away

The ceiling hoses were the project's only **curve-based geometry** — a tube
swept along a cubic Bézier. Removing them outright would have dropped a
required concept, so the same sweep now builds the **lift's hydraulic hoses**
at floor level, running from a power pack to the lift's base rail. A hose is
the honest place for a curve: fixed at both ends, free in between, so it falls
into exactly the shape a Bézier describes. See `Scene::drawLiftHydraulicHose`.

## 5. The turntable

Two bugs, one visible from outside the building:

* **It was being translated with the vehicle.** `driveZ` was applied to the
  plate as well as to the car, so when the vehicle drove out, a 5.6 m steel
  disc floated out onto the apron underneath it. The plate belongs to the lift;
  `driveZ` no longer reaches it.
* **It was 5.6 m across**, overhanging the 2.96 m lift by well over a metre on
  each side with nothing underneath. It is now 4.20 m — enough to carry the
  3.46 m wheelbase and no more — and it has a skirt beneath it, so it reads as
  a thick rotating table rather than a disc hanging in mid-air.

## 6. Platform rotation safety

The turntable turns the vehicle through 180°, and during the turn the body does
not occupy its parked footprint. It sweeps a **cylinder** about the lift axis:

```
R_sweep = sqrt(2.84² + 1.03²) = 3.02 m
```

That is 1.99 m more than the half-width, so the corners reach two metres
further toward the robot than the parked vehicle does. A pose comfortably clear
of the stationary vehicle can be struck by its own corner as it comes round.

`POSE_PLATFORM_SAFE = { -6.00, 14.00, -150.00, 230.00, 0.0 }` folds the elbow
to its −150° mechanical stop and stands the forearm up over the arm's own base,
leaving every link at least **0.51 m** outside that cylinder. The service
machine moves there before every turntable command, at all four rotation sites.

The base yaw is deliberately left where the scanning poses leave it: with the
arm folded over its own base the yaw barely changes the clearance (0.496 m at
0°, 0.513 m at −6.66°), so spinning the base would be a large, slow, visible
motion in exchange for 17 mm.

## 7. New command-line switches

Added so the figures in the report and the slides are rendered by the project
itself rather than mocked up:

```
--cardrive F     freeze the arrival part-way (1 = outside, 0 = on the lift)
--reelstart N    start the recorded reel at frame N, simulating the rest silently
--gouraud        capture with per-vertex shading
--notex          capture with textures disabled
```

`--reelstart` is what makes the demonstration video recordable in chunks: the
simulation is advanced for every earlier frame without drawing anything, so a
run can resume part-way through with the service in exactly the state
continuous playback would have produced.

## 8. Recording the video

`Video/make_video.py` records the scripted two-minute reel and encodes it.

```
python Video/make_video.py
```

It launches the renderer, consumes each BMP as soon as its successor appears
(which proves the file is complete), writes it to the video and deletes it, so
peak disk use stays under a gigabyte instead of the 8 GB the raw frames would
need. It records in short chunks and relaunches automatically if a run ends
early. Two files come out: the 1280×720 master and a 960×540 copy for anywhere
with an upload limit.

H.264 is not available on this machine — the OpenH264 runtime DLL is absent and
OpenCV has no other H.264 backend — so the container is MPEG-4 Part 2. It plays
in every desktop player and in PowerPoint, but it is roughly twice the size an
H.264 file of the same quality would be.

## 9. v10.1 — the raised yellow lip, and more headroom during rotation

Two further defects, found by watching the platform actually turn with the
vehicle on it:

### The runway's raised lip was inside the turntable's rotation footprint

The runway pads either side of the turntable carried a raised yellow lip
along their outer edge ("so a wheel cannot roll off"), 90 mm proud of the
surface, running the full 5.2 m length of the pad. The turntable's rotation
footprint (radius 2.10 m) overlaps those pads in plan view — it has to, since
the vehicle's wheels have to be standing on them before the plate turns — so
the lip stood as a **fixed raised obstacle inside the area the vehicle sweeps
through**. A wheel riding the turntable around would pass directly over it,
which read exactly like what it was: a yellow panel colliding with the wheel.
It is removed; the thin flush chrome trim along the inner edge is kept, since
it never stood proud of the surface.

### More headroom for the arm during rotation

`POSE_PLATFORM_SAFE` previously folded the arm back over its own base for
**0.51 m** of clearance to the vehicle's 3.02 m rotation-sweep cylinder — enough
by the strict either/or test, but visually close in some camera angles, and
not what "move it out of the way" means to a human watching it work.

The pose now also **raises** the tool tip, to y = 2.53 m — clear *over* the
vehicle's roofline rather than merely clear of it sideways — while the tip
stays only 1.68 m out from the arm's own base, short of the tool rack:

```
POSE_PLATFORM_SAFE = { -6.66, -10.00, -150.00, 190.00, 0.0 }   // was { -6.00, 14.00, -150.00, 230.00, 0.0 }
```

Clearance to the sweep cylinder is now **1.58 m**, roughly three times the
previous margin, verified both at the three sampled rotation angles (0°, 90°,
180°) and through the fold-up transition that reaches it from the end of the
rear-scan path. Both are a direct render of the project's own pose, not a mock.

## 10. v10.2 — the turntable colour, and the oversized front fascia

### Turntable: blue, not yellow

The rotating disc's top surface and its eight index marks were both in the
same warm yellow as the fixed runway pads either side of it. With both
surfaces reading as "the floor", the disc turning was easy to miss - nothing
about its colour said "this is a separate machine". The disc top is now the
lift's own blue (matching the scissor stack and base rails: blue = moving
machinery), and the index marks are white, which reads against both the blue
disc and the yellow runway. Nothing about the geometry changed, only the
colours.

### The front fascia was most of the front, not a bumper

Measuring it against the reference: the dark zone ran from y 0.420 to
y 0.881 - **46 cm of a 95 cm front face**, about three-quarters of the panel.
On the real vehicle the black is confined to roughly the bottom third; above
it the stainless shell continues almost to the apron.

Most of what was labelled "fascia" is now stainless, on the same raked plane
it always was (so the outer surface position is unchanged - this cannot
affect the collision model or the calibrated robot poses, which are set from
the body's outer bounds, not from where a colour boundary falls inside it).
What remains dark is a thin transition seam (`y 0.420 .. 0.470`, 5 cm) right
above the physical black bumper block, which is unchanged.

```
BEFORE                          AFTER
y 0.947 .. 0.951  hood lip      y 0.947 .. 0.951  hood lip
y 0.881 .. 0.947  light blade   y 0.881 .. 0.947  light blade  (also thinned: 66mm -> 48mm)
y 0.420 .. 0.881  dark fascia   y 0.470 .. 0.881  STAINLESS lower nose  (new)
                                 y 0.420 .. 0.470  dark transition seam (5 cm, was 46 cm)
y 0.185 .. 0.420  black bumper  y 0.185 .. 0.420  black bumper  (unchanged)
```

### The mirrors had been left behind by the v10 profile correction

`CT_DLO_FRONT_Z` (the front corner of the side glass, which is what v10
measured and moved forward by 230 mm) is read by the window and pillar code,
but the mirrors were still three hardcoded z coordinates from before that
move - so after v10 they sat 230 mm aft of the actual A-pillar, floating over
the fender rather than at the base of the front window. They are now
positioned as offsets from `CT_DLO_FRONT_Z`, so a future change to the
greenhouse geometry carries the mirrors with it instead of leaving them behind
a second time.

## 11. v10.3 — the actual turntable collision, and a genuine colour fix

Both of the previous round's fixes were real but incomplete, and the
screenshots that followed showed exactly why.

### The arm really was clipping the vehicle - at the far flank, not the near one

`POSE_PLATFORM_SAFE` was verified by sweeping it against the vehicle's
rotation-sweep cylinder at several yaws and declared clear with 1.58 m to
spare. That check was run once, at the pose's own destination, and it missed
the thing that actually mattered: the **transition into** that pose starts
from the end of the rear-scan path, and that path is reused identically for
both the near flank and the far flank, because the whole point of the
turntable is that both passes use the same robot poses - the vehicle turns,
not the robot. After the 180-degree turn, the world location that held the
vehicle's TAIL on the first pass holds its NOSE on the second, and the nose
and tail are not the same shape (the bumper, the light bar and the mirrors
all project differently). A transition proven clear of the tail was never
proven clear of the nose, and it wasn't: the robot's scanner head swept
through the front bumper in exactly the pose the screenshots showed, at the
real service lift height of 1.10 m (the earlier verification had used 2.6 m,
which raised the car enough to hide the error).

The fix does not depend on knowing which end of the car is where. Each of the
four transitions into `POSE_PLATFORM_SAFE` is now two stages
(`moveToPlatformSafe()` in Main.cpp):

1. **Fold in place.** The elbow goes to its -150 degree stop and the wrist
   raises, at WHATEVER YAW THE ARM ALREADY HAS - no sideways sweep at all,
   only retraction toward the arm's own base.
2. **Sweep.** Only once the arm is already folded and elevated does the yaw
   turn, from wherever it is, to the final -6.66 degrees.

An offline sweep of the fold shape confirms it holds the same 1.58 m
clearance to the vehicle's full rotation envelope at every yaw from -39 to
+60 degrees - it is yaw-independent, because folded this tight it is governed
by its distance from the arm's own base, not by which way the base is
pointing. That is what makes the fix correct regardless of which end of the
vehicle the previous pose left the arm near.

### The turntable really was flickering yellow and blue - z-fighting, not a leftover colour

The disc's top surface and the fixed runway pads it overlaps were both built
from the same `liftTopY()`, so wherever they overlap in plan view they sat at
EXACTLY the same height - a textbook z-fighting setup, invisible while both
were the same yellow and suddenly very visible as a shifting wedge pattern
once the disc became blue and the GPU had to pick a winner between two
coplanar surfaces on a per-pixel basis. Recolouring was necessary but not
sufficient; the actual depth tie needed resolving. The disc top is now 6 mm
above the runway, which lets it fully occlude the yellow pad wherever the two
overlap - the whole visible turntable area reads as one solid blue disc, with
no flicker at any camera angle.

Both fixes were verified against the actual failure mode rather than a
plausible-looking replacement: the fold-up sweep was checked at the real
1.10 m service lift height, at the exact far-flank yaw the reused scan path
leaves the arm at, and the full automatic sequence was run to SERVICE
COMPLETE to confirm all four platform rotations complete without a stall.

## 12. v10.4 — the arm really goes up, and the lower body is rebuilt

### Why the previous two collision fixes did not work

Both were verified with an offline arm model that did not match the game. The
shoulder tilt direction was mirrored (`rotateZ(+a)` tilts +Y toward **-X** in
Math3D.h, and the model assumed +X), and the tool was pointed along the wrong
axis (the tool hangs along the wrist frame's **-Y**). The pose that model
called "1.58 m clear" actually pointed the forearm down beside the car with
the tool near the lift: re-checked with the real kinematics it sits **0.44 m
inside** the vehicle's rotation volume.

The checker is now a line-for-line port of `robotSafetyPoints()` /
`yawRobotPoint()` in Main.cpp, against the vehicle's master profile at the
real 1.10 m service lift height. It is calibrated against a known case: a
scan pose at its designed stand-off reads exactly 0.000.

### The new platform pose: mast up, boom out

`POSE_PLATFORM_UP = { 180, 0, -90, 180 }` stands the shoulder link vertical
over its base and lays the forearm flat at 5.55 m, pointing away from the car
over the tool rack, tool held level along it. The two links total 8.5 m in a
7 m room, so "fully up" is a vertical mast with a horizontal boom.

It is reached in three moves and left in three moves:

| Stage | Into the pose | Out of the pose |
|---|---|---|
| 1 | pull back: shoulder leans 25 deg away, drawing the tool off the panel | swing to the target's yaw while still high |
| 2 | raise: mast vertical, boom level, at the same yaw | lower into the working shape, still leaning back |
| 3 | swing the base to 180 deg | lean in to the panel |

Verified clearances (car / fixed scenery):

- during the full 0-180 degree turn: **1.33 m / 0.91 m**
- pull-back: rises monotonically from the contact point
- raise: never below 0.71 m; swing: never below 1.51 m
- descent: never below 1.06 m until the final approach to the stand-off
- raised pose to the tool rack: 3.33 m

### The lower body: one continuous black band

The reference has a single black band wrapping the bottom of the truck - front
fascia, sill, rear bumper - with the arch trims running down into it, and the
stainless sitting on top. The model had instead:

- arch trims whose legs stopped at hub height, reading as rainbows over the wheels
- a black rocker bar at 0.40-0.55 m with daylight under it
- two 12 cm-deep bumper boxes hanging under the nose and tail

Rebuilt as a faceted, recessed wrap-around fascia and rear bumper, a chamfered
sill at 0.22-0.36 m, and arch trims extended down to the band (and slimmed to
the reference's ~9.5 cm). The stainless overlaps the top of the black by 2 cm
so it reads as tucked under. Heights come from the ramp: chin 0.28 m and tail
0.30 m clear the 13.2-degree approach and departure (the old boxes at 0.185
and 0.175 m dug into the ramp and floor); sill 0.22 m clears the crest.

### Side glass at the reference's proportions

| Station | Reference | Was | Now |
|---|---|---|---|
| glass front | 23.7 % | 25 % | at the cowl (22.3 %) |
| B-pillar | 45.1 % | 39 % | 45.6 % |
| glass rear | 63.0 % | 59 % | 63.0 % |
| rear edge lean | ~10 deg | ~43 deg | ~6 deg |

The front window's top now follows the roof rail from the cowl over the apex
to the B-pillar; it was a triangle cut by a straight diagonal, leaving a
stainless wedge where most of the window should be.

### Scan poses re-solved

The v10 profile correction (hood +146 mm, apex forward) had left the front and
windscreen scanner lenses 0.12-0.16 m off the surface, and two rear-quarter
poses had always sat 35 mm off the top of the rear tyre. All four were backed
off along their own viewing axes to the 0.22 m stand-off with their aim kept.
Every service pose's arm links now stay at least 0.475 m from the body.

## 13. v10.5 — raised in place, self-collision, wheel wells

**The arm rises where it is.** The platform pose no longer swings the base
round to face away from the vehicle; that was a long rotation that achieved
nothing. The arm pulls back off the panel and raises at the yaw it was already
working at: mast vertical, boom level at 5.55 m over the vehicle, which turns
beneath it. Verified from both entry yaws (-39 and -16 degrees) at every turn
angle from 0 to 180: 1.33 m above the body's highest point, 0.91 m from the
ceiling fixtures and the rest of the scenery. Going from this pose to the tool
rack is staged (swing high, then lower): done as one move, the 135-degree
swing combined with the drop cut through the ceiling fixtures.

**Self-collision.** `robotPoseSafe()` - which gates the manual arm keys and
the lift interlock - now rejects:

- a wrist pitch within 50 degrees of 0 (mod 360). The tool hangs along the
  wrist frame's -Y, which is the direction the forearm runs back toward the
  elbow, so tilting the head toward 0 folds it into its own forearm. Every
  scripted pose uses at least 60.
- the tool segment coming within 0.32 m of the upper arm.

It also now knows about scenery it previously ignored: the turntable disc
(radius 2.10 m, wider than the 1.62 m lift box it was checked against), the
approach ramp, the hydraulic power pack and the ceiling fixtures. HOME and
every other resting pose were checked against the new rules, so the lift
interlock cannot stall the service.

**Wheel wells.** Inside each arch, behind the tyre, the bright stainless skin
showed through the gap. Each arch now has a matte near-black liner filling
its inner outline, so the opening reads as a dark cavity.
