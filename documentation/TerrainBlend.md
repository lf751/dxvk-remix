# Procedural terrain blend presets

This custom runtime accepts a terrain preset independently of captured texture hashes.
The Toolkit **Window > Terrain Blend** extension authors and exports the preset.
Only draws classified as terrain use it. JPOG's existing terrain-render hook supplies
that classification for generated island textures.

JPOG requires matching custom bridge binaries exposing `AddTextureHash` and
`dxvk_GetTextureHash`: the 32-bit bridge is named `d3d9_remix.dll` beside the game's
proxy, and the 64-bit server is `.trex/NvRemixBridge.exe`. Older bridge builds can
load the mod while failing automatic terrain tagging.

## Authoring and loading

Use the Terrain Blend window to configure grass, sand/dirt, and rock. Each layer uses
ordinary AperturePBR albedo, normal, roughness, and height textures, plus albedo and
roughness constants. Texture inputs must be runtime-ready DDS assets; normal maps
must have passed Remix's normal-map ingestion. Height maps control the blend,
independently of POM displacement.

The window can apply an undoable preset to the current stage without selecting a
captured asset. Export creates a separate mod directory containing `mod.usda` and
copies the referenced textures. Install that directory through the game's normal
Remix mod-loading workflow. No island-specific hash list or replacement model is
needed.

The runtime reads `/RootNode/TerrainBlend` from each loaded mod. Existing mod priority
chooses the strongest authored preset. An explicitly disabled preset suppresses
weaker terrain presets. Deleting the preset restores normal terrain handling.
USD hot reload updates the preset through the existing material-reload path.

## Schema version 1

Attributes below use the `remix:terrainBlend:` prefix:

| Name | USD type | Meaning |
| --- | --- | --- |
| `schemaVersion` | int | Must be `1`. |
| `enabled` | bool | Enable automatic terrain assignment. |
| `flipElevation` | bool | Negate world elevation before evaluating both elevation blends. Default false. |
| `rockMode` | token | `slope`, `elevation`, or `both`. Default `slope`. |
| `rockLowElevation` | float | Elevation where rock begins. Default 4. |
| `rockHighElevation` | float | Elevation where rock fully replaces grass/sand. Default 8. |
| `grassMaterial` | relationship | One ordinary opaque Material target. |
| `sandMaterial` | relationship | One ordinary opaque Material target. |
| `rockMaterial` | relationship | One ordinary opaque Material target. |
| `unitsPerRepeat` | float3 | Positive world units per repeat, grass/sand/rock. |
| `lowElevation` | float | Below this elevation flat ground uses sand. |
| `highElevation` | float | Above this elevation flat ground uses grass. |
| `slopeStartDegrees` | float | Slope where rock begins, measured from horizontal. |
| `slopeEndDegrees` | float | Slope where rock completely replaces flat-ground layers. |
| `heightBlendStrength` | float | Material height influence, from zero to one. |
| `heightBlendWidth` | float | Positive transition width, at most one. |

Lengths and elevations use the game's world units. The runtime uses `rtx.zUp` to
choose the vertical axis. Elevation bounds must increase; slope bounds must increase
within zero to ninety degrees. Parameters must be finite. Invalid enabled presets
are logged and ignored.

## Evaluation

The preset bypasses the downward terrain baker and evaluates at opaque material
interactions in primary and indirect tracing. Grass and sand transition with
elevation; rock replaces them as slope increases. Material height samples refine
those weights while preserving zero-weight layers. A height strength of zero leaves
the elevation/slope weights unchanged.

World-space triplanar sampling covers cliffs as well as horizontal surfaces. The
shader blends linear albedo, world-space normals, and perceptual roughness, producing
an opaque dielectric surface. Artificial camera world offsets are removed before
sampling so texture positions remain in the authored world coordinates.

Flip elevation direction negates the selected world-axis coordinate; it does not
swap materials or rotate textures. In JPOG's inverted Y convention, enabling it
lets thresholds increase toward physically higher ground. Recalibrate existing
negative thresholds after enabling the flip. Rock's elevation thresholds use this
same coordinate. Slope mode ignores rock elevation, elevation mode ignores slope,
and both mode combines the weights as `1 - (1 - slope) * (1 - elevation)`.
Use a broad rock slope transition (for example 25 to 65 degrees) and zero material
height influence while tuning placement; narrow angle ranges produce sharp borders.

These new attributes are optional in schema version 1. Presets that omit them keep
the original unflipped elevation and slope-only rock behavior.

Terrain variants are compiled separately and selected only when a preset is active.
Terrain layer textures are registered before bindless tables are prepared. The first
version loads these textures synchronously; large layer assets can cause a load hitch.

## Current scope and validation

This version supports three layers, opaque dielectric terrain, triplanar tiling,
elevation, slope, and height-map transitions. It does not yet ingest JPOG's generated
paint/detail textures as layer masks. Those maps require live identification and
coordinate validation before preserving game-painted terrain can be promised.
It also does not provide a procedural Toolkit viewport shader or geometric displacement.

Use a constant-color preset first to verify automatic terrain assignment on multiple
islands. Then verify imported textures, steep slopes, landscaping changes, reflections,
and preset hot reload. Build and schema tests alone do not establish in-game rendering
or performance correctness.

## Verified development build

The October 9, 2026 development build passed full release compilation and the
dedicated `test_hot_reload` unit target. Toolkit build, formatting, lint, and all
19 headless Kit checks passed after the placement-control revision; coverage exceeded
98% for each new extension.

An isolated JPOG game copy verified automatic terrain tagging, constant-color
replacement on an existing island, a live change to cyan, and disabling the preset
to restore the original ground material. Vegetation and buildings remained intact.
This smoke check does not establish imported texture quality, cliff normal blending,
multiple-island behavior, or performance. Screenshots and logs are under
`patches/JPOG/terrain-blend/` in the reverse-engineering workspace. The original
game installation and saves were not modified by those smoke tests.

The placement revision additionally verified live elevation reversal, elevation-only
rock on flat terrain, both-mode rock, and broad combined transitions on a generated
mountain island. Daylight was temporarily held through the public runtime API to
inspect the colors, then the existing day/night cycle was restored. The complete
191-file runtime package was deployed and hash-verified in `D:\Games\JPOGRTX\JPOG`,
with its integration, configuration, terrain preset, and saves preserved.
