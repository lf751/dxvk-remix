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
#include "rtx_ngx_neural_library.h"

#include <windows.h>
#include <cstring>
#include <nvsdk_ngx_vk.h>
#ifdef _M_X64
#include <nvsdk_ngx_defs_dlssnr.h>
#endif

#include "dxvk_device.h"
#include "rtx_options.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

namespace dxvk {
  namespace {
    DWORD WINAPI neuralCallerName(HMODULE module, LPWSTR path, DWORD size) {
      HMODULE caller = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&neuralCallerName), &caller);
      if (module == caller && size >= 10) {
        wcscpy_s(path, size, L"nvngx.dll");
        return 9;
      }
      return GetModuleFileNameW(module, path, size);
    }

    void** findCallerNameImport(HMODULE module) {
      const auto* base = reinterpret_cast<const uint8_t*>(module);
      const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return nullptr;
      }
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
      if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
      }
      const uint64_t size = nt->OptionalHeader.SizeOfImage;
      const auto fits = [size](uint64_t offset, uint64_t length) { return offset < size && length <= size - offset; };
      const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      for (uint64_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const uint64_t rva = imports.VirtualAddress + offset;
        if (!fits(rva, sizeof(IMAGE_IMPORT_DESCRIPTOR))) {
          break;
        }
        const auto* entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + rva);
        if (!entry->Name) {
          break;
        }
        if (!entry->OriginalFirstThunk || !entry->FirstThunk) {
          continue;
        }
        for (uint64_t thunk = 0; fits(entry->OriginalFirstThunk + thunk, sizeof(IMAGE_THUNK_DATA)); thunk += sizeof(IMAGE_THUNK_DATA)) {
          const auto* name = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + entry->OriginalFirstThunk + thunk);
          if (!name->u1.AddressOfData) {
            break;
          }
          if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) {
            continue;
          }
          constexpr char importName[] = "GetModuleFileNameW";
          const uint64_t nameRva = name->u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name);
          const uint64_t slotRva = entry->FirstThunk + thunk;
          if (fits(nameRva, sizeof(importName)) && fits(slotRva, sizeof(void*))
            && std::memcmp(base + nameRva, importName, sizeof(importName)) == 0) {
            return reinterpret_cast<void**>(const_cast<uint8_t*>(base) + slotRva);
          }
        }
      }
      return nullptr;
    }

    bool writeImport(void** slot, void* value) {
      DWORD protection = 0;
      if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) {
        return false;
      }
      InterlockedExchangePointer(slot, value);
      DWORD previous = 0;
      return VirtualProtect(slot, sizeof(void*), protection, &previous) != 0;
    }
  }

  struct NGXNeuralLibrary::State {
    DxvkDevice* device;
    HMODULE module = nullptr;
    void** callerSlot = nullptr;
    void* callerFunction = nullptr;
    bool initialized = false;
    using Init = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice,
      PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version);
    Init init = nullptr;
    decltype(&NVSDK_NGX_VULKAN_CreateFeature1) create = nullptr;
    decltype(&NVSDK_NGX_VULKAN_EvaluateFeature) evaluate = nullptr;
    decltype(&NVSDK_NGX_VULKAN_ReleaseFeature) release = nullptr;
    decltype(&NVSDK_NGX_VULKAN_Shutdown1) shutdown = nullptr;
  };

  NGXNeuralLibrary::NGXNeuralLibrary(DxvkDevice* device) : m_state(std::make_unique<State>()) {
    m_state->device = device;
  }

  NGXNeuralLibrary::~NGXNeuralLibrary() {
    State& state = *m_state;
    if (state.initialized) {
      state.shutdown(state.device->handle());
    }
    if (state.callerSlot && !writeImport(state.callerSlot, state.callerFunction)) {
      Logger::err("[DLSS-NR direct] Could not restore the caller-name import.");
    }
    if (state.module) {
      FreeLibrary(state.module);
    }
  }

  bool NGXNeuralLibrary::initialize(const std::wstring& path) {
#ifndef _M_X64
    return false;
#else
    State& state = *m_state;
    state.module = LoadLibraryW(path.c_str());
    if (!state.module) {
      Logger::err(str::format("[DLSS-NR direct] LoadLibrary failed: ", GetLastError()));
      return false;
    }
    state.init = reinterpret_cast<State::Init>(GetProcAddress(state.module, "NVSDK_NGX_VULKAN_Init_Ext2"));
    state.create = reinterpret_cast<decltype(state.create)>(GetProcAddress(state.module, "NVSDK_NGX_VULKAN_CreateFeature1"));
    state.evaluate = reinterpret_cast<decltype(state.evaluate)>(GetProcAddress(state.module, "NVSDK_NGX_VULKAN_EvaluateFeature"));
    state.release = reinterpret_cast<decltype(state.release)>(GetProcAddress(state.module, "NVSDK_NGX_VULKAN_ReleaseFeature"));
    state.shutdown = reinterpret_cast<decltype(state.shutdown)>(GetProcAddress(state.module, "NVSDK_NGX_VULKAN_Shutdown1"));
    if (!state.init || !state.create || !state.evaluate || !state.release || !state.shutdown) {
      Logger::err("[DLSS-NR direct] Required Vulkan entry points are missing.");
      return false;
    }
    state.callerSlot = findCallerNameImport(state.module);
    if (!state.callerSlot) {
      Logger::err("[DLSS-NR direct] The DLL has no supported caller-name import.");
      return false;
    }
    state.callerFunction = *state.callerSlot;
    if (!writeImport(state.callerSlot, reinterpret_cast<void*>(&neuralCallerName))) {
      Logger::err("[DLSS-NR direct] Could not configure the caller-name import.");
      return false;
    }
    const std::wstring folder = str::tows(env::getExePath().c_str());
    const std::wstring directory = folder.substr(0, folder.find_last_of(L"\\/"));
    const auto result = state.init(RtxOptions::applicationId(), directory.c_str(), state.device->instance()->handle(),
      state.device->adapter()->handle(), state.device->handle(), nullptr, nullptr, nullptr, NVSDK_NGX_Version_API);
    state.initialized = NVSDK_NGX_SUCCEED(result);
    Logger::info(str::format("[DLSS-NR direct] ", str::fromws(path.c_str()), " initialization result: ", static_cast<uint32_t>(result)));
    return state.initialized;
#endif
  }

  uint32_t NGXNeuralLibrary::create(VkCommandBuffer commands, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle) {
#ifdef _M_X64
    return m_state->create(m_state->device->handle(), commands, NVSDK_NGX_Feature_DLSSNR, parameters, handle);
#else
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
#endif
  }

  uint32_t NGXNeuralLibrary::evaluate(VkCommandBuffer commands, NVSDK_NGX_Handle* handle, NVSDK_NGX_Parameter* parameters) {
    return m_state->evaluate(commands, handle, parameters, nullptr);
  }

  void NGXNeuralLibrary::release(NVSDK_NGX_Handle* handle) {
    m_state->release(handle);
  }
}
