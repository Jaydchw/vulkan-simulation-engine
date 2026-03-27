#define NOMINMAX
#include "SpawnerSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#include "Network/NetworkManager.h"
#include "Util/Debug.h"

void SpawnerSystem::update(float deltaTime) {
  if (!registry) return;

  for (auto& [spawnerEntity, spawner] : registry->allSpawnersMut()) {
    if (!spawner.enabled) continue;
    if (networkManager && !networkManager->isLocallyOwned(spawnerEntity)) continue;

    if (spawner.currentInterval < 0.0f)
      spawner.currentInterval = computeInterval(spawner);

    spawner.timer += deltaTime;

    while (spawner.timer >= spawner.currentInterval) {
      spawner.timer -= spawner.currentInterval;

      if (spawner.maxSpawns >= 0 && spawner.spawnCount >= spawner.maxSpawns) {
        spawner.timer = 0.0f;
        break;
      }

      SpawnTemplate* tmpl = selectTemplate(spawner);
      if (!tmpl) break;

      Entity newEntity = doSpawn(spawnerEntity, spawner, *tmpl);
      spawner.spawnCount++;
      spawner.currentInterval = computeInterval(spawner);

      if (networkManager) {
        networkManager->assignObjectOwnership();
        networkManager->broadcastSpawnEntity(newEntity);
      }

      Debug::log(Debug::Category::OBJECTS, "SpawnerSystem: Spawned entity ", newEntity,
                 " from spawner ", spawnerEntity);
    }
  }
}

void SpawnerSystem::applyRemoteSpawn(const SpawnEntityPacket& pkt) {
  if (!registry) return;

  Entity entity = registry->createEntityWithId(static_cast<Entity>(pkt.entityId));

  registry->addComponent<NameComponent>(entity, {std::string(pkt.name)});

  TransformComponent transform;
  transform.position = {pkt.posX, pkt.posY, pkt.posZ};
  transform.rotation = glm::quat(pkt.rotW, pkt.rotX, pkt.rotY, pkt.rotZ);
  transform.scale    = {pkt.scaleX, pkt.scaleY, pkt.scaleZ};
  registry->addComponent<TransformComponent>(entity, transform);

  PhysicsComponent phys;
  phys.velocity        = {pkt.velX, pkt.velY, pkt.velZ};
  phys.angularVelocity = {pkt.angVelX, pkt.angVelY, pkt.angVelZ};
  phys.mass            = pkt.mass;
  phys.restitution     = pkt.restitution;
  phys.damping         = pkt.damping;
  phys.useGravity      = pkt.useGravity != 0;
  registry->addComponent<PhysicsComponent>(entity, phys);

  ColliderComponent collider;
  collider.type        = static_cast<ColliderType>(pkt.colliderType);
  collider.radius      = pkt.radius;
  collider.height      = pkt.height;
  collider.halfExtents = {pkt.halfExtX, pkt.halfExtY, pkt.halfExtZ};
  collider.normal      = {pkt.normalX, pkt.normalY, pkt.normalZ};
  collider.finite      = pkt.finite != 0;
  registry->addComponent<ColliderComponent>(entity, collider);

  if (pkt.hasRender && pkt.meshId != INVALID_MESH_ID && pkt.materialId != INVALID_MATERIAL_ID) {
    registry->addComponent<MeshComponent>(entity, {pkt.meshId});
    registry->addComponent<MaterialComponent>(entity, {pkt.materialId});
    registry->addComponent<RenderComponent>(entity, {});
  }

  if (networkManager) {
    networkManager->assignObjectOwnership();
  }

  Debug::log(Debug::Category::OBJECTS, "SpawnerSystem: Replicated remote entity ", entity);
}

// ── Private helpers ───────────────────────────────────────────────────────────

float SpawnerSystem::computeInterval(const SpawnerComponent& spawner) {
  float interval = spawner.spawnInterval;
  if (spawner.spawnIntervalRandomness > 0.0f) {
    std::uniform_real_distribution<float> dist(0.0f, spawner.spawnIntervalRandomness);
    interval *= (1.0f + dist(rng));
  }
  return std::max(interval, 0.0001f);
}

