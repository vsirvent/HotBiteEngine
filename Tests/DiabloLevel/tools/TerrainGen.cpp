// Standalone one-shot tool: builds a heightmap-displaced terrain mesh (rolling
// steppe, rocky mountains with a carved winding path) and exports it as a
// binary FBX the HotBiteEngine importer can load directly. Not part of the
// engine build - compiled and run once against the repo's vendored FbxSdk.
// See Tests/DiabloLevel/tools/README.md for how to build/run it and why the
// terrain looks the way it does.
#include <fbxsdk.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <string>

// ---- deterministic hash-based value noise (no external deps) -------------
static float Hash(int x, int z) {
    uint32_t h = (uint32_t)(x * 374761393 + z * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF; // [0,1)
}
static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
static float ValueNoise(float x, float z) {
    int x0 = (int)std::floor(x), z0 = (int)std::floor(z);
    int x1 = x0 + 1, z1 = z0 + 1;
    float tx = Smooth(x - x0), tz = Smooth(z - z0);
    float a = Hash(x0, z0), b = Hash(x1, z0), c = Hash(x0, z1), d = Hash(x1, z1);
    float ab = a + (b - a) * tx;
    float cd = c + (d - c) * tx;
    return ab + (cd - ab) * tz; // [0,1)
}
static float Fbm(float x, float z, int octaves, float freq, float amp, float lac = 2.03f, float gain = 0.5f) {
    float sum = 0, a = amp, f = freq;
    for (int i = 0; i < octaves; i++) {
        sum += (ValueNoise(x * f, z * f) * 2.0f - 1.0f) * a;
        f *= lac;
        a *= gain;
    }
    return sum;
}
// Ridged noise: sharp creases along noise iso-lines - the classic recipe for
// jagged rock/cliff detail instead of rolling dunes.
static float RidgedNoise(float x, float z) {
    float n = ValueNoise(x, z);
    return 1.0f - std::fabs(2.0f * n - 1.0f);
}
static float RidgedFbm(float x, float z, int octaves, float freq, float amp) {
    float sum = 0, a = amp, f = freq, weight = 1.0f;
    for (int i = 0; i < octaves; i++) {
        float r = RidgedNoise(x * f, z * f);
        r = r * r * weight;
        weight = std::min(1.0f, r * 2.2f); // successive octaves sharpen where the previous one was already ridged
        sum += r * a;
        f *= 2.08f;
        a *= 0.5f;
    }
    return sum;
}
static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Domain-warps world position before it becomes UV, so the texture's tiling
// grid comes out as irregular wavy cells instead of a dead-straight repeating
// lattice - the single biggest fix for "the tiling is obviously a grid",
// since every material layer otherwise samples the same linear UV and their
// repeats all lock into perfect alignment with each other and with the mesh.
// No shader change needed: this only reshapes the UV this tool authors.
static void WarpForUV(float x, float z, float& outX, float& outZ) {
    outX = x + Fbm(x * 0.045f + 300.0f, z * 0.045f + 300.0f, 3, 1.0f, 2.6f);
    outZ = z + Fbm(x * 0.045f - 300.0f, z * 0.045f - 300.0f, 3, 1.0f, 2.6f);
}

// A smooth radial "bump" (dome or basin), positive amp = peak, negative = dip.
struct Bump { float cx, cz, radius, amp; bool isMountain; };

static float BumpFalloff(float x, float z, const Bump& b) {
    float dx = x - b.cx, dz = z - b.cz;
    float dist = std::sqrt(dx * dx + dz * dz);
    return Clamp01(1.0f - dist / b.radius);
}
static float BumpHeight(float t) { float s = Smooth(t); return s * s; } // squared smoothstep -> rounder peak, flatter base

// Diablo-4-style "steppes" reference: the walkable ground near the player is
// gently rolling, almost flat; dramatic jagged peaks sit at the *edges* of
// the playable space as a backdrop, not filling the whole camera view. So the
// central bumps (near the path) are now low, gentle rises, and only the
// outer ring stays tall/dramatic.
static const Bump kBumps[] = {
    { 16.0f,  -6.0f, 17.0f,   5.5f, true  }, // mountain_1 (gentle rise near path)
    {-20.0f, -16.0f, 15.0f,   6.5f, true  }, // mountain_2
    { -2.0f, -26.0f, 14.0f,   5.0f, true  }, // mountain_3
    {  9.0f, -18.0f, 10.0f,   2.5f, true  }, // foothill_1
    { -9.0f, -10.0f,  9.0f,   2.2f, true  }, // foothill_2
    {-14.0f,   6.0f, 11.0f,  -2.0f, false }, // hollow basin (dip, stays smooth/grassy)
    // Outer ring: distant, dramatic peaks framing the playable steppe.
    { 55.0f,  40.0f, 26.0f,  17.0f, true  },
    { 70.0f, -55.0f, 22.0f,  14.0f, true  },
    {-60.0f,  60.0f, 24.0f,  16.0f, true  },
    {-75.0f, -70.0f, 20.0f,  12.5f, true  },
    { 30.0f,  95.0f, 18.0f,  10.0f, true  },
    {-35.0f, -100.0f, 20.0f, 13.0f, true  },
    { 90.0f,  10.0f, 16.0f,   9.0f, true  },
};
static const int kBumpCount = sizeof(kBumps) / sizeof(kBumps[0]);

// A single winding trail threading between the peaks, from the south edge to
// the north edge, skirting the basin - now stretched across the larger map.
static const float kPath[][2] = {
    {  4.0f, -130.0f },
    {  2.0f,  -65.0f },
    { -2.0f,  -42.0f },
    {  6.0f,  -22.0f },
    { -6.0f,   -2.0f },
    {  0.0f,   18.0f },
    {  4.0f,   45.0f },
    { -2.0f,   65.0f },
    {  6.0f,  100.0f },
    {  10.0f, 130.0f },
};
static const int kPathCount = sizeof(kPath) / sizeof(kPath[0]);
static const float kPathHalfWidth = 4.2f;
static const float kPathFeather = 4.5f;

static float PointSegDist(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    float t = len2 > 1e-6f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
    t = Clamp01(t);
    float cx = ax + dx * t, cz = az + dz * t;
    float ex = px - cx, ez = pz - cz;
    return std::sqrt(ex * ex + ez * ez);
}
static float DistToPath(float x, float z) {
    float best = 1e9f;
    for (int i = 0; i + 1 < kPathCount; i++) {
        float d = PointSegDist(x, z, kPath[i][0], kPath[i][1], kPath[i + 1][0], kPath[i + 1][1]);
        best = std::min(best, d);
    }
    return best;
}
// 1 on the path centerline, fading to 0 over kPathFeather beyond the half-width.
// The distance itself is jittered by small-scale noise first, so the trail's
// edge is a ragged, eroded line rather than a mathematically constant offset
// from the centerline - a perfectly parallel-banked ravine reads as artificial.
static float PathFactor(float x, float z) {
    float d = DistToPath(x, z);
    d += (ValueNoise(x * 0.25f, z * 0.25f) - 0.5f) * 2.6f;
    float t = Clamp01((d - kPathHalfWidth) / kPathFeather);
    return 1.0f - Smooth(t);
}

// The smooth, low-frequency shape: gentle rolling ground plus the bump domes/
// basin, with none of the jagged cliff detail. This is both a component of
// the final height AND the "graded trail" target a path carves back down to.
static float RegionalHeight(float x, float z) {
    float h = Fbm(x, z, 3, 0.045f, 1.0f);
    for (int i = 0; i < kBumpCount; i++) {
        float t = BumpFalloff(x, z, kBumps[i]);
        h += kBumps[i].amp * BumpHeight(t);
    }
    return h;
}

// How "rocky" a point is: flank-shaped (0 at the very base AND at the exact
// summit, peaking mid-slope) rather than the raw radial falloff - the raw
// falloff peaks *at* the summit, which carved a crater/donut into every peak
// tip. Real mountains are craggy on the flanks and comparatively rounded at
// the very top (where snow caps sit) and at the foot (where scree blends into
// the valley).
static float RockWeight(float x, float z) {
    float weight = 0.0f;
    for (int i = 0; i < kBumpCount; i++) {
        if (!kBumps[i].isMountain) continue;
        float t = BumpFalloff(x, z, kBumps[i]);
        float flank = 4.0f * t * (1.0f - t);
        weight = std::max(weight, flank);
    }
    return weight;
}

// Rock relief, deliberately split into two scales - the mistake before was
// treating "less jagged" and "less detailed" as the same knob:
//
//   macro  - big rounded ridges that set the *silhouette*. Kept low-amplitude
//            and low-frequency so peaks read as weathered cones, not as thin
//            blade ridgelines.
//   crag   - the surface actually being rock: broken, high-frequency relief a
//            metre or two across. This is what a smooth cone was missing, and
//            what makes the flanks read as stone instead of poured concrete.
//
// The top octave sits near 0.6 (a ~1.6 unit wavelength) because the mesh is
// 0.4 units per vertex - anything finer than about 0.8 units cannot be
// resolved and just aliases into noise.
static float CliffDetail(float x, float z) {
    float weight = RockWeight(x, z);
    // Domain-warp the sampling coordinates so ridges don't radiate in neat
    // rings around each bump centre - the single biggest thing that made the
    // first pass read as symmetric "donuts" instead of natural rock.
    float wx = x + Fbm(x * 0.5f + 100.0f, z * 0.5f + 100.0f, 2, 0.06f, 6.0f);
    float wz = z + Fbm(x * 0.5f - 100.0f, z * 0.5f - 100.0f, 2, 0.06f, 6.0f);

    float macro = (RidgedFbm(wx, wz, 3, 0.032f, 2.4f) - 0.75f) * weight;

    // Broken rock surface. Ridged for sharp-edged crags plus a plain fbm for
    // lumpy irregularity between them; a little of it survives off the
    // mountains so exposed ground elsewhere is stony rather than glassy.
    //
    // Amplitude here is what decides whether a face reads as stone or as
    // poured concrete: at a ~2-8 unit wavelength, an amplitude under about
    // half a unit is only a ~10 degree undulation, which the eye reads as
    // smooth. These values put the crag faces up around 60-70 degrees, which
    // is what actually looks like rock, and the mesh (0.4 units/vertex) still
    // resolves them cleanly.
    float crag = (RidgedFbm(wx * 1.6f + 70.0f, wz * 1.6f + 70.0f, 3, 0.11f, 2.4f) - 0.95f);
    float lumps = Fbm(x + 900.0f, z - 900.0f, 3, 0.15f, 0.9f);
    float surface = (crag + lumps) * (0.3f + 0.7f * weight);

    return macro + surface;
}

// Sedimentary stepping. Quantising height into shallow benches is what puts
// the horizontal ledge/strata bands on a rocky face - and those benches are
// exactly where snow settles into level stripes, which is the read the
// reference peak has. `k` above 1 flattens the tread and steepens the riser,
// so the steps look eroded rather than like a staircase.
static float Terrace(float h, float step, float k) {
    float f = h / step;
    float base = std::floor(f);
    float frac = f - base;
    float shaped = std::pow(Smooth(frac), k);
    return (base + shaped) * step;
}

// A worn trail on near-flat ground, not a canyon - but deep/wide enough to
// read clearly via shading alone, since there is no dedicated path texture
// (an engine bug in the multi-material mask path blocked that approach).
static const float kPathDepth = 1.15f;

// A little bit of rocky character everywhere, not just inside a mountain's
// influence - otherwise the outer stretches of a much larger map read as a
// featureless, perfectly smooth plain between the peaks. Kept subtle so the
// steppe stays a steppe rather than turning bumpy everywhere.
static float BackgroundRoughness(float x, float z) {
    return RidgedFbm(x * 1.3f, z * 1.3f, 3, 0.035f, 0.6f) - 0.3f;
}

static float TerrainHeight(float x, float z) {
    float regional = RegionalHeight(x, z) + BackgroundRoughness(x, z);
    float full = regional + CliffDetail(x, z);

    // Cut ledges into the rocky parts only - the steppe floor must stay a
    // smooth walkable surface, and terracing it would read as contour lines on
    // a map rather than as rock. The bench height itself varies so the strata
    // are not one uniform rhythm across the whole map.
    float rock = RockWeight(x, z);
    if (rock > 0.01f) {
        float step = 0.7f + 0.4f * ValueNoise(x * 0.02f + 11.0f, z * 0.02f + 11.0f);
        float terraced = Terrace(full, step, 2.4f);
        full = Lerp(full, terraced, 0.7f * rock);
    }

    float pathT = PathFactor(x, z);
    float pathTarget = regional - kPathDepth * pathT;
    return Lerp(full, pathTarget, pathT);
}

// Builds one tile's mesh as a node named "Tile_r{row}_c{col}" in `scene`,
// covering world space [x0,x0+TILE_SIZE] x [z0,z0+TILE_SIZE]. Every tile is
// generated on the same world-space lattice (same STEP, positions computed
// directly from TerrainHeight(world x, world z)), so adjacent tiles share
// identical boundary vertices and seam with no gap or crack.
static void BuildTile(FbxScene* scene, int row, int col, float x0, float z0,
                      float tileSize, int segs, float uvTile, float half) {
    float step = tileSize / segs;
    int vertsPerRow = segs + 1;
    std::vector<FbxVector4> cp((size_t)vertsPerRow * vertsPerRow);
    for (int zi = 0; zi <= segs; zi++) {
        for (int xi = 0; xi <= segs; xi++) {
            float x = x0 + xi * step;
            float z = z0 + zi * step;
            float y = TerrainHeight(x, z);
            cp[(size_t)zi * vertsPerRow + xi] = FbxVector4(x, y, z);
        }
    }
    auto VIdx = [&](int xi, int zi) { return zi * vertsPerRow + xi; };

    char nodeName[64];
    std::snprintf(nodeName, sizeof(nodeName), "Tile_r%d_c%d", row, col);
    FbxNode* node = FbxNode::Create(scene, nodeName);
    node->LclScaling.Set(FbxDouble3(100.0, 100.0, 100.0));
    node->LclTranslation.Set(FbxDouble3(0.0, 0.0, 0.0));
    node->LclRotation.Set(FbxDouble3(0.0, 0.0, 0.0));
    scene->GetRootNode()->AddChild(node);

    FbxMesh* mesh = FbxMesh::Create(scene, (std::string(nodeName) + "Mesh").c_str());
    mesh->InitControlPoints((int)cp.size());
    FbxVector4* meshCp = mesh->GetControlPoints();
    for (size_t i = 0; i < cp.size(); i++) meshCp[i] = cp[i];

    mesh->CreateLayer();
    FbxLayer* layer0 = mesh->GetLayer(0);

    FbxLayerElementNormal* normalLayer = FbxLayerElementNormal::Create(mesh, "");
    normalLayer->SetMappingMode(FbxLayerElement::eByPolygonVertex);
    normalLayer->SetReferenceMode(FbxLayerElement::eDirect);

    FbxLayerElementUV* uvLayer = FbxLayerElementUV::Create(mesh, "UVMap");
    uvLayer->SetMappingMode(FbxLayerElement::eByPolygonVertex);
    uvLayer->SetReferenceMode(FbxLayerElement::eDirect);

    int triCount = segs * segs * 2;
    normalLayer->GetDirectArray().SetCount(triCount * 3);
    uvLayer->GetDirectArray().SetCount(triCount * 3);

    int polyVertCursor = 0;
    for (int zi = 0; zi < segs; zi++) {
        for (int xi = 0; xi < segs; xi++) {
            int i00 = VIdx(xi, zi), i10 = VIdx(xi + 1, zi);
            int i01 = VIdx(xi, zi + 1), i11 = VIdx(xi + 1, zi + 1);

            int tris[2][3] = { { i00, i11, i10 }, { i00, i01, i11 } };
            for (int t = 0; t < 2; t++) {
                int a = tris[t][0], b = tris[t][1], c = tris[t][2];
                FbxVector4 pa = meshCp[a], pb = meshCp[b], pc = meshCp[c];
                FbxVector4 e1 = pb - pa, e2 = pc - pa;
                FbxVector4 n(
                    e1.mData[1] * e2.mData[2] - e1.mData[2] * e2.mData[1],
                    e1.mData[2] * e2.mData[0] - e1.mData[0] * e2.mData[2],
                    e1.mData[0] * e2.mData[1] - e1.mData[1] * e2.mData[0]);
                double len = std::sqrt(n.mData[0] * n.mData[0] + n.mData[1] * n.mData[1] + n.mData[2] * n.mData[2]);
                if (len > 1e-8) { n.mData[0] /= len; n.mData[1] /= len; n.mData[2] /= len; }
                if (n.mData[1] < 0) { n.mData[0] = -n.mData[0]; n.mData[1] = -n.mData[1]; n.mData[2] = -n.mData[2]; }

                mesh->BeginPolygon();
                int idx[3] = { a, b, c };
                for (int k = 0; k < 3; k++) {
                    mesh->AddPolygon(idx[k]);
                    normalLayer->GetDirectArray().SetAt(polyVertCursor, FbxVector4(n.mData[0], n.mData[1], n.mData[2], 0));
                    float px = (float)meshCp[idx[k]].mData[0];
                    float py = (float)meshCp[idx[k]].mData[1];
                    float pz = (float)meshCp[idx[k]].mData[2];
                    // UV is a function of *world* position, not tile-local
                    // index, so texture tiling is continuous across tiles too.
                    float wx, wz;
                    WarpForUV(px, pz, wx, wz);

                    // Dominant-axis projection, picked per *triangle* from its
                    // own normal. A single top-down (x,z) projection smears a
                    // few texels down the whole of any steep face - the XZ
                    // footprint barely moves while Y runs the length of the
                    // face - which is very visible now that crags are near
                    // vertical. Choosing the plane the face most faces keeps
                    // texel density even everywhere.
                    //
                    // Per triangle rather than per vertex on purpose: one
                    // triangle must not mix two projections, or the texture
                    // swims across it. The cost is a seam where neighbouring
                    // faces pick different axes, which on a homogeneous rock
                    // texture reads as far less than the stretching did.
                    double ax = std::fabs(n.mData[0]);
                    double ay = std::fabs(n.mData[1]);
                    double az = std::fabs(n.mData[2]);
                    FbxVector2 uv;
                    if (ay >= ax && ay >= az) {
                        uv = FbxVector2((wx + half) / uvTile, (wz + half) / uvTile);
                    } else if (ax >= az) {
                        uv = FbxVector2((wz + half) / uvTile, py / uvTile);
                    } else {
                        uv = FbxVector2((wx + half) / uvTile, py / uvTile);
                    }
                    uvLayer->GetDirectArray().SetAt(polyVertCursor, uv);
                    polyVertCursor++;
                }
                mesh->EndPolygon();
            }
        }
    }

    layer0->SetNormals(normalLayer);
    layer0->SetUVs(uvLayer, FbxLayerElement::eTextureDiffuse);
    node->SetNodeAttribute(mesh);
}

int main() {
    const float HALF = 140.0f;       // 280x280 total - 4x the previous single-mesh terrain
    const int TILES_PER_SIDE = 7;    // 49 tiles
    const float TILE_SIZE = (HALF * 2.0f) / TILES_PER_SIDE; // 40 units
    const int SEGS_PER_TILE = 100;   // 20000 tris/tile LOD0 - each tile is its own object/draw call,
                                      // so this is well clear of the ~28800 single-mesh crash ceiling
                                      // found earlier, with margin since that ceiling was never pinned
                                      // more precisely than "between 28800 and 39200".
    const float UV_TILE = 6.0f;

    FbxManager* mgr = FbxManager::Create();
    FbxIOSettings* ios = FbxIOSettings::Create(mgr, IOSROOT);
    mgr->SetIOSettings(ios);
    FbxScene* scene = FbxScene::Create(mgr, "scene");

    int tileCount = 0;
    for (int row = 0; row < TILES_PER_SIDE; row++) {
        for (int col = 0; col < TILES_PER_SIDE; col++) {
            float x0 = -HALF + col * TILE_SIZE;
            float z0 = -HALF + row * TILE_SIZE;
            BuildTile(scene, row, col, x0, z0, TILE_SIZE, SEGS_PER_TILE, UV_TILE, HALF);
            tileCount++;
        }
    }

    FbxExporter* exporter = FbxExporter::Create(mgr, "");
    int fileFormat = mgr->GetIOPluginRegistry()->GetNativeWriterFormat();
    const char* outPath = "terrain.fbx";
    if (!exporter->Initialize(outPath, fileFormat, mgr->GetIOSettings())) {
        printf("Exporter Initialize failed: %s\n", exporter->GetStatus().GetErrorString());
        return 1;
    }
    bool ok = exporter->Export(scene);
    exporter->Destroy();
    int trisPerTile = SEGS_PER_TILE * SEGS_PER_TILE * 2;
    printf(ok ? "OK wrote %s (%d tiles, %d tris/tile, %d total tris)\n" : "Export FAILED\n",
        outPath, tileCount, trisPerTile, tileCount * trisPerTile);

    mgr->Destroy();
    return ok ? 0 : 1;
}
