# Rendering Gaussian Splats Through a Deferred G-Buffer

*Gaussian Splatting was designed as a forward, image-producing technique. This describes
an implementation that instead treats a splat cloud as a source of material — albedo,
normal, specular — so it can be rasterized straight into a deferred renderer's G-buffer
and lit, shadowed and ray-traced like ordinary geometry.*

## 1. Gaussian Splatting in brief

3D Gaussian Splatting (3DGS) represents a scene as millions of anisotropic 3D Gaussians,
each with a position, a covariance (scale + rotation, defining an ellipsoid), an opacity,
and spherical harmonics colour coefficients that let its apparent colour vary with
viewing direction. The whole cloud is fit by differentiable rendering against calibrated
photographs until the render matches them.

The reference renderer projects each Gaussian to a 2D screen-space ellipse, sorts by
depth, and alpha-composites front-to-back directly into a final image. There is no
G-buffer and nothing to relight — the spherical harmonics coefficients **are** the appearance, baked once
under whatever lighting the source photos had. That's why a splat capture dropped into a
game scene usually looks pasted in: it's lit by a sun that isn't there.

## 2. The challenge: writing directly into the G-buffer

The core idea is to never render a splat cloud as its own image at all. Instead, the
splat pass writes its result **directly into the deferred renderer's G-buffer** — the
same albedo, normal, depth, motion and ray-tracing inputs any other surface would leave
there. Once that write happens, the rest of the pipeline is completely transparent to the
fact that a splat cloud was ever involved: the lighting pass, the shadow lookup, the
reflection and global-illumination tracers and the motion-blur pass all just consume a
surface, with no splat-specific branch anywhere downstream. Getting there, however, means
clearing several real obstacles:

- **One surface per pixel, out of a volumetric blend.** A deferred renderer assumes a
  single surface per pixel described by a few discrete values. A splat cloud is the
  opposite — an unstructured, overlapping, semi-transparent volume with no inherent "the
  surface." Something has to decide, per pixel, which one thing out of everything
  overlapping it counts as *the* surface, and what material it has.
- **The G-buffer's exact format, nothing looser.** Since downstream code is meant to stay
  unaware splats exist, the pass cannot invent its own lighter-weight representation —
  it has to produce full, correctly-scaled albedo, world-space normal, depth, etc... in the same layout ordinary geometry produces them in.
- **Still reading as a soft, translucent volume up close.** Collapsing to one hard surface
  per pixel can't come at the cost of a hard-edged cutout — a cloud's silhouette and any
  genuinely translucent detail still need to look smoothly composited.
- **Correctness under unordered, parallel execution.** The reference technique composites
  sequentially, back-to-front, on the CPU's terms. Here potentially millions of splats and
  a full screen of pixels are processed by thousands of concurrent GPU threads with no
  natural ordering guarantee, and the result still has to be deterministic and stable
  frame to frame.

The approach: treat it as an **occlusion problem**. Every splat
overlapping a pixel is walked in exact depth order and accumulated as an ordinary
front-to-back transmittance composite; the point where accumulated coverage crosses a
threshold *is* the surface, and the material accumulated up to that point is exactly what
gets written into the G-buffer. Sections 4–6 are how each part of that is made exact and
cheap.

## 3. Albedo from the constant term of the spherical harmonics

Each splat's spherical harmonics colour has a direction-independent (degree-0) term, `f_dc`, plus
higher-order terms that add view-dependent detail (mostly baked-in highlights). The
degree-0 basis function is a constant, `C0 ≈ 0.2821`, and the trained colour along any
direction is `0.5 + C0·f_dc + (higher-order terms)`.

The higher-order terms are discarded entirely, and only the direction-independent part is
kept, clamped to zero:

```
albedo = max(0, 0.5 + C0 · f_dc)
```

This is the move that makes deferred shading possible at all: a view-dependent colour
can't be relit consistently frame to frame as the camera moves, but a single
Lambertian-style albedo is exactly what an ordinary lighting model expects. 

**Caveat:** this recovers something *usable as* albedo, not true albedo — the DC term is
still trained radiance under the capture's own lighting. A capture taken in flat light
reinterprets well; one taken in hard sun keeps its baked highlights and gets lit a second
time by the new scene's lights. De-lighting a radiance field back to true albedo is an
open research problem and isn't attempted; a per-cloud tint is the practical way to
compensate. Specular can't be recovered from the data at all (a radiance field has none)
and is instead a flat authored constant, kept low so a scanned surface doesn't get a
second highlight on top of its baked one.

A plain colour point cloud with no spherical harmonics data (e.g. photogrammetry output) skips this
entirely and uses its stored vertex colour as albedo directly.

## 4. Normal calculation — two methods, one hard problem

**Trained Gaussians** are flat against the surface they represent, so the *shortest* of
the three ellipsoid axes points along the surface normal. That axis is real but noisy, so
it's blended with a neighbourhood plane fit (below) — roughly 30% trained axis, 70% fit —
rather than trusted alone.

**Point clouds with no shape data** (equal scale on all axes — a sphere, with no minor
axis to read) fall back to a standard PCA normal estimator: fit a plane through each
point's *k* nearest neighbours and take the eigenvector of the smallest eigenvalue of
their local covariance — the direction the neighbourhood is thinnest in.

