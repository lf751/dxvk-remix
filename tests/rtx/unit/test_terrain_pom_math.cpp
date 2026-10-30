/*
 * Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "rtx/pass/terrain_pom_math.h"

namespace {
void expect(bool condition, const char* label) {
  if (!condition) {
    throw std::runtime_error(label);
  }
}

void expectNear(float actual, float expected, float tolerance, const char* label) {
  expect(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, label);
}
}

int main() {
  try {
    float hit, root0, root1;
    expect(terrainPomFirstRoot(2.f, -2.05f, 0.45f, 0.f, 1.f, hit), "Interior peak first hit missing");
    expectNear(hit, 0.31845f, 1e-4f, "Interior peak first hit wrong");
    expect(terrainPomRoots(2.f, -2.05f, 0.45f, root0, root1) == 2, "Interior peak roots missing");
    expect(root0 < root1 && root1 < 1.f, "Interior peak roots unordered");
    expect(terrainPomFirstRoot(2.f, -2.05f, 0.45f, 0.5f, 1.f, hit), "Second root in clipped interval missing");
    expectNear(hit, root1, 1e-6f, "Wrong clipped root");
    expect(terrainPomRoots(1.f, -2.f, 1.f, root0, root1) == 1, "Tangent must have one root");
    expectNear(root0, 1.f, 1e-6f, "Tangent root wrong");
    expect(terrainPomFirstRoot(0.f, 2.f, -1.f, 0.f, 1.f, hit), "Linear root missing");
    expectNear(hit, 0.5f, 1e-6f, "Linear root wrong");
    expect(!terrainPomFirstRoot(1.f, 0.f, 1.f, -10.f, 10.f, hit), "Imaginary roots accepted");
    expect(!terrainPomFirstRoot(1.f, -2.f, 1.f, 0.f, 0.9f, hit), "Out of interval root accepted");
    expect(!terrainPomFirstRoot(0.f, 0.f, 1.f, 0.f, 1.f, hit), "Nonzero constant accepted");
    expect(terrainPomFirstRoot(0.f, 0.f, 0.f, 0.25f, 1.f, hit), "Coincident polynomial missed");
    expectNear(hit, 0.25f, 0.f, "Coincident polynomial wrong first hit");
    expect(!terrainPomFirstRoot(0.f, 0.f, 0.f, 1.f, 0.f, hit), "Invalid interval accepted");
    expect(terrainPomRoots(1.f, 1e8f, 1.f, root0, root1) == 2, "Large b roots missing");
    expectNear(root0, -1e8f, 8.f, "Large b large root wrong");
    expectNear(root1, -1e-8f, 1e-14f, "Cancellation destroyed small root");
    expect(terrainPomRoots(1.f, 0.f, 0.f, root0, root1) == 1, "Zero tangent root missing");
    expectNear(root0, 0.f, 0.f, "Zero tangent wrong");
    expect(terrainPomFirstRoot(1.f, -1.f, 0.f, 0.f, 0.f, hit), "Inclusive endpoint root missing");
    std::cout << "Terrain POM polynomial intersection tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return -1;
  }
}
