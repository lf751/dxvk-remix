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

#include "rtx_terrain_grass_math.h"
#include "../util/util_matrix.h"
#include "../util/util_vector.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace dxvk {
  struct MeshReplacement;
  struct MaterialData;

  struct TerrainGrassMeshPart {
    std::shared_ptr<MeshReplacement> geometry;
    std::shared_ptr<MaterialData> materialData;
    Matrix4 meshToPrototype;
    uint32_t categoryFlagsRaw = 0;
  };

  struct TerrainGrassPrototype {
    std::vector<TerrainGrassMeshPart> meshes;
  };

  struct TerrainGrassRingConfig {
    std::vector<TerrainGrassPrototype> prototypes;
    float spacing = 2.0f;
    float outerRadius = 75.0f;
    float scale = 1.0f;
  };

  struct TerrainGrassPreset {
    bool enabled = true;
    bool hideVanillaGrass = true;
    bool flipUp = true;
    bool flipUpAuthored = false;
    bool useTerrainBlendRules = true;
    float worldSize = 1024.0f;
    Vector2 worldOrigin = Vector2(0.f, 0.f);
    float minElevation = 0.25f;
    float maxSlopeDegrees = 35.0f;
    float sinkDepth = 0.05f;
    float normalAlign = 0.65f;
    float borderShrinkWidth = 1.5f;
    float minScale = 0.85f;
    float maxScale = 1.25f;
    float transitionWidth = 12.0f;
    float coverageThreshold = 0.5f;
    // 0 = Near, 1 = Mid, 2 = Far (whole island)
    TerrainGrassRingConfig rings[kTerrainGrassRingCount];
    uint64_t generation = 0;

    TerrainGrassPreset() {
      rings[0].spacing = 1.8f;
      rings[0].outerRadius = 75.0f;
      rings[0].scale = 1.0f;
      rings[1].spacing = 3.8f;
      rings[1].outerRadius = 220.0f;
      rings[1].scale = 1.0f;
      rings[2].spacing = 8.0f;
      rings[2].outerRadius = 1024.0f;
      rings[2].scale = 1.0f;
    }
  };

  // Attached to synthetic grass RtSurface / PointInstancerBatch entries so the GPU
  // scatter shader knows which ring, variant, and toroidal grid layout this batch owns.
  struct TerrainGrassBatchInfo {
    Matrix4 meshToPrototype;
    uint32_t ringIndex = 0;
    uint32_t variantIndex = 0;
    uint32_t variantCount = 1;
    uint32_t gridWidth = 1;
    uint32_t rowSlots = 1;
    bool isWholeIsland = false;
    float spacing = 2.0f;
    float innerRadius = 0.f;
    float outerRadius = 75.0f;
    float ringScale = 1.0f;
  };

} // namespace dxvk
