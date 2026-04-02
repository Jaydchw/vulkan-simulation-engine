#pragma once

#include <glm/glm.hpp>

enum class WindAffectsMode {
  ClothOnly,
  AllObjects
};

struct EnvironmentSettings {
  glm::vec3 wind = glm::vec3(0.0f);
  float windDrag = 0.2f;
  WindAffectsMode windAffects = WindAffectsMode::ClothOnly;
};
