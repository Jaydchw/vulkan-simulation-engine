#pragma once
#include <vulkan/vulkan.h>

#include <string>

using TextureID = uint32_t;
constexpr TextureID INVALID_TEXTURE_ID = 0;

enum class TextureType { sRGB, Linear };

enum class TextureFilter { Nearest, Linear };

enum class TextureWrap { Repeat, ClampToEdge, MirroredRepeat };

struct TextureCreateInfo {
  std::string filepath;
  TextureType type = TextureType::sRGB;
  TextureFilter filter = TextureFilter::Linear;
  TextureWrap wrap = TextureWrap::Repeat;
  bool generateMipmaps = true;
};