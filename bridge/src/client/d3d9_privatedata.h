/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace std {
  template<> struct hash<GUID> {
    size_t operator()(const GUID& guid) const noexcept {
      std::uint64_t words[2];
      std::memcpy(words, &guid, sizeof(guid));
      return std::hash<std::uint64_t>()(words[0]) ^ std::hash<std::uint64_t>()(words[1]);
    }
  };
}

class PrivateDataFactory {
  struct PrivateData {
    std::vector<std::uint8_t> bytes;
    IUnknown* pInterface = nullptr;

    PrivateData(const void* data, DWORD size, bool isInterface) {
      if (isInterface) {
        pInterface = const_cast<IUnknown*>(static_cast<const IUnknown*>(data));
        pInterface->AddRef();
      } else if (size) {
        const auto* source = static_cast<const std::uint8_t*>(data);
        bytes.assign(source, source + size);
      }
    }

    PrivateData(const PrivateData&) = delete;
    PrivateData& operator=(const PrivateData&) = delete;

    ~PrivateData() {
      if (pInterface) {
        pInterface->Release();
      }
    }

    DWORD size() const {
      return pInterface ? sizeof(IUnknown*) : static_cast<DWORD>(bytes.size());
    }
  };

  std::mutex m_mutex;
  std::unordered_map<GUID, std::shared_ptr<PrivateData>> m_privateData;

public:
  HRESULT setData(REFGUID refguid, const void* pData, DWORD sizeOfData, DWORD flags) {
    if ((flags & ~D3DSPD_IUNKNOWN) || (!pData && sizeOfData) ||
        ((flags & D3DSPD_IUNKNOWN) && (!pData || sizeOfData != sizeof(IUnknown*)))) {
      return D3DERR_INVALIDCALL;
    }

    try {
      auto data = std::make_shared<PrivateData>(pData, sizeOfData, (flags & D3DSPD_IUNKNOWN) != 0);
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_privateData[refguid].swap(data);
      }
      // Release replaced COM objects outside the lock, since Release can call application code.
      return D3D_OK;
    } catch (const std::bad_alloc&) {
      return E_OUTOFMEMORY;
    }
  }

  HRESULT getData(REFGUID refguid, void* pData, DWORD* pSizeOfData) {
    if (!pSizeOfData) {
      return D3DERR_INVALIDCALL;
    }
    std::shared_ptr<PrivateData> data;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      const auto entry = m_privateData.find(refguid);
      if (entry == m_privateData.end()) {
        return D3DERR_NOTFOUND;
      }
      data = entry->second;
    }
    const DWORD capacity = *pSizeOfData;
    *pSizeOfData = data->size();
    if (capacity < data->size()) {
      return D3DERR_MOREDATA;
    }
    if (pData) {
      if (data->pInterface) {
        data->pInterface->AddRef();
        std::memcpy(pData, &data->pInterface, sizeof(IUnknown*));
      } else if (!data->bytes.empty()) {
        std::memcpy(pData, data->bytes.data(), data->bytes.size());
      }
    }
    return D3D_OK;
  }

  HRESULT freeData(REFGUID refguid) {
    std::shared_ptr<PrivateData> data;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      const auto entry = m_privateData.find(refguid);
      if (entry == m_privateData.end()) {
        return D3DERR_NOTFOUND;
      }
      data = std::move(entry->second);
      m_privateData.erase(entry);
    }
    return D3D_OK;
  }
};
