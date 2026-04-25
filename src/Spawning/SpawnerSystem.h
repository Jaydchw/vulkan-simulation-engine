#pragma once
#include <random>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "ECS/Registry.h"
#include "Network/NetworkPackets.h"
#include "Physics/PhysicsMaterialManager.h"
#include "Timeline/TimelineSystem.h"

class NetworkManager;

class SpawnerSystem final {
 public:
  void setRegistry(Registry* reg) { registry = reg; }
  void setNetworkManager(NetworkManager* nm) { networkManager = nm; }
  void setPhysicsMaterialManager(PhysicsMaterialManager* pmm) { physMatManager = pmm; }
  void setTimelineSystem(TimelineSystem* tls) { timelineSystem = tls; }

  // Advances all locally-owned spawner timers; creates entities when intervals fire.
  void update(float deltaTime);

  // Creates a remote-spawned entity in the local registry using the packet description.
  // Called by Application after polling NetworkManager::pollPendingSpawnedEntity().
  void applyRemoteSpawn(const SpawnEntityPacket& packet);

 private:
  Registry*               registry       = nullptr;
  NetworkManager*         networkManager = nullptr;
  PhysicsMaterialManager* physMatManager = nullptr;
  TimelineSystem*         timelineSystem = nullptr;
  std::mt19937    rng{std::random_device{}()};

  // Counter for SEQUENTIAL spawner ownership — cycles across active peers in order.
  int seqOwnerNext = 0;

  float computeInterval(const SpawnerComponent& spawner);
  SpawnTemplate* selectTemplate(SpawnerComponent& spawner);
  Entity doSpawn(Entity spawnerEntity, SpawnerComponent& spawner, const SpawnTemplate& tmpl);

  // Assign ownership for a freshly-spawned entity according to ownerMode.
  void assignSpawnedOwner(Entity entity, SpawnerComponent& spawner);

  glm::vec3 randomInSphere(float radius);
  glm::vec3 randomInBox(const glm::vec3& min, const glm::vec3& max);
  glm::vec3 randomConeDir(const glm::vec3& axis, float halfAngle);
};
