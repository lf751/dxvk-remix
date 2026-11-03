/*
* Copyright (c) 2023-2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace dxvk {
  class DxvkDevice;

  // Owns a direct DLSS-NR DLL session shared by independent temporal-history handles.
  class NGXNeuralLibrary final {
  public:
    explicit NGXNeuralLibrary(DxvkDevice* device);
    ~NGXNeuralLibrary();

    bool initialize(const std::wstring& path);
    uint32_t create(VkCommandBuffer commands, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle);
    uint32_t evaluate(VkCommandBuffer commands, NVSDK_NGX_Handle* handle, NVSDK_NGX_Parameter* parameters);
    void release(NVSDK_NGX_Handle* handle);

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
}
