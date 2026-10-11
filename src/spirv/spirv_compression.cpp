#include "spirv_compression.h"

// NV-DXVK start: Embedded SPIR-V decoding
#include <cstring>
#include "../util/util_error.h"
// NV-DXVK end

namespace dxvk {

  // NV-DXVK start: Embedded SPIR-V decoding
  SpirvCodeBuffer SpirvCompressedBuffer::decodeEmbedded(size_t byteSize, const uint32_t* pData) {
    constexpr uint32_t kCompressedMagic = 0x43565053;
    if (byteSize < sizeof(uint32_t) || byteSize % sizeof(uint32_t) != 0) {
      throw DxvkError("Invalid embedded shader size");
    }
    if (pData[0] != kCompressedMagic) {
      return SpirvCodeBuffer(uint32_t(byteSize / sizeof(uint32_t)), pData);
    }
    if (byteSize < 16 || pData[1] < 5 || pData[3] != 0) {
      throw DxvkError("Invalid compressed shader header");
    }
    const size_t maskCount = (size_t(pData[1]) + NumMaskWords - 1) / NumMaskWords;
    const size_t payloadBytes = pData[2];
    const size_t payloadWords = (payloadBytes + 7) / 8;
    if (byteSize != 16 + (maskCount + payloadWords) * sizeof(uint64_t)) {
      throw DxvkError("Invalid compressed shader payload size");
    }
    SpirvCompressedBuffer compressed;
    compressed.m_size = pData[1];
    compressed.m_mask.resize(maskCount);
    compressed.m_code.resize(payloadWords);
    const auto* pBytes = reinterpret_cast<const uint8_t*>(pData) + 16;
    std::memcpy(compressed.m_mask.data(), pBytes, maskCount * sizeof(uint64_t));
    std::memcpy(compressed.m_code.data(), pBytes + maskCount * sizeof(uint64_t), payloadWords * sizeof(uint64_t));
    size_t expectedPayloadBytes = 0;
    for (uint32_t i = 0; i < compressed.m_size; i++) {
      expectedPayloadBytes += ((compressed.m_mask[i / NumMaskWords] >> (2 * (i % NumMaskWords))) & 3) + 1;
    }
    if (expectedPayloadBytes != payloadBytes) {
      throw DxvkError("Invalid compressed shader byte counts");
    }
    SpirvCodeBuffer code = compressed.decompress();
    if (code.data()[0] != 0x07230203) {
      throw DxvkError("Invalid decompressed SPIR-V header");
    }
    return code;
  }
  // NV-DXVK end

  SpirvCompressedBuffer::SpirvCompressedBuffer()
  : m_size(0) {

  }


  SpirvCompressedBuffer::SpirvCompressedBuffer(
    const SpirvCodeBuffer& code)
  : m_size(code.dwords()) {
    const uint32_t* data = code.data();

    // The compression works by eliminating leading null bytes
    // from DWORDs, exploiting that SPIR-V IDs are consecutive
    // integers that usually fall into the 16-bit range. For
    // each DWORD, a two-bit integer is stored which indicates
    // the number of bytes it takes in the compressed buffer.
    // This way, it can achieve a compression ratio of ~50%.
    m_mask.reserve((m_size + NumMaskWords - 1) / NumMaskWords);
    m_code.reserve((m_size + 1) / 2);

    uint64_t dstWord  = 0;
    uint32_t dstShift = 0;

    for (uint32_t i = 0; i < m_size; i += NumMaskWords) {
      uint64_t byteCounts = 0;

      for (uint32_t w = 0; w < NumMaskWords && i + w < m_size; w++) {
        uint64_t word = data[i + w];
        uint64_t bytes = 0;

        if      (word < (1 <<  8)) bytes = 0;
        else if (word < (1 << 16)) bytes = 1;
        else if (word < (1 << 24)) bytes = 2;
        else                       bytes = 3;

        byteCounts |= bytes << (2 * w);

        uint32_t bits = 8 * bytes + 8;
        uint32_t rem  = bit::pack(dstWord, dstShift, word, bits);

        if (unlikely(rem != 0)) {
          m_code.push_back(dstWord);

          dstWord  = 0;
          dstShift = 0;

          bit::pack(dstWord, dstShift, word >> (bits - rem), rem);
        }
      }

      m_mask.push_back(byteCounts);
    }

    if (dstShift)
      m_code.push_back(dstWord);

    m_mask.shrink_to_fit();
    m_code.shrink_to_fit();
  }

    
  SpirvCompressedBuffer::~SpirvCompressedBuffer() {

  }


  SpirvCodeBuffer SpirvCompressedBuffer::decompress() const {
    SpirvCodeBuffer code(m_size);
    uint32_t* data = code.data();

    if (m_size == 0)
      return code;

    uint32_t maskIdx = 0;
    uint32_t codeIdx = 0;

    uint64_t srcWord  = m_code[codeIdx++];
    uint32_t srcShift = 0;

    for (uint32_t i = 0; i < m_size; i += NumMaskWords) {
      uint64_t srcMask = m_mask[maskIdx++];

      for (uint32_t w = 0; w < NumMaskWords && i + w < m_size; w++) {
        uint32_t bits = 8 * ((srcMask & 3) + 1);

        uint64_t word = 0;
        uint32_t rem = bit::unpack(word, srcWord, srcShift, bits);

        if (unlikely(rem != 0)) {
          srcWord  = m_code[codeIdx++];
          srcShift = 0;

          uint64_t tmp = 0;
          bit::unpack(tmp, srcWord, srcShift, rem);
          word |= tmp << (bits - rem);
        }

        data[i + w] = word;
        srcMask >>= 2;
      }
    }

    return code;
  }

}
