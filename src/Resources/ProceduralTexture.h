#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "Resources/TextureManager.h"

class ProceduralTexture final {
 public:
  static TextureID solid(TextureManager* tm, const glm::vec3& color,
                         uint32_t size = 4) {
    std::vector<unsigned char> pixels(size * size * 4);
    const auto r = static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    const auto g = static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    const auto b = static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    for (uint32_t i = 0; i < size * size; i++) {
      pixels[i * 4 + 0] = r;
      pixels[i * 4 + 1] = g;
      pixels[i * 4 + 2] = b;
      pixels[i * 4 + 3] = 255;
    }
    return tm->createFromPixels(pixels.data(), size, size);
  }

  static TextureID checker(TextureManager* tm, const glm::vec3& colorA,
                           const glm::vec3& colorB, uint32_t size = 256,
                           uint32_t divisions = 8) {
    std::vector<unsigned char> pixels(size * size * 4);
    const uint32_t cellSize = size / divisions;
    for (uint32_t y = 0; y < size; y++) {
      for (uint32_t x = 0; x < size; x++) {
        const bool isA = ((x / cellSize) + (y / cellSize)) % 2 == 0;
        const glm::vec3& c = isA ? colorA : colorB;
        const uint32_t idx = (y * size + x) * 4;
        pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 3] = 255;
      }
    }
    return tm->createFromPixels(pixels.data(), size, size);
  }

  static TextureID linearGradient(TextureManager* tm, const glm::vec3& colorA,
                                  const glm::vec3& colorB, uint32_t size = 256,
                                  bool vertical = false) {
    std::vector<unsigned char> pixels(size * size * 4);
    for (uint32_t y = 0; y < size; y++) {
      for (uint32_t x = 0; x < size; x++) {
        const float t = vertical ? static_cast<float>(y) / static_cast<float>(size - 1)
                                 : static_cast<float>(x) / static_cast<float>(size - 1);
        const glm::vec3 c = glm::mix(colorA, colorB, t);
        const uint32_t idx = (y * size + x) * 4;
        pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 3] = 255;
      }
    }
    return tm->createFromPixels(pixels.data(), size, size);
  }

  static TextureID radialGradient(TextureManager* tm, const glm::vec3& center,
                                  const glm::vec3& edge,
                                  uint32_t size = 256) {
    std::vector<unsigned char> pixels(size * size * 4);
    const float half = static_cast<float>(size) * 0.5f;
    for (uint32_t y = 0; y < size; y++) {
      for (uint32_t x = 0; x < size; x++) {
        const float dx = (static_cast<float>(x) - half) / half;
        const float dy = (static_cast<float>(y) - half) / half;
        const float t = glm::clamp(std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
        const glm::vec3 c = glm::mix(center, edge, t);
        const uint32_t idx = (y * size + x) * 4;
        pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 3] = 255;
      }
    }
    return tm->createFromPixels(pixels.data(), size, size);
  }

  static TextureID stripe(TextureManager* tm, const glm::vec3& colorA,
                          const glm::vec3& colorB, uint32_t size = 256,
                          uint32_t stripeCount = 8, bool vertical = true) {
    std::vector<unsigned char> pixels(size * size * 4);
    const uint32_t stripeWidth = size / (stripeCount * 2);
    for (uint32_t y = 0; y < size; y++) {
      for (uint32_t x = 0; x < size; x++) {
        const uint32_t coord = vertical ? x : y;
        const bool isA = (coord / stripeWidth) % 2 == 0;
        const glm::vec3& c = isA ? colorA : colorB;
        const uint32_t idx = (y * size + x) * 4;
        pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        pixels[idx + 3] = 255;
      }
    }
    return tm->createFromPixels(pixels.data(), size, size);
  }
};
