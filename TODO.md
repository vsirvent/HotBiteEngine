- Allow grid placement in the editor, so we can align correctly objects in the scene
- We need to be able to set materials that do not stretch with the model dimensions, for example for a wall I want to keep the bricks size and just have more bricks when I scale the wall. 
- We also need to be able to draw the masks with brushes directly in the object, the idea is that I can have a complex material that can blend different textures in a terrain for example with parts with snow, grass and terrain blended. 

## Status

All three shipped. Each also added its own automation suite (Tools/SceneEditor/automation/tests/suites/), and passes the full Scene Editor suite plus the Marbles suite (the two engine changes below reach both).

- **Grid placement** - `View/Grid Snap` (and `View/Grid Settings...` for the
  step sizes) snaps the gizmo's translate/rotate/scale drags and view-center
  template placement to a configurable world-space grid, with a visual grid
  overlay while it's on. Editor-only (`Tools/SceneEditor/GridSnap.h`,
  `GridOverlay.h`, `SelectionGizmo.cpp`, `AssetBrowser.cpp`); persists per
  level. Known limitation: the rotation/scale steps are a fixed increment,
  not adaptive to the object's own size.

- **Non-stretching materials** - a material can tile a texture in world
  units instead of the mesh's authored UV: "World-aligned tiling" plus a
  tile-size field in the Materials panel (`WORLD_UV_ENABLED_FLAG` /
  `world_uv_scale` on `Core::MaterialProps`, `MainRenderPS.hlsli`). Scaling a
  wall now changes how much brick it shows rather than stretching the
  texture. Known limitations: it's dominant-axis projection, not blended
  triplanar (a visible seam at box edges, same trade-off Source/Hammer-style
  "world-aligned" texturing makes), and it doesn't extend to multi-material
  (terrain) layers, which already have their own independent per-layer
  tiling.

- **Mask painting in the viewport** - the existing multi-material mask
  painter (`Tools/SceneEditor/MaskPaint.h`) now has a real brush: a
  "Paint in viewport" checkbox, then holding the left mouse button over a
  surface wearing the multi-material being painted dabs it, raycasting the
  mesh's own triangles for an exact hit and UV (`Core::RaycastMeshUV`,
  `Engine/Engine/Core/MeshRaycast.h`) rather than guessing from a bounding
  box. Known limitation: that raycast is brute-force per mesh, not
  BVH-accelerated, so it's fine at terrain/prop scale but would lag on a
  very high poly count mesh.
