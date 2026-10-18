# Graph Report - Laser-XY-Scanner  (2026-10-16)

## Corpus Check
- Large corpus: 59 files · ~870,451 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder.

## Summary
- 248 nodes · 362 edges · 16 communities (9 shown, 7 thin omitted)
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 45 edges (avg confidence: 0.89)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Bill of Materials
- main.py
- parse_svg
- arduino
- Laser Engraver Server Python Dependencies
- Laser-XY-Scanner Command Reference and Hardware Concepts
- 12V TTL Laser Diode LD Driver Power Supply Board
- WiFiServerController
- app.js
- ESP32TCPClient
- preview.py
- motion.cpp
- ESP32SerialClient
- parseCommand

## God Nodes (most connected - your core abstractions)
1. `Bill of Materials` - 12 edges
2. `parse_svg()` - 11 edges
3. `upload()` - 10 edges
4. `Laser Engraver Server Python Dependencies` - 10 edges
5. `WiFiServerController` - 9 edges
6. `Laser Engraver Full Implementation Plan` - 8 edges
7. `LaserController` - 7 edges
8. `ESP32SerialClient` - 7 edges
9. `ESP32TCPClient` - 7 edges
10. `generate_gcode()` - 7 edges

## Surprising Connections (you probably didn't know these)
- `Assembled Laser XY Scanner Physical Hardware Photograph` --references--> `Bill of Materials`  [INFERRED]
  Laser_XY_3D_Parts/images/IMG_2976.JPG → BOM.md
- `FC-51 / KY-032 IR Obstacle Avoidance Sensor Module` --semantically_similar_to--> `FC-51 / TCRT5000 IR Obstacle Avoidance Sensor Module`  [INFERRED] [semantically similar]
  ir_sensor_test/TESTING_GUIDE.md → BOM.md
- `FC-51 Optical Obstacle Avoidance IR Sensor Component Diagram` --references--> `FC-51 / TCRT5000 IR Obstacle Avoidance Sensor Module`  [INFERRED]
  resources/Infrared Obstacle Avoidance IR Sensor Module.jpeg → BOM.md
- `Laser Constant-Current Driver PCB Backside Photograph` --references--> `12V TTL Laser Diode LD Driver Power Supply Board`  [INFERRED]
  resources/laser_back_01.jpg → BOM.md
- `Laser Driver Board Top-Side Buck Converter Layout Photograph` --references--> `12V TTL Laser Diode LD Driver Power Supply Board`  [INFERRED]
  resources/laser_front_01.jpg → BOM.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Mechatronic Motion and Homing Electronics** — laser_xy_3d_parts_images_laser_xy_scanner_wiring_schematic, resources_28byj48_stepper_motor_product_photo, resources_a4988_stepper_motor_driver_pinout_diagram, resources_infrared_obstacle_avoidance_ir_sensor_module_diagram [INFERRED 0.85]
- **Dual-Axis Stepper Mirror Scanning Mechanism** — laser_xy_3d_parts_images_laser_xy_scanner_1_optical_ray_trace, laser_xy_3d_parts_images_laser_xy_scanner_2_orthogonal_mirrors, laser_xy_3d_parts_images_img_2976_dual_mirror_stepper_assembly, resources_report_dual_mirror_deflection [INFERRED 0.95]
- **Constant-Current Laser Driver Hardware Subsystem** — resources_laser_front_01_driver_pcb_front_photo, resources_laser_back_01_driver_pcb_back_photo, resources_laser_front_01_buck_regulator_circuit, resources_laser_back_01_current_sense_bank, bom_laser_driver_board [INFERRED 0.95]

## Communities (16 total, 7 thin omitted)

### Community 0 - "Bill of Materials"
Cohesion: 0.06
Nodes (44): 28BYJ-48 Stepper Motor & ULN2003 Driver, AixiZ 12mm Aluminum Mount and Heatsink, Arduino Nano ATmega328P Board, Bill of Materials, 1-inch Round Glass Mirror Blanks, Certified Laser Safety Goggles (OD 5+), Nichia M140 2W 445nm Blue Laser Diode in Copper Module, Chemical Paint Stripper for Front-Surface Delamination (+36 more)

