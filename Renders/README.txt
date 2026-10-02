RENDERS - v6
============

Stills produced by the build itself, for the report and the presentation.
Every one was captured with the program's own offline capture path, so each is
reproducible with a single command line:

  YoutubeOpenGL.exe --capture out.bmp --orbit <deg> --radius <m> --height <m>
                    --lift <m> --polish <0..1> --preset <0|1|2>
                    --size <w> <h> --fov <deg>

Or press F while running to grab the current frame.

01_front_three_quarter.png   The single straight wedge line from bumper to roof
                             apex, the raked A-pillar, chunky arch cladding and
                             the seven-spoke aero covers.

02_side_profile.png          Side elevation, for comparison against
                             Reference/03_reference_side.png.

03_rear_three_quarter.png    Sail panel, sloping vault cover and the full-width
                             recessed red light bar.

04_flank_as_rolled.png       SAME vehicle, SAME frame, polish = 0.
05_flank_polished.png        polish = 1.
                             The pair is the before/after for the polishing
                             stage: micro-roughness drops from 0.40 to 0.16, the
                             anisotropic brushed streak is reduced, and specular
                             reflectance rises slightly. The hood, roof and
                             tailgate keep the factory finish in both, because
                             the side skin is its own mesh batch with its own
                             surface finish.

06_impact_driver_on_lug.png  The socket tip stopping ON the lug nut face. It
                             previously drove 211 mm inside the wheel.

07_polisher_on_panel.png     The polishing pad in contact with the side panel.
                             It previously stopped 225 mm short of it.

08_projection_perspective.png     SAME camera, SAME frame.  Perspective: the
09_projection_orthographic.png    runway edges converge to a vanishing point
                                  and the far parked car is visibly smaller.
                                  Orthographic: the scissor arms stay parallel,
                                  foreshortening disappears entirely, and the
                                  distant car is drawn at the same scale as the
                                  near one.  BACKSPACE switches between them;
                                  the two are matched at the subject's distance
                                  so only the projection changes, not the zoom.

10_textures_concrete_and_treadplate.png
                                  The sampled material texture array: hairline
                                  cracks and aggregate on the concrete floor,
                                  and the diamond non-slip pattern on the lift
                                  runways.  Both come from a 4-layer
                                  GL_TEXTURE_2D_ARRAY sampled by triplanar
                                  projection, with mipmaps and 8x anisotropic
                                  filtering.  F6 turns it off.

11_front_head_on.png              The rebuilt front, square on.  Full-width
                                  light blade sitting just under the hood lip,
                                  dark grey fascia raked back beneath it, and
                                  two driving lamps in the black bumper.
                                  Compare against the reference photograph:
                                  previously this was one flat stainless slab
                                  with the blade switched off and no lamps.

12_front_three_quarter.png        The same front at three quarters, showing the
                                  blade wrapping around the corner and the hood
                                  lip overhanging the fascia.

13_forecourt_and_passing_traffic.png
                                  The view out through the door.  The nine
                                  distant tower blocks and the four parked cars
                                  are gone; the forecourt is 20 m wide instead
                                  of 30 m and walled on both sides.  A vehicle
                                  is crossing the opening on the through road,
                                  wheels rolling at the rate its speed demands.
                                  PAGE UP stops and starts it.

15_grime_before.png               Road grime as a shaded surface film: heaviest
16_grime_after_cleaned_polished.png  low on the panel, thinning with height,
                                  blotchy and vertically streaked.  It drops the
                                  metallic term, which is what makes it kill the
                                  reflection rather than just tint it.  It used
                                  to be six flat grey boxes floating 4 mm off
                                  the door.

17_survey_pass_over_vehicle.png   The inspection head part-way through its
                                  whole-body survey, hovering 620 mm above the
                                  skin.  It now flies nose-cowl-roof-bed before
                                  dropping to the flank the dirt is on.

18_lamp_occluded_far_side.png     The far side of the vehicle while the lamp is
                                  aimed at the near flank.  No light reaches it.
                                  Before the spot shadow map was added, the beam
                                  went straight through the bodywork.
