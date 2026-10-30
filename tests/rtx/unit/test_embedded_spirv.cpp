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
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../../../src/spirv/spirv_compression.h"
#include "../../../src/util/util_error.h"

namespace {
std::vector<uint32_t> readWords(const char* pPath) {
  std::ifstream file(pPath, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() < 0 || file.tellg() % 4 != 0) {
    throw std::runtime_error("Invalid shader fixture");
  }
  std::vector<uint32_t> words(size_t(file.tellg()) / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(words.data()), words.size() * 4);
  if (!file) {
    throw std::runtime_error("Cannot read shader fixture");
  }
  return words;
}

void expectRejected(const std::vector<uint32_t>& words, size_t bytes) {
  try {
    dxvk::SpirvCompressedBuffer::decodeEmbedded(bytes, words.data());
  } catch (const dxvk::DxvkError&) {
    return;
  }
  throw std::runtime_error("Malformed shader was accepted");
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::runtime_error("Expected raw and compressed shader fixtures");
    }
    const auto raw = readWords(argv[1]);
    const auto packed = readWords(argv[2]);
    const auto decoded = dxvk::SpirvCompressedBuffer::decodeEmbedded(packed.size() * 4, packed.data());
    const auto direct = dxvk::SpirvCompressedBuffer::decodeEmbedded(raw.size() * 4, raw.data());
    if (decoded.dwords() != raw.size() || direct.dwords() != raw.size() ||
        std::memcmp(decoded.data(), raw.data(), raw.size() * 4) != 0 ||
        std::memcmp(direct.data(), raw.data(), raw.size() * 4) != 0) {
      throw std::runtime_error("Embedded shader round trip failed");
    }
    expectRejected(packed, 0);
    expectRejected(packed, 12);
    expectRejected(raw, raw.size() * 4 - 1);
    expectRejected(packed, packed.size() * 4 - 4);
    auto invalid = packed;
    invalid[2]++;
    expectRejected(invalid, invalid.size() * 4);
    invalid = packed;
    invalid[4] ^= 3;
    expectRejected(invalid, invalid.size() * 4);
    invalid = packed;
    invalid[3] = 1;
    expectRejected(invalid, invalid.size() * 4);
    invalid = packed;
    invalid[1] = 4;
    expectRejected(invalid, invalid.size() * 4);
    invalid = packed;
    invalid[1] = 0xffffffffu;
    expectRejected(invalid, invalid.size() * 4);
    invalid = packed;
    const size_t payloadOffset = 4 + 2 * ((packed[1] + 31) / 32);
    invalid[payloadOffset] ^= 1;
    expectRejected(invalid, invalid.size() * 4);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; i++) {
      dxvk::SpirvCompressedBuffer::decodeEmbedded(packed.size() * 4, packed.data());
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Lossless decode passed: " << raw.size() * 4 << " -> " << packed.size() * 4
      << " bytes; average decode " << elapsed / 100 << " ms\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
