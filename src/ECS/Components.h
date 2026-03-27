#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

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

struct PhysicsComponent {
  glm::vec3 velocity = glm::vec3(0.0f);
  glm::vec3 acceleration = glm::vec3(0.0f);
  glm::vec3 angularVelocity = glm::vec3(0.0f);
  glm::vec3 constantTorque = glm::vec3(0.0f);
  float mass = 1.0f;
  float restitution = 0.5f;
  float damping = 0.99f;
  bool useGravity = true;
};

enum class ColliderType { Sphere, AABB, Plane, Cylinder, Capsule, Cone };

struct ColliderComponent {
  ColliderType type = ColliderType::Sphere;
  float radius = 1.0f;
  float height = 1.0f;
  glm::vec3 halfExtents = glm::vec3(0.5f);
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  bool finite = false;
};

// Defines a single spawnable object prototype with its own weight for random selection.
struct SpawnTemplate {
  float weight = 1.0f;

  PhysicsComponent physics;
  ColliderComponent collider;

  bool hasRender = true;
  MeshID meshID = INVALID_MESH_ID;
  MaterialID materialID = INVALID_MATERIAL_ID;
  glm::vec3 scale = glm::vec3(1.0f);
  std::string namePrefix = "Spawned";
};

enum class CameraType { Perspective, Orthographic };

struct CameraComponent {
  CameraType type = CameraType::Perspective;
  float fov = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 50000.0f;
  float orthographicSize = 50.0f;
};

struct SpawnerComponent {
  std::vector<SpawnTemplate> templates;

  // How often to spawn (seconds). Actual interval = spawnInterval * (1 + U[0, intervalRandomness]).
  float spawnInterval = 1.0f;
  float spawnIntervalRandomness = 0.0f;

  // Launch direction and speed applied on top of the template's base velocity.
  glm::vec3 spawnDirection = glm::vec3(0.0f, 1.0f, 0.0f);
  float directionRandomness = 0.0f;  // half-angle cone in radians
  float spawnSpeed = 0.0f;
  float speedRandomness = 0.0f;      // speed *= (1 + U[-r, r])

  // Spawned entity position = spawner position + spawnOffset + random sphere sample.
  glm::vec3 spawnOffset = glm::vec3(0.0f);
  float positionRandomness = 0.0f;   // sphere radius for random offset

  // Angular velocity applied to spawned entities (template angularVelocity is additive).
  glm::vec3 angularVelocity = glm::vec3(0.0f);
  float angularVelocityRandomness = 0.0f;

  int maxSpawns = -1;  // -1 = unlimited
  bool enabled = true;

  // Runtime state — not intended for serialization.
  float timer = 0.0f;
  float currentInterval = -1.0f;  // recomputed after each spawn; -1 triggers first computation
  int spawnCount = 0;
};
