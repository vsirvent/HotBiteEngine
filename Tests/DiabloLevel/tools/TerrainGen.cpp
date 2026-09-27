// Standalone one-shot tool: builds a heightmap-displaced terrain mesh (rolling
// steppe, rocky mountains with a carved winding path) and exports it as a
// binary FBX the HotBiteEngine importer can load directly. Not part of the
// engine build - compiled and run once against the repo's vendored FbxSdk.
// See Tests/DiabloLevel/tools/README.md for how to build/run it and why the
// terrain looks the way it does.
//
// This is Solo/1's generator - 280x280 units, one hand-authored bump list and
// one hand-authored path polyline. The shared noise/height/mesh-building code
// lives in TerrainGenCommon.h now (also used by TerrainGenBig.cpp, Solo/2's
// procedural 1000x1000 generator, and TerrainPreview.cpp); this file's own
// content - `kBumps`, `kPath`, the 7x7/40-unit tile loop - is unchanged from
// before that split, so re-running it reproduces Solo/1's terrain.fbx exactly.
#include "TerrainGenMesh.h"

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
static const std::array<float, 2> kPathPts[] = {
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
static const int kPathPtCount = sizeof(kPathPts) / sizeof(kPathPts[0]);
static const float kPathHalfWidth = 4.2f;

int main() {
    const float HALF = 140.0f;       // 280x280 total - 4x the previous single-mesh terrain
    const int TILES_PER_SIDE = 7;    // 49 tiles
    const float TILE_SIZE = (HALF * 2.0f) / TILES_PER_SIDE; // 40 units
    const int SEGS_PER_TILE = 100;   // 20000 tris/tile LOD0 - each tile is its own object/draw call,
                                      // so this is well clear of the ~28800 single-mesh crash ceiling
                                      // found earlier, with margin since that ceiling was never pinned
                                      // more precisely than "between 28800 and 39200".
    const float UV_TILE = 6.0f;

    std::vector<Bump> bumps(kBumps, kBumps + kBumpCount);
    std::vector<std::array<float, 2>> pathPts(kPathPts, kPathPts + kPathPtCount);
    std::vector<PathSegment> paths = { { pathPts, kPathHalfWidth } };

    FbxManager* mgr = FbxManager::Create();
    FbxIOSettings* ios = FbxIOSettings::Create(mgr, IOSROOT);
    mgr->SetIOSettings(ios);
    FbxScene* scene = FbxScene::Create(mgr, "scene");

    int tileCount = 0;
    for (int row = 0; row < TILES_PER_SIDE; row++) {
        for (int col = 0; col < TILES_PER_SIDE; col++) {
            float x0 = -HALF + col * TILE_SIZE;
            float z0 = -HALF + row * TILE_SIZE;
            BuildTile(scene, row, col, x0, z0, TILE_SIZE, SEGS_PER_TILE, UV_TILE, HALF, bumps, paths);
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
