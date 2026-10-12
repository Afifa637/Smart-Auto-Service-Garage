SmartGarage Project Report (showcase / viva edition) - how to build the PDF
=========================================================================

Contents
  main.tex                          the report (article, 10 pt, A4, 20 pages)
  references.bib                    bibliography (BibTeX, ieeetr style)
  tikz/hierarchy.tex                parent-child transformation trees
  tikz/trs_pipeline.tex             unit cube -> S -> R -> T diagram
  tikz/service_state_machine.tex    18-state automatic service state machine
  tikz/scissor_lift.tex             scissor geometry at h = 1.10 m
  tikz/robot_kinematics.tex         planar robot chain with numbers
  tikz/bezier_hose.tex              hose control polygon and B(0.5)
  tikz/door_winding.tex             roller-door winding geometry
  tikz/ramp_geometry.tex            ramp profile and vehicle axles
  images/<topic>/*                  every figure, rendered by the project itself
  Screenshot_Capture_Checklist.txt  exact command line for every image
  Technical_Audit.txt               proposal check, corrections, limitations
  main.pdf                          compiled report

Build (any one of these):
  latexmk -pdf main.tex
  pdflatex main.tex ; bibtex main ; pdflatex main.tex ; pdflatex main.tex
  tectonic main.tex          (this is how main.pdf was built)

Packages: graphicx, amsmath, amssymb, booktabs, longtable, array, tabularx,
float, caption, subcaption, geometry, xcolor, listings, tikz (arrows.meta,
calc, positioning), siunitx, fancyhdr, enumitem, placeins, hyperref, lmodern,
microtype.

Before submission: fill in the title-page placeholders (course, code,
department, university, instructor, date).

Corrected project files (already in place in SmartGarage_AUTOMATIC_SERVICE_V5_FINAL):
  Shader.cpp       classical Phong model (Gouraud per vertex, Phong per fragment),
                   10 point lights
  Main.cpp         F5 cycles PBR/Gouraud/Phong, headlight point lights,
                   DRIVE_OUT state, capture switches, ceiling-fan keys removed
  SceneObjects.cpp/.h  serviceCarMatrix(), headlightLocalPosition(),
                   carWheelRadius(); ceiling fan removed
  SceneObjects.cpp  drawExterior(): forecourt side walls, piers and back wall removed
  Report/SmartGarage_Showcase_Final_v3.pptx  10-slide showcase deck (python-pptx, all text/shapes editable)
  Report/Final_Consistency_Checklist.txt  source = report = deck checklist
  Bangla_Notes/01..03_*.md                Bangla explanation of everything in the report with code files, line numbers, functions, equations and 'how to change' recipes

