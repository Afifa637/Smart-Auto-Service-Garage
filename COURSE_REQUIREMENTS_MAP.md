# Course requirements → where each one lives in this project

Written for: the presenter, to prepare slides, the report and the viva.

Every row names the **key to press** so a claim can be proved on screen rather
than asserted. Press `V` at any time for a full render report in the console.

---

## 1. Graphics concepts the marking scheme asks for

| Required | Where it is | Prove it with |
|---|---|---|
| **Cube** | `Primitives::drawCubeMesh` — one 24-vertex cube reused for walls, panels, racks, shut lines | `5`, then `L` for wireframe |
| **Cylinder** | Two detail levels, 28- and 48-segment | `6` and `7` |
| **Sphere** | UV sphere, used for joints, bulbs, the sponge | `8` |
| **General 3D object** | The Cybertruck shell — a custom-built triangle mesh, not a primitive | `2`, then `N`/`B` to step through its 10 build stages |
| **Curved objects** | Cubic **Bézier**-swept air hoses on the ceiling reels | look up at the reels; `L` shows the swept tube segments |
| **Textures** | 4-layer `GL_TEXTURE_2D_ARRAY`, triplanar, mipmapped, 8× anisotropic | `F6` toggles it — watch the floor and the lift runways |
| **Shading** | Cook-Torrance GGX vs per-vertex Gouraud, same scene | `F5` |
| **Illumination** | Split-sum image-based lighting from an HDR probe | `F9`, and `,` / `.` to scale it |
| **Projection** | Perspective ↔ orthographic | `BACKSPACE` — runway edges converge, then go parallel |
| **Motion** | Robot arm, lift, door, fan, turntable, passing traffic, drive-out | `ENTER`, `P`, `O`, `9`/`0`, `HOME` |
| **Interaction** | Full keyboard control of camera, arm, lights, tools | `TAB`, `WASD`, `T`/`G`, `U`/`J`, `I`/`K` |
| **Collision** | Arm and lift refuse moves that would intersect the car | drive the arm into the car with `U`/`J` — it stops |
| **Dynamic objects** | Dirt that is cleaned off, a finish that is polished, lights that switch | `ENTER` runs the whole thing |
| **Complex motion** | The 24-phase inspection sequence with coordinated 5-joint IK | `ENTER` |

## 2. Lighting — three types, as required

| Type | Count | Notes |
|---|---|---|
| **Directional** | 1 | the daylight key, casts the 5×5 PCF shadow map |
| **Point** | 8 | ceiling fixtures; `;` switches them off and the room really goes dark |
| **Spot** | 1 | the robot's inspection lamp — **moving**, because its position and direction come from the scanner head's transform, and **shadowed** by its own depth map |

`M` cycles three complete lighting rigs: Workshop, Studio (a three-point product
rig), and Night.

## 3. What was added specifically for the marking scheme

* **Bézier curve geometry.** Everything else in the project is a transformed
  cube, cylinder or sphere. The air hoses are the one place where geometry
  follows an *equation*:

  ```
  B(t)  = (1-t)³P₀ + 3(1-t)²t P₁ + 3(1-t)t² P₂ + t³P₃
  B'(t) = 3(1-t)²(P₁-P₀) + 6(1-t)t(P₂-P₁) + 3t²(P₃-P₂)
  ```

  The derivative is the tangent, which is what orients each tube segment. Two
  Euler angles suffice to aim a cylinder because it is rotationally symmetric
  about its own axis — the roll is not observable.

* **An alignment turntable.** The arm is bolted beside the vehicle, so no
  amount of reach lets it aim *inward* at the far flank — a shoulder-mounted arm
  can only ever point away from its own base. The lift therefore carries a
  turntable, and the vehicle is turned to present each flank in turn. Both
  flank passes reuse exactly the same robot poses; the vehicle moves, not the
  robot.

## 4. Deliverables still outstanding

These are **not code** — they are things to produce from the project:

1. **~10 slides**, visual-heavy: title, project idea, the graphics-concept map
   above, screenshots with callouts, extra work, thank-you.
2. **2-minute video** showing motion, interaction, collision and the effects.
3. **10–15 page report** with the mathematics. The equations already written up
   in the source and in `V6_REALISM_AND_RENDERING.md` cover: the Bézier curve
   and its derivative, the sphere and cylinder parameterisations, the
   transformation chain `translate · rotate · scale`, the perspective and
   orthographic projection matrices, Cook-Torrance GGX with the split-sum
   approximation, the shortest-angle interpolation used by the robot, and the
   analytic two-link inverse kinematics.

Ray tracing is deliberately **not** implemented.

## 5. Honest limitations, worth stating rather than hiding

* The arm cannot reach the vehicle's front and rear *vertical* faces from the
  side: at 90° those faces sit about 1.5 m from the base, closer than the
  elbow limit allows. They are inspected from above instead, in the front/hood
  and rear/bed regions.
* The wheelbase is 3.46 m against the real 3.63 m. The axles were left where
  they are because seventeen robot poses are calibrated against them.
* Reflections come from a static environment probe, so they do not update when
  the garage door opens or the lights are switched off.
