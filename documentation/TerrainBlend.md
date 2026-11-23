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

Use the Terrain Blend window to configure dirt, sand, grass, dead grass, and rock. Each layer uses
ordinary AperturePBR albedo, normal, roughness, and height textures, plus albedo and
roughness constants. Texture inputs must be runtime-ready DDS assets; normal maps
must have passed Remix's normal-map ingestion. Height maps control transitions and
optional POM displacement through independent parameters.

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
| `flipElevation` | bool | Negate world elevation before evaluating all elevation blends. Default false. |
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
| `displacementEnabled` | bool | Enable terrain Quadtree POM. Default false. |
| `displacementMode` | int | 0 Off, 2 Quadtree POM. Default 2; legacy value 1 migrates to 2. |
| `displacementStrength` | float | Finite nonnegative master relief multiplier. Default 1. |
| `displacementMaxIterations` | int | View-ray iteration budget, 4 to 128. Default 64. |
| `displacementInward` | float4 | Nonnegative inward world-unit relief for grass/sand/rock/dirt. Default (0.1, 0.1, 0.1, 0.1). |
| `displacementOutward` | float4 | Nonnegative outward world-unit relief. Default (0, 0, 0, 0). |
| `displacementHeightMin` | float4 | Height remap lower endpoints. Default (0, 0, 0, 0). |
| `displacementHeightMax` | float4 | Height remap upper endpoints; 0 <= min < max <= 1. Default (1, 1, 1, 1). |
| `displacementSelfShadowEnabled` | bool | Enable terrain relief self-shadowing. Default true. |
| `displacementShadowStrength` | float | Height-field shadow strength, 0 to 1. Default 1. |
| `displacementShadowMaxIterations` | int | Light-ray iteration budget, 4 to 64. Default 32. |
| `dirtMaterial` | relationship | One opaque Material target. Required for palette blending or positive dirt strength. |
| `dirtUnitsPerRepeat` | float | Positive dirt world units per repeat. Default 8. |
| `deadGrassMaterial` | relationship | One opaque Material target, required for palette blending. |
| `deadGrassUnitsPerRepeat` | float | Positive dead grass world units per repeat. Default 8. |
| `deadGrassDisplacementInward`, `deadGrassDisplacementOutward` | float | Nonnegative dead grass relief depths. Defaults 0.1 and 0. |
| `deadGrassDisplacementHeightMin`, `deadGrassDisplacementHeightMax` | float | Dead grass height remap endpoints. Defaults 0 and 1. |
| `paletteStrength` | float | Influence of the live terrain color, from zero to one. Defaults to zero when omitted. |
| `paletteSoftness` | float | Width of the color overlap, greater than zero and at most one. Default 0.16. |
| `controlMap` | asset | Linear RGBA terrain weight map. Red is grass, green is dead grass, blue is dirt, and alpha is rock. Sand takes the remaining weight. Requires all five materials. |
| `controlWorldToUv` | float4 | Required with `controlMap`. Two nonzero coordinate scales followed by two offsets. Applies to world XY when `rtx.zUp` is true and world XZ otherwise. |
| `geoMap` | asset | Optional linear RGBA texture with height along U. RGB is a multiplicative tint with 0.5 neutral. Alpha controls the local tint strength. |
| `geoElevationRange` | float2 | Required with `geoMap`. Increasing world elevations for the first and last texels. Uses the vertical axis and `flipElevation` of the terrain preset. |
| `geoStrength` | float | Global geo tint strength from zero to one. Defaults to one when a map is supplied. |
| `geoDipAmplitude`, `geoDipPeriod` | float | Optional nonnegative height offset and positive horizontal period in world units. Defaults 0 and 32. Blends changes in the tilt of geo strata across the terrain. |
| `peakRockMaterial` | rel | Optional opaque summit material. Its weight comes from the cliff rock layer. |
| `peakRockElevationRange` | float2 | Required with `peakRockMaterial`. Smooth elevation transition with a higher start on steep faces. Uses the preset's vertical axis and elevation sign. |
| `peakRockUnitsPerRepeat` | float | Positive summit rock repeat distance in world units. Default 8. |
| `peakRockRelief` | float4 | Inward and outward POM depth, then the lower and upper height remap endpoints. Default (0.1, 0, 0, 1). |
| `grassMaskColor`, `sandMaskColor`, `rockMaskColor`, `dirtMaskColor`, `deadGrassMaskColor` | color3f | Reference colors in encoded sRGB, with each channel in [0, 1]. |
| `dirtStrength` | float | Grass-exclusion dirt influence, zero to one. Default zero. |
| `dirtWidth` | float | Positive border sampling radius in world units. Default 2. |
| `dirtMinElevation` | float | Dirt starts above this elevation, respecting flip elevation. Default 0. |
| `dirtElevationFadeWidth` | float | Positive elevation range over which dirt reaches full influence. Default 1. |

