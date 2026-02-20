#pragma once
#include <glm/glm.hpp>

struct StandardPushConstants {
  alignas(16) glm::mat4 model;
  alignas(4) uint32_t layerMask;
  alignas(4) uint32_t cameraLayer;
  alignas(4) float highlightIntensity;
};