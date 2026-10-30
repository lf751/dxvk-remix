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

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_materials.h"

namespace dxvk {
Logger Logger::s_instance("test_terrain_surface_packing.log");
}

namespace {
using namespace dxvk;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

uint16_t readU16(const unsigned char* bytes) {
  uint16_t value;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

uint32_t readU32(const unsigned char* bytes) {
  uint32_t value;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

RtSurfaceMaterial makeMaterial(uint32_t secondaryTexture) {
  constexpr uint32_t kInvalid = kSurfaceMaterialInvalidTextureIndex;
  return RtSurfaceMaterial(RtOpaqueSurfaceMaterial(
    kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
    0.f, 0.f, Vector4(1.f), 0.7f, 0.f, Vector3(0.f), false,
    false, false, false, 200.f, 0, 0.f, 0.f,
    false, 1.f, 1.f, 1.f, SURFACE_INDEX_INVALID, false, false, 0, secondaryTexture));
}

void testSurfaceFlagsStayIndependent() {
  RtSurface surface;
  surface.positionBufferIndex = 0;
  std::array<unsigned char, kSurfaceGPUSize> baseline {};
  size_t offset = 0;
  surface.writeGPUData(baseline.data(), offset);
  require(offset == kSurfaceGPUSize, "Surface record size changed.");
  const uint16_t defaultFlags = readU16(baseline.data() + 12);
  const uint32_t defaultAlpha = rtSurfaceCoreAlphaDword(surface);
  require((defaultFlags & 0xcu) == 0, "Terrain flags must default off.");
  require((defaultAlpha & CORE_MATERIAL_SURFACE_WORLD_OPACITY) == 0, "Core world opacity must default off.");

  for (uint32_t flags = 0; flags < 4; ++flags) {
    surface.terrainBlend = (flags & 1u) != 0;
    surface.worldSpaceSecondaryOpacity = (flags & 2u) != 0;
    std::array<unsigned char, kSurfaceGPUSize> bytes {};
    offset = 0;
    surface.writeGPUData(bytes.data(), offset);
    require(readU16(bytes.data() + 12) == uint16_t(defaultFlags | (flags << 2)),
            "Terrain flags must occupy only surface flags0 bits 2 and 3.");
    bytes[12] = baseline[12];
    bytes[13] = baseline[13];
    require(bytes == baseline, "Terrain flags changed unrelated serialized surface fields.");
    const uint32_t expectedAlpha = defaultAlpha | (surface.worldSpaceSecondaryOpacity ? (1u << 29) : 0u);
    require(rtSurfaceCoreAlphaDword(surface) == expectedAlpha,
            "Core alpha must mirror only world opacity at bit 29.");
  }
}

void testCoreSecondaryTextureMatchesOpaqueRecord() {
  const std::vector<RtSurfaceMaterial> extensions;
  RtSurface surface;
  surface.positionBufferIndex = 0;
  surface.worldSpaceSecondaryOpacity = true;
  static_assert(CORE_MATERIAL_EXT_OFFSET_SECONDARY_TEXTURE == 42);
  static_assert(CORE_MATERIAL_SURFACE_WORLD_OPACITY == (1u << 29));
  for (const uint32_t texture : { 0x3456u, kSurfaceMaterialInvalidTextureIndex }) {
    const RtSurfaceMaterial material = makeMaterial(texture);
    require(material.needsCoreExtension(extensions) == (texture != kSurfaceMaterialInvalidTextureIndex),
            "A secondary texture must retain a core extension even without POM or subsurface data.");
    std::array<unsigned char, kSurfaceMaterialGPUSize> opaque {};
    std::array<unsigned char, CORE_MATERIAL_EXT_SIZE> extension {};
    std::array<unsigned char, CORE_MATERIAL_SIZE> core {};
    size_t offset = 0;
    material.writeGPUData(opaque.data(), offset, 0);
    require(readU16(opaque.data() + 6) == texture, "Opaque secondary texture offset changed.");
    offset = 0;
    material.writeCoreExtensionGPUData(extension.data(), offset, extensions);
    require(offset == CORE_MATERIAL_EXT_SIZE, "Core extension record size changed.");
    require(readU16(extension.data() + 42) == texture, "Core extension lost the secondary texture index.");
    offset = 0;
    material.writeCoreGPUData(core.data(), offset, 0, surface, false, extensions, 7);
    require((readU32(core.data() + CORE_MATERIAL_OFFSET_ALPHA) & (1u << 29)) != 0,
            "Serialized core material lost world opacity.");
  }
}
}

int main() {
  try {
    testSurfaceFlagsStayIndependent();
    testCoreSecondaryTextureMatchesOpaqueRecord();
    std::cout << "Terrain surface and core packing tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return -1;
  }
}
