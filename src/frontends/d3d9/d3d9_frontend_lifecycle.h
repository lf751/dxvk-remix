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

#include <cstdint>

namespace dxvk {
  namespace frontends {
    namespace d3d9 {

      // Per-object lifecycle and visibility flags that survive an in-place
      // CommitMode::Update on the CS thread.
      struct DirectObjectLifecycle {
        uint32_t frameCreated = 0;
        uint32_t frameMoved = ~0u;
        uint32_t frameMergedPass = 0; // The last frame mergePass ran for the object
        bool releasing = false;       // Released sent, waiting for the app thread's destroy
        bool agedOut = false;         // released because the game stopped drawing it
        bool hiddenUnseen = false;    // hidden in the frames it is not drawn, until the app thread's destroy arrives
      };

      // Called before rebuilding an existing handle in place (CommitMode::Update).
      // Returns true when the scene entry must be made visible before buildInstance
      // so buildNow builds it visible under every retainedHiddenPolicy.
      inline bool prepareHandoverRebuild(DirectObjectLifecycle& existing) {
        if (!existing.hiddenUnseen) {
          return false;
        }
        existing.hiddenUnseen = false;
        return true;
      }

      // Copies lifecycle state from the existing entry onto the replacement entry
      // created for an accepted in-place CommitMode::Update.
      inline void carryOverOnUpdate(DirectObjectLifecycle& next,
                                    const DirectObjectLifecycle& previous,
                                    bool moved,
                                    uint32_t frame) {
        next.frameCreated = previous.frameCreated;
        next.frameMoved = moved ? frame : previous.frameMoved;
        next.frameMergedPass = previous.frameMergedPass;
        next.releasing = previous.releasing;
        next.agedOut = previous.agedOut;
        next.hiddenUnseen = previous.hiddenUnseen;
      }

    } // namespace d3d9
  } // namespace frontends
} // namespace dxvk