Lengths and elevations use the game's world units. The runtime uses `rtx.zUp` to
choose the vertical axis. Elevation bounds must increase; slope bounds must increase
within zero to ninety degrees. Parameters must be finite. Invalid enabled presets
are logged and ignored.

## Evaluation

An authored `controlMap` supplies the initial material weights. The renderer filters it independently of the triplanar material maps and retains the selected mip level for the relief shadow pass. Use linear data textures for the weights.

The optional geo map tints the evaluated ground with an elevation-addressed strip. Its influence follows the cliff rock weight, contributes lightly to dirt, and increases on steeper faces. Optional geo dip varies the sample elevation and direction across the horizontal plane. Use zero alpha at the ends of the strip for a smooth fade outside its authored range.

The summit material takes weight from cliff rock across its authored elevation range. Steeper faces make that transition higher. Its four detail maps participate in height blending, view parallax and relief self-shadowing with the other layers.

Terrain presets with smooth normals accumulate normals at shared world positions across terrain draws. This keeps shading continuous at matching chunk borders when the live island is redrawn.

With a control map, `rockMode = "elevation"` fades grass and dead grass to rock between `rockLowElevation` and `rockHighElevation`, using the live terrain position and `flipElevation`. Sand and dirt keep their authored base weights. The `both` mode also applies the slope rule to those vegetation weights. The default `slope` mode leaves the initial rock weight to the map. Live grass-exclusion dirt can use its own texture alongside the control map. The authored map remains fixed in world space.

The palette samples the original terrain draw's RGB with that draw's UVs and sampler. Its five reference colors give nearby colors overlapping material weights. Elevation preferences help distinguish sand from inland dirt, and slope or elevation reinforce rock. Black source areas fade to the procedural elevation and slope blend. The layer colors and maps still come from the authored materials.

Optional dirt uses the live secondary alpha texture of draws selected by
`rtx.legacyMaterial.worldSpaceSecondaryOpacityTextures`. The shader uses the same
world-space projection as grass opacity. Set
`rtx.legacyMaterial.worldSpaceSecondaryOpacityScale` in the game's `rtx.conf`;
its default is 1. JPOG's coverage map uses `0.0009765625` (1/1024).
A nine-sample filter extends excluded areas into nearby grass. Coverage transfers
a continuous fraction of the grass and dead grass weights into dirt.
Dirt is zero at or below its minimum elevation and fades in over the configured
positive elevation range. The threshold uses the same flipped coordinate as grass
and rock. Missing coverage leaves the procedural blend unchanged. The last observed
coverage texture remains available when frontend material translation is cached.
The mask includes paths, buildings, shores, and other grass exclusions.
The old coverage-to-sand attributes are replaced by these dirt controls; Toolkit
migrates old presets when loaded and writes only the new controls.

The preset bypasses the downward terrain baker and evaluates at opaque material
interactions in primary and indirect tracing. Grass and sand transition with
elevation, and rock increases with slope. Material height samples bias active
weights smoothly. The shader normalizes their sum to one. Height strength zero
uses the placement weights directly.

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

## Terrain POM

Enable displacement and set inward and outward relief on each material. Height
remapping saturates each sampled height between its minimum and maximum endpoints.
Black maps to the inward limit and white to the outward limit. The master strength
multiplies both limits; zero strength or a missing height texture contributes no relief.
Changing repeat scale changes texture frequency without changing world-unit relief.

POM intersects one blended signed height field using the current primary or indirect
view ray. The five material weights and raw height-map transitions are frozen
at the real terrain hit. All material colors, normals, and roughness then sample the
same virtual hit, avoiding independent material offsets at transitions. Quadtree leaf solving
refines intersections within the configured maximum iteration budget. Near
tangent or back-facing view rays retain the original sampling position. Relief fades
between normal/view dot products 0.05 and 0.2, with the same fade applied to shadows.

