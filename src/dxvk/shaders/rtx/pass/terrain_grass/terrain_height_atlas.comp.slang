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
#include "rtx/pass/terrain_grass/terrain_grass_bindings.h"

layout(binding = TERRAIN_GRASS_HEIGHT_BINDING_CONSTANTS)
ConstantBuffer<TerrainGrassHeightConstants> cb;

layout(binding = TERRAIN_GRASS_HEIGHT_BINDING_CHUNKS)
StructuredBuffer<TerrainGrassHeightChunk> chunks;

layout(binding = TERRAIN_GRASS_HEIGHT_BINDING_JOBS)
StructuredBuffer<TerrainGrassHeightJob> jobs;

layout(rgba32f, binding = TERRAIN_GRASS_HEIGHT_BINDING_ATLAS_OUT)
RWTexture2D<float4> heightNormalAtlas;

uint loadTerrainIndex(TerrainGrassHeightChunk chunk, uint index) {
  if (chunk.useShortIndices == 0) {
    uint* indices = (uint*)chunk.indexAddress;
    return indices[index];
  }
  const uint64_t alignedAddress = chunk.indexAddress & ~3ull;
  const uint half = index + uint((chunk.indexAddress - alignedAddress) / 2);
  uint* indices = (uint*)alignedAddress;
  return (indices[half / 2] >> (16 * (half & 1))) & 0xffffu;
}

float3 loadWorldPos(TerrainGrassHeightChunk chunk, uint vertexIdx) {
  float* src = (float*)chunk.positionAddress;
  const uint base = vertexIdx * chunk.positionStride;
  const float4 local = float4(src[base], src[base + 1], src[base + 2], 1.f);
  const float3 renderPos = float3(
    dot(local, chunk.worldRows[0]),
    dot(local, chunk.worldRows[1]),
    dot(local, chunk.worldRows[2]));
  return renderPos + cb.worldOffset.xyz;
}

float2 toPlane(float3 p) {
  return cb.zUp != 0 ? p.xy : p.xz;
}

float toHeight(float3 p) {
  return cb.zUp != 0 ? p.z : p.y;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint group : SV_GroupID, uint lane : SV_GroupThreadID) {
  if (group >= cb.jobCount) {
    return;
  }

  const TerrainGrassHeightJob job = jobs[group];
  const TerrainGrassHeightChunk chunk = chunks[job.chunk];
  const uint triIdx = job.firstTriangle + lane;
  if (triIdx >= chunk.numTriangles) {
    return;
  }

  const uint i0 = loadTerrainIndex(chunk, triIdx * 3u + 0u);
  const uint i1 = loadTerrainIndex(chunk, triIdx * 3u + 1u);
  const uint i2 = loadTerrainIndex(chunk, triIdx * 3u + 2u);
  if (i0 >= chunk.numVertices || i1 >= chunk.numVertices || i2 >= chunk.numVertices) {
    return;
  }

  const float3 p0 = loadWorldPos(chunk, i0);
  const float3 p1 = loadWorldPos(chunk, i1);
  const float3 p2 = loadWorldPos(chunk, i2);

  float3 n = cross(p1 - p0, p2 - p0);
  const float nLen = length(n);
  if (!isfinite(nLen) || nLen <= 1e-12f) {
    return;
  }
  n /= nLen;

  const float upSign = cb.flipUp != 0 ? -1.f : 1.f;
  const float3 upDir = cb.zUp != 0 ? float3(0.f, 0.f, upSign) : float3(0.f, upSign, 0.f);
  if (dot(n, upDir) < 0.f) {
    n = -n;
  }

  const float invWorld = float(cb.atlasSize) / max(cb.worldSize, 1e-4f);
  const float2 t0 = (toPlane(p0) - cb.worldOrigin) * invWorld;
  const float2 t1 = (toPlane(p1) - cb.worldOrigin) * invWorld;
  const float2 t2 = (toPlane(p2) - cb.worldOrigin) * invWorld;

  const float2 bmin = min(t0, min(t1, t2));
  const float2 bmax = max(t0, max(t1, t2));

  // Reject out-of-bounds or abnormally large triangles (e.g. non-terrain sky/water skirts)
  if (bmax.x < 0.f || bmax.y < 0.f || bmin.x >= float(cb.atlasSize) || bmin.y >= float(cb.atlasSize)) {
    return;
  }
  const float2 span = bmax - bmin;
  if (span.x > 64.f || span.y > 64.f) {
    return;
  }

  const float2 e0 = t1 - t0;
  const float2 e1 = t2 - t0;
  const float area = e0.x * e1.y - e0.y * e1.x;
  if (abs(area) <= 1e-8f) {
    return;
  }
  const float invArea = 1.f / area;

  const int minX = max(0, int(floor(bmin.x)));
  const int minY = max(0, int(floor(bmin.y)));
  const int maxX = min(int(cb.atlasSize) - 1, int(ceil(bmax.x)));
  const int maxY = min(int(cb.atlasSize) - 1, int(ceil(bmax.y)));

  const float h0 = toHeight(p0);
  const float h1 = toHeight(p1);
  const float h2 = toHeight(p2);

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const float2 samplePos = float2(float(x) + 0.5f, float(y) + 0.5f);
      const float2 d = samplePos - t0;
      const float w1 = (d.x * e1.y - d.y * e1.x) * invArea;
      const float w2 = (e0.x * d.y - e0.y * d.x) * invArea;
      const float w0 = 1.f - w1 - w2;
      if (w0 >= -1e-3f && w1 >= -1e-3f && w2 >= -1e-3f) {
        const float h = w0 * h0 + w1 * h1 + w2 * h2;
        heightNormalAtlas[uint2(x, y)] = float4(h, n.x, n.y, n.z);
      }
    }
  }
}