### Community 1 - "main.py"
Cohesion: 0.09
Nodes (10): Settings, get_job(), init_db(), save_job(), health(), home(), index(), job_preview() (+2 more)

### Community 2 - "parse_svg"
Cohesion: 0.12
Nodes (10): upload(), generate_gcode(), _fallback_parse_svg(), parse_svg(), _distance(), sort_paths_nearest_neighbour(), fit_to_work_area(), test_fit_to_work_area_scales_points() (+2 more)

### Community 3 - "arduino"
Cohesion: 0.14
Nodes (4): HomingController, LaserController, duty_, serializeStatus()

### Community 4 - "Laser Engraver Server Python Dependencies"
Cohesion: 0.10
Nodes (19): FastAPI Web Framework, Laser Engraver Server Python Dependencies, HTTPX Async Client Library, NumPy Numerical Computing Library, Pillow Image Processing Library, PySerial Serial Bridge Library, Pytest Test Framework, svgpathtools Geometric Curve Engine (+11 more)

### Community 5 - "Laser-XY-Scanner Command Reference and Hardware Concepts"
Cohesion: 0.14
Nodes (16): FC-51 / TCRT5000 IR Obstacle Avoidance Sensor Module, Software Debounce and Glitch Filtering, Trim-Pot Distance Calibration (5-10mm), FC-51 / KY-032 IR Obstacle Avoidance Sensor Module, IR Obstacle Avoidance Sensor Test & Integration Guide, LM393 Precision Voltage Comparator, Stepper Driver & Homing Limit Switch Digital Pin Mapping, Laser-XY-Scanner Command Reference and Hardware Concepts (+8 more)

### Community 6 - "12V TTL Laser Diode LD Driver Power Supply Board"
Cohesion: 0.22
Nodes (11): 12V TTL Laser Diode LD Driver Power Supply Board, Laser Diode Lasing Threshold (Dead Band) Discovery, Laser Test Firmware Step-by-Step Guide, Thermal Stress & Soak Test Workflow, PWM Duty Cycle and Optical Power Control, Parallel SMD Current-Sense Resistor Bank (R250), Laser Constant-Current Driver PCB Backside Photograph, Switch-Mode Buck Inductor, Power MOSFET & Capacitor Bank (+3 more)

### Community 7 - "WiFiServerController"
Cohesion: 0.22
Nodes (3): WiFiServerController, client_, server_

### Community 8 - "app.js"
Cohesion: 0.20
Nodes (8): gcodeOutput, homeBtn, powerSlider, powerValue, previewImage, statusTag, transportTag, uploadForm

## Knowledge Gaps
- **26 isolated node(s):** `duty_`, `server_`, `client_`, `Creative Commons Attribution-ShareAlike License`, `Creative Commons Attribution-ShareAlike 4.0 International` (+21 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 84 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **7 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Bill of Materials` connect `Bill of Materials` to `Laser-XY-Scanner Command Reference and Hardware Concepts`, `12V TTL Laser Diode LD Driver Power Supply Board`?**
  _High betweenness centrality (0.097) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `Bill of Materials` (e.g. with `Assembled Laser XY Scanner Physical Hardware Photograph` and `Laser XY Scanner Project README`) actually correct?**
  _`Bill of Materials` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `duty_`, `server_`, `client_` to the rest of the system?**
  _26 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Bill of Materials` be split into smaller, more focused modules?**
  _Cohesion score 0.05851063829787234 - nodes in this community are weakly interconnected._
- **Why does `Laser Engraver Full Implementation Plan` connect `Laser Engraver Server Python Dependencies` to `Laser-XY-Scanner Command Reference and Hardware Concepts`?**
  _High betweenness centrality (0.059) - this node is a cross-community bridge._