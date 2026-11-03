#include <iostream>
#include <limits>
#include <stdexcept>

#include "../../../src/dxvk/rtx_render/rtx_scene_types.h"
#include "../../../src/dxvk/rtx_render/rtx_scene_manager.h"
#include "../../../src/frontends/translator/draw_key.h"
#include "../../../src/frontends/translator/change_test.h"

namespace dxvk {
Logger Logger::s_instance("test_geometry_continuity.log");
}

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

void testTopology() {
  dxvk::RasterGeometry original;
  original.vertexCount = 6;
  original.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  dxvk::RasterGeometry changed = original;
  require(changed.sameTopology(original), "Unchanged non-indexed geometry must permit refitting");
  changed.vertexCount = 9;
  require(!changed.sameTopology(original), "A resized non-indexed batch must rebuild despite its empty index hash");
  changed = original;
  changed.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  require(!changed.sameTopology(original), "A primitive topology change must rebuild");
  changed = original;
  changed.indexCount = 3;
  require(!changed.sameTopology(original), "A changed index count must rebuild");
  changed = original;
  changed.hashes[dxvk::HashComponents::Indices] = 123;
  require(!changed.sameTopology(original), "Changed connectivity must rebuild");
}

void testParkedBones() {
  dxvk::Matrix4 bones[3] = { dxvk::Matrix4(1.f), dxvk::Matrix4(1.f), dxvk::Matrix4(1.f) };
  bones[0][3].x = 10.f;
  bones[1][3].x = 12.f;
  bones[2][3].x = 1e29f;
  dxvk::Vector3 position;
  float radius = 0.f;
  require(dxvk::translator::skinPlacement(bones, 3, position, radius), "Finite bones must have a placement");
  require(position.x == 12.f && radius == 2.f, "A parked bone must not displace matching bounds");
  bones[0][0].x = std::numeric_limits<float>::infinity();
  require(!dxvk::translator::bonesFinite(bones, 3), "Non-finite bone matrices must be rejected");
}

void testSkinnedWorldChange() {
  using namespace dxvk::translator;
  ChangeRecord record;
  record.hasLast = true;
  record.inputs = 10;
  record.inputsNoWorld = 20;
  ChangeInputs input;
  input.comparable = true;
  input.inputs = 11;
  input.inputsNoWorld = 20;
  input.skinned = true;
  const ChangeVerdict skinned = ChangeTest::test(record, input, nullptr, 0);
  require(!skinned.repeat && !skinned.moved, "A skinned world change requires a pose update, not a rigid move");
  record.inputs = 10;
  input.skinned = false;
  require(ChangeTest::test(record, input, nullptr, 0).moved, "An equivalent rigid world change remains a move");
}

struct PosedDraw : dxvk::SceneDraw {
  PosedDraw() {
    skinningData.numBones = 1;
    skinningData.numBonesPerVertex = 1;
    skinningData.pBoneMatrices.emplace_back(1.f);
    skinningData.computeHash();
    geometryData.numBonesPerVertex = 1;
    usesVertexShader = true;
    newGeometryEveryFrame = true;
  }
};

void testReplacementOwnership() {
  dxvk::ExternalDrawState state;
  state.frontendResolved = true;
  state.deforming = true;
  auto parts = std::make_shared<std::vector<dxvk::ExternalDrawState::SubmeshPart>>(3);
  (*parts)[1].meshReplacement = false;
  state.inlineSubmeshParts = parts;
  const PosedDraw original;
  auto staticMesh = std::make_shared<dxvk::RasterGeometry>();
  staticMesh->vertexCount = 100;
  auto skinnedMesh = std::make_shared<dxvk::RasterGeometry>(*staticMesh);
  skinnedMesh->numBonesPerVertex = 1;

  state.drawCall = original;
  state.drawCall.overrideReplacementGeometryData(staticMesh);
  require(!state.submeshIsDeforming(0), "A static USD mesh must share geometry despite a deforming original");
  require(state.drawCall.getSkinningState().numBones == 0 && state.drawCall.getSkinningState().boneHash == 0,
          "Static replacement geometry must not inherit a source pose or its history hash");
  require(!state.drawCall.usesVertexShader && !state.drawCall.newGeometryEveryFrame,
          "Static replacements must not inherit vertex capture or ephemeral geometry semantics");
  require(&state.drawCall.getGeometryData() == staticMesh.get(), "Replacement mesh storage must remain shared");

  state.drawCall = original;
  require(state.submeshIsDeforming(1), "An includeOriginal part after a static part must keep its deformation");
  require(state.drawCall.getSkinningState().boneHash == original.getSkinningState().boneHash,
          "Restoring the original part must restore its pose history");

  state.drawCall.overrideReplacementGeometryData(skinnedMesh);
  require(state.submeshIsDeforming(2), "A genuinely skinned replacement must retain private pose geometry");
  require(state.drawCall.getSkinningState().boneHash == original.getSkinningState().boneHash,
          "A skinned replacement must retain the source pose");

  state.drawCall = dxvk::SceneDraw {};
  state.drawCall.overrideReplacementGeometryData(skinnedMesh);
  require(!state.submeshIsDeforming(2), "Skinning attributes without a live pose must not invent deformation");
  state.frontendResolved = false;
  require(state.submeshIsDeforming(0), "An API mesh must retain its explicit deformation contract");
}
}

int main() {
  try {
    testTopology();
    testParkedBones();
    testSkinnedWorldChange();
    testReplacementOwnership();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
