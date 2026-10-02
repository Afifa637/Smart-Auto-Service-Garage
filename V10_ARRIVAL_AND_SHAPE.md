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
