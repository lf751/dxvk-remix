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
#include "smooth_terrain_normals_args.h"
#include "smooth_normals.h"

layout(binding = SMOOTH_TERRAIN_BINDING_CHUNKS)
StructuredBuffer<SmoothTerrainChunkArgs> chunks;
layout(binding = SMOOTH_TERRAIN_BINDING_JOBS)
StructuredBuffer<SmoothTerrainNormalJob> jobs;
layout(binding = SMOOTH_NORMALS_BINDING_HASH_TABLE)
RWStructuredBuffer<int> hashTable;
layout(binding = SMOOTH_TERRAIN_BINDING_WORLD_WRITE)
RWStructuredBuffer<float> worldPositionOutput;
layout(binding = SMOOTH_TERRAIN_BINDING_WORLD_READ)
StructuredBuffer<float> worldPositions;
layout(push_constant)
ConstantBuffer<SmoothTerrainNormalsArgs> cb;

uint terrainLoadIndex(SmoothTerrainChunkArgs chunk, uint index) {
  if (chunk.useShortIndices == 0) {
    uint* indices = (uint*)chunk.indexAddress;
    return indices[index];
  }
  const uint64_t alignedAddress = chunk.indexAddress & ~3ull;
  const uint half = index + uint((chunk.indexAddress - alignedAddress) / 2);
  uint* indices = (uint*)alignedAddress;
  return (indices[half / 2] >> (16 * (half & 1))) & 0xffffu;
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint group : SV_GroupID, uint lane : SV_GroupThreadID) {
  const SmoothTerrainNormalJob job = jobs[cb.jobStart + group];
  const SmoothTerrainChunkArgs chunk = chunks[job.chunk];
  const uint idx = job.first + lane;
  SmoothNormalsArgs world = {};
  world.positionStride = 16;
  world.hashTableSize = cb.hashTableSize;

  if (cb.phase == 0 && idx < chunk.numVertices) {
    float* source = (float*)chunk.positionAddress;
    const uint base = idx * chunk.positionStride;
    const float4 p = float4(source[base], source[base + 1], source[base + 2], 1.f);
    const float3 position = float3(dot(p, chunk.worldRows[0]), dot(p, chunk.worldRows[1]), dot(p, chunk.worldRows[2]));
    // Quantization absorbs sub-texel floating-point differences between chunk transforms.
    const float3 key = round(position * 4096.f) / 4096.f;
    const uint output = (chunk.vertexStart + idx) * 4;
    worldPositionOutput[output] = key.x;
    worldPositionOutput[output + 1] = key.y;
    worldPositionOutput[output + 2] = key.z;
  } else if (cb.phase == 1 && idx < chunk.numTriangles) {
    const uint3 local = uint3(terrainLoadIndex(chunk, idx * 3), terrainLoadIndex(chunk, idx * 3 + 1),
      terrainLoadIndex(chunk, idx * 3 + 2));
    if (any(local >= chunk.numVertices)) return;
    const uint3 indices = chunk.vertexStart + local;
    const float3 p0 = smoothNormalsLoadPosition(indices.x, worldPositions, world);
    const float3 p1 = smoothNormalsLoadPosition(indices.y, worldPositions, world);
    const float3 p2 = smoothNormalsLoadPosition(indices.z, worldPositions, world);
    const float3 normal = cross(p1 - p0, p2 - p0);
    const float magnitude = length(normal);
    if (!isfinite(magnitude) || magnitude <= 1e-20f) return;
    const float3 facing = normal * (chunk.orientation / magnitude);
    accumulateAtPosition(indices.x, p0, facing, worldPositions, hashTable, world);
    accumulateAtPosition(indices.y, p1, facing, worldPositions, hashTable, world);
    accumulateAtPosition(indices.z, p2, facing, worldPositions, hashTable, world);
  } else if (cb.phase == 2 && idx < chunk.numVertices) {
    const float3 p = smoothNormalsLoadPosition(chunk.vertexStart + idx, worldPositions, world);
    const float3 normal = lookupSmoothedNormal(p, worldPositions, hashTable, world);
    const float3 local = normal.x * chunk.worldRows[0].xyz + normal.y * chunk.worldRows[1].xyz + normal.z * chunk.worldRows[2].xyz;
    const float magnitude = length(local);
    if (!isfinite(magnitude) || magnitude <= 1e-7f) return;
    uint* normals = (uint*)chunk.normalAddress;
    normals[idx * chunk.normalStride] = encodeNormal(local / magnitude);
  }
}
