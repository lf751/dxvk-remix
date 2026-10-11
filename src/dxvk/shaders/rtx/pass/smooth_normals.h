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

#include "../utility/cpu_gpu_compat.h"

// Shared smooth normals implementation — compiles as both C++ and Slang/HLSL.

#ifdef __cplusplus
namespace dxvk {

#define RWIntBuffer     int32_t*
#define ConstBuffer(T)  const T&

#else // GPU (Slang/HLSL)

#define RWIntBuffer      RWStructuredBuffer<int>
// Use plain struct (not ConstantBuffer<T>) so callers can pass push constants
// or regular constant buffers alike — Slang handles the implicit conversion.
#define ConstBuffer(T)  T

#endif

// Scale factor for fixed-point integer accumulation.
// Face normals are normalized to unit length before scaling, so each triangle
// contributes at most FIXED_POINT_SCALE per component.  With 10000, a vertex
// can safely accumulate normals from up to ~200 000 triangles before int32
// overflow.  The angular precision is ~0.006 degrees.
static const float FIXED_POINT_SCALE = 10000.0f;

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Load a vertex position from the position buffer.
float3 smoothNormalsLoadPosition(uint vertexIndex,
                                 ReadBuffer(float) positionData,
                                 ConstBuffer(SmoothNormalsArgs) cb)
{
  uint baseOffset = cb.positionOffset / 4 + vertexIndex * (cb.positionStride / 4);
  return float3(
    positionData[baseOffset + 0],
    positionData[baseOffset + 1],
    positionData[baseOffset + 2]
  );
}

// Load an index from the index buffer.
uint smoothNormalsLoadIndex(uint idx,
                            ReadBuffer(uint) srcIndex,
                            ConstBuffer(SmoothNormalsArgs) cb)
{
  if (cb.useShortIndices != 0) {
    uint wordIndex = idx / 2;
    uint wordOffset = idx % 2;
    uint packed = srcIndex[cb.indexOffset / 4 + wordIndex];
    return (packed >> (wordOffset * 16)) & 0xFFFF;
  } else {
    return srcIndex[cb.indexOffset / 4 + idx];
  }
}

// Canonicalize float bits: flush -0.0 to +0.0 so that vertices at the
// same spatial position always produce identical bit patterns.
uint canonicalizeFloatBits(float v)
{
  if (v == 0.0f) return 0u;
  return asuint(v);
}

uint mixPositionBits(uint value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

uint hashPositionSlot(float3 pos, uint hashTableSize) {
  const uint hx = mixPositionBits(canonicalizeFloatBits(pos.x));
  const uint hy = mixPositionBits(canonicalizeFloatBits(pos.y) ^ 0x9e3779b9u);
  const uint hz = mixPositionBits(canonicalizeFloatBits(pos.z) ^ 0x85ebca6bu);
  return mixPositionBits(hx ^ hy ^ hz) & (hashTableSize - 1);
}

bool samePosition(float3 a, float3 b) {
  return canonicalizeFloatBits(a.x) == canonicalizeFloatBits(b.x) &&
    canonicalizeFloatBits(a.y) == canonicalizeFloatBits(b.y) &&
    canonicalizeFloatBits(a.z) == canonicalizeFloatBits(b.z);
}

void accumulateAtPosition(uint vertexIndex, float3 pos, float3 faceNormal,
    ReadBuffer(float) positionData, RWIntBuffer hashTableData, ConstBuffer(SmoothNormalsArgs) cb) {
  const uint slot = hashPositionSlot(pos, cb.hashTableSize);
  const int owner = int(vertexIndex + 1u);
  const int ix = int(faceNormal.x * FIXED_POINT_SCALE);
  const int iy = int(faceNormal.y * FIXED_POINT_SCALE);
  const int iz = int(faceNormal.z * FIXED_POINT_SCALE);
  // The owner points into immutable position data, so publishing it needs no separate key writes.
  for (uint probe = 0; probe < cb.hashTableSize; ++probe) {
    const uint base = ((slot + probe) & (cb.hashTableSize - 1)) * 4;
    int storedOwner;
#ifdef __cplusplus
    storedOwner = hashTableData[base];
    if (storedOwner == 0) {
      hashTableData[base] = owner;
    }
#else
    InterlockedCompareExchange(hashTableData[base], 0, owner, storedOwner);
#endif
    bool matches = storedOwner == 0;
    if (storedOwner != 0) {
      matches = samePosition(pos, smoothNormalsLoadPosition(uint(storedOwner) - 1u, positionData, cb));
    }
    if (matches) {
#ifdef __cplusplus
      hashTableData[base + 1] += ix;
      hashTableData[base + 2] += iy;
      hashTableData[base + 3] += iz;
#else
      InterlockedAdd(hashTableData[base + 1], ix);
      InterlockedAdd(hashTableData[base + 2], iy);
      InterlockedAdd(hashTableData[base + 3], iz);
#endif
      return;
    }
  }
}

float3 lookupSmoothedNormal(float3 pos, ReadBuffer(float) positionData,
#ifdef __cplusplus
    const int32_t* hashTableData,
#else
    RWIntBuffer hashTableData,
#endif
    ConstBuffer(SmoothNormalsArgs) cb) {
  const uint slot = hashPositionSlot(pos, cb.hashTableSize);
  for (uint probe = 0; probe < cb.hashTableSize; ++probe) {
    const uint base = ((slot + probe) & (cb.hashTableSize - 1)) * 4;
    const int owner = hashTableData[base];
    if (owner == 0) {
      break;
    }
    if (samePosition(pos, smoothNormalsLoadPosition(uint(owner) - 1u, positionData, cb))) {
      return float3(float(hashTableData[base + 1]), float(hashTableData[base + 2]), float(hashTableData[base + 3]));
    }
  }
  return float3(0.f, 0.f, 0.f);
}

// ---------------------------------------------------------------------------
// Phase 1: Per-triangle accumulation
// ---------------------------------------------------------------------------

void smoothNormalsAccumulate(uint triIdx,
                             ReadBuffer(float) positionData,
                             ReadBuffer(uint) srcIndex,
                             RWIntBuffer hashTableData,
                             ConstBuffer(SmoothNormalsArgs) cb)
{
  uint i0 = smoothNormalsLoadIndex(triIdx * 3 + 0, srcIndex, cb);
  uint i1 = smoothNormalsLoadIndex(triIdx * 3 + 1, srcIndex, cb);
  uint i2 = smoothNormalsLoadIndex(triIdx * 3 + 2, srcIndex, cb);

  float3 p0 = smoothNormalsLoadPosition(i0, positionData, cb);
  float3 p1 = smoothNormalsLoadPosition(i1, positionData, cb);
  float3 p2 = smoothNormalsLoadPosition(i2, positionData, cb);

  float3 edge1 = p1 - p0;
  float3 edge2 = p2 - p0;
  float3 faceNormal = cross(edge1, edge2);

  float faceLen = length(faceNormal);
  if (faceLen <= 1e-20f) return; // degenerate triangle
  faceNormal /= faceLen;

  accumulateAtPosition(i0, p0, faceNormal, positionData, hashTableData, cb);
  accumulateAtPosition(i1, p1, faceNormal, positionData, hashTableData, cb);
  accumulateAtPosition(i2, p2, faceNormal, positionData, hashTableData, cb);
}

// ---------------------------------------------------------------------------
// Phase 2: Per-vertex scatter, normalize, and encode
// ---------------------------------------------------------------------------

void smoothNormalsScatter(uint vertIdx,
                          ReadBuffer(float) positionData,
                          WriteBuffer(float) normalData,
#ifdef __cplusplus
                          const int32_t* hashTableData,
#else
                          RWIntBuffer hashTableData,
#endif
                          ConstBuffer(SmoothNormalsArgs) cb)
{
  float3 pos = smoothNormalsLoadPosition(vertIdx, positionData, cb);
  float3 n = lookupSmoothedNormal(pos, positionData, hashTableData, cb);

  float len = length(n);
  if (len > 1e-7f) {
    n /= len;
  } else {
    n = float3(0.0f, 1.0f, 0.0f); // Default up normal for degenerate cases
  }

  // Always output octahedral-encoded normals (single uint32 stored as float).
  // This is more efficient than 3-float normals and matches what the surface
  // interaction shader expects when normalsEncoded is set.
  uint normalBase = cb.normalOffset / 4 + vertIdx * (cb.normalStride / 4);
  uint encoded = encodeNormal(n);
  normalData[normalBase] = asfloat(encoded);
}

#ifdef __cplusplus
#undef RWIntBuffer
#undef ConstBuffer
} // namespace dxvk
#endif

#include "../utility/cpu_gpu_compat_undef.h"
