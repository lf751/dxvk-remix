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
#include "rtx_terrain_grass_system.h"

#include "dxvk_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_camera_manager.h"
#include "rtx_imgui.h"
#include "rtx_instance_manager.h"
#include "rtx_options.h"
#include "rtx_shader_manager.h"

#include <rtx_shaders/terrain_height_atlas.h>
#include <rtx_shaders/terrain_grass_scatter.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dxvk {

  namespace {
    class TerrainHeightAtlasShader : public ManagedShader {
      SHADER_SOURCE(TerrainHeightAtlasShader, VK_SHADER_STAGE_COMPUTE_BIT, terrain_height_atlas)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(TERRAIN_GRASS_HEIGHT_BINDING_CONSTANTS)
        STRUCTURED_BUFFER(TERRAIN_GRASS_HEIGHT_BINDING_CHUNKS)
        STRUCTURED_BUFFER(TERRAIN_GRASS_HEIGHT_BINDING_JOBS)
        RW_TEXTURE2D(TERRAIN_GRASS_HEIGHT_BINDING_ATLAS_OUT)
      END_PARAMETER()
    };

    class TerrainGrassScatterShader : public ManagedShader {
      SHADER_SOURCE(TerrainGrassScatterShader, VK_SHADER_STAGE_COMPUTE_BIT, terrain_grass_scatter)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_CONSTANTS)
        STRUCTURED_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_BATCHES)
        STRUCTURED_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_THREAD_BATCH)
        TEXTURE2D(TERRAIN_GRASS_SCATTER_BINDING_HEIGHT_ATLAS)
        SAMPLER2D(TERRAIN_GRASS_SCATTER_BINDING_COVERAGE_TEX)
        RW_STRUCTURED_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_INSTANCE_BUFFER)
        RW_STRUCTURED_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_SURFACE_BUFFER)
        RW_STRUCTURED_BUFFER(TERRAIN_GRASS_SCATTER_BINDING_MATERIAL_BUFFER)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(TerrainHeightAtlasShader);
    PREWARM_SHADER_PIPELINE(TerrainGrassScatterShader);

    constexpr float kDegToRad = 0.017453292519943295f;
  }

  RtxTerrainGrassSystem::RtxTerrainGrassSystem(DxvkDevice* device)
    : CommonDeviceObject(device) { }

  void RtxTerrainGrassSystem::onSceneClear() {
    m_atlasNeedsClear = true;
    m_lastHeightChunks.clear();
    m_uploadedBatches.clear();
    m_uploadedThreadCount = 0;
    m_hasPrevWorldOffset = false;
  }

  void RtxTerrainGrassSystem::showImguiSettings() {
    if (RemixGui::CollapsingHeader("Terrain Grass")) {
      ImGui::PushID("rtx_terrain_grass");
      ImGui::Dummy({ 0, 2 });
      ImGui::Indent();
      RemixGui::Checkbox("Enable 3D Terrain Grass", &enableObject());
      RemixGui::DragFloat("Density Scale", &densityScaleObject(), 0.05f, 0.25f, 2.0f, "%.2f");
      RemixGui::DragFloat("Scale Multiplier", &scaleMultiplierObject(), 0.05f, 0.1f, 4.0f, "%.2f");
      RemixGui::DragFloat("Near Distance (0 = USD)", &nearDistanceObject(), 1.0f, 0.0f, 1024.0f, "%.1f");
      RemixGui::DragFloat("Mid Distance (0 = USD)", &midDistanceObject(), 1.0f, 0.0f, 1024.0f, "%.1f");
      RemixGui::Checkbox("Whole Island", &wholeIslandObject());
      ImGui::BeginDisabled(wholeIsland());
      RemixGui::DragFloat("Far Distance (0 = USD)", &farDistanceObject(), 2.0f, 0.0f, 2048.0f, "%.1f");
      ImGui::EndDisabled();
      RemixGui::DragFloat("Transition Width (<0 = USD)", &transitionWidthObject(), 0.5f, -1.0f, 200.0f, "%.1f");
      RemixGui::Checkbox("Force Hide Vanilla Grass", &forceHideVanillaGrassObject());
      ImGui::Unindent();
      ImGui::PopID();
    }
  }

  void RtxTerrainGrassSystem::ensureAtlasResources(Rc<DxvkContext> ctx) {
    const Rc<DxvkDevice>& dev = ctx->getDevice();
    if (m_heightNormalAtlas.ptr() == nullptr) {
      DxvkImageCreateInfo imgInfo {};
      imgInfo.type = VK_IMAGE_TYPE_2D;
      imgInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      imgInfo.flags = 0;
      imgInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      imgInfo.extent = { kTerrainGrassAtlasSize, kTerrainGrassAtlasSize, 1u };
      imgInfo.numLayers = 1u;
      imgInfo.mipLevels = 1u;
      imgInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      imgInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      imgInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imgInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
      m_heightNormalAtlas = dev->createImage(imgInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXRenderTarget, "RTX TerrainGrass - HeightNormal Atlas");

      DxvkImageViewCreateInfo viewInfo {};
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = imgInfo.format;
      viewInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLevel = 0u;
      viewInfo.numLevels = 1u;
      viewInfo.minLayer = 0u;
      viewInfo.numLayers = 1u;
      m_heightNormalAtlasView = dev->createImageView(m_heightNormalAtlas, viewInfo);
      m_atlasNeedsClear = true;
    }

    if (m_coverageSampler.ptr() == nullptr) {
      DxvkSamplerCreateInfo sampInfo {};
      sampInfo.magFilter = VK_FILTER_LINEAR;
      sampInfo.minFilter = VK_FILTER_LINEAR;
      sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampInfo.mipmapLodMin = 0.f;
      sampInfo.mipmapLodMax = 0.f;
      m_coverageSampler = dev->createSampler(sampInfo);
    }
  }

  void RtxTerrainGrassSystem::clearAtlas(Rc<DxvkContext> ctx) {
    if (m_heightNormalAtlasView.ptr() == nullptr) {
      return;
    }
    VkClearColorValue clearVal {};
    clearVal.float32[0] = 0.f;
    clearVal.float32[1] = 0.f;
    clearVal.float32[2] = 0.f;
    clearVal.float32[3] = 0.f;
    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0u;
    range.levelCount = 1u;
    range.baseArrayLayer = 0u;
    range.layerCount = 1u;
    ctx->clearColorImage(m_heightNormalAtlas, clearVal, range);
    m_atlasNeedsClear = false;
  }

  void RtxTerrainGrassSystem::updateHeightAtlas(
      Rc<DxvkContext> ctx,
      const std::vector<RtInstance*>& instances,
      const TerrainGrassPreset& preset,
      const Vector3& worldOffset,
      bool flipUp) {
    if (!m_device->features().core.features.shaderInt64 ||
        !m_device->features().vulkan12Features.bufferDeviceAddress) {
      return;
    }

    ensureAtlasResources(ctx);
    if (m_atlasNeedsClear) {
      clearAtlas(ctx);
    }

    std::vector<HeightChunkSignature> signatures;
    signatures.reserve(64);

    for (const RtInstance* instance : instances) {
      if (instance == nullptr || instance->isHidden()) {
        continue;
      }
      if (!instance->testCategoryFlags(InstanceCategories::Terrain) && !instance->surface.terrainBlend) {
        continue;
      }
      if (instance->surface.instancesToObject != nullptr) {
        continue;
      }
      const BlasEntry* blas = instance->getBlas();
      if (blas == nullptr) {
        continue;
      }
      const auto& geo = blas->modifiedGeometryData;
      if (!geo.positionBuffer.defined() || !geo.indexBuffer.defined() || geo.calculatePrimitiveCount() == 0) {
        continue;
      }
      if ((geo.positionBuffer.buffer()->info().usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0 ||
          (geo.indexBuffer.buffer()->info().usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0) {
        continue;
      }
      signatures.push_back({ blas, instance->surface.objectToWorld, blas->frameCreated, blas->frameLastUpdated });
    }

    if (signatures.empty()) {
      return;
    }

    if (signatures == m_lastHeightChunks && m_lastHeightWorldOffset == worldOffset) {
      return;
    }
    m_lastHeightChunks = signatures;
    m_lastHeightWorldOffset = worldOffset;

    ScopedGpuProfileZone(ctx, "TerrainGrassHeightAtlas");

    std::vector<TerrainGrassHeightChunk> chunkArgs;
    std::vector<TerrainGrassHeightJob> jobs;
    chunkArgs.reserve(signatures.size());
    jobs.reserve(signatures.size() * 4u);

    for (const auto& sig : signatures) {
      const auto& geo = sig.blas->modifiedGeometryData;
      const Matrix4& m = sig.transform;

      TerrainGrassHeightChunk chunk {};
      chunk.positionAddress = geo.positionBuffer.getDeviceAddress() + geo.positionBuffer.offsetFromSlice();
      chunk.indexAddress = geo.indexBuffer.getDeviceAddress() + geo.indexBuffer.offsetFromSlice();
      chunk.positionStride = geo.positionBuffer.stride() / sizeof(float);
      chunk.numTriangles = geo.calculatePrimitiveCount();
      chunk.numVertices = geo.vertexCount;
      chunk.useShortIndices = geo.indexBuffer.indexType() == VK_INDEX_TYPE_UINT16 ? 1u : 0u;
      for (uint32_t axis = 0; axis < 3; ++axis) {
        chunk.worldRows[axis] = vec4(m[0][axis], m[1][axis], m[2][axis], m[3][axis]);
      }

      const uint32_t chunkIdx = static_cast<uint32_t>(chunkArgs.size());
      for (uint32_t first = 0; first < chunk.numTriangles; first += 64u) {
        jobs.push_back({ chunkIdx, first });
      }
      chunkArgs.push_back(chunk);

      ctx->getCommandList()->trackResource<DxvkAccess::Read>(geo.positionBuffer.buffer());
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(geo.indexBuffer.buffer());
    }

    if (jobs.empty()) {
      return;
    }

    const Rc<DxvkDevice>& dev = ctx->getDevice();
    const auto ensureStorageBuffer = [&](Rc<DxvkBuffer>& buffer, size_t size, const char* name) {
      if (buffer.ptr() == nullptr || buffer->info().size < size) {
        DxvkBufferCreateInfo info {};
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        info.size = align(size + size / 4, 256);
        buffer = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXBuffer, name);
      }
    };

    if (m_heightCb.ptr() == nullptr) {
      DxvkBufferCreateInfo info {};
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size = sizeof(TerrainGrassHeightConstants);
      m_heightCb = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXBuffer, "RTX TerrainGrass - Height Constants");
    }

    const size_t chunksBytes = chunkArgs.size() * sizeof(TerrainGrassHeightChunk);
    const size_t jobsBytes = jobs.size() * sizeof(TerrainGrassHeightJob);
    ensureStorageBuffer(m_heightChunksGpu, chunksBytes, "RTX TerrainGrass - Height Chunks");
    ensureStorageBuffer(m_heightJobsGpu, jobsBytes, "RTX TerrainGrass - Height Jobs");

    ctx->writeToBuffer(m_heightChunksGpu, 0, chunksBytes, chunkArgs.data());
    ctx->writeToBuffer(m_heightJobsGpu, 0, jobsBytes, jobs.data());

    TerrainGrassHeightConstants cb {};
    cb.worldOffset = vec4(worldOffset.x, worldOffset.y, worldOffset.z, 0.f);
    cb.worldOrigin = vec2(preset.worldOrigin.x, preset.worldOrigin.y);
    cb.worldSize = preset.worldSize;
    cb.atlasSize = kTerrainGrassAtlasSize;
    cb.zUp = RtxOptions::zUp() ? 1u : 0u;
    cb.flipUp = flipUp ? 1u : 0u;
    cb.jobCount = static_cast<uint32_t>(jobs.size());

    const DxvkBufferSliceHandle cSlice = m_heightCb->allocSlice();
    ctx->invalidateBuffer(m_heightCb, cSlice);
    ctx->writeToBuffer(m_heightCb, 0, sizeof(cb), &cb);

    ctx->emitMemoryBarrier(0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    ctx->bindResourceBuffer(TERRAIN_GRASS_HEIGHT_BINDING_CONSTANTS, DxvkBufferSlice(m_heightCb));
    ctx->bindResourceBuffer(TERRAIN_GRASS_HEIGHT_BINDING_CHUNKS, DxvkBufferSlice(m_heightChunksGpu, 0, chunksBytes));
    ctx->bindResourceBuffer(TERRAIN_GRASS_HEIGHT_BINDING_JOBS, DxvkBufferSlice(m_heightJobsGpu, 0, jobsBytes));
    ctx->bindResourceView(TERRAIN_GRASS_HEIGHT_BINDING_ATLAS_OUT, m_heightNormalAtlasView, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, TerrainHeightAtlasShader::getShader());
    ctx->dispatch(static_cast<uint32_t>(jobs.size()), 1u, 1u);

    ctx->emitMemoryBarrier(0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    ONCE(Logger::info(str::format("[RTX Terrain Grass] Rasterized height atlas from ",
      chunkArgs.size(), " terrain chunks (", jobs.size(), " jobs).")));
  }

  void RtxTerrainGrassSystem::dispatchScatter(
      Rc<DxvkContext> ctx,
      const Rc<DxvkBuffer>& instanceBuffer,
      const Rc<DxvkBuffer>& surfaceBuffer,
      const Rc<DxvkBuffer>& surfaceMaterialBuffer,
      const std::vector<PointInstancerBatch>& batches,
      const CameraManager& cameraManager,
      const TerrainGrassPreset& preset,
      const TerrainBlendArgs& terrainBlendArgs,
      const TextureRef& coverageTexture,
      bool flipUp) {
    if (batches.empty() || instanceBuffer.ptr() == nullptr) {
      return;
    }

    ensureAtlasResources(ctx);
    if (m_atlasNeedsClear) {
      clearAtlas(ctx);
    }

    m_batchScratch.clear();
    m_batchScratch.reserve(batches.size());

    uint32_t threadCount = 0;
    const PointInstancerBatch* prevGrassBatch = nullptr;

    for (const PointInstancerBatch& batch : batches) {
      if (batch.terrainGrass == nullptr || batch.instanceCount == 0) {
        continue;
      }

      const bool isTwin = prevGrassBatch != nullptr &&
        prevGrassBatch->terrainGrass == batch.terrainGrass &&
        prevGrassBatch->baseSurfaceIndex == batch.baseSurfaceIndex &&
        prevGrassBatch->instanceCount == batch.instanceCount;
      prevGrassBatch = &batch;

      const TerrainGrassBatchInfo& info = *batch.terrainGrass;
      TerrainGrassScatterBatch gpu {};
      gpu.meshToPrototype = info.meshToPrototype;
      gpu.baseSurfaceIndex = batch.baseSurfaceIndex;
      gpu.instanceCount = batch.instanceCount;
      gpu.customIndexFlags = batch.customIndexFlags;
      gpu.instanceMask = batch.instanceMask;
      gpu.sbtOffsetAndFlags = batch.sbtOffsetAndFlags;
      gpu.blasRefLo = static_cast<uint32_t>(batch.blasReference & 0xFFFFFFFFull);
      gpu.blasRefHi = static_cast<uint32_t>(batch.blasReference >> 32);
      gpu.instanceBufferOffset = batch.instanceBufferByteOffset;
      gpu.firstThread = threadCount;
      gpu.writeSurfaceData = isTwin ? 0u : 1u;
      gpu.ringIndex = info.ringIndex;
      gpu.variantIndex = info.variantIndex;
      gpu.variantCount = info.variantCount;
      gpu.gridWidth = info.gridWidth;
      gpu.rowSlots = info.rowSlots;
      gpu.isWholeIsland = info.isWholeIsland ? 1u : 0u;
      gpu.spacing = info.spacing;
      gpu.innerRadius = info.innerRadius;
      gpu.outerRadius = info.outerRadius;
      gpu.ringScale = info.ringScale * scaleMultiplier();
      m_batchScratch.push_back(gpu);

      threadCount += batch.instanceCount;
    }

    if (threadCount == 0) {
      return;
    }

    ScopedGpuProfileZone(ctx, "TerrainGrassScatter");

    const Rc<DxvkDevice>& dev = ctx->getDevice();
    if (m_scatterCb.ptr() == nullptr) {
      DxvkBufferCreateInfo info {};
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size = sizeof(TerrainGrassScatterConstants);
      m_scatterCb = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXBuffer, "RTX TerrainGrass - Scatter Constants");
    }

    const auto ensureStorageBuffer = [&](Rc<DxvkBuffer>& buffer, size_t size, const char* name) {
      if (buffer.ptr() == nullptr || buffer->info().size < size) {
        DxvkBufferCreateInfo info {};
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        info.size = align(size + size / 4, 256);
        buffer = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXBuffer, name);
        return true;
      }
      return false;
    };

    const size_t batchesBytes = m_batchScratch.size() * sizeof(TerrainGrassScatterBatch);
    const size_t threadBatchBytes = size_t(threadCount) * sizeof(uint32_t);
    const bool batchesUnchanged = m_scatterBatchesGpu.ptr() != nullptr &&
      m_uploadedThreadCount == threadCount &&
      m_uploadedBatches.size() == m_batchScratch.size() &&
      std::memcmp(m_uploadedBatches.data(), m_batchScratch.data(), batchesBytes) == 0;

    const bool batchRealloc = ensureStorageBuffer(m_scatterBatchesGpu, batchesBytes, "RTX TerrainGrass - Scatter Batches");
    const bool threadRealloc = ensureStorageBuffer(m_scatterThreadBatchGpu, threadBatchBytes, "RTX TerrainGrass - Thread Batch");
    if (!batchesUnchanged || batchRealloc || threadRealloc) {
      ctx->writeToBuffer(m_scatterBatchesGpu, 0, batchesBytes, m_batchScratch.data());
      m_threadBatchScratch.resize(threadCount);
      for (size_t i = 0; i < m_batchScratch.size(); ++i) {
        std::fill_n(m_threadBatchScratch.begin() + m_batchScratch[i].firstThread,
          m_batchScratch[i].instanceCount, static_cast<uint32_t>(i));
      }
      ctx->writeToBuffer(m_scatterThreadBatchGpu, 0, threadBatchBytes, m_threadBatchScratch.data());
      m_uploadedBatches = m_batchScratch;
      m_uploadedThreadCount = threadCount;
    }

    const RtCamera& mainCamera = cameraManager.getMainCamera();
    const Vector3 artificialOffset = mainCamera.getArtificialWorldOffset();
    const Vector3 worldOffset = Vector3(-artificialOffset.x, -artificialOffset.y, -artificialOffset.z);
    const Vector3 prevWorldOffset = m_hasPrevWorldOffset ? m_prevWorldOffset : worldOffset;
    m_prevWorldOffset = worldOffset;
    m_hasPrevWorldOffset = true;

    const Vector3 camRenderPos = mainCamera.getPosition();
    const Vector3 camGamePos = camRenderPos + worldOffset;

    const float maxSlopeRad = std::clamp(preset.maxSlopeDegrees, 1.f, 89.f) * kDegToRad;
    const float maxSlopeCos = std::cos(maxSlopeRad);
    const float fadeStartRad = std::max(0.f, preset.maxSlopeDegrees - 5.f) * kDegToRad;
    const float slopeFadeWidth = std::max(0.01f, std::cos(fadeStartRad) - maxSlopeCos);

    const bool hasCoverage = coverageTexture.isValid() && !coverageTexture.isImageEmpty() &&
      coverageTexture.getImageView() != nullptr;
    const float covScale = LegacyMaterialDefaults::worldSpaceSecondaryOpacityScale() > 0.f
      ? LegacyMaterialDefaults::worldSpaceSecondaryOpacityScale()
      : (1.f / std::max(preset.worldSize, 1.f));

    TerrainGrassScatterConstants cb {};
    cb.cameraWorldPos = vec4(camGamePos.x, camGamePos.y, camGamePos.z, 0.f);
    cb.worldOffset = vec4(worldOffset.x, worldOffset.y, worldOffset.z, 0.f);
    cb.prevWorldOffset = vec4(prevWorldOffset.x, prevWorldOffset.y, prevWorldOffset.z, 0.f);
    cb.worldOrigin = vec2(preset.worldOrigin.x, preset.worldOrigin.y);
    cb.worldSize = preset.worldSize;
    cb.coverageUvScale = covScale;
    cb.minElevation = preset.minElevation;
    cb.maxSlopeCos = maxSlopeCos;
    cb.slopeFadeWidth = slopeFadeWidth;
    cb.sinkDepth = preset.sinkDepth;
    cb.normalAlign = preset.normalAlign;
    cb.borderShrinkWidth = preset.borderShrinkWidth;
    cb.minScale = preset.minScale;
    cb.maxScale = preset.maxScale;
    const float twOverride = transitionWidth();
    cb.transitionWidth = twOverride >= 0.f ? twOverride : preset.transitionWidth;
    cb.coverageThreshold = preset.coverageThreshold;
    cb.rockLowElevation = terrainBlendArgs.rockLowElevation;
    cb.rockHighElevation = terrainBlendArgs.rockHighElevation;
    cb.rockElevationSuppress = (preset.useTerrainBlendRules && terrainBlendArgs.enabled != 0u &&
      terrainBlendArgs.rockMode != 0u) ? 1u : 0u;
    cb.zUp = RtxOptions::zUp() ? 1u : 0u;
    cb.flipUp = flipUp ? 1u : 0u;
    cb.hasCoverageTexture = hasCoverage ? 1u : 0u;
    cb.atlasSize = kTerrainGrassAtlasSize;
    cb.threadCount = threadCount;

    const DxvkBufferSliceHandle cSlice = m_scatterCb->allocSlice();
    ctx->invalidateBuffer(m_scatterCb, cSlice);
    ctx->writeToBuffer(m_scatterCb, 0, sizeof(cb), &cb);

    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_CONSTANTS, DxvkBufferSlice(m_scatterCb));
    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_BATCHES, DxvkBufferSlice(m_scatterBatchesGpu, 0, batchesBytes));
    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_THREAD_BATCH, DxvkBufferSlice(m_scatterThreadBatchGpu, 0, threadBatchBytes));
    ctx->bindResourceView(TERRAIN_GRASS_SCATTER_BINDING_HEIGHT_ATLAS, m_heightNormalAtlasView, nullptr);
    ctx->bindResourceView(TERRAIN_GRASS_SCATTER_BINDING_COVERAGE_TEX,
      Rc<DxvkImageView>(hasCoverage ? coverageTexture.getImageView() : m_heightNormalAtlasView.ptr()), nullptr);
    ctx->bindResourceSampler(TERRAIN_GRASS_SCATTER_BINDING_COVERAGE_TEX, m_coverageSampler);
    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_INSTANCE_BUFFER, DxvkBufferSlice(instanceBuffer));
    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_SURFACE_BUFFER, DxvkBufferSlice(surfaceBuffer));
    ctx->bindResourceBuffer(TERRAIN_GRASS_SCATTER_BINDING_MATERIAL_BUFFER, DxvkBufferSlice(surfaceMaterialBuffer));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, TerrainGrassScatterShader::getShader());

    const VkExtent3D workgroups = util::computeBlockCount(
      VkExtent3D { threadCount, 1u, 1u },
      VkExtent3D { 64u, 1u, 1u });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    ONCE(Logger::info(str::format("[RTX Terrain Grass] GPU scatter active: ", threadCount,
      " slots across ", m_batchScratch.size(), " batches (hasCoverage=", hasCoverage ? 1 : 0,
      ", flipUp=", flipUp ? 1 : 0, ").")));
  }

} // namespace dxvk
