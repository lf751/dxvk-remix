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
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
#include "rtx/pass/smooth_normals_binding_indices.h"
#include "rtx/pass/smooth_normals.h"
#include "rtx/pass/gen_tri_list_index_buffer_indices.h"
#include "rtx/pass/gen_tri_list_index_buffer.h"

namespace dxvk {
  Logger Logger::s_instance("test_smooth_normals.log");
}

namespace {
using namespace dxvk;

void expectNormal(Vector3 actual, Vector3 expected, const char* label) {
  expected /= length(expected);
  if (length(actual - expected) > 2e-4f) {
    std::cerr << label << ": got " << actual.x << ", " << actual.y << ", " << actual.z << std::endl;
    throw std::runtime_error(label);
  }
}

std::vector<Vector3> smooth(const std::vector<float>& positions, const std::vector<uint32_t>& indices, uint tableSize = 2048) {
  SmoothNormalsArgs args {};
  args.positionStride = 12;
  args.indexStride = 4;
  args.numVertices = static_cast<uint32_t>(positions.size() / 3);
  args.numTriangles = static_cast<uint32_t>(indices.size() / 3);
  args.hashTableSize = tableSize;
  std::vector<int32_t> table(tableSize * 4, 0);
  for (uint32_t tri = 0; tri < args.numTriangles; ++tri) {
    smoothNormalsAccumulate(tri, positions.data(), indices.data(), table.data(), args);
  }
  std::vector<Vector3> normals;
  for (uint32_t vertex = 0; vertex < args.numVertices; ++vertex) {
    float encoded;
    smoothNormalsScatter(vertex, positions.data(), &encoded, table.data(), args);
    normals.push_back(decodeNormal(asuint(encoded)));
  }
  return normals;
}

void testSteepGrid() {
  std::vector<float> positions;
  std::vector<uint32_t> indices;
  for (uint32_t z = 0; z < 17; ++z) {
    for (uint32_t x = 0; x < 17; ++x) {
      positions.insert(positions.end(), { float(x), float(x * 2), float(z) });
      if (x < 16 && z < 16) {
        const uint32_t vertex = z * 17 + x;
        indices.insert(indices.end(), { vertex, vertex + 17, vertex + 1, vertex + 1, vertex + 17, vertex + 18 });
      }
    }
  }
  for (const auto normal : smooth(positions, indices)) {
    expectNormal(normal, Vector3(-2.f, 1.f, 0.f), "steep integer grid normal");
  }
}

void testDistinctPositionsAndSharedSeam() {
  // The first vertices had the same old 32-bit position tag despite different positions.
  const std::vector<float> positions = { 4,0,1, 4,0,2, 5,0,1, 1,0,4, 1,1,4, 1,0,5 };
  const auto separate = smooth(positions, { 0,1,2, 3,4,5 });
  expectNormal(separate[0], Vector3(0,1,0), "distinct position keeps up normal");
  expectNormal(separate[3], Vector3(1,0,0), "distinct position keeps side normal");
  const std::vector<float> seam = { 0,0,0, 0,0,1, 1,0,0, -0.f,0,0, 0,1,0, 0,0,1 };
  const auto joined = smooth(seam, { 0,1,2, 3,4,5 });
  expectNormal(joined[0], Vector3(1,1,0), "duplicate position weld");
  expectNormal(joined[3], Vector3(1,1,0), "signed zero duplicate weld");
}

void testCollisionChainBeyond128() {
  SmoothNormalsArgs args {};
  args.positionStride = 12;
  args.hashTableSize = 256;
  std::vector<float> positions;
  for (uint32_t candidate = 0; positions.size() < 160 * 3; ++candidate) {
    const Vector3 position(float(candidate), 0.f, 0.f);
    if (hashPositionSlot(position, args.hashTableSize) == 0) {
      positions.insert(positions.end(), { position.x, position.y, position.z });
    }
  }
  args.numVertices = 160;
  std::vector<int32_t> table(args.hashTableSize * 4, 0);
  for (uint32_t vertex = 0; vertex < args.numVertices; ++vertex) {
    const auto position = smoothNormalsLoadPosition(vertex, positions.data(), args);
    accumulateAtPosition(vertex, position, Vector3(0,0,1), positions.data(), table.data(), args);
  }
  for (uint32_t vertex = 0; vertex < args.numVertices; ++vertex) {
    float encoded;
    smoothNormalsScatter(vertex, positions.data(), &encoded, table.data(), args);
    expectNormal(decodeNormal(asuint(encoded)), Vector3(0,0,1), "long collision chain retained");
  }
}

void testConvertedTopology(VkPrimitiveTopology topology) {
  const std::vector<float> positions = { 0,0,0, 0,0,1, 1,2,0, 1,2,1 };
  GenTriListArgs conversion {};
  conversion.topology = static_cast<uint32_t>(topology);
  conversion.useIndexBuffer = 1;
  conversion.maxVertex = 3;
  const uint16_t strip[] = { 0,1,2,3 };
  const uint16_t fan[] = { 0,1,3,2 };
  uint16_t converted[6] {};
  for (uint32_t tri = 0; tri < 2; ++tri) {
    generateIndices(tri, converted, topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ? strip : fan, conversion);
  }
  const std::vector<uint32_t> indices(converted, converted + 6);
  for (const auto normal : smooth(positions, indices)) {
    expectNormal(normal, Vector3(-2,1,0), "converted strip/fan normal");
  }
}

void testConvertedVertexDomain() {
  const std::vector<Vector3> vertices = {
    Vector3(1,0,1), Vector3(0,0,0), Vector3(0,0,1), Vector3(0,0,2), Vector3(1,0,2),
    Vector3(2,0,2), Vector3(2,0,1), Vector3(2,0,0), Vector3(1,0,0)
  };
  std::vector<float> positions(4 + vertices.size() * 7, -1000.f);
  for (uint32_t i = 0; i < vertices.size(); ++i) {
    positions[4 + i * 7] = vertices[i].x;
    positions[5 + i * 7] = vertices[i].y;
    positions[6 + i * 7] = vertices[i].z;
  }
  std::vector<uint32_t> rasterIndices = {0xffffffff, 0xffffffff, 100};
  for (uint32_t i = 0; i < 17; ++i) {
    rasterIndices.push_back(101 + i % 8);
  }
  rasterIndices.push_back(19711);
  GenTriListArgs conversion {};
  conversion.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  conversion.useIndexBuffer = 1;
  conversion.srcUseUint32 = 1;
  conversion.firstIndex = 2;
  conversion.minVertex = 100;
  conversion.maxVertex = 108;
  std::vector<uint16_t> converted(17 * 3);
  for (uint32_t tri = 0; tri < 17; ++tri) {
    generateIndices(tri, converted.data(), rasterIndices.data(), conversion);
  }
  std::vector<uint32_t> indices((converted.size() + 3) / 2, 0);
  for (uint32_t i = 0; i < converted.size(); ++i) {
    if (converted[i] >= vertices.size()) {
      throw std::runtime_error("converted index outside vertex domain");
    }
    const uint32_t halfword = i + 2;
    indices[halfword / 2] |= uint32_t(converted[i]) << ((halfword % 2) * 16);
  }
  SmoothNormalsArgs args {};
  args.positionOffset = 16;
  args.positionStride = 28;
  args.indexOffset = 4;
  args.indexStride = 2;
  args.useShortIndices = 1;
  args.numVertices = 9;
  args.numTriangles = 17;
  args.hashTableSize = 256;
  std::vector<int32_t> table(args.hashTableSize * 4, 0);
  for (uint32_t tri = 0; tri < args.numTriangles; ++tri) {
    smoothNormalsAccumulate(tri, positions.data(), indices.data(), table.data(), args);
  }
  for (uint32_t vertex = 0; vertex < args.numVertices; ++vertex) {
    float encoded;
    smoothNormalsScatter(vertex, positions.data(), &encoded, table.data(), args);
    expectNormal(decodeNormal(asuint(encoded)), Vector3(0,1,0), "converted vertex domain normal");
  }
}
}

int main() {
  try {
    testSteepGrid();
    testDistinctPositionsAndSharedSeam();
    testCollisionChainBeyond128();
    testConvertedTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    testConvertedTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);
    testConvertedVertexDomain();
    std::cout << "Smooth normals regressions passed." << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return -1;
  }
}
