# ColorSlicer

A fork of [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) 3.0.0-alpha12 that keeps **colors** and
**print settings** when you open a 3MF written by another application, and **colors** from OBJ files.

**Download:** see the [Releases page](https://github.com/PrintsNCode/ColorSlicer/releases) (macOS, Apple Silicon).
The original PrusaSlicer README is in [README-PrusaSlicer.md](README-PrusaSlicer.md).

> ColorSlicer is an independent, unofficial project, **not affiliated with or endorsed by Prusa Research**.
> "Prusa" and "PrusaSlicer" are trademarks of Prusa Research a.s. This fork is licensed under the same
> **GNU AGPL-3.0** as PrusaSlicer. See [LICENSE](LICENSE).

## The problem

PrusaSlicer 3.0 opens 3MF files from Bambu Studio, OrcaSlicer, Anycubic Slicer Next, MakerWorld, Blender
or CAD tools as geometry only. Colors, the filament palette and the author's print settings are dropped
(see [`doc/3mf-import-matrix.md`](doc/3mf-import-matrix.md)). Related upstream issues:
[#4503](https://github.com/prusa3d/PrusaSlicer/issues/4503) (`basematerials` ignored),
[#12502](https://github.com/prusa3d/PrusaSlicer/issues/12502) (Bambu painting, fixed in 2.7.4),
[#14964](https://github.com/prusa3d/PrusaSlicer/issues/14964) (filament colors lost).

## What this fork does

When a 3MF **without PrusaSlicer's own project data** is opened:

1. **Colors are kept.** Every color becomes an extruder of the printer. If the file uses more than one color
   the new project starts on the printer variant with the most tool slots (for example *Prusa CORE One INDX*
   with 8 tools). If the file has more colors than extruders, the first colors the model uses get an extruder
   each and every other color is printed with the extruder of the nearest one.
2. **Print settings are imported.** The settings stored in the file are translated to PrusaSlicer options and
   written over the selected print preset. They show up as **modified** (highlighted), so you can revert any
   of them to the preset value or keep the ones you want, exactly as in other slicers.

3MF files saved by PrusaSlicer are loaded as before.

### Color sources understood

| Source in the 3MF | What is read |
|---|---|
| Core spec `basematerials` (`pid`, `pindex`, `p1`-`p3`) | Per-object and per-triangle colors |
| Materials extension `m:colorgroup` | Per-triangle colors |
| Bambu / Orca `paint_color` attribute | Per-triangle painting (filament states) |
| `Metadata/model_settings.config` | Extruder of every object and part |
| `Metadata/project_settings.config` → `filament_colour` | The filament palette |

The most used color of a volume becomes the extruder of that volume; triangles with another color are painted.

### OBJ files

OBJ files keep their colors too, from two sources:

| Source in the OBJ | What is read |
|---|---|
| Vertex colors `v x y z r g b` (Blender, MeshLab...) | A face takes the average of its vertices (0-1 or 0-255) |
| `mtllib` + `usemtl` | `Kd` of the material in the `.mtl` next to the OBJ |

Vertex colors win over materials. Colors are weighted by the area they cover; more than 16 are merged into
the nearest ones. The most used color becomes the extruder of the object and the other faces are painted.
Only OBJ files with two or more colors are treated as colored.

- In an **empty project with one extruder**, a colored OBJ starts a new project on the printer with the most
  tool slots (CORE One INDX), like a foreign 3MF.
- In any other project, each color goes to the extruder with the nearest color.

### Print settings translated

Read from `Metadata/project_settings.config` (Bambu Studio / OrcaSlicer / Anycubic Slicer Next format).
Unknown keys, and values that are not valid for the target option, are skipped.

| Group | Foreign key → PrusaSlicer option |
|---|---|
| Layers | `layer_height`→`layer_height`, `initial_layer_print_height`→`first_layer_height` |
| Walls and shells | `wall_loops`→`perimeters`, `top_shell_layers`→`top_solid_layers`, `bottom_shell_layers`→`bottom_solid_layers`, `top_shell_thickness`→`top_solid_min_thickness`, `bottom_shell_thickness`→`bottom_solid_min_thickness`, `wall_generator`→`perimeter_generator`, `wall_sequence`→`external_perimeters_first`, `detect_thin_wall`→`thin_walls`, `ensure_vertical_shell_thickness`→`ensure_vertical_shell_thickness`, `spiral_mode`→`spiral_vase` |
| Seam | `seam_position`→`seam_position` (`back`→`rear`, `aligned_back`→`aligned`) |
| Infill and surfaces | `sparse_infill_density`→`fill_density`, `sparse_infill_pattern`→`fill_pattern`, `top_surface_pattern`→`top_fill_pattern`, `bottom_surface_pattern`→`bottom_fill_pattern`, `infill_direction`→`fill_angle`, `ironing_type`→`ironing` + `ironing_type` |
| Supports | `enable_support` + `support_type`→`support_material` (off / manual only / on), `support_style` / tree→`support_material_style`, `support_threshold_angle`→`support_material_threshold`, `support_top_z_distance`→`support_material_contact_distance`, `support_bottom_z_distance`→`support_material_bottom_contact_distance`, `support_interface_top_layers`→`support_material_interface_layers`, `support_base_pattern_spacing`→`support_material_spacing`, `support_object_xy_distance`→`support_material_xy_spacing`, `support_on_build_plate_only`→`support_material_buildplate_only`, `raft_layers`→`raft_layers` |
| Skirt, brim, prime tower | `brim_type`→`brim_type`, `brim_width`→`brim_width`, `skirt_loops`→`skirts`, `skirt_distance`, `skirt_height`, `enable_prime_tower`→`wipe_tower`, `prime_tower_width`→`wipe_tower_width` |
| Speeds | `outer_wall_speed`→`external_perimeter_speed`, `inner_wall_speed`→`perimeter_speed`, `small_perimeter_speed`, `sparse_infill_speed`→`infill_speed`, `internal_solid_infill_speed`→`solid_infill_speed`, `top_surface_speed`→`top_solid_infill_speed`, `bridge_speed`, `gap_infill_speed`→`gap_fill_speed`, `initial_layer_speed`→`first_layer_speed`, `travel_speed`, `support_speed`→`support_material_speed` |
| Accelerations | `default_acceleration`, `outer_wall_acceleration`→`external_perimeter_acceleration`, `inner_wall_acceleration`→`perimeter_acceleration`, `sparse_infill_acceleration`→`infill_acceleration`, `top_surface_acceleration`→`top_solid_infill_acceleration`, `initial_layer_acceleration`→`first_layer_acceleration`, `bridge_acceleration`, `travel_acceleration` |
| Line widths | `line_width`→`extrusion_width`, and the per-feature widths (`outer_wall_line_width`, `inner_wall_line_width`, `sparse_infill_line_width`, `internal_solid_infill_line_width`, `top_surface_line_width`, `initial_layer_line_width`, `support_line_width`) |
| Precision | `resolution`, `xy_contour_compensation`→`xy_size_compensation`, `bridge_flow`→`bridge_flow_ratio`, `fuzzy_skin`, `fuzzy_skin_thickness` |

Approximations: Orca `auto_brim`/`brim_ears` → `outer_only`; `ensure_moderate` and `ensure_critical_only` →
`partial`; `zig-zag` → `rectilinear`; `tri-hexagon` → `stars`; `monotonicline` → `monotoniclines`.
Speeds and accelerations come from another machine, so review them before printing.

**Not imported:** filament settings (temperatures, fans, flow), printer settings (retraction, machine limits,
G-code), per-object overrides. Print settings are imported for FFF printers only.

## How it works

```
3MF ──► load_3mf ──► Loaded3MF.file_colors / file_colors_used / file_print_settings
                 └─► Project.imported_colors() / imported_print_settings()   (transient, never saved)
                 └─► ProjectInteractor:
                       1. pick the printer (INDX variant with most slots when > 1 color is used)
                       2. FileLoadingLogic::apply_imported_colors()   → extruder_colour + model remap
                       3. ForeignPrint::apply()                       → modified print preset values
```

Colors cannot be mapped to extruders while loading, because only the project knows its printer. So the
loader collects one palette per file (color *i* is painting state *i + 1* and extruder *i + 1* of the loaded
model) and the importer maps the palette to the real extruders afterwards.

### Code map (paths under `src/`)

| File | Role |
|---|---|
| `slic3r-shared/.../Format/3mf/ColorImport.{hpp,cpp}` | Dependency-free helpers: color parsing, palette building, nearest color, extruder planning |
| `slic3r-shared/.../Format/3mf/ForeignConfig.{hpp,cpp}` | Reads Bambu/Orca filament colors and extruder per object / part |
| `slic3r-shared/.../Format/3mf/Model3mf.{hpp,cpp}` | Parses `basematerials`, `colorgroup` and `paint_color` |
| `slic3r-shared/.../Format/3mf.cpp` | Turns colors into extruders and painting; collects the used colors and settings |
| `slic3r-shared/.../Format/3mf/ForeignPrint.{hpp,cpp}` | JSON parsing and the translation table |
| `slic3r-shared/.../Format/3mf/ForeignPrintApply.cpp` | Writes translated values into the print preset |
| `slic3r-shared/.../Format/ObjColors.{hpp,cpp}`, `OBJ.cpp`, `objparser.cpp` | OBJ vertex colors, `.mtl` reading, per-face palette |
| `slic3r-shared/.../FileLoadingLogic.{hpp,cpp}` | `apply_imported_colors`, `remap_model_extruders`, `map_colors_to_extruders` |
| `slic3r-shared/.../ProjectInteractor.cpp` | Applies colors and settings to a freshly loaded project |
| `slic3r-shared/.../Preset/PresetInteractor.{hpp,cpp}` | Selects the printer variant with the most tool slots |
| `slic3r-domain/.../Project.{hpp,cpp}` | `ImportedColors`, `imported_colors()`, `imported_print_settings()` |

To translate another option, add one line to `translate()` in `ForeignPrint.cpp`
(`m.direct("their_key", "prusa_key")`, or `m.mapped(...)` with a value table) and a test case.

## Building

Follow [`doc/Build.md`](doc/Build.md) (build the dependencies first), configuring with
`-DSLIC3R_BUILD_TESTS=ON`. Tested on macOS (Apple Silicon) with CMake 3.27.

```bash
cmake --build build -j4
./build/src/slic3r-app-launcher/slic3r-app-launcher --datadir ~/colorslicer-data
```

`--datadir` keeps the test configuration apart from an existing PrusaSlicer installation.

### macOS app and disk image

PrusaSlicer 3.0 alpha does not ship macOS packaging. [`make-macos-app.sh`](make-macos-app.sh) builds
`dist/ColorSlicer.app` and `dist/ColorSlicer.dmg` from an existing build: it creates the bundle, copies the
non-system dynamic libraries and rewrites their paths, signs ad-hoc and creates the image.

```bash
./make-macos-app.sh . ColorSlicer
```

The build is **not notarized**. On first launch use right click → Open, or
`xattr -dr com.apple.quarantine /Applications/ColorSlicer.app`. The image is for the architecture it was built on.

## Tests

```bash
./build/src/slic3r-shared/slic3r-shared-tests "[colors],[colorimport],[foreignprint],[objcolors]"
```

| Tag | Covers |
|---|---|
| `[colorimport]` | Color parsing, painting-state encoding, palette reduction, extruder planning |
| `[colors]` | 3MF fixtures in `test/data/test_3mf/color_*.3mf` (basematerials, colorgroup, Bambu, per-part colors), extruder remapping |
| `[objcolors]` | OBJ vertex colors, `.mtl` materials, 0-255 colors, files without colors (`test/data/test_obj/`) |
| `[foreignprint]` | JSON parsing, translation tables, applying values to a real print config box |

Manually verified: a 10-filament, 2-plate 3MF from Anycubic Slicer Next on a *Prusa CORE One INDX* (8 tools):
colors on both plates, settings highlighted as modified.

## Known limitations

- OBJ textures (`map_Kd` images) are not supported, only flat colors (see OBJ section).
- `paint_color` from Bambu/Orca is read as-is; files from Bambu Studio versions that changed the format may need adjusting.
- Import of print settings is FFF only.
- At most as many distinct colors as the printer has extruders; the rest are merged into the nearest one.

## Upstream

The color part is intended as a small, separate proposal to PrusaSlicer. This fork is where the settings
translation lives. Related tools that solve the color part outside the slicer, by converting the file first:
Panda2Prusa, PaintPort, BedReady. ColorSlicer does it inside the slicer, without a conversion step.

## Credits and license

Based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is based on
Slic3r by Alessandro Ranellucci and the RepRap community. Changes by Carlos ([@PrintsNCode](https://github.com/PrintsNCode)). Licensed under the **GNU Affero General Public License v3.0**; the source of any distributed
binary must be made available. See [LICENSE](LICENSE).
