# TerrainGen

A pair of standalone, one-shot tools that procedurally build the
`DiabloLevel` terrains and export them as FBX. Neither is part of the engine
build and neither is driven by any `.vcxproj` - compile and run by hand
against the repo's vendored FBX SDK whenever a terrain needs regenerating.

- **`TerrainGen.cpp`** - Solo/1's 280x280 unit terrain: a hand-authored bump
  list (`kBumps`) and one hand-authored path polyline (`kPathPts`).
- **`TerrainGenBig.cpp`** - Solo/2's 1000x1000 unit (~1km^2) terrain, at the
  *same* per-tile density as Solo/1 (40-unit tiles, 100 segs/tile, 0.4
  units/vertex - a bigger tile at the same triangle budget was tried and
  visibly under-resolved, individual vertices/facets legible in LOD0, so
  this stays at Solo/1's density and pays for it in tile count: 625 tiles
  instead of Solo/1's 49): a procedurally scattered bump field
  (`GenerateBumpField`) and a branching network of paths
  (`GenerateStandardPathNetwork` - two trunks crossing near the map center
  plus eight branches, some dead-ending at a mountain cluster, some looping
  back to rejoin a trunk further along) instead of hand-authored ones. ~6-8
  minutes to export on this machine (canyon carving, see below, adds real
  per-vertex cost, and there are far more vertices in total at 625 tiles of
  this density than a coarser point in the tradeoff space would have).

  Two more feature types on top of the bump field, for elevation change a
  rounded `Bump` dome can't produce:
  - **Plateaus** (`GeneratePlateaus`/`Plateau`/`PlateauHeight`) - a flat-topped
    mesa (positive height) or a flat-bottomed valley floor (negative), sparse
    and large enough to span several tiles, with a true flat interior
    (`flatFraction` of the radius) and only the rim tapering - unlike a dome,
    which is curved everywhere including its own center. A pure
    `dist/radius` falloff draws a mathematically perfect circle, which reads
    as an obviously artificial landform - `PlateauOutlineWarp` warps the
    *radius* by noise sampled as a function of angle (not position) so the
    outline bulges and pinches into an irregular blob while the radial
    profile itself (and so the flat top) stays intact.
  - **Canyons** (`GenerateCanyons`/`CanyonSegment`/`CanyonCarve`) - a handful
    of long, meandering cuts carved in *after* everything else (regional
    roll, cliff detail, plateaus, even the path carve), steeper-walled than
    the path system's worn-trail look (`kCanyonFeather` narrower relative to
    width/depth than `kPathFeather`). They walk with no mountain repulsion at
    all - cutting through a mountain range or a plateau's flank is the point
    - but *do* steer clear of the walkable path network, via the same
    repulsion mechanism a trunk uses against mountains (`SteerStep`'s
    `avoidPaths`), just pushing away from road segments instead of bump
    centers. A canyon that can't clear a bad starting roll in time is dropped
    rather than kept partially inside the safety margin.

  Both a plateau's skirt and a canyon's wall are smooth carves by
  construction (a radial falloff, a feathered depth), and a smooth slope
  reads as "poured concrete" *regardless of how steep it is* - exactly the
  failure mode the original tool's own mountain `CliffDetail` comment
  already warns about, just recurring in two new places. `PlateauRockWeight`
  and `CanyonRockWeight` apply the same flank-shaped rock treatment a
  mountain flank gets (0 at the feature's flat/floor and at its outer edge,
  peaking mid-slope) and fold into `RockWeight` alongside the existing
  bump-flank term, so `CliffDetail`'s broken-rock noise - and `Terrace`'s
  strata benches - now also apply on a mesa's rim and a canyon's walls. A
  further fine-frequency layer inside `CliffDetail` (gated on
  `max(PlateauRockWeight, CanyonRockWeight)`) exists because the shared
  crag/macro noise is tuned for a mountain flank tens of units wide - a
  canyon wall's cross-slope width is much narrower, too narrow for that same
  wavelength to do more than complete about one cycle, which read as linear
  grooves running down the slope instead of broken rock.

  The *outline* is a separate problem from the *slope texture* above, and
  needed a separate fix: `PlateauOutlineWarp`'s low-frequency angular warp
  (see above) breaks a plateau's silhouette into an irregular blob at a
  broad scale, but up close the rim was still a mathematically smooth curve
  - a hard CG edge. `PlateauT` (shared by `PlateauHeight` and
  `PlateauRockWeight`, so the visible rock zone and the actual sloped rim
  are always the same edge) adds a *fine* erosion jitter on top of the warp,
  same idea as `PathFactor`'s own edge jitter. `CanyonJitteredDist` does the
  equivalent for canyons, at two noise scales - a coarse wobble so the
  centerline itself visibly wanders off the walk's own polyline, and a fine
  one for close-range raggedness - shared by `CanyonCarve` and
  `CanyonRockWeight` for the same reason.

  One more naturalness fix that isn't feature-specific: `WalkTowards` (what
  every branch uses to curve at a fixed point instead of holding a compass
  heading) used to aim exactly at that point every step, which traces a
  mathematically clean pursuit curve - visibly so on the longer loop-back
  branches, reading as a compass-drawn arc rather than a road. The aim point
  is now wobbled with low-frequency noise, strong far from the target and
  fading to nothing on final approach, so the walk still arrives precisely
  but the bend along the way meanders instead of sweeping one clean curve.
