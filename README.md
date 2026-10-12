<div align="center">

# 🚗 SmartGarage

### A fully automatic vehicle inspection & service bay, hand-built in modern OpenGL

*A car rolls in, a robot arm scans, scrubs, polishes and tightens its wheels, and then it drives away shining.*
*No imported models: every object is built in code from a cube, two cylinders and a sphere (plus one hand-made mesh for the car body).*

<br>

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![OpenGL 3.3](https://img.shields.io/badge/OpenGL-3.3%20Core-5586A4?style=for-the-badge&logo=opengl&logoColor=white)
![GLSL](https://img.shields.io/badge/GLSL-330-1B8BA3?style=for-the-badge)
![GLFW](https://img.shields.io/badge/GLFW-+%20GLAD-D9A525?style=for-the-badge)
![Windows](https://img.shields.io/badge/Windows-Visual%20Studio%202026-5C2D91?style=for-the-badge&logo=visualstudio&logoColor=white)

<br>

<a href="media/SmartGarage_Demo_720p.mp4">
  <img src="media/hero.gif" alt="SmartGarage highlight reel: arrival, scanning, ray-traced monitor, polishing, wheel service" width="100%">
</a>

**[▶ Watch the full 2:40 demo video](media/SmartGarage_Demo_720p.mp4)** &nbsp;·&nbsp;
**[📄 Read the report](Report/SmartGarage_Report.pdf)** &nbsp;·&nbsp;
**[🎞 Open the slides](Report/SmartGarage_Showcase.pptx)**

</div>

---

## ✨ What is this?

**SmartGarage** is a real-time 3D simulation of a smart car-service garage, written from scratch in **C++20 and OpenGL 3.3 (core profile)**.
Press **Enter** and you watch an 18-step service: the roller door opens, a Cybertruck-style vehicle drives up a ramp onto a hydraulic scissor lift, the door closes, the lift rises, and a **five-joint robotic arm** takes over.
It changes its own tools, scans the body with a moving spotlight, scrubs off the grime, polishes the paint to a mirror finish and tightens every wheel nut in star order.
Then everything reverses and the clean car drives back out.

Next to the lift, an **inspection monitor shows a live, genuinely ray-traced picture** of the car's underside (with shadow rays), rendered in a fragment shader every frame.

> Built for **CSE 4102: Computer Graphics and Image Processing Laboratory** at **Khulna University of Engineering & Technology (KUET)**.
> All **21 / 21** items of the project proposal are implemented, plus a ray-traced bonus.

<div align="center">

| 🔁 Service states | ⏱ One full cycle | 🦾 Robot joints | 🧰 Swappable tools | 🎨 Shading models | 🔦 Ray-traced items |
|:---:|:---:|:---:|:---:|:---:|:---:|
| **18** | **197.7 s** | **5** | **4** | **3** (PBR · Gouraud · Phong) | **793** |

</div>

---

## 🎬 The service, start to finish

```mermaid
flowchart LR
    A([ENTER]) --> B[Door opens] --> C[Drive in<br/>+ ramp] --> D[Door closes] --> E[Lift up]
    E --> F[🔍 Scan<br/>24 poses] --> G[🧽 Get sponge] --> H[Clean] --> I[Return sponge]
    I --> J[✨ Get polisher] --> K[Polish<br/>both flanks] --> L[🔧 Get driver] --> M[Tighten wheels<br/>star order]
    M --> N[Return driver] --> O[Robot home] --> P[Lift down] --> Q[Door opens] --> R[Drive out] --> S([✅ COMPLETE])
```

<table>
  <tr>
    <td width="33%"><img src="Report/images/garage/exterior_arrival.jpg" alt="Vehicle arriving at the garage"><br><sub><b>1 · Arrival.</b> The roller door winds open for the waiting car.</sub></td>
    <td width="33%"><img src="Report/images/service/ramp_climb.jpg" alt="Vehicle climbing the ramp"><br><sub><b>2 · Drive in.</b> Wheels follow the ramp slope and roll without slipping.</sub></td>
    <td width="33%"><img src="Report/images/service/scan_front.jpg" alt="Robot scanning the car"><br><sub><b>3 · Scan.</b> A moving spotlight sweeps 6 regions while the turntable spins.</sub></td>
  </tr>
  <tr>
    <td><img src="Report/images/service/cleaning_dirt.jpg" alt="Robot cleaning the car"><br><sub><b>4 · Clean.</b> Grime vanishes band by band, in step with the sponge.</sub></td>
    <td><img src="Report/images/service/polishing.jpg" alt="Robot polishing the car"><br><sub><b>5 · Polish.</b> Six passes turn brushed steel into a mirror.</sub></td>
    <td><img src="Report/images/service/wheel_service.jpg" alt="Robot tightening wheel nuts"><br><sub><b>6 · Wheel service.</b> Lug nuts tightened in 1-3-5-2-4 star order.</sub></td>
  </tr>
</table>

---

## 🦾 A robot that changes its own tools

The arm is a real **transformation hierarchy**: base → turntable → shoulder → elbow → wrist → tool, where each part's matrix is `M_child = M_parent · M_local`.
It swings over to the tool table, releases its current head, grips the next one and locks it on through a visible 10-phase exchange. Every move is collision-checked against the car, the lift and the furniture.

<table>
  <tr>
    <td width="33%"><img src="Report/images/service/tool_exchange_a_release.jpg" alt="Robot releasing a tool"><br><sub><b>Release.</b> The old head is docked.</sub></td>
    <td width="33%"><img src="Report/images/service/tool_exchange_b_bare.jpg" alt="Bare robot wrist"><br><sub><b>Bare wrist.</b> Moving to the next dock.</sub></td>
    <td width="33%"><img src="Report/images/service/tool_exchange_c_grasp.jpg" alt="Robot grasping a new tool"><br><sub><b>Grasp.</b> The new head is locked on.</sub></td>
  </tr>
</table>

<div align="center">
  <img src="Report/images/orthographic/robot_vars.png" alt="Robot arm joint variables" width="70%"><br>
  <sub>Every joint is a variable you can drive by hand: <code>T/G</code> base · <code>U/J</code> shoulder · <code>I/K</code> elbow · <code>Y/H</code> wrist · <code>F1–F4</code> tools.</sub>
</div>

---

## ✨ Dirty → clean → mirror

Polishing isn't a texture swap. It changes the surface **roughness** (`r′ = mix(r, 0.34r + 0.03, p)`), so the highlight tightens from a wide glow into a sharp reflection of the ceiling lights.

<table>
  <tr>
    <td width="33%"><img src="Report/images/polish/p1_dirty.jpg" alt="Dirty car"><br><sub><b>Dirty.</b> Matte, grimy, rough.</sub></td>
    <td width="33%"><img src="Report/images/polish/p3_clean.jpg" alt="Clean car"><br><sub><b>Clean.</b> Grime gone, still brushed.</sub></td>
    <td width="33%"><img src="Report/images/polish/p5_polished.jpg" alt="Polished car"><br><sub><b>Polished.</b> Crisp, mirror-like glints.</sub></td>
  </tr>
</table>

---

## 🎨 Three shading models, one key (`F5`)

<table>
  <tr>
    <td width="33%"><img src="Report/images/shading/bay_gouraud.jpg" alt="Gouraud shading"><br><sub><b>Gouraud.</b> Lighting per vertex, colour interpolated.</sub></td>
    <td width="33%"><img src="Report/images/shading/bay_phong.jpg" alt="Phong shading"><br><sub><b>Phong.</b> Normal interpolated, lighting per pixel.</sub></td>
    <td width="33%"><img src="Report/images/shading/bay_pbr.jpg" alt="PBR shading"><br><sub><b>PBR (default).</b> Cook–Torrance GGX + image-based light.</sub></td>
  </tr>
</table>

---

## 💡 Lighting that you can play with

Directional sun/moon, 8 ceiling point lights, 2 headlights and a **moving spotlight on the robot's tool tip**, with PCF soft shadows, HDR, bloom and MSAA.

<table>
  <tr>
    <td width="33%"><img src="Report/images/lighting/preset_studio.jpg" alt="Studio lighting preset"><br><sub><b>Studio preset</b> (<code>M</code>)</sub></td>
    <td width="33%"><img src="Report/images/lighting/preset_night.jpg" alt="Night lighting preset"><br><sub><b>Night</b> (<code>0</code>): dark sky, dim moon</sub></td>
    <td width="33%"><img src="Report/images/lighting/spotlight_night.jpg" alt="Robot spotlight at night"><br><sub><b>Scanner spotlight</b>: 14° / 23° cone</sub></td>
  </tr>
</table>

---

## 🔦 Bonus: a real ray tracer inside the garage

The monitor beside the lift is **not a screenshot**. Every frame, a fragment shader fires one primary ray and one shadow ray per pixel through **793 scene items** (with bounding-sphere culling), using the same transforms as the rasterised garage, so the monitor stays live and in sync.

<table>
  <tr>
    <td width="40%"><img src="Report/images/raytrace/room.jpg" alt="Inspection monitor in the garage"><br><sub>The monitor stands beside the lift.</sub></td>
    <td width="30%"><img src="Report/images/raytrace/on.jpg" alt="Ray-traced view with shadows"><br><sub>Shadow rays <b>on</b> (key <code>-</code>)</sub></td>
    <td width="30%"><img src="Report/images/raytrace/off.jpg" alt="Ray-traced view without shadows"><br><sub>Shadow rays <b>off</b></sub></td>
  </tr>
</table>

---

## 📐 Engineered, not eyeballed

Every object has real dimensions in metres, taken straight from the source constants. The lift angle, ramp slope, door drum and hose curves are all computed, not guessed.

<table>
  <tr>
    <td width="50%"><img src="Report/images/orthographic/vehicle_right_dim.png" alt="Vehicle side view with dimensions"><br><sub>Vehicle: 5.56 m long, 3.46 m wheelbase, wheel Ø 0.90 m</sub></td>
    <td width="50%"><img src="Report/images/lift/lift_raised.jpg" alt="Scissor lift raised"><br><sub>Scissor lift: <code>θ = asin((0.58 + h) / 2L)</code></sub></td>
  </tr>
  <tr>
    <td><img src="Report/images/lift/bezier_hoses.jpg" alt="Bezier hydraulic hoses"><br><sub>Hydraulic hoses as cubic Bézier curves</sub></td>
    <td><img src="Report/images/comparisons/wireframe.jpg" alt="Wireframe view of the whole garage"><br><sub>Wireframe (<code>L</code>): everything is primitives</sub></td>
  </tr>
</table>

<div align="center">

**Everything above is built from just four meshes:**

<img src="Report/images/primitives/cube_wire.jpg" width="22%" alt="Cube primitive"> <img src="Report/images/primitives/cylinder28_wire.jpg" width="22%" alt="Cylinder primitive"> <img src="Report/images/primitives/cylinder48_wire.jpg" width="22%" alt="Smooth cylinder primitive"> <img src="Report/images/primitives/sphere_wire.jpg" width="22%" alt="Sphere primitive">

<sub>Cube (24 V) · Cylinder N=28 (118 V) · Smooth cylinder N=48 (198 V) · Sphere 24×36 (925 V)</sub>

</div>

---

## 🧠 Graphics techniques inside

| Area | What's implemented |
|---|---|
| **Modelling** | Procedural cube / cylinder / sphere, a custom triangle-mesh vehicle shell, hierarchical models (robot, lift, door) |
| **Transformations** | `M = T · Rᵧ · Rₓ · R_z · S`, parent–child hierarchies, look-at camera, perspective ↔ orthographic (`Backspace`) |
| **Lighting** | Directional, 8 point lights with attenuation, headlights, moving spotlight, day/night, lighting presets |
| **Shading** | Gouraud, Phong and Cook–Torrance **PBR** (GGX), switchable live with `F5` |
| **Realism** | PCF shadow maps, triplanar textures, normal mapping, image-based reflections, HDR, bloom, MSAA |
| **Curves** | Cubic Bézier hydraulic hoses, smoothstep-eased robot motion |
| **Animation** | 18-state automation, rolling wheels (`φ = s / r`), door winding on a drum, turntable, lift |
| **Interaction** | Full keyboard + mouse control of camera, door, lift, car, robot, lights and renderer |
| **Collision** | Swept-capsule arm vs. AABB tests (slab method); unsafe manual moves show **SAFETY STOP** |
| **Ray tracing** | GPU ray tracer for the inspection monitor: primary + shadow rays, 793 items |

---

## 🎮 Controls

<details>
<summary><b>Click to see every key</b></summary>

| Keys | Action |
|---|---|
| **Enter** / **R** | Start / reset the automatic service |
| `W A S D`, `Space` / `Ctrl` (`E` / `Q`), `Shift` | Fly the camera (Shift = ×2.5 speed) |
| Mouse, scroll, `Tab` | Look around, zoom (FOV 20–90°), free the cursor |
| `O`; `Z` / `X` / `C` | Door auto toggle; close / open / stop |
| `P`; `[` `]` `\` | Lift auto; lower / raise / stop |
| `Home`; `PgUp` | Vehicle out / in; back to the apron |
| `T/G`, `U/J`, `I/K`, `Y/H`, `←/→` | Robot base yaw, shoulder, elbow, wrist pitch, wrist roll |
| `F1`–`F4` | Scanner / impact driver / gripper / polisher |
| `;` `'` `/` `End` | Ceiling lights, headlights, neon sign, warning beacon |
| `0` | Daylight ↔ night |
| `M`; `,` `.` | Lighting preset; reflection intensity |
| `F5` | Shading: PBR → Gouraud → Phong |
| `F6`–`F11` | Textures, bump, shadows, reflections, bloom, MSAA |
| `Backspace` | Perspective ↔ orthographic |
| `9`, `-`, `=` | Monitor: enlarge / shadow rays / camera preset |
| `Insert` / `Delete`, `PageDown`, numpad | Monitor: orbit camera, light preset, move light |
| `L`, `1`–`8`, `N` / `B`, `V`, `F`, `F12` | Wireframe, isolate object, build stages, report, screenshot, orbit |

</details>

---

## 🛠 Build & run

**Requirements:** Windows 10/11, Visual Studio 2026 with the *Desktop development with C++* workload (toolset v145), and a GPU with OpenGL 3.3+.
GLFW and GLAD are already bundled in [`Libraries/`](Libraries), so there's nothing else to install.

```bash
git clone https://github.com/Afifa637/Smart-Auto-Service-Garage.git
cd Smart-Auto-Service-Garage
```

1. Open **`YoutubeOpenGL.slnx`** in Visual Studio.
2. Choose **x64** and **Release**.
3. Press **F5** (or *Build → Build Solution* and run `x64/Release/YoutubeOpenGL.exe`).
4. Press **Enter** in the window and watch the service run. 🍿

> Using an older Visual Studio? Right-click the project → *Retarget Projects* to your installed toolset (it uses C++20).

---

## 📁 Project structure

```text
Smart-Auto-Service-Garage/
├── Main.cpp              # window, input, 18-state service automation, collision, render passes
├── SceneObjects.cpp/.h   # garage, vehicle, lift, door, robot, tools, props
├── Shader.cpp/.h         # GLSL: PBR / Gouraud / Phong, shadows, post-processing, ray tracer
├── Primitives.cpp/.h     # cube, cylinder and sphere mesh generators
├── Camera.h, Math3D.h    # fly camera and the small vector/matrix library
├── glad.c, Libraries/    # GLAD loader + GLFW (bundled)
├── YoutubeOpenGL.slnx    # Visual Studio solution
├── annotated_source/     # heavily commented copy of the source, for studying
├── media/                # demo video + highlight GIF
├── Report/               # report (PDF + LaTeX), slides, figures, proposal
└── archive/              # notes, renders and video from earlier iterations
```

---

## 📚 Documentation

| | |
|---|---|
| 📄 **[Project report (PDF)](Report/SmartGarage_Report.pdf)** | Construction of every object with the calculations, equations and source references |
| 🎞 **[Presentation slides](Report/SmartGarage_Showcase.pptx)** | 10-slide showcase with 42 numbered figures and the embedded demo |
| 📝 **[Project proposal](Report/Project_Proposal.pdf)** | The original 21-item proposal |
| 🗺 **[Course requirements map](COURSE_REQUIREMENTS_MAP.md)** | Where each course requirement lives in the code |
| ✏️ **[LaTeX source](Report/main.tex)** | Rebuild the report with `latexmk -pdf main.tex` |

---

<div align="center">

### 👩‍💻 Author

**Afifa Sultana Offa** · Roll 2107087
Department of Computer Science and Engineering
Khulna University of Engineering & Technology (KUET)

<sub>Supervised by **Md. Tajmilur Rahman** and **Md. Mubtashim Abrar Nihal**, Lecturers, CSE, KUET.</sub>
<sub>Third-party code: [GLFW](https://www.glfw.org/) (window and input) and [GLAD](https://glad.dav1d.de/) (OpenGL loader). Everything else is original.</sub>

<br>

⭐ **If you enjoyed watching a robot polish a Cybertruck, leave a star!** ⭐

</div>
