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

#include "rtx/pass/common_binding_indices.h"
#include "rtx/utility/shader_types.h"

// ---- Terrain Height + Normal Atlas rasterizer ----

struct TerrainGrassHeightChunk {
  uint64_t positionAddress;
  uint64_t indexAddress;
  uint32_t positionStride;
  uint32_t numTriangles;
  uint32_t numVertices;
  uint32_t useShortIndices;
  vec4 worldRows[3];
};

struct TerrainGrassHeightJob {
  uint32_t chunk;
  uint32_t firstTriangle;
};

struct TerrainGrassHeightConstants {
  vec4 worldOffset;       // Game-world offset (= -artificialWorldOffset)
  vec2 worldOrigin;       // Horizontal origin matching atlas UV (0, 0)
  float worldSize;        // Horizontal span matching atlas UV (1, 1)
  uint32_t atlasSize;     // Atlas width/height in texels (1024)
  uint32_t zUp;           // 1 if world up is Z, 0 if world up is Y
  uint32_t flipUp;        // 1 if skyward is -Y/-Z (JPOG inverted height)
  uint32_t jobCount;
  uint32_t pad0;
};

#define TERRAIN_GRASS_HEIGHT_BINDING_CONSTANTS  50
#define TERRAIN_GRASS_HEIGHT_BINDING_CHUNKS     51
#define TERRAIN_GRASS_HEIGHT_BINDING_JOBS       52
#define TERRAIN_GRASS_HEIGHT_BINDING_ATLAS_OUT  53

// ---- Terrain Grass Scatter (PointInstancer-compatible GPU writer) ----

struct TerrainGrassScatterBatch {
  mat4 meshToPrototype;       // Prototype submesh local transform (column-major)
  uint32_t baseSurfaceIndex;  // surfaceIndexOfFirstInstance
  uint32_t instanceCount;     // Total slots reserved for this batch
  uint32_t customIndexFlags;  // Upper bits of instanceCustomIndex
  uint32_t instanceMask;      // 8-bit visibility mask
  uint32_t sbtOffsetAndFlags; // Packed SBT offset (24) | VkGeometryInstanceFlags (8)
  uint32_t blasRefLo;
  uint32_t blasRefHi;
  uint32_t instanceBufferOffset;
  uint32_t firstThread;       // Global thread index of slot 0
  uint32_t writeSurfaceData;  // 0 for SSS twin
  uint32_t ringIndex;         // 0 = Near, 1 = Mid, 2 = Far
  uint32_t variantIndex;      // Prototype index within this ring
  uint32_t variantCount;      // Number of prototypes in this ring
  uint32_t gridWidth;         // Grid cells per axis (multiple of variantCount)
  uint32_t rowSlots;          // gridWidth / variantCount
  uint32_t isWholeIsland;     // 1 = fixed world grid covering [0, worldSize], 0 = toroidal around camera
  float spacing;              // Cell size in world units
  float innerRadius;          // Inner ring radius (0 for Near)
  float outerRadius;          // Outer ring radius
  float ringScale;            // Per-ring scale multiplier
};

struct TerrainGrassScatterConstants {
  vec4 cameraWorldPos;        // Camera position in game-world coordinates
  vec4 worldOffset;           // Game-world offset (= -artificialWorldOffset)
  vec4 prevWorldOffset;       // Previous-frame game-world offset (for motion vectors)
  vec2 worldOrigin;
  float worldSize;
  float coverageUvScale;      // 1 / 1024 for JPOG
  float minElevation;
  float maxSlopeCos;          // cos(maxSlopeDegrees)
  float slopeFadeWidth;       // cos-angle fade band before maxSlopeCos
  float sinkDepth;
  float normalAlign;          // 0 = vertical, 1 = full terrain normal
  float borderShrinkWidth;    // world units over which clumps shrink near excluded coverage texels
  float minScale;
  float maxScale;
  float transitionWidth;      // ring cross-fade width in world units
  float coverageThreshold;
  float rockLowElevation;     // TerrainBlend rock elevation suppression (if active)
  float rockHighElevation;
  uint32_t rockElevationSuppress; // 1 if TerrainBlend rockMode uses elevation
  uint32_t zUp;
  uint32_t flipUp;
  uint32_t hasCoverageTexture;
  uint32_t atlasSize;
  uint32_t threadCount;
  uint32_t pad0;
  uint32_t pad1;
};

#define TERRAIN_GRASS_SCATTER_BINDING_CONSTANTS       50
#define TERRAIN_GRASS_SCATTER_BINDING_BATCHES         51
#define TERRAIN_GRASS_SCATTER_BINDING_THREAD_BATCH    52
#define TERRAIN_GRASS_SCATTER_BINDING_HEIGHT_ATLAS    53
#define TERRAIN_GRASS_SCATTER_BINDING_COVERAGE_TEX    54
#define TERRAIN_GRASS_SCATTER_BINDING_INSTANCE_BUFFER 55
#define TERRAIN_GRASS_SCATTER_BINDING_SURFACE_BUFFER  56
#define TERRAIN_GRASS_SCATTER_BINDING_MATERIAL_BUFFER 57

#if TERRAIN_GRASS_HEIGHT_BINDING_CONSTANTS <= COMMON_MAX_BINDING
#error "Terrain grass binding indices overlap common bindings!"
#endif
