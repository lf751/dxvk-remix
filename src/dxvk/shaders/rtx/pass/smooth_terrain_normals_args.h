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

#include "smooth_normals_binding_indices.h"

#define SMOOTH_TERRAIN_BINDING_CHUNKS 0
#define SMOOTH_TERRAIN_BINDING_JOBS   1
#define SMOOTH_TERRAIN_BINDING_WORLD_WRITE 4
#define SMOOTH_TERRAIN_BINDING_WORLD_READ  5

struct SmoothTerrainChunkArgs {
  uint64_t positionAddress;
  uint64_t normalAddress;
  uint64_t indexAddress;
  uint vertexStart;
  uint numVertices;
  uint numTriangles;
  uint positionStride;
  uint normalStride;
  uint useShortIndices;
  vec4 worldRows[3];
  float orientation;
  uint padding[3];
};

struct SmoothTerrainNormalJob {
  uint chunk;
  uint first;
};

struct SmoothTerrainNormalsArgs {
  uint phase;
  uint hashTableSize;
  uint jobStart;
};

#ifdef __cplusplus
static_assert(sizeof(SmoothTerrainChunkArgs) == 112);
static_assert(sizeof(SmoothTerrainNormalJob) == 8);
#endif