SpawnTemplate* SpawnerSystem::selectTemplate(SpawnerComponent& spawner) {
  if (spawner.templates.empty()) return nullptr;
  if (spawner.templates.size() == 1) return &spawner.templates[0];

  float totalWeight = 0.0f;
  for (const auto& t : spawner.templates) totalWeight += std::max(t.weight, 0.0f);
  if (totalWeight <= 0.0f) return &spawner.templates[0];

  std::uniform_real_distribution<float> dist(0.0f, totalWeight);
  float roll = dist(rng);
  for (auto& t : spawner.templates) {
    roll -= std::max(t.weight, 0.0f);
    if (roll <= 0.0f) return &t;
  }
  return &spawner.templates.back();
}

Entity SpawnerSystem::doSpawn(Entity spawnerEntity, SpawnerComponent& spawner,
                               const SpawnTemplate& tmpl) {
  const auto* spawnerTransform = registry->getComponent<TransformComponent>(spawnerEntity);

  // Position
  glm::vec3 pos = spawnerTransform ? spawnerTransform->position : glm::vec3(0.0f);
  pos += spawner.spawnOffset;
  if (spawner.positionRandomness > 0.0f)
    pos += randomInSphere(spawner.positionRandomness);

  // Launch direction with optional cone scatter
  glm::vec3 dir = glm::length(spawner.spawnDirection) > 1e-6f
                      ? glm::normalize(spawner.spawnDirection)
                      : glm::vec3(0.0f, 1.0f, 0.0f);
  if (spawner.directionRandomness > 0.0f)
    dir = randomConeDir(dir, spawner.directionRandomness);

  // Speed with symmetric random variation
  float speed = spawner.spawnSpeed;
  if (spawner.speedRandomness > 0.0f) {
    std::uniform_real_distribution<float> dist(-spawner.speedRandomness, spawner.speedRandomness);
    speed = std::max(speed * (1.0f + dist(rng)), 0.0f);
  }

  // Angular velocity with random sphere offset
  glm::vec3 angVel = spawner.angularVelocity + tmpl.physics.angularVelocity;
  if (spawner.angularVelocityRandomness > 0.0f)
    angVel += randomInSphere(spawner.angularVelocityRandomness);

  // Build components
  PhysicsComponent phys = tmpl.physics;
  phys.velocity         = tmpl.physics.velocity + dir * speed;
  phys.angularVelocity  = angVel;

  TransformComponent transform;
  transform.position = pos;
  transform.scale    = tmpl.scale;

  static int spawnSeq = 0;
  std::string entityName = tmpl.namePrefix + "_" + std::to_string(++spawnSeq);

  Entity entity = registry->createEntity();
  registry->addComponent<NameComponent>(entity, {entityName});
  registry->addComponent<TransformComponent>(entity, transform);
  registry->addComponent<PhysicsComponent>(entity, phys);
  registry->addComponent<ColliderComponent>(entity, tmpl.collider);

  if (tmpl.hasRender && tmpl.meshID != INVALID_MESH_ID && tmpl.materialID != INVALID_MATERIAL_ID) {
    registry->addComponent<MeshComponent>(entity, {tmpl.meshID});
    registry->addComponent<MaterialComponent>(entity, {tmpl.materialID});
    registry->addComponent<RenderComponent>(entity, {});
  }

  return entity;
}

glm::vec3 SpawnerSystem::randomInSphere(float radius) {
  std::normal_distribution<float>    nd(0.0f, 1.0f);
  std::uniform_real_distribution<float> ud(0.0f, 1.0f);
  glm::vec3 dir = glm::normalize(glm::vec3(nd(rng), nd(rng), nd(rng)));
  return dir * (std::cbrt(ud(rng)) * radius);
}

glm::vec3 SpawnerSystem::randomConeDir(const glm::vec3& axis, float halfAngle) {
  std::uniform_real_distribution<float> u01(0.0f, 1.0f);
  std::uniform_real_distribution<float> phiDist(0.0f, 2.0f * glm::pi<float>());

  float cosHalf  = std::cos(halfAngle);
  float cosTheta = 1.0f - u01(rng) * (1.0f - cosHalf);
  float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
  float phi      = phiDist(rng);

  // Build orthonormal basis around axis
  glm::vec3 perp     = (std::abs(axis.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  glm::vec3 tangent  = glm::normalize(glm::cross(axis, perp));
  glm::vec3 bitangent = glm::cross(axis, tangent);

  return glm::normalize(tangent * (sinTheta * std::cos(phi)) +
                         bitangent * (sinTheta * std::sin(phi)) +
                         axis * cosTheta);
}
