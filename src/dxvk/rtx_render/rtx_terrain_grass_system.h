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

#include "rtx_common_object.h"
#include "rtx_point_instancer_system.h"
#include "rtx_terrain_grass_types.h"
#include "rtx_texture.h"
#include "rtx/pass/terrain_grass/terrain_grass_bindings.h"
#include "rtx/pass/terrain_blend_args.h"

#include <memory>
#include <vector>

namespace dxvk {
  class DxvkContext;
  class DxvkDevice;
  class DxvkBuffer;
  class DxvkImage;
  class DxvkImageView;
  class DxvkSampler;
  class CameraManager;
  class RtInstance;
  struct BlasEntry;

  /**
    * GPU-driven whole-island procedural 3D grass placement system.
    *
    * Maintains a persistent 1024x1024 world-space terrain height + normal atlas
    * rasterized directly from tagged InstanceCategories::Terrain draws, then
    * scatters 3 cascaded LOD rings (Near/Mid toroidal + Far whole-island) using
    * the game's live grass coverage mask into the PointInstancer TLAS/surface
    * buffers with zero per-frame CPU transform iteration or BLAS rebuilds.
    */
  class RtxTerrainGrassSystem : public CommonDeviceObject {
  public:
    explicit RtxTerrainGrassSystem(DxvkDevice* device);
    ~RtxTerrainGrassSystem() = default;

    // Resets the persistent height+normal atlas on scene/island clear.
    void onSceneClear();

    // Rasterizes active terrain mesh chunks into the persistent 1024x1024 height+normal atlas.
    void updateHeightAtlas(Rc<DxvkContext> ctx,
                           const std::vector<RtInstance*>& instances,
                           const TerrainGrassPreset& preset,
                           const Vector3& worldOffset,
                           bool flipUp);

    // Dispatches GPU grass scatter for all terrain-grass PointInstancer batches.
    void dispatchScatter(Rc<DxvkContext> ctx,
                         const Rc<DxvkBuffer>& instanceBuffer,
                         const Rc<DxvkBuffer>& surfaceBuffer,
                         const Rc<DxvkBuffer>& surfaceMaterialBuffer,
                         const std::vector<PointInstancerBatch>& batches,
                         const CameraManager& cameraManager,
                         const TerrainGrassPreset& preset,
                         const TerrainBlendArgs& terrainBlendArgs,
                         const TextureRef& coverageTexture,
                         bool flipUp);

    static void showImguiSettings();

    static bool isEnabled() { return enable(); }
    static float getDensityScale() { return densityScale(); }
    static float getHeightScale() { return scaleMultiplier(); }
    static bool getForceHideVanillaGrass() { return forceHideVanillaGrass(); }

  private:
    struct HeightChunkSignature {
      const BlasEntry* blas = nullptr;
      Matrix4 transform;
      uint32_t frameCreated = 0;
      uint32_t frameUpdated = 0;

      bool operator==(const HeightChunkSignature& other) const {
        return blas == other.blas && frameCreated == other.frameCreated &&
               frameUpdated == other.frameUpdated && transform == other.transform;
      }
    };

    void ensureAtlasResources(Rc<DxvkContext> ctx);
    void clearAtlas(Rc<DxvkContext> ctx);

    RTX_OPTION("rtx.terrainGrass", bool, enable, true,
      "Master toggle for GPU-driven 3D terrain grass placement when a /RootNode/TerrainGrass preset is loaded.");
    RTX_OPTION_ARGS("rtx.terrainGrass", float, densityScale, 1.0f,
      "Multiplier on grass grid density (higher = denser grass, lower = wider spacing).",
      args.minValue = 0.25f;
      args.maxValue = 2.0f);
    RTX_OPTION_ARGS("rtx.terrainGrass", float, scaleMultiplier, 1.0f,
      "Global uniform scale multiplier applied to all 3D terrain grass clusters.",
      args.minValue = 0.1f;
      args.maxValue = 4.0f);
    RTX_OPTION("rtx.terrainGrass", bool, forceHideVanillaGrass, false,
      "Forces vanilla grass billboards hidden even if the USD preset leaves hideVanillaGrass disabled.");

    Rc<DxvkImage> m_heightNormalAtlas;
    Rc<DxvkImageView> m_heightNormalAtlasView;
    Rc<DxvkSampler> m_coverageSampler;
    bool m_atlasNeedsClear = true;

    Rc<DxvkBuffer> m_heightCb;
    Rc<DxvkBuffer> m_heightChunksGpu;
    Rc<DxvkBuffer> m_heightJobsGpu;

    Rc<DxvkBuffer> m_scatterCb;
    Rc<DxvkBuffer> m_scatterBatchesGpu;
    Rc<DxvkBuffer> m_scatterThreadBatchGpu;

    std::vector<HeightChunkSignature> m_lastHeightChunks;
    Vector3 m_lastHeightWorldOffset = Vector3(0.f, 0.f, 0.f);
    Vector3 m_prevWorldOffset = Vector3(0.f, 0.f, 0.f);
    bool m_hasPrevWorldOffset = false;

    std::vector<TerrainGrassScatterBatch> m_batchScratch;
    std::vector<TerrainGrassScatterBatch> m_uploadedBatches;
    std::vector<uint32_t> m_threadBatchScratch;
    uint32_t m_uploadedThreadCount = 0;
  };
}