Height samples use one explicit mip level derived from the largest height-texture
footprint across the triplanar projections. That level and the signed view-ray hit
distance are retained for light-ray marching. Self-shadowing samples the same field
and frozen weights toward the light, stops at the outward relief bound, and attenuates
visibility by the configured shadow strength. Comparing with the sampled origin
height prevents a constant height field from shadowing itself.

Terrain POM uses the terrain-capable shader variants selected by the preset;
it does not require enabling ordinary material POM globally. The terrain material copies keep
generic displacement disabled because their terrain-specific sampling path owns POM.

This is texture parallax, not mesh displacement. It does not move ray-hit positions,
change silhouettes or collision. Relief self-shadows are combined with mesh visibility,
without moving the mesh shadow rays. Large relief can expose the underlying flat
silhouette. The iteration budgets bound view and light marches; performance depends on overlapping layers, slopes,
view angles, and height textures and must be measured in the intended scene.

## Current scope and validation

The optional `smoothNormals` bool defaults to false. The Terrain Blend window's
Smooth terrain normals toggle requests Remix's existing GPU normal
generation for terrain draws. Changing the toggle rebuilds the cached normal
buffer, and turning it off restores the game's normals unless the draw is also
explicitly assigned Remix's Smooth Normals category. Landscaping geometry updates
regenerate normals through the same pass. Smoothing influences terrain slope
placement and triplanar projection as well as lighting. Normals are generated per
draw mesh; continuity across separately submitted terrain chunks is not guaranteed.

Terrain Blend supports up to six layers, opaque dielectric terrain, triplanar tiling,
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

## Terrain shader variants

Terrain blending and displacement share one terrain-capable shader variant.
The authored displacement toggle and strength control relief at runtime; disabled
displacement skips the relief intersection and terrain self-shadow sampling.
Scenes without terrain blending select the separate non-terrain variants.

## Displacement algorithm

`remix:terrainBlend:displacementMode` selects `0` (Off) or `2` (Quadtree POM).
An omitted mode defaults to Quadtree. Legacy value `1` is read as Quadtree,
preserving the enable switch, depths and other controls. Runtime loading does not
modify the source USD; Toolkit writes value `2` when exporting an imported preset.
Off suppresses both displacement and its self-shadowing.

Quadtree POM traverses maximum-height mip cells for every active material and
triplanar projection, combines their conservative height bounds with the layer
weights, and descends where the ray can intersect the blended surface. Use the
maximum-value height mipmaps generated by Toolkit height ingestion; averaged colour
mipmaps are not suitable. Inward/outward depth, height remapping, grazing fade,
iteration budget and self-shadow controls remain available. More blended layers
and projections increase traversal cost. The raymarched terrain traversal is removed;
ordinary Remix material displacement modes are independent.

Quadtree leaves use an integer mip selected from the terrain footprint and solve
the bilinear height polynomial along the ray. Height-remap clamp transitions
split the leaf into polynomial intervals, so interior peaks are tested even
when both endpoints lie above the heightfield. An exhausted traversal budget
does not produce an unconfirmed displaced hit or terrain self-shadow flag.

## Startup shader compilation

Terrain shader selection reads the loaded mod preset before geometry or textures
are bound. Publishing or hot-reloading a preset refreshes the G-buffer,
direct/indirect lighting, RTXDI and ReSTIR GI variants for the active settings.
These requests use Remix's existing precompilation queue, shader cache and
**Compiling shaders...** screen. Startup tracking stays open while mod assets can
still publish shader requirements.

The existing `rtx.initializer.asyncShaderPrewarming` and
`rtx.shader.blockUntilPrewarmed` controls govern precompilation and its wait screen.
Driver or shader updates can require compilation again; the normal cache mechanism
handles reuse.

The November 5, 2026 validation passed 31 dedicated runtime tests and 27 Toolkit
checks. In an isolated cold-local-cache run, 40 terrain pipelines joined 127 other
startup requests before the first 3D frame. The wait screen opened and closed,
and no pipelines compiled on demand or in the background during the first ten
seconds of 3D frames. The user confirmed the screen and terrain result looked fine.
Terrain SPIR-V decreased 2.18% (5.37 MiB); the compressed runtime DLL decreased
3.89 MiB. These are code-size measurements, not a frame-time benchmark.
