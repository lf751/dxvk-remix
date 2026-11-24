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
#include <cstdio>

#include "../../../src/frontends/d3d9/d3d9_frontend_lifecycle.h"

namespace {
  using namespace dxvk::frontends::d3d9;

  int g_failures = 0;

  void expect(bool condition, const char* what) {
    if (!condition) {
      std::printf("FAILED: %s\n", what);
      ++g_failures;
    }
  }

  void testHiddenUnseenAgedOutUpdate() {
    DirectObjectLifecycle existing;
    existing.frameCreated = 10;
    existing.frameMoved = 12;
    existing.frameMergedPass = 15;
    existing.releasing = true;
    existing.agedOut = true;
    existing.hiddenUnseen = true;

    const bool shouldUnhide = prepareHandoverRebuild(existing);
    expect(shouldUnhide, "hidden unseen entry must be unhidden before buildInstance");
    expect(!existing.hiddenUnseen, "prepareHandoverRebuild clears hiddenUnseen after scheduling unhide");

    DirectObjectLifecycle replacement;
    carryOverOnUpdate(replacement, existing, false, 25);
    expect(replacement.frameCreated == 10, "frameCreated carries over across update");
    expect(replacement.frameMoved == 12, "stationary update keeps previous frameMoved");
    expect(replacement.frameMergedPass == 15, "frameMergedPass carries over across update");
    expect(replacement.releasing, "releasing flag carries over while awaiting app destroy");
    expect(replacement.agedOut, "agedOut flag carries over while awaiting app destroy");
    expect(!replacement.hiddenUnseen, "unhidden entry remains visible after replacement");
  }

  void testVisibleMovingUpdate() {
    DirectObjectLifecycle existing;
    existing.frameCreated = 4;
    existing.frameMoved = 8;
    existing.frameMergedPass = 9;
    existing.releasing = false;
    existing.agedOut = false;
    existing.hiddenUnseen = false;

    expect(!prepareHandoverRebuild(existing), "visible entry does not need an unhide call");

    DirectObjectLifecycle replacement;
    carryOverOnUpdate(replacement, existing, true, 30);
    expect(replacement.frameCreated == 4, "frameCreated stays at original creation frame");
    expect(replacement.frameMoved == 30, "moving update advances frameMoved to current frame");
    expect(replacement.frameMergedPass == 9, "frameMergedPass stays intact");
    expect(!replacement.releasing && !replacement.agedOut && !replacement.hiddenUnseen,
           "active entry remains non-releasing and visible");
  }
}

int main() {
  testHiddenUnseenAgedOutUpdate();
  testVisibleMovingUpdate();
  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_d3d9_frontend_lifecycle PASSED\n");
  return 0;
}