- **`TerrainGenCommon.h`** - the noise/height-shaping pipeline and the
  procedural bump/path generators, shared by both tools above *and* by
  `TerrainPreview.cpp` below. Kept free of any FbxSdk dependency on purpose.
- **`TerrainGenMesh.h`** - `BuildTile`, the FBX mesh-building shared by
  `TerrainGen.cpp`/`TerrainGenBig.cpp` (kept out of `TerrainGenCommon.h` so
  that header - and `TerrainPreview.cpp` - can stay FbxSdk-free).
- **`TerrainPreview.cpp`** - a fast, FbxSdk-free top-down PPM visualizer of a
  bump field + path network (height as brightness, rock as gray-vs-steppe-
  green tint, mountain influence radius as a faint ring, path network in
  brown). Seconds to build and run, vs. minutes-to-generate-and-import for
  the real FBX - use it to sanity-check a bump/path layout change (does a
  trunk actually bend around a mountain instead of cutting through it? do
  branches reach their targets?) before paying for a real terrain export.
  `.\TerrainPreview.exe [halfExtent] [cellSize] [seed] [out.ppm]`; convert to
  PNG to view it, e.g. `python -c "from PIL import Image;
  Image.open('preview.ppm').save('preview.png')"`. Also prints a reasonable
  player/camera spawn point (near the trunk crossing) and the terrain height
  there.

## Build and run

Verify the FbxSdk path before trusting the one below - this repo has moved
before. It currently lives at the repo root (`<repo>\FbxSdk`), not under
`source\repos`:

```powershell
$fbxInc = 'C:\Users\vicen\Documents\GitHub\HotBiteEngine\FbxSdk\include'
$fbxLib = 'C:\Users\vicen\Documents\GitHub\HotBiteEngine\FbxSdk\lib'
```

Also verify which VS 18 edition is installed (see the top-level `CLAUDE.md`'s
Building section - it has moved between Community and Insiders on this
machine) and call that edition's `vcvars64.bat` before `cl.exe`.

```powershell
& '<VS 18 edition path>\VC\Auxiliary\Build\vcvars64.bat'
cl.exe /std:c++17 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS /DFBXSDK_SHARED `
    /I $fbxInc TerrainGen.cpp /Fe:TerrainGen.exe `
    /link /LIBPATH:$fbxLib libfbxsdk.lib
copy $fbxLib\libfbxsdk.dll .   # needed alongside the exe to run it
.\TerrainGen.exe               # writes terrain.fbx next to itself
copy terrain.fbx ..\assets\Objects\terrain.fbx
```

Same recipe for `TerrainGenBig.cpp` / `terrain_big.fbx`, just swap the source
file and output name. `TerrainPreview.cpp` needs none of this - it has no
FbxSdk dependency, so a plain `cl.exe /std:c++17 /EHsc /O2 TerrainPreview.cpp`
(after `vcvars64.bat`, still needed for `cl.exe` itself) is enough.

## Solo/2: assembling the level after a `terrain_big.fbx` regeneration

