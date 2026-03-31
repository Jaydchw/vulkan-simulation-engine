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

  for (auto&& [spawnerEntity, spawner] : registry->allSpawnersMut()) {
    if (!spawner.enabled) continue;
    if (networkManager && !networkManager->isLocallyOwned(spawnerEntity)) continue;

    spawner.timer += deltaTime;

    // Item 4: SingleBurstSpawn — fire all objects at once when start_time elapses.
    if (spawner.burstMode) {
      if (spawner.timer >= 0.0f) {
        int remaining = (spawner.maxSpawns < 0) ? 1 : (spawner.maxSpawns - spawner.spawnCount);
        for (int b = 0; b < remaining; ++b) {
          SpawnTemplate* tmpl = selectTemplate(spawner);
          if (!tmpl) break;
          Entity newEntity = doSpawn(spawnerEntity, spawner, *tmpl);
          spawner.spawnCount++;
          assignSpawnedOwner(newEntity, spawner);
          if (networkManager) networkManager->broadcastSpawnEntity(newEntity);
          Debug::log(Debug::Category::OBJECTS, "SpawnerSystem: Burst-spawned entity ", newEntity,
                     " from spawner ", spawnerEntity);
        }
        spawner.enabled = false;  // burst is done
      }
      continue;
    }

    if (spawner.currentInterval < 0.0f)
      spawner.currentInterval = computeInterval(spawner);

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

      assignSpawnedOwner(newEntity, spawner);
      if (networkManager) networkManager->broadcastSpawnEntity(newEntity);

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

  SimulatedComponent phys;
  phys.velocity        = {pkt.velX, pkt.velY, pkt.velZ};
  phys.angularVelocity = {pkt.angVelX, pkt.angVelY, pkt.angVelZ};
  phys.mass            = pkt.mass;
  phys.restitution     = pkt.restitution;
  phys.damping         = pkt.damping;
  phys.useGravity      = pkt.useGravity != 0;
  registry->addComponent<SimulatedComponent>(entity, phys);

  ColliderComponent collider;
  collider.type        = static_cast<ColliderType>(pkt.colliderType);
  collider.radius      = pkt.radius;
  collider.height      = pkt.height;
  collider.halfExtents = {pkt.halfExtX, pkt.halfExtY, pkt.halfExtZ};
  collider.normal      = {pkt.normalX, pkt.normalY, pkt.normalZ};
  collider.finite      = pkt.finite != 0;
  registry->addComponent<ColliderComponent>(entity, collider);

  if (pkt.hasRender && pkt.meshId != INVALID_MESH_ID && pkt.materialId != INVALID_RENDER_MATERIAL_ID) {
    registry->addComponent<MeshComponent>(entity, {pkt.meshId});
    registry->addComponent<RenderMaterialComponent>(entity, {pkt.materialId});
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

  // Position — Item 6: use per-axis box sampling for RandomBox spawners.
  glm::vec3 pos;
  if (spawner.useBoxSpawn) {
    pos = randomInBox(spawner.spawnBoxMin, spawner.spawnBoxMax);
  } else {
    pos = spawnerTransform ? spawnerTransform->position : glm::vec3(0.0f);
    pos += spawner.spawnOffset;
    if (spawner.positionRandomness > 0.0f)
      pos += randomInSphere(spawner.positionRandomness);
  }

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
  glm::vec3 angVel = spawner.angularVelocity + tmpl.simulated.angularVelocity;
  if (spawner.angularVelocityRandomness > 0.0f)
    angVel += randomInSphere(spawner.angularVelocityRandomness);

  // Build components
  SimulatedComponent phys = tmpl.simulated;
  phys.velocity         = tmpl.simulated.velocity + dir * speed;
  phys.angularVelocity  = angVel;

  TransformComponent transform;
  transform.position = pos;
  transform.scale    = tmpl.scale;

  static int spawnSeq = 0;
  std::string entityName = tmpl.namePrefix + "_" + std::to_string(++spawnSeq);

  Entity entity = registry->createEntity();
  registry->addComponent<NameComponent>(entity, {entityName});
  registry->addComponent<TransformComponent>(entity, transform);
  registry->addComponent<SimulatedComponent>(entity, phys);
  registry->addComponent<ColliderComponent>(entity, tmpl.collider);

  if (tmpl.hasRender && tmpl.meshID != INVALID_MESH_ID && tmpl.renderMaterialID != INVALID_RENDER_MATERIAL_ID) {
    registry->addComponent<MeshComponent>(entity, {tmpl.meshID});
    registry->addComponent<RenderMaterialComponent>(entity, {tmpl.renderMaterialID});
    registry->addComponent<RenderComponent>(entity, {});
  }

  if (tmpl.physicsMaterialID != INVALID_PHYSICS_MATERIAL_ID) {
    registry->addComponent<PhysicsMaterialComponent>(entity, {tmpl.physicsMaterialID});
  }

  return entity;
}

glm::vec3 SpawnerSystem::randomInSphere(float radius) {
  std::normal_distribution<float>    nd(0.0f, 1.0f);
  std::uniform_real_distribution<float> ud(0.0f, 1.0f);
  glm::vec3 dir = glm::normalize(glm::vec3(nd(rng), nd(rng), nd(rng)));
  return dir * (std::cbrt(ud(rng)) * radius);
}

// Item 6: per-axis uniform sampling within an axis-aligned box.
glm::vec3 SpawnerSystem::randomInBox(const glm::vec3& min, const glm::vec3& max) {
  std::uniform_real_distribution<float> ux(min.x, max.x);
  std::uniform_real_distribution<float> uy(min.y, max.y);
  std::uniform_real_distribution<float> uz(min.z, max.z);
  return { ux(rng), uy(rng), uz(rng) };
}

// Item 5: assign ownership of a freshly-spawned entity per the spawner's ownerMode.
void SpawnerSystem::assignSpawnedOwner(Entity entity, SpawnerComponent& spawner) {
  if (!networkManager) return;

  uint8_t peerID;
  if (spawner.ownerMode == 0 || spawner.ownerMode == 5) {
    // Auto (0) and SEQUENTIAL (5): round-robin across currently active peers.
    // ownerMode 0 previously called assignObjectOwnership() on every spawn,
    // causing O(n²) behaviour (full O(n) redistribution per spawn = n²total).
    // Full redistribution is still triggered by Application on peer connect/drop,
    // which is the only time global rebalancing is actually needed.
    auto activePeers = networkManager->getActivePeerIDs();
    if (activePeers.empty()) {
      peerID = networkManager->getLocalPeerID();
    } else {
      peerID = activePeers[static_cast<size_t>(seqOwnerNext) % activePeers.size()];
      ++seqOwnerNext;
    }
  } else {
    // Fixed peer (ownerMode 1-4).
    peerID = spawner.ownerMode;
  }

  networkManager->setEntityOwner(entity, peerID);
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
