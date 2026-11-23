/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*/
#include "../../src/dxvk/rtx_render/rtx_terrain_grass_math.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <utility>

namespace {
  using namespace dxvk;

  void testToroidalBijectionAndStability() {
    for (uint32_t variants : { 1u, 2u, 3u, 4u }) {
      const TerrainGrassRingLayout layout = computeTerrainGrassRingLayout(
        1.8f, 75.0f, 1024.0f, variants, false, 128u);

      assert(layout.gridWidth > 0u);
      assert(layout.gridWidth % variants == 0u);
      assert(layout.rowSlots == layout.gridWidth / variants);
      assert(layout.slotsPerVariant == layout.rowSlots * layout.gridWidth);

      const int32_t camX = 317;
      const int32_t camZ = 482;
      const int32_t wi = static_cast<int32_t>(layout.gridWidth);
      const int32_t minX = camX - wi / 2;
      const int32_t minZ = camZ - wi / 2;

      std::set<std::pair<int32_t, int32_t>> seen;
      for (uint32_t v = 0; v < variants; ++v) {
        for (uint32_t slot = 0; slot < layout.slotsPerVariant; ++slot) {
          int32_t cx = 0, cz = 0;
          terrainGrassCellFromSlot(slot, v, layout, camX, camZ, cx, cz);

          assert(cx >= minX && cx < minX + wi);
          assert(cz >= minZ && cz < minZ + wi);
          const auto inserted = seen.emplace(cx, cz);
          assert(inserted.second && "Duplicate cell emitted across slots/variants!");

          // Moving camera by a few cells must keep interior cells at the exact same (v, slot).
          const int32_t camX2 = camX + 3;
          const int32_t camZ2 = camZ - 2;
          const int32_t minX2 = camX2 - wi / 2;
          const int32_t minZ2 = camZ2 - wi / 2;
          if (cx >= minX2 && cx < minX2 + wi && cz >= minZ2 && cz < minZ2 + wi) {
            int32_t cx2 = 0, cz2 = 0;
            terrainGrassCellFromSlot(slot, v, layout, camX2, camZ2, cx2, cz2);
            assert(cx2 == cx && cz2 == cz && "Interior cell shifted slot on camera pan!");
          }
        }
      }
      assert(seen.size() == size_t(layout.gridWidth) * size_t(layout.gridWidth));
    }
  }

  void testWholeIslandCoverage() {
    const TerrainGrassRingLayout layout = computeTerrainGrassRingLayout(
      8.0f, 1024.0f, 1024.0f, 2u, true, 160u);
    assert(layout.isWholeIsland);
    assert(layout.gridWidth == 128u);
    assert(layout.slotsPerVariant == 8192u);

    std::set<std::pair<int32_t, int32_t>> seen;
    for (uint32_t v = 0; v < 2u; ++v) {
      for (uint32_t slot = 0; slot < layout.slotsPerVariant; ++slot) {
        int32_t cx = 0, cz = 0;
        terrainGrassCellFromSlot(slot, v, layout, 0, 0, cx, cz);
        assert(cx >= 0 && cx < 128 && cz >= 0 && cz < 128);
        assert(seen.emplace(cx, cz).second);
      }
    }
    assert(seen.size() == 128u * 128u);
  }
}

int main() {
  testToroidalBijectionAndStability();
  testWholeIslandCoverage();
  std::cout << "test_terrain_grass: PASSED" << std::endl;
  return 0;
}
