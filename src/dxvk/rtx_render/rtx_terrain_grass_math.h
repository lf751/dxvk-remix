/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dxvk {

  static constexpr uint32_t kTerrainGrassRingCount = 3u;
  static constexpr uint32_t kTerrainGrassAtlasSize = 1024u;

  struct TerrainGrassRingLayout {
    uint32_t gridWidth = 0;         // Grid cells per axis (always a multiple of variantCount)
    uint32_t rowSlots = 0;          // Slots per row per variant (gridWidth / variantCount)
    uint32_t slotsPerVariant = 0;   // Total slots per prototype variant (rowSlots * gridWidth)
    uint32_t variantCount = 1;
    bool isWholeIsland = false;
  };

  // Computes a toroidal (for Near/Mid) or whole-island (for Far) grid layout whose width
  // is a multiple of variantCount so every world cell maps to a unique (variant, slot)
  // that remains invariant as the camera moves.
  inline TerrainGrassRingLayout computeTerrainGrassRingLayout(
      float spacing,
      float outerRadius,
      float worldSize,
      uint32_t variantCount,
      bool isWholeIsland,
      uint32_t maxGridWidth) {
    TerrainGrassRingLayout layout {};
    const uint32_t v = std::max<uint32_t>(1u, variantCount);
    const float safeSpacing = std::max(0.25f, spacing);
    const uint32_t safeMax = std::max<uint32_t>(v, maxGridWidth);

    uint32_t rawWidth = 1u;
    if (isWholeIsland) {
      rawWidth = static_cast<uint32_t>(std::ceil(std::max(safeSpacing, worldSize) / safeSpacing));
    } else {
      const uint32_t half = static_cast<uint32_t>(std::ceil(std::max(safeSpacing, outerRadius) / safeSpacing));
      rawWidth = half * 2u + 1u;
    }

    uint32_t w = std::max<uint32_t>(rawWidth, v);
    w = ((w + v - 1u) / v) * v;
    if (w > safeMax) {
      w = std::max<uint32_t>(v, (safeMax / v) * v);
    }

    layout.gridWidth = w;
    layout.variantCount = v;
    layout.rowSlots = w / v;
    layout.slotsPerVariant = layout.rowSlots * w;
    layout.isWholeIsland = isWholeIsland;
    return layout;
  }

  // Exact CPU mirror of the GPU slot-to-world-cell mapping in terrain_grass_scatter.comp.slang.
  inline void terrainGrassCellFromSlot(
      uint32_t slotIdx,
      uint32_t variantIndex,
      const TerrainGrassRingLayout& layout,
      int32_t cameraCellX,
      int32_t cameraCellZ,
      int32_t& outCellX,
      int32_t& outCellZ) {
    const uint32_t v = layout.variantCount;
    const uint32_t w = layout.gridWidth;
    const uint32_t localZ = slotIdx / layout.rowSlots;
    const uint32_t k = slotIdx % layout.rowSlots;
    const uint32_t xOffset = (variantIndex + v - (localZ % v)) % v;
    const uint32_t localX = k * v + xOffset;

    if (layout.isWholeIsland) {
      outCellX = static_cast<int32_t>(localX);
      outCellZ = static_cast<int32_t>(localZ);
      return;
    }

    const int32_t wi = static_cast<int32_t>(w);
    const int32_t baseX = cameraCellX - wi / 2;
    const int32_t baseZ = cameraCellZ - wi / 2;
    const int32_t modBaseX = ((baseX % wi) + wi) % wi;
    const int32_t modBaseZ = ((baseZ % wi) + wi) % wi;
    outCellX = baseX + ((static_cast<int32_t>(localX) - modBaseX + wi) % wi);
    outCellZ = baseZ + ((static_cast<int32_t>(localZ) - modBaseZ + wi) % wi);
  }

} // namespace dxvk