Both give an **axis**, not a **direction** — an eigenvector has no sign — and resolving
that sign is the harder half of the problem. The naive fix, "point every normal away from
the cloud's centroid," only works for a single convex blob seen from outside; any
concavity (the underside of a strap, the inside of a rim, a floor running through the
middle of a scan) flips independently against its neighbours and shows up as
scattered, full-spectrum speckle in the shading.

The real fix is Hoppe et al.'s 1992 construction, still used by point-cloud tools today:
build a nearest-neighbour graph, weight each edge by how far the two normals disagree,
grow a minimum spanning tree over it, and flip each normal to agree with whichever
neighbour reached it first. Because disagreement is the edge weight, the traversal
crosses flat, easy regions first and only crosses a real crease once forced to — by which
point both sides already agree internally and can't drag each other out of alignment.
That fixes local agreement; a whole isolated region can still be inside-out as a whole, so
a final per-region flip (toward its own centroid, or "the topmost point faces up" for
something flat) sets the last global sign, with a manual override as the last resort for
an ambiguous case (e.g. a room scanned from the inside).

## 5. Shadows and mesh calculations

**Receiving light and shadow costs nothing extra.** Because a splat cloud is rasterized
into material rather than baked colour, the exact same lighting and shadow code used for
ordinary geometry applies to it directly — no splat-specific shadow logic exists.

**Casting shadows and colliding needs a stand-in mesh.** A splat cloud never passes
through a conventional vertex/triangle pipeline, so it can't appear in a shadow map or a
physics collider on its own. The fix is to reconstruct an approximate low-poly mesh from
the point cloud once, offline:

1. Drop low-opacity splats (noise shouldn't shape the surface).
2. Bucket the rest into a coarse 3D grid.
3. At every grid corner, evaluate a signed distance as the opacity-weighted average of
   nearby splats' own oriented tangent planes — reusing the per-splat normals from
   section 4 directly.
4. Extract the zero isosurface with **Surface Nets** (one vertex per sign-crossing grid
   cell) rather than Marching Cubes — no large lookup table, at the cost of a very
   slightly rounder surface, which is fine since nothing ever looks directly at this mesh.
5. Decimate the result down to a low triangle count.

That mesh is attached to every instance of the cloud, flagged **shadow-only**: it's
excluded from the colour pass (so its rough silhouette never doubles up on the rasterizer's
own, exact result) and from the depth pre-pass (so its approximate depth can't confuse the
splat rasterizer's own occlusion test), but it still participates in shadow casting and
collision. The result: the cloud's *own* appearance is exact, per-pixel; the shadow it
casts and the shape it collides as are both a reasonable approximation — and the two never
need to agree, because they're never visible through the same channel at once.

## 6. Tiling and the rasterizer

The rasterizer bins splats into screen tiles and composites them, in compute, as a
seven-step pipeline per frame:

| Stage | What it does |
|---|---|
| **1. Project** | One thread per splat: project to screen space, cull (LOD thinning, off-screen, sub-pixel, occluded), compute the 2D covariance, record each covered tile's nearest depth. |
| **2. Compact** | Pack the tiles that actually have splats in them into a dense list, and build the indirect-dispatch arguments for the stages that follow — so no work is launched over empty tiles. |
| **3. Bin (count)** | For every (splat, tile) pair, increment a histogram bucketed by tile *and* quantized depth. |
| **4. Scan (per tile)** | Turn each tile's own histogram into a prefix sum — one group per tile, entirely in fast local memory. |
| **5. Scan (global)** | Turn the per-tile totals into a base offset for each tile inside one shared pool. |
| **6. Bin (scatter)** | Repeat step 3's enumeration, this time writing each splat's index into its now-known, exactly sorted slot. |
| **7. Rasterize** | One thread group per tile, one thread per pixel: walk each pixel's slice of the pool front-to-back, composite by transmittance, and once coverage passes a threshold, write albedo/normal/depth/motion/ray-tracing data into the G-buffer, lit by the ordinary lighting path. |

Three design choices make this correct and fast rather than merely plausible:

- **No fixed capacity per tile.** A tile's entries occupy exactly the slice the two scan
  passes hand it in a shared pool — a dense tile costs what it needs, a sparse one costs
  nothing extra. Earlier fixed-capacity designs silently dropped a different fraction of
  each tile's splats every frame, which read as a flickering grid pattern invisible until
  a magnified diff.
- **The binning *is* the sort.** Histogram → prefix scan → scatter is already a counting
  sort; choosing enough depth buckets to make each bucket correspond to exactly one
  quantized depth value makes the sort *exact* — two splats in the same bucket share an
  identical depth, so there's no ordering left to get wrong.
- **Equal-depth splats are merged before compositing, not walked individually.**
  Alpha blending isn't associative, so splats that land in the same depth bucket but get
  visited in a different (atomic-write-determined) order every frame would otherwise
  produce a small but visible flicker. Gathering them into one commutative sum first — and
  compositing that sum once — removes it.

The walk stops early once the accumulated transmittance becomes negligible, since nothing
further back can then change the result meaningfully — which is what keeps a cloud's
per-frame cost tied to what's actually visible of it, rather than to how many splats the
source capture contains.