Importing 625 mesh nodes and placing each one by hand (or via 1250+
automation round-trips - the editor's `create_template_from_model` only
builds a template from a model's *first* mesh node) doesn't scale, so
`BuildSolo2Level.py` writes `Solo/2/level.json`'s 625 tile
templates/instances directly, mirroring Solo/1's own template/instance JSON
shape (read out of its level.json, not guessed). It reuses Solo/1's sun,
ambient, lights catalog, camera template and materials verbatim - only the
terrain model and the Player/camera_rig spawn position differ. After
regenerating `terrain_big.fbx` (a different seed, `HALF`, or
`TILES_PER_SIDE`): copy it to `../assets/Objects/terrain_big.fbx`, get a
fresh spawn point via `TerrainPreview.exe` (see above), update the
`SPAWN_X`/`SPAWN_Z`/`SPAWN_TERRAIN_H`/`TILES_PER_SIDE` constants at the top
of `BuildSolo2Level.py` to match, then `python BuildSolo2Level.py`.

**LOD chains are deliberately not generated for Solo/2's tiles yet** - every
tile currently draws at LOD0 (20,000 tris) regardless of distance. Building
them means the same `generate_lod` x3-per-tile call Solo/1's 49 tiles got,
625 times over - a comparable one-time cost to generating the terrain
itself, deferred as a separate follow-up rather than folded into the initial
build.

After copying a regenerated `terrain.fbx` in, delete
`Tests/DiabloLevel/assets/GeneratedMeshes/` - it is an LOD cache keyed by
vertex/index counts, not by content, so it will not notice the geometry
changed if the counts didn't. The engine rebuilds it automatically (via the
`generated_meshes` recipe already saved in `level.json`) the next time the
level loads; with 49 tiles x 3 LOD levels this takes several minutes, and the
editor is unresponsive while it works through the queue.

## Why the terrain looks the way it does

- **Tiled, not one mesh.** 49 separate `Tile_r{row}_c{col}` mesh nodes in one
  FBX, each its own entity/template/instance in `level.json`. A single mesh
  above roughly 30-40k triangles reliably took the whole editor process down
  with a GPU device-removed error during this level's development - root
  cause never pinned down, but splitting into tiles (each comfortably under
  that ceiling, since it's one draw call/object rather than one giant mesh)
  made it a non-issue and is the right architecture for a large terrain
  anyway. Each tile gets a real LOD chain via the engine's own
  `generate_lod` (quadric edge collapse), so a big terrain stays cheap to
  render at distance without hand-authoring multiple resolutions.
- **Height is layered, each layer answering one question:**
  `RegionalHeight` (gentle rolling steppe + named bump domes/basin) to shape
  where the mountains and hollow are, `CliffDetail` split into `macro` (low
  amplitude/frequency - sets the rounded weathered silhouette) and a separate
  `crag` + `lumps` pair (high frequency, deliberately steep amplitude - the
  actual broken rock surface), `Terrace` (quantizes height into shallow
  benches on rocky ground only, which is what puts horizontal ledge/strata
  bands into a peak and gives snow somewhere flat to settle), and a path
  carved back down toward the regional (pre-cliff) height along a polyline.
  Treating "how jagged the silhouette is" and "how detailed the rock surface
  is" as the same knob was a real mistake made and fixed during development -
  turning down one to fix "too spiky" also flattened the surface into
  something that read as smooth poured concrete instead of rock. They need to
  stay two independent amplitude/frequency pairs.
- **Dominant-axis UV projection, chosen per triangle from its own normal**,
  not one flat top-down (x,z) projection. A single projection smears texels
  down the length of any steep face (the XZ footprint barely changes while Y
  runs the whole face) - invisible on gentle terrain, very visible once the
  crag amplitude went up enough for near-vertical facets. Per-triangle (never
  per-vertex - one triangle must not blend two projections, or the texture
  swims across it) trades that for a visible seam where neighbouring faces
  pick different axes, which reads as far less on a homogeneous rock texture.
- **UV domain-warp** (`WarpForUV`) bends the texture tiling grid into
  irregular wavy cells instead of a dead-straight repeating lattice. This is
  the actual fix for visible texture tiling on a large terrain - the
  `uv_noise` flag on a multi-material layer looks like it should help and
  does nothing at all (defined in `MultiTexture.hlsli`, never read anywhere).
  Also give each material layer in the `.mat` a different `uv_scale so their
  repeats don't all lock into the same grid.
- **A multi-material layer's `diffuse_color` tint is silently ignored** - the
  shader only ever samples the layer's texture directly
  (`MultiTexture.hlsli`'s `getMutliTextureValue`). To recolour a layer (e.g.
  green grass -> dead tundra brown), generate an actually-recoloured texture
  file; don't rely on the material's tint multiplier.
- **A multi-material layer's `mask` field is broken for a 5th+ layer** - a
  real file there breaks the *entire* stack's rendering (every layer, not
  just the masked one), even with a trivially valid mask image. Root cause
  not found (suspected NaN from the domain shader also sampling the mask
  array for displacement, even at `displacement_scale: 0`); the workaround
  used here was dropping the masked layer entirely and using height/slope
  rules plus mesh geometry (a real carved dip) to make the path readable
  instead of a dedicated trail texture.
