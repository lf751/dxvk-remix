#include "rtx_option.h"
#include "rtx_option_layer.h"
#include <remix/remix_c.h>

namespace dxvk::fork_hooks {
  remixapi_ErrorCode mutateTextureHashOption(
      const char* textureCategory,
      const char* textureHash,
      bool add) {
    if (!textureCategory || textureCategory[0] == '\0' || !textureHash) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    std::string category(textureCategory);
    constexpr char kSessionPrefix[] = "session:";
    const bool session = category.compare(0, sizeof(kSessionPrefix) - 1, kSessionPrefix) == 0;
    if (session) {
      category.erase(0, sizeof(kSessionPrefix) - 1);
    }
    RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(category);
    if (!option) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }
    if (option->getType() != OptionType::HashSet) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    XXH64_hash_t h = 0;
    try {
      h = std::stoull(textureHash, nullptr, 16);
    } catch (...) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    const RtxOptionLayer* layer = session ? RtxOptionLayer::getDerivedLayer() : RtxOptionLayer::getUserLayer();
    if (!layer) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    // Safe because getType() == HashSet implies T == fast_unordered_set.
    auto* hashSetOption = static_cast<RtxOption<fast_unordered_set>*>(option);
    if (add) {
      hashSetOption->addHash(h, layer);
    } else {
      hashSetOption->removeHash(h, layer);
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

}
