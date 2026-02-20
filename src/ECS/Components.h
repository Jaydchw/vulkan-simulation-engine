#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

#include "Resources/Material.h"

using MeshID = uint32_t;
constexpr MeshID INVALID_MESH_ID = 0;

struct NameComponent {
  std::string name = "Unnamed Entity";
};

struct TransformComponent {
  glm::vec3 position = glm::vec3(0.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 scale = glm::vec3(1.0f);

  glm::mat4 getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = model * glm::mat4_cast(rotation);
    model = glm::scale(model, scale);
    return model;
  }
};

struct MeshComponent {
  MeshID meshID = INVALID_MESH_ID;
};

struct MaterialComponent {
  MaterialID materialID = INVALID_MATERIAL_ID;
};

struct RenderComponent {
  bool visible = true;
  uint32_t layerMask = 0xFFFFFFFF;
};

enum class LightType { Point, Sun };

struct LightComponent {
  LightType type = LightType::Point;
  glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
  glm::vec3 color = glm::vec3(1.0f);
  float intensity = 1.0f;
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;
  float cutOff = 0.0f;
  float outerCutOff = 0.0f;
  bool castsShadows = true;
  uint32_t shadowMapIndex = UINT32_MAX;
};
