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

//The reflection ray of a pixel, shared by the two solvers that can answer it:
//RayTraceCS (world space, the BVH walk) and RayReflexScreenSolverCS (screen space,
//the Hi-Z march). They have to start from the SAME ray. A pixel the screen solver
//does not answer is answered by the world one, and if the two disagreed about where
//the ray points a reflection would change with whichever of them found it - which is
//exactly the seam this exists to prevent. So the direction is built here, once.
//
//Needs, before it: RGBANoise.hlsli (rgba_tnoise), Utils.hlsli (GetSpaceVectors) and
//RayDefines.hlsli, and a `cameraPosition` constant.

#ifndef __REFLEX_RAY_HLSLI__
#define __REFLEX_RAY_HLSLI__

float3 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = rX * N * dispersion;

    // First point at the top (up direction)
    if (index < 1.0f) {
        return dir; // The first point is directly at the top
    }

    //Largest m >= 0 with 1 + m * (m + 1) < index, closed form of the previous
    //linear search (levels grow by 2 points per level).
    float m = max(ceil((sqrt(max(4.0f * (index - 1.0f) + 1.0f, 0.0f)) - 1.0f) * 0.5f) - 1.0f, 0.0f);
    float level = m + 1.0f;
    float cumulativePoints = 1.0f + m * (m + 1.0f);

    float pointsAtLevel = level * 2;  // Quadratic growth

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;

    float phi = level / NLevels * M_PI * (0.4 + rX);

    // Azimuthal angle (theta) based on number of points at this level
    float theta = (2.0f * M_PI + rX) * localIndex / pointsAtLevel; // Spread points evenly in azimuthal direction

    // Convert spherical coordinates to Cartesian coordinates
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    // Local ray direction in spherical coordinates
    float3 localRay = float3(sinPhi * cosTheta, sinPhi * sinTheta, cosPhi);

    // Convert local ray to global coordinates (tangent space to world space)
    float3 globalRay = localRay.x * tangent + localRay.y * bitangent + localRay.z * dir;


    return normalize(dir + globalRay);
}

static float NCOUNT = 32.0f;
static uint N2 = 32;
static float N = N2 * NCOUNT;

//Whether a pixel traces a reflection at all. The same four tests both solvers gate
//on: a surface that does not reflect, one with no normal (nothing was drawn), and
//one whose dispersion is out of the range a ray can be spread over.
bool PixelReflects(RaySource source)
{
    return !(source.reflex <= Epsilon || dist2(source.normal) <= Epsilon || source.dispersion < 0.0f || source.dispersion >= 1.0f);
}

//The perfect mirror direction of the view ray off this surface: what
//GetReflectedRayFromSource (RayFunctions.hlsli) returns as `dir`. Written out here
//because the screen solver does not carry the BVH walk's includes; keep the two in step.
float3 ReflexMirrorDirection(RaySource source)
{
    float3 in_dir = normalize(source.orig - cameraPosition);
    return normalize(reflect(in_dir, source.normal));
}

//The direction the pixel's ray actually goes: the mirror direction spread over a
//cone by the surface's dispersion. The seed is the surface POINT, not the pixel or
//the frame, so a point reflects along the same ray every frame (the denoiser
//accumulates it) and two pixels on the same point agree.
float3 ReflexRayDirection(float3 orig_pos, float3 mirror_dir, float dispersion, out float rX)
{
    //Smallest level with level * (level + 1) >= N, closed form of a linear search.
    float level = ceil((sqrt(4.0f * N + 1.0f) - 1.0f) * 0.5f);
    float3 seed = orig_pos * 100.0f;
    rX = rgba_tnoise(seed);
    rX = pow(rX, 4.0f);
    float3 tangent;
    float3 bitangent;
    GetSpaceVectors(mirror_dir, tangent, bitangent);
    return GenerateHemisphereRay(mirror_dir, tangent, bitangent, dispersion, N, level, rX);
}

#endif
