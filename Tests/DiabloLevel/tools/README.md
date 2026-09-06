# TerrainGen

A standalone, one-shot tool that procedurally builds the `DiabloLevel` terrain
and exports it as `terrain.fbx`. Not part of the engine build and not driven
by any `.vcxproj` - compile and run it by hand against the repo's vendored
FBX SDK whenever the terrain needs regenerating.

## Build and run

```powershell
$fbxInc = 'C:\Users\Vicen\source\repos\HotBiteEngine\FbxSdk\include'
$fbxLib = 'C:\Users\Vicen\source\repos\HotBiteEngine\FbxSdk\lib'
& 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cl.exe /std:c++17 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS /DFBXSDK_SHARED `
    /I $fbxInc TerrainGen.cpp /Fe:TerrainGen.exe `
    /link /LIBPATH:$fbxLib libfbxsdk.lib
copy $fbxLib\libfbxsdk.dll .   # needed alongside the exe to run it
.\TerrainGen.exe               # writes terrain.fbx next to itself
copy terrain.fbx ..\assets\Objects\terrain.fbx
```

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
