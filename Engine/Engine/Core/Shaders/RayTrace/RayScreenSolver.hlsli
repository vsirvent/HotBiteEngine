/*
The HotBite Game Engine

Copyright(c) 2024 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//Screen space ray marching: the first of the two steps a ray is solved in. The ray is
//followed across the depth buffer, through a min (Hi-Z) pyramid of it so that empty
//space is crossed in a few big steps, and if it passes behind a surface the colour of
//that pixel IS the answer. A ray that leaves the screen, or never meets a surface on
//it, is not solved here; the world space tracer (the BVH walk) takes it from there.
//
//Everything is in VIEW space and in PIXELS of the full resolution depth map. The
//depth_map holds the distance from the camera, not the planar z, and so does the
//pyramid built from it: the ray is measured the same way (ScreenLine::Depth).
//
//How the march this is adapted from found the ray's depth at a pixel was the weak
//part of it. It intersected the eye ray with a plane through the ray and the camera's
//up axis, and that plane goes through the camera whenever the ray lies in the
//vertical plane through it - the centre column of the screen, where a floor's
//reflections of what is ahead live - so there every eye ray met it at the camera, the
//whole column read a depth of zero and never hit anything. Here the ray is a line in
//homogeneous space instead: 1/w and (position / w) are both LINEAR along the ray's
//projection on the screen, so the exact view-space point under any pixel of it is one
//lerp and a divide away, with no unprojection and no degenerate case. That is also a
//fraction of the cost per step, which is what pays for the budget a floor needs: seen
//from above, depth changes a lot within one Hi-Z cell, the cell's nearest point says
//little about the pixels in it, and a ray skimming a floor walks it nearly pixel by
//pixel until it has climbed away.
//
//It also tested the ray at the *entry* of each cell, which only sees a ray going
//behind a surface a cell late; here a cell is tested for the deepest the ray gets
//inside it, so a cell is skipped only when the ray provably stays in front.

#ifndef __RAY_SCREEN_SOLVER_HLSLI__
#define __RAY_SCREEN_SOLVER_HLSLI__

//Levels of the pyramid, the depth map itself included. HiZ level n has cells of
//hiz_ratio^n pixels. Must match RenderSystem::HIZ_NTEXTURES.
#define HIZ_TEXTURES 4


//The first pixels of the march are not looked at: the ray starts on the surface it
//leaves, and this is the margin before it is allowed to have gone behind anything.
#define SCREEN_TRACE_SKIP_PIXELS 1.5f

//How many pixels in a row the ray may stay behind the surface before it is given up as hidden.
#define SCREEN_TRACE_MAX_HIDDEN 48

//How many cells it walks at the level it dropped to before it tries a bigger one.
#define SCREEN_TRACE_LINGER 6

//Past the edge of a cell, so the next step lands inside the next one.
#define SCREEN_TRACE_EDGE_EPS 0.01f

//Nearest view depth a point of the ray may have: where a ray heading at the camera
//is cut short, so that its projection stays on the right side of the lens.
#define SCREEN_TRACE_NEAR 0.05f

//The ray as a line on the screen, with what it takes to say how far from the camera
//it is under any point of that line.
struct ScreenLine
{
    float2 p0;      //start, in pixels
    float2 p1;      //end, in pixels
    float k0;       //1 / w at the start
    float k1;       //1 / w at the end
    float3 q0;      //(world position - camera) / w at the start
    float3 q1;      //(world position - camera) / w at the end
};

//The ray (world space, `dir` unit) as far as it can usefully go: `length_end` long, but cut
//short where it would reach the lens. False when it never is in front of the camera.
bool ScreenLineFromRay(float3 orig, float3 dir, float length_end, float3 camera, matrix view_proj, float2 dimensions,
                       out ScreenLine sl)
{
    sl = (ScreenLine)0;
    float4 c0 = mul(float4(orig, 1.0f), view_proj);
    float4 c1 = mul(float4(orig + dir * length_end, 1.0f), view_proj);
    if (c0.w <= SCREEN_TRACE_NEAR) {
        return false;
    }
    //w is linear along the ray: where it falls to the near limit is where the ray ends. A ray
    //that heads at the camera would otherwise project to the wrong side of the screen.
    if (c1.w < SCREEN_TRACE_NEAR) {
        length_end *= 0.95f * (c0.w - SCREEN_TRACE_NEAR) / (c0.w - c1.w);
        c1 = mul(float4(orig + dir * length_end, 1.0f), view_proj);
    }
    sl.k0 = 1.0f / c0.w;
    sl.k1 = 1.0f / c1.w;
    //Relative to the camera, so that the length of this is a distance from it: the unit the
    //depth map is in. Any affine function of the position is linear on the screen once it is
    //divided by w, the camera's offset included.
    sl.q0 = (orig - camera) * sl.k0;
    sl.q1 = (orig + dir * length_end - camera) * sl.k1;
    float2 n0 = c0.xy * sl.k0;
    float2 n1 = c1.xy * sl.k1;
    sl.p0 = float2(n0.x * 0.5f + 0.5f, 0.5f - n0.y * 0.5f) * dimensions;
    sl.p1 = float2(n1.x * 0.5f + 0.5f, 0.5f - n1.y * 0.5f) * dimensions;
    return true;
}

//How far from the camera the ray is under the point of the line `u` of the way along
//it (0 at p0, 1 at p1, measured on the screen): the point that projects there, as a
//distance.
float ScreenLineDepth(ScreenLine sl, float u)
{
    float k = lerp(sl.k0, sl.k1, u);
    float3 q = lerp(sl.q0, sl.q1, u);
    return length(q) / k;
}
//How far behind (positive) or in front of (negative) the surface drawn there the ray is
//at the CENTRE of the pixel that `pos` is in: its depth where the line passes closest to
//that centre, against the depth map's value, which is the surface at the centre.
float ScreenCenterDiff(ScreenLine sl, float2 d, float inv_total, Texture2D<float> depth, float2 pos)
{
    float2 pixel = floor(pos);
    float u = saturate(dot(pixel + 0.5f - sl.p0, d) * inv_total);
    return ScreenLineDepth(sl, u) - depth[(uint2)pixel];
}
float ScreenCellSize(int level, float ratio)
{
    //Whole numbers of pixels, exactly (a pow() would not be): a cell edge is found by
    //flooring against this.
    return level <= 0 ? 1.0f : (level == 1 ? ratio : (level == 2 ? ratio * ratio : ratio * ratio * ratio));
}

//How far, along the unit direction d, the cell of `cell_size` pixels that holds `pos`
//is left.
float ScreenCellExit(float2 pos, float2 d, float cell_size)
{
    float2 cell_min = floor(pos / cell_size) * cell_size;
    float2 bound = cell_min + float2(d.x > 0.0f ? cell_size : 0.0f, d.y > 0.0f ? cell_size : 0.0f);
    float2 t = float2(d.x != 0.0f ? (bound.x - pos.x) / d.x : FLT_MAX,
                      d.y != 0.0f ? (bound.y - pos.y) / d.y : FLT_MAX);
    return max(min(t.x, t.y), 0.0f);
}

float ScreenHiZ(Texture2D<float> hiz_textures[HIZ_TEXTURES], int level, float2 cell)
{
    uint2 c = (uint2)cell;
    //The index of a texture array has to be a literal, so a branch per level.
    float nearest = FLT_MAX;
    if (level == 0) {
        nearest = hiz_textures[0][c];
    }
    else if (level == 1) {
        nearest = hiz_textures[1][c];
    }
    else if (level == 2) {
        nearest = hiz_textures[2][c];
    }
    else {
        nearest = hiz_textures[3][c];
    }
    return nearest;
}

//Whether the surface at a pixel faces the ray that has just gone behind it: the one
//the ray left (or the back of something) does not.
//
//Asked of two normals, and a pass from either is a pass. The one the G-buffer holds
//is the surface's SHADING normal: smoothed across a hard edge, a box's side reads as
//pointing half way to its neighbour's, and a ray that really does hit the side can be
//on the wrong side of that. The other is rebuilt from where the neighbouring pixels
//are (a face is flat whatever its normals say). The surface a ray leaves fails both,
//which is the case this is here for; a pixel on a depth edge makes the second one
//noise, and that costs a hit at the silhouette, not a false one in the open.
bool ScreenFacesRay(uint2 pixel, float3 dir_world, float3 camera, Texture2D<float4> positions, Texture2D<float4> normals)
{
    if (dot(normals[pixel].xyz, dir_world) < 0.0f) {
        return true;
    }
    float3 p0 = positions[pixel].xyz;
    float3 px = positions[pixel + uint2(1, 0)].xyz;
    float3 py = positions[pixel + uint2(0, 1)].xyz;
    float3 n = cross(px - p0, py - p0);
    float len = length(n);
    if (len <= 1e-8f) {
        return false;
    }
    n /= len;
    //Turned to face the camera: the winding of the screen axes decides which way the
    //cross product points, not the surface.
    if (dot(n, p0 - camera) > 0.0f) {
        n = -n;
    }
    return dot(n, dir_world) < 0.0f;
}

//Marches the ray (world space, `dir` unit) until it passes behind a surface.
//
//A hit is the first pixel the ray enters IN FRONT of the surface drawn there and leaves
//BEHIND it. A ray that goes behind something it entered already behind is hidden, not
//hitting. Three knobs say what counts as behind:
//
//  bias          how far behind the surface, in world units, the ray has to be. What the ray
//                left is on its own surface and a ray skimming it measures as a hair behind
//                now and then: with a bias that is not a hit. Reflections leave it to the
//                facing test; shadows, whose casters face any way, cannot.
//  thickness     how far behind it may be and still have hit. Past that it went behind a
//                nearer surface, which is not in its way: the depth map says nothing about
//                what is behind a surface, only that it is there. Negative means no limit.
//                `thickness_rel` adds a share of the surface's own distance to it, because
//                the steps of the march, and the pixels' reach, grow with distance.
//  check_facing  the surface has to face the ray (ScreenFacesRay): the one a reflection leaves
//                (or the back of something) is not what it sees.
//
//`positions` and `normals` are the packed ray source pair (xyz = world position, world
//normal), for the facing test.
//
//Returns false for a ray that leaves the screen, runs out of length (`max_distance`, world
//units) or of steps (`max_steps`: what keeps a ray that never settles - grazing a surface,
//hidden behind a long wall - from holding the GPU, and how much of the frame's time the
//march may take. A ray that uses it all is simply not solved on the screen).
bool ScreenTrace(float3 orig, float3 dir, float3 camera, float2 dimensions, matrix view_proj,
                 Texture2D<float> hiz_textures[HIZ_TEXTURES], float hiz_ratio, float max_distance, int max_steps,
                 float bias, float thickness, float thickness_rel, bool check_facing,
                 Texture2D<float4> positions, Texture2D<float4> normals, out uint2 hit_pixel)
{
    hit_pixel = uint2(0, 0);

    ScreenLine sl;
    if (!ScreenLineFromRay(orig, dir, max_distance, camera, view_proj, dimensions, sl)) {
        return false;
    }
    float2 delta = sl.p1 - sl.p0;
    float total = length(delta);
    //A ray that hardly moves on the screen (straight at, or away from, the camera) has
    //no line to march along.
    if (total <= SCREEN_TRACE_SKIP_PIXELS + 1.0f) {
        return false;
    }
    float2 d = delta / total;
    float inv_total = 1.0f / total;

    float2 pos = sl.p0 + d * SCREEN_TRACE_SKIP_PIXELS;
    float travelled = SCREEN_TRACE_SKIP_PIXELS;
    int level = 0;
    float z_in = ScreenLineDepth(sl, travelled * inv_total);
    //Pixels left to walk one by one before leaping again. A cell is only worth skipping
    //when the ray is in front of the nearest point of ALL of it, and over a floor seen
    //from above that is only true once the ray has climbed well clear: until then the
    //leap is refused at once, the march drops to the pixel and moves on one, climbs, is
    //refused, drops - three steps a pixel for the same pixel. Having had to look closely,
    //it keeps looking closely for a while.
    int linger = 0;
    //How far behind (positive) or in front of (negative) the surface, less the bias, the ray
    //was at the centre of the last pixel it was looked at in. A hit is that changing sign
    //between two pixels, both measured at their centres: the ray's depth at the edge of a
    //pixel against the surface's at its centre disagrees by up to half a pixel of the
    //surface's slope, and on a steep one the crossing then falls between the two readings -
    //a hit missed on every few rows, which is how this looked.
    float prev_diff = ScreenCenterDiff(sl, d, inv_total, hiz_textures[0], pos) - bias;
    //Pixels in a row the ray has been behind the surface. A ray that starts inside
    //something, or goes behind a big surface, would otherwise spend the whole budget
    //being hidden.
    int hidden = 0;

    [loop]
    for (int step = 0; step < max_steps; ++step) {
        if (travelled >= total) {
            break;
        }
        if (pos.x < 0.0f || pos.y < 0.0f || pos.x >= dimensions.x || pos.y >= dimensions.y) {
            break;
        }
        float cell_size = ScreenCellSize(level, hiz_ratio);
        float t_exit = min(ScreenCellExit(pos, d, cell_size) + SCREEN_TRACE_EDGE_EPS, total - travelled);
        float z_out = ScreenLineDepth(sl, (travelled + t_exit) * inv_total);
        float nearest = ScreenHiZ(hiz_textures, level, pos / cell_size);

        //Somewhere in this cell the ray is as deep as its nearest surface: it may be
        //behind something in there, so look closer.
        if (max(z_in, z_out) >= nearest + bias) {
            if (level > 0) {
                level--;
                linger = SCREEN_TRACE_LINGER;
                continue;
            }
            float diff = ScreenCenterDiff(sl, d, inv_total, hiz_textures[0], pos) - bias;
            if (prev_diff < 0.0f && diff >= 0.0f) {
                bool deep = thickness >= 0.0f && diff + bias > thickness + thickness_rel * nearest;
                if (!deep && (!check_facing || ScreenFacesRay((uint2)pos, dir, camera, positions, normals))) {
                    hit_pixel = (uint2)pos;
                    return true;
                }
            }
            if (diff >= 0.0f) {
                if (++hidden > SCREEN_TRACE_MAX_HIDDEN) {
                    break;
                }
            }
            else {
                hidden = 0;
            }
            prev_diff = diff;
        }
        else {
            //In front of every pixel of the cell.
            prev_diff = -1.0f;
            hidden = 0;
        }
        pos += d * t_exit;
        travelled += t_exit;
        z_in = z_out;
        //This cell was clear: the next is worth looking at from further up - unless the
        //last leap was refused a moment ago.
        if (linger > 0) {
            linger--;
        }
        else {
            level = min(level + 1, HIZ_TEXTURES - 1);
        }
    }
    return false;
}

#endif