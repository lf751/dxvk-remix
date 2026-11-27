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
#include "rtx/pass/instance_definitions.h"
#include "rtx/concept/surface/surface.h"
#include "rtx/concept/surface_material/surface_material.h"

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_CONSTANTS)
ConstantBuffer<TerrainGrassScatterConstants> cb;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_BATCHES)
StructuredBuffer<TerrainGrassScatterBatch> batches;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_THREAD_BATCH)
StructuredBuffer<uint> threadBatch;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_HEIGHT_ATLAS)
Texture2D<float4> heightNormalAtlas;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_COVERAGE_TEX)
Sampler2D coverageTexture;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_INSTANCE_BUFFER)
RWByteAddressBuffer instanceBuffer;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_SURFACE_BUFFER)
RWStructuredBuffer<Surface> surfaceBuffer;

layout(binding = TERRAIN_GRASS_SCATTER_BINDING_MATERIAL_BUFFER)
RWStructuredBuffer<MemoryPolymorphicSurfaceMaterial> materialBuffer;

static const uint kVkAccelInstanceSize = 64u;
static const float kTwoPi = 6.28318530718f;

uint hashCell(int cx, int cz, uint salt) {
  uint h = uint(cx) * 374761393u + uint(cz) * 668265263u + salt * 2246822519u;
  h = (h ^ (h >> 13u)) * 1274126177u;
  return h ^ (h >> 16u);
}

float hashToFloat01(uint h) {
  return float(h & 0x00FFFFFFu) * (1.f / 16777215.f);
}

// Validity-aware bilinear sample so unwritten coastal/empty atlas texels never pull heights toward 0.
bool sampleHeightNormal(float2 uv, out float outHeight, out float3 outNormal) {
  const float sizeF = float(cb.atlasSize);
  const float2 f = uv * sizeF - 0.5f;
  const int2 i0 = int2(floor(f));
  const float2 fracPos = f - float2(i0);
  const int maxIdx = int(cb.atlasSize) - 1;

  float hSum = 0.f;
  float3 nSum = float3(0.f, 0.f, 0.f);
  float wSum = 0.f;

  [unroll]
  for (int dy = 0; dy <= 1; ++dy) {
    [unroll]
    for (int dx = 0; dx <= 1; ++dx) {
      const int2 coord = clamp(i0 + int2(dx, dy), int2(0, 0), int2(maxIdx, maxIdx));
      const float4 texel = heightNormalAtlas.Load(int3(coord, 0));
      const float3 n = texel.yzw;
      if (dot(n, n) > 0.25f) {
        const float wx = dx == 0 ? (1.f - fracPos.x) : fracPos.x;
        const float wy = dy == 0 ? (1.f - fracPos.y) : fracPos.y;
        const float w = wx * wy;
        hSum += texel.x * w;
        nSum += n * w;
        wSum += w;
      }
    }
  }

  if (wSum <= 1e-4f) {
    outHeight = 0.f;
    outNormal = float3(0.f, 1.f, 0.f);
    return false;
  }

  outHeight = hSum / wSum;
  const float nLen = length(nSum);
  outNormal = nLen > 1e-5f ? (nSum / nLen) : float3(0.f, 1.f, 0.f);
  return true;
}

bool samplePointElevationValid(float2 uv, out float outElevation) {
  if (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f) {
    outElevation = -1e6f;
    return false;
  }
  const int maxIdx = int(cb.atlasSize) - 1;
  const int2 coord = clamp(int2(floor(uv * float(cb.atlasSize))), int2(0, 0), int2(maxIdx, maxIdx));
  const float4 texel = heightNormalAtlas.Load(int3(coord, 0));
  if (dot(texel.yzw, texel.yzw) <= 0.25f) {
    outElevation = -1e6f;
    return false;
  }
  outElevation = cb.flipUp != 0u ? -texel.x : texel.x;
  return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
  const uint globalThread = threadId.x;
  if (globalThread >= cb.threadCount) {
    return;
  }

  const TerrainGrassScatterBatch batch = batches[threadBatch[globalThread]];
  const uint slotIdx = globalThread - batch.firstThread;

  // Map slotIdx -> toroidal or whole-island cell coordinates
  const uint v = max(1u, batch.variantCount);
  const uint w = max(1u, batch.gridWidth);
  const uint rowSlots = max(1u, batch.rowSlots);
  const uint localZ = slotIdx / rowSlots;
  const uint k = slotIdx % rowSlots;
  const uint xOffset = (batch.variantIndex + v - (localZ % v)) % v;
  const uint localX = k * v + xOffset;

  const float2 camPlane = cb.zUp != 0 ? cb.cameraWorldPos.xy : cb.cameraWorldPos.xz;
  int cellX = int(localX);
  int cellZ = int(localZ);
  if (batch.isWholeIsland == 0u) {
    const int wi = int(w);
    const int camCellX = int(floor((camPlane.x - cb.worldOrigin.x) / batch.spacing));
    const int camCellZ = int(floor((camPlane.y - cb.worldOrigin.y) / batch.spacing));
    const int baseX = camCellX - wi / 2;
    const int baseZ = camCellZ - wi / 2;
    const int modBaseX = ((baseX % wi) + wi) % wi;
    const int modBaseZ = ((baseZ % wi) + wi) % wi;
    cellX = baseX + ((int(localX) - modBaseX + wi) % wi);
    cellZ = baseZ + ((int(localZ) - modBaseZ + wi) % wi);
  }

  // Deterministic per-cell random jitter, yaw, and scale
  const uint h0 = hashCell(cellX, cellZ, batch.ringIndex * 17u + 1u);
  const uint h1 = hashCell(cellX, cellZ, batch.ringIndex * 17u + 5u);
  const uint h2 = hashCell(cellX, cellZ, batch.ringIndex * 17u + 9u);
  const float2 jitter = (float2(hashToFloat01(h0), hashToFloat01(h1)) - 0.5f) * 0.84f;
  const float2 worldPlane = cb.worldOrigin + (float2(float(cellX), float(cellZ)) + 0.5f + jitter) * batch.spacing;
  const float2 atlasUv = (worldPlane - cb.worldOrigin) / max(cb.worldSize, 1e-4f);

  bool visible = (atlasUv.x >= 0.f && atlasUv.x <= 1.f && atlasUv.y >= 0.f && atlasUv.y <= 1.f);

  // Horizontal distance to camera for ring selection & smooth cross-fade
  const float dist = length(worldPlane - camPlane);
  float ringFade = 1.f;
  if (visible) {
    if (batch.outerRadius > 0.f && dist > batch.outerRadius) {
      visible = false;
    } else if (batch.innerRadius > 0.f && dist < max(0.f, batch.innerRadius - cb.transitionWidth)) {
      visible = false;
    } else {
      if (cb.transitionWidth > 1e-3f) {
        if (batch.outerRadius > 0.f && dist > batch.outerRadius - cb.transitionWidth) {
          ringFade = min(ringFade, saturate((batch.outerRadius - dist) / cb.transitionWidth));
        }
        if (batch.innerRadius > 0.f && dist < batch.innerRadius) {
          ringFade = min(ringFade, saturate((dist - (batch.innerRadius - cb.transitionWidth)) / cb.transitionWidth));
        }
      }
      if (ringFade <= 0.05f) {
        visible = false;
      }
    }
  }

  // Live grass coverage mask check + footprint perimeter guard so wide clusters never overhang paths/buildings/sand
  static const float2 kPerimeterDirs[8] = {
    float2( 1.f,  0.f), float2(-1.f,  0.f), float2( 0.f,  1.f), float2( 0.f, -1.f),
    float2( 0.70710678f,  0.70710678f), float2(-0.70710678f,  0.70710678f),
    float2( 0.70710678f, -0.70710678f), float2(-0.70710678f, -0.70710678f)
  };
  const float rOuterWorld = cb.borderShrinkWidth * (batch.isWholeIsland != 0u ? 1.5f : 1.0f);
  const float rInnerWorld = rOuterWorld * 0.62f;

  float borderScale = 1.f;
  if (visible && cb.hasCoverageTexture != 0u) {
    const float2 covUv = worldPlane * cb.coverageUvScale;
    const float c0 = coverageTexture.SampleLevel(covUv, 0.f).a;
    if (c0 < cb.coverageThreshold) {
      visible = false;
    } else if (rOuterWorld > 1e-3f) {
      const float rInUv = rInnerWorld * cb.coverageUvScale;
      const float rOutUv = rOuterWorld * cb.coverageUvScale;
      float outerSum = 0.f;
      [unroll]
      for (uint d = 0u; d < 8u; ++d) {
        if (coverageTexture.SampleLevel(covUv + kPerimeterDirs[d] * rInUv, 0.f).a < cb.coverageThreshold) {
          visible = false;
        }
        outerSum += coverageTexture.SampleLevel(covUv + kPerimeterDirs[d] * rOutUv, 0.f).a;
      }
      if (visible) {
        borderScale = min(borderScale, lerp(0.55f, 1.f, smoothstep(0.35f, 0.98f, outerSum * 0.125f)));
      }
    }
  }

  // Sample GPU terrain height + normal atlas
  float terrainHeight = 0.f;
  float3 terrainNormal = float3(0.f, 1.f, 0.f);
  const float upSign = cb.flipUp != 0u ? -1.f : 1.f;
  const float3 upDir = cb.zUp != 0u ? float3(0.f, 0.f, upSign) : float3(0.f, upSign, 0.f);
  float slopeScale = 1.f;

  if (visible) {
    if (!sampleHeightNormal(atlasUv, terrainHeight, terrainNormal)) {
      visible = false;
    } else {
      const float worldElevation = cb.flipUp != 0u ? -terrainHeight : terrainHeight;
      if (worldElevation < cb.minElevation) {
        visible = false;
      }
      if (visible && rOuterWorld > 1e-3f) {
        const float invWorldSize = 1.f / max(cb.worldSize, 1e-4f);
        const float rInAtlas = rInnerWorld * invWorldSize;
        const float rOutAtlas = rOuterWorld * invWorldSize;
        float outerValidCount = 0.f;
        [unroll]
        for (uint d = 0u; d < 8u; ++d) {
          float innerElev = 0.f;
          if (!samplePointElevationValid(atlasUv + kPerimeterDirs[d] * rInAtlas, innerElev) || innerElev < cb.minElevation) {
            visible = false;
          }
          float outerElev = 0.f;
          if (samplePointElevationValid(atlasUv + kPerimeterDirs[d] * rOutAtlas, outerElev) && outerElev >= cb.minElevation) {
            outerValidCount += 1.f;
          }
        }
        if (visible) {
          borderScale = min(borderScale, lerp(0.55f, 1.f, smoothstep(0.35f, 0.98f, outerValidCount * 0.125f)));
        }
      }
      if (visible && cb.rockElevationSuppress != 0u) {
        const float rockFade = 1.f - smoothstep(cb.rockLowElevation, cb.rockHighElevation, worldElevation);
        if (rockFade <= 0.1f) {
          visible = false;
        } else {
          slopeScale *= lerp(0.55f, 1.f, rockFade);
        }
      }
      if (visible) {
        const float cosSlope = saturate(dot(terrainNormal, upDir));
        if (cosSlope < cb.maxSlopeCos) {
          visible = false;
        } else if (cb.slopeFadeWidth > 1e-4f) {
          slopeScale *= lerp(0.5f, 1.f, saturate((cosSlope - cb.maxSlopeCos) / cb.slopeFadeWidth));
        }
      }
    }
  }

  // Build world-space transform (always valid even when culled so surface buffer stays finite)
  const float3 grassUp = normalize(lerp(upDir, terrainNormal, saturate(cb.normalAlign)));
  const float3 refAxis = abs(grassUp.z) < 0.9f ? float3(0.f, 0.f, 1.f) : float3(1.f, 0.f, 0.f);
  const float3 basisX0 = normalize(cross(grassUp, refAxis));
  const float3 basisZ0 = cross(basisX0, grassUp); // strictly right-handed: det([X, Y, Z]) = +1

  const float yaw = hashToFloat01(h2) * kTwoPi;
  const float sinYaw = sin(yaw);
  const float cosYaw = cos(yaw);
  const float randScale = lerp(cb.minScale, cb.maxScale, hashToFloat01(h0 ^ (h2 >> 3u)));
  const float effectiveRandScale = lerp(min(randScale, 0.95f), randScale, smoothstep(0.6f, 0.95f, borderScale));
  const float finalScale = max(0.05f, effectiveRandScale * batch.ringScale * borderScale * slopeScale * ringFade);

  const float3 right = (cosYaw * basisX0 + sinYaw * basisZ0) * finalScale;
  const float3 forward = (-sinYaw * basisX0 + cosYaw * basisZ0) * finalScale;
  const float3 upScaled = grassUp * finalScale;

  const float3 gamePos = (cb.zUp != 0u
    ? float3(worldPlane.x, worldPlane.y, terrainHeight)
    : float3(worldPlane.x, terrainHeight, worldPlane.y)) - grassUp * cb.sinkDepth;

  // Render-space position = gamePos - worldOffset (matches terrain_blend.slangh)
  const float3 renderPos = gamePos - cb.worldOffset.xyz;
  const float3 prevRenderPos = gamePos - cb.prevWorldOffset.xyz;

  const float4x4 cellToWorld = float4x4(
    right.x, upScaled.x, forward.x, renderPos.x,
    right.y, upScaled.y, forward.y, renderPos.y,
    right.z, upScaled.z, forward.z, renderPos.z,
    0.f,     0.f,        0.f,       1.f
  );
  const float4x4 prevCellToWorld = float4x4(
    right.x, upScaled.x, forward.x, prevRenderPos.x,
    right.y, upScaled.y, forward.y, prevRenderPos.y,
    right.z, upScaled.z, forward.z, prevRenderPos.z,
    0.f,     0.f,        0.f,       1.f
  );

  const float4x4 fullTransform = mul(cellToWorld, batch.meshToPrototype);
  const float4x4 prevFull = mul(prevCellToWorld, batch.meshToPrototype);

  if (batch.writeSurfaceData != 0u) {
    Surface surface = surfaceBuffer[batch.baseSurfaceIndex];
    const float3x3 normalMat = transpose(inverse((float3x3)fullTransform));

    surface.objectToWorld = (mat4x3)fullTransform;
    surface.prevObjectToWorld = (mat4x3)prevFull;
    surface.normalObjectToWorld = normalMat;

    surfaceBuffer[batch.baseSurfaceIndex + slotIdx] = surface;
    materialBuffer[batch.baseSurfaceIndex + slotIdx] = materialBuffer[batch.baseSurfaceIndex];
  }

  const uint mask = visible ? batch.instanceMask : 0u;
  const uint byteOffset = batch.instanceBufferOffset + slotIdx * kVkAccelInstanceSize;

  instanceBuffer.Store(byteOffset +  0, asuint(fullTransform[0].x));
  instanceBuffer.Store(byteOffset +  4, asuint(fullTransform[0].y));
  instanceBuffer.Store(byteOffset +  8, asuint(fullTransform[0].z));
  instanceBuffer.Store(byteOffset + 12, asuint(fullTransform[0].w));

  instanceBuffer.Store(byteOffset + 16, asuint(fullTransform[1].x));
  instanceBuffer.Store(byteOffset + 20, asuint(fullTransform[1].y));
  instanceBuffer.Store(byteOffset + 24, asuint(fullTransform[1].z));
  instanceBuffer.Store(byteOffset + 28, asuint(fullTransform[1].w));

  instanceBuffer.Store(byteOffset + 32, asuint(fullTransform[2].x));
  instanceBuffer.Store(byteOffset + 36, asuint(fullTransform[2].y));
  instanceBuffer.Store(byteOffset + 40, asuint(fullTransform[2].z));
  instanceBuffer.Store(byteOffset + 44, asuint(fullTransform[2].w));

  const uint perInstanceSurfaceIndex = batch.baseSurfaceIndex + slotIdx;
  const uint customIndexAndMask = (batch.customIndexFlags | (perInstanceSurfaceIndex & CUSTOM_INDEX_SURFACE_MASK)) | (mask << 24);
  instanceBuffer.Store(byteOffset + 48, customIndexAndMask);
  instanceBuffer.Store(byteOffset + 52, batch.sbtOffsetAndFlags);
  instanceBuffer.Store(byteOffset + 56, batch.blasRefLo);
  instanceBuffer.Store(byteOffset + 60, batch.blasRefHi);
}
