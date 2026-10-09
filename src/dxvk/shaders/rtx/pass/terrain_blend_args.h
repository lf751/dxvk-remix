/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/utility/shader_types.h"

struct TerrainBlendLayerArgs {
  uint albedoTextureIndex = 0xffff;
  uint normalTextureIndex = 0xffff;
  uint roughnessTextureIndex = 0xffff;
  uint heightTextureIndex = 0xffff;
  uint samplerIndex = 0xffff;
  uint albedoIsSrgb = 0;
  float unitsPerRepeat = 8.f;
  float roughnessConstant = 0.7f;
  vec4 albedoConstant = vec4(0.5f, 0.5f, 0.5f, 1.f);
};

struct TerrainBlendArgs {
  uint enabled = 0;
  uint zUp = 0;
  float lowElevation = 0.f;
  float highElevation = 4.f;
  float slopeCosStart = 0.8660254f;
  float slopeCosEnd = 0.5735764f;
  float heightBlendStrength = 0.2f;
  float heightBlendWidth = 0.2f;
  uint flipElevation = 0;
  uint rockMode = 0;
  float rockLowElevation = 4.f;
  float rockHighElevation = 8.f;
  uint coverageTextureIndex = 0xffff;
  uint coverageSamplerIndex = 0xffff;
  float coverageSandStrength = 0.f;
  float coverageSandWidth = 2.f;
  vec4 worldOffset = vec4(0.f);
  TerrainBlendLayerArgs layers[3];
};

#ifdef __cplusplus
static_assert(sizeof(TerrainBlendLayerArgs) == 48);
static_assert(sizeof(TerrainBlendArgs) == 224);
#endif
