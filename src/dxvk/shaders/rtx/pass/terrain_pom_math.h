/*
 * Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once

// NV-DXVK start: Shared terrain relief polynomial intersection math.
#ifdef __cplusplus
#include <cmath>
#define TERRAIN_POM_OUT(type, name) type& name
#define TERRAIN_POM_INLINE inline
#define TERRAIN_POM_ABS(value) std::abs(value)
#define TERRAIN_POM_SQRT(value) std::sqrt(value)
#else
#define TERRAIN_POM_OUT(type, name) out type name
#define TERRAIN_POM_INLINE
#define TERRAIN_POM_ABS(value) abs(value)
#define TERRAIN_POM_SQRT(value) sqrt(value)
#endif

TERRAIN_POM_INLINE int terrainPomRoots(float a, float b, float c,
    TERRAIN_POM_OUT(float, root0), TERRAIN_POM_OUT(float, root1)) {
  root0 = 0.f;
  root1 = 0.f;
  if (a == 0.f) {
    if (b == 0.f) {
      return c == 0.f ? 1 : 0;
    }
    root0 = -c / b;
    root1 = root0;
    return 1;
  }
  const float absoluteA = TERRAIN_POM_ABS(a);
  const float absoluteB = TERRAIN_POM_ABS(b);
  const float absoluteC = TERRAIN_POM_ABS(c);
  const float scaleAB = absoluteA > absoluteB ? absoluteA : absoluteB;
  const float scale = scaleAB > absoluteC ? scaleAB : absoluteC;
  a /= scale;
  b /= scale;
  c /= scale;
  const float discriminant = b * b - 4.f * a * c;
  if (discriminant < 0.f) {
    return 0;
  }
  if (discriminant == 0.f) {
    root0 = -0.5f * b / a;
    root1 = root0;
    return 1;
  }
  // The q formulation preserves the small root when b nearly cancels sqrt(discriminant).
  const float radical = TERRAIN_POM_SQRT(discriminant);
  const float q = -0.5f * (b + (b >= 0.f ? radical : -radical));
  const float first = q / a;
  const float second = c / q;
  root0 = first < second ? first : second;
  root1 = first < second ? second : first;
  return 2;
}

TERRAIN_POM_INLINE bool terrainPomFirstRoot(float a, float b, float c, float lower, float upper,
    TERRAIN_POM_OUT(float, hit)) {
  hit = lower;
  if (upper < lower) {
    return false;
  }
  if (a == 0.f && b == 0.f && c == 0.f) {
    return true;
  }
  float root0, root1;
  const int count = terrainPomRoots(a, b, c, root0, root1);
  if (count > 0 && root0 >= lower && root0 <= upper) {
    hit = root0;
    return true;
  }
  if (count > 1 && root1 >= lower && root1 <= upper) {
    hit = root1;
    return true;
  }
  return false;
}

#undef TERRAIN_POM_OUT
#undef TERRAIN_POM_INLINE
#undef TERRAIN_POM_ABS
#undef TERRAIN_POM_SQRT
// NV-DXVK end
