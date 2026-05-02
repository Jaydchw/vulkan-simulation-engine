#include "PhysicsSystem.h"

#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <EnvironmentForces.h>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Network/NetworkManager.h"
#include "Util/Debug.h"

namespace {

struct CellKey {
  int x, y, z;
  bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellKeyHash {
  size_t operator()(const CellKey& k) const {
    size_t h = 0;
    h ^= std::hash<int>{}(k.x) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
  }
};

} // namespace

PhysicsSystem::PhysicsSystem() {
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Created");
  Debug::logVerbose(Debug::Category::PHYSICS, "PhysicsSystem: World instance initialized");
}

void PhysicsSystem::setRegistry(Registry* reg) {
  world.clear();
  trackedEntities.clear();
  staticCache.clear();
  dirtyDynamic.clear();
  registry = reg;
  if (registry) {
    registry->allPhysicsObjectsMut().reserve(16384);
  }
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Registry set, world cleared");
}

void PhysicsSystem::update(float deltaTime) {
  if (!registry || deltaTime <= 0.0f) return;
  Debug::logTrace(Debug::Category::PHYSICS, "PhysicsSystem: update dt=", deltaTime);
  syncToLibrary();
  world.step(deltaTime);
  syncFromLibrary();
  resolveCrossPeerCollisions();
}

PhysicsStepTimings PhysicsSystem::timedUpdate(float deltaTime) {
  using clock = std::chrono::high_resolution_clock;
  PhysicsStepTimings t;
  if (!registry || deltaTime <= 0.0f) return t;

  auto t0 = clock::now();
  syncToLibrary();
  auto t1 = clock::now();
  world.step(deltaTime);
  auto t2 = clock::now();
  syncFromLibrary();
  resolveCrossPeerCollisions();
  auto t3 = clock::now();

  auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  t.syncToMs    = ms(t0, t1);
  t.physStepMs  = ms(t1, t2);
  t.syncFromMs  = ms(t2, t3);
  t.collisionStats = world.getLastCollisionStats();

  Debug::logVerbose(Debug::Category::PHYSICS,
      "PhysicsSystem: timedUpdate syncTo=", t.syncToMs,
      "ms step=", t.physStepMs,
      "ms syncFrom=", t.syncFromMs, "ms");
  Debug::logTrace(Debug::Category::PHYSICS,
      "PhysicsSystem: timedUpdate total=", t.syncToMs + t.physStepMs + t.syncFromMs,
      "ms  collisionPairs=", t.collisionStats.size());

  return t;
}

void PhysicsSystem::resolveCrossPeerCollisions() {
  if (!registry || !networkManager) return;
  if (networkManager->getConnectedPeerCount() == 0) return;
  Debug::logTrace(Debug::Category::PHYSICS_COLLISION,
      "PhysicsSystem: resolveCrossPeerCollisions start, peers=",
      networkManager->getConnectedPeerCount());

  auto boundingRadius = [](const ColliderComponent& c, const glm::vec3& scale) -> float {
    switch (c.type) {
      case ColliderType::Sphere:
        return c.radius * scale.x;
      case ColliderType::AABB:
        return glm::length(c.halfExtents * scale);
      case ColliderType::Cylinder:
      case ColliderType::Capsule:
      case ColliderType::Cone:
        return (std::max)(c.radius * (std::max)(scale.x, scale.z),
                          (c.height * 0.5f) * scale.y);
      case ColliderType::Plane:
        return 0.0f;
    }
    return 0.0f;
  };

  auto entities = registry->getEntities();

  struct RemoteBody {
    Entity    entity;
    glm::vec3 pos;
    glm::vec3 vel;
    float     radius;
    float     mass;
  };

  float maxRemoteRadius = 0.0f;
  std::vector<RemoteBody> remoteBodies;
  remoteBodies.reserve(entities.size() / 2);

  for (Entity e : entities) {
    if (networkManager->isLocallyOwned(e)) continue;
    auto* phys      = registry->getComponent<SimulatedComponent>(e);
    auto* collider  = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    if (!phys || !collider || !transform) continue;

    const float r = boundingRadius(*collider, transform->scale);
    if (r <= 0.0f) continue;

    maxRemoteRadius = (std::max)(maxRemoteRadius, r);
    remoteBodies.push_back({e, transform->position, phys->velocity, r,
                            (phys->mass > 0.0f) ? phys->mass : 1.0f});
  }

  if (remoteBodies.empty()) return;
  Debug::logTrace(Debug::Category::PHYSICS_COLLISION,
      "PhysicsSystem: remoteBodies=", remoteBodies.size(),
      " maxRemoteRadius=", maxRemoteRadius);

  const float cellSize = (std::max)(maxRemoteRadius * 2.0f, 1.0f);
  const float invCell  = 1.0f / cellSize;

  std::unordered_map<CellKey, std::vector<int>, CellKeyHash> grid;
  grid.reserve(remoteBodies.size() * 2);

  for (int i = 0; i < static_cast<int>(remoteBodies.size()); ++i) {
    const glm::vec3& p = remoteBodies[i].pos;
    CellKey k{static_cast<int>(std::floor(p.x * invCell)),
              static_cast<int>(std::floor(p.y * invCell)),
              static_cast<int>(std::floor(p.z * invCell))};
    grid[k].push_back(i);
  }

  for (Entity local : entities) {
    auto* localPhys      = registry->getComponent<SimulatedComponent>(local);
    auto* localCollider  = registry->getComponent<ColliderComponent>(local);
    auto* localTransform = registry->getComponent<TransformComponent>(local);
    if (!localPhys || !localCollider || !localTransform) continue;
    if (!networkManager->isLocallyOwned(local)) continue;

    const float localRadius = boundingRadius(*localCollider, localTransform->scale);
    if (localRadius <= 0.0f) continue;
    glm::vec3   localPos    = localTransform->position;
    glm::vec3   localVel    = localPhys->velocity;
    const float localMass   = (localPhys->mass > 0.0f) ? localPhys->mass : 1.0f;

    const float queryR = localRadius + maxRemoteRadius;
    const int x0 = static_cast<int>(std::floor((localPos.x - queryR) * invCell));
    const int x1 = static_cast<int>(std::floor((localPos.x + queryR) * invCell));
    const int y0 = static_cast<int>(std::floor((localPos.y - queryR) * invCell));
    const int y1 = static_cast<int>(std::floor((localPos.y + queryR) * invCell));
    const int z0 = static_cast<int>(std::floor((localPos.z - queryR) * invCell));
    const int z1 = static_cast<int>(std::floor((localPos.z + queryR) * invCell));

    for (int cx = x0; cx <= x1; ++cx) {
      for (int cy = y0; cy <= y1; ++cy) {
        for (int cz = z0; cz <= z1; ++cz) {
          auto it = grid.find({cx, cy, cz});
          if (it == grid.end()) continue;

          for (int ri : it->second) {
            const RemoteBody& rs = remoteBodies[ri];
            const glm::vec3 diff   = localPos - rs.pos;
            const float     distSq = glm::dot(diff, diff);
            const float     minDist = localRadius + rs.radius;
            if (distSq >= minDist * minDist || distSq < 1e-10f) continue;

            const float     dist   = std::sqrt(distSq);
            const glm::vec3 normal = diff / dist;

            const float relVel = glm::dot(localVel - rs.vel, normal);
            if (relVel < 0.0f) {
              const float j = -(1.0f + localPhys->restitution) * relVel /
                              (1.0f / localMass + 1.0f / rs.mass);
              localVel += (j / localMass) * normal;
            }

            localPos += normal * ((minDist - dist) * 0.5f);
          }
        }
      }
    }

    localPhys->velocity      = localVel;
    localTransform->position = localPos;
    auto* physObj = registry->getPhysicsObjectPtr(local);
    if (physObj) {
      physObj->setVelocity(localVel);
      physObj->setPosition(localPos);
    }
  }
}

void PhysicsSystem::rebuildWorldObjects() {
  world.clear();
  auto& store = registry->allPhysicsObjectsMut();
  const auto& ents  = store.entityList();
  auto&       comps = store.componentListMut();
  for (size_t i = 0; i < ents.size(); ++i) {
    if (trackedEntities.count(ents[i])) {
      world.addObject(&comps[i]);
    }
  }
}

void PhysicsSystem::configurePhysicsObject(jphys::PhysicsObject*      obj,
                                            const SimulatedComponent*  phys,
                                            const ColliderComponent*   collider,
                                            const TransformComponent*  transform) {
  obj->setPosition(transform->position);

  const bool isStatic = !phys;

  switch (collider->type) {
    case ColliderType::Sphere:
      obj->setCollider(jphys::Collider::createSphere(collider->radius));
      break;
    case ColliderType::AABB: {
      glm::vec3 he = collider->halfExtents;
      if (isStatic) {
        glm::mat3 R = glm::mat3_cast(transform->rotation);
        he = glm::vec3(
          std::abs(R[0][0])*he.x + std::abs(R[1][0])*he.y + std::abs(R[2][0])*he.z,
          std::abs(R[0][1])*he.x + std::abs(R[1][1])*he.y + std::abs(R[2][1])*he.z,
          std::abs(R[0][2])*he.x + std::abs(R[1][2])*he.y + std::abs(R[2][2])*he.z
        );
      }
      obj->setCollider(jphys::Collider::createAABB(he));
      break;
    }
    case ColliderType::Plane: {
      glm::vec3 worldNormal = transform->rotation * collider->normal;
      if (collider->finite)
        obj->setCollider(jphys::Collider::createFinitePlane(
            worldNormal, collider->halfExtents));
      else
        obj->setCollider(jphys::Collider::createPlane(worldNormal));
      break;
    }
    case ColliderType::Cylinder:
      obj->setCollider(
          jphys::Collider::createCylinder(collider->radius, collider->height));
      break;
    case ColliderType::Capsule:
      obj->setCollider(
          jphys::Collider::createCapsule(collider->radius, collider->height));
      break;
    case ColliderType::Cone:
      obj->setCollider(
          jphys::Collider::createCone(collider->radius, collider->height));
      break;
  }

  if (phys) {
    obj->setVelocity(phys->velocity);
    obj->setAcceleration(phys->acceleration);
    obj->setMass(phys->mass);
    obj->setRestitution(phys->restitution);
    obj->setFriction(phys->friction);
    obj->setDamping(phys->damping);
    obj->setUseGravity(phys->useGravity);
    obj->setAngularVelocity(phys->angularVelocity);
    obj->setOrientation(transform->rotation);
    obj->setStatic(false);

    if (phys->constantTorque != glm::vec3(0.0f))
      obj->addTorque(phys->constantTorque);
  } else {
    obj->setOrientation(transform->rotation);
    obj->setStatic(true);
  }
}

void PhysicsSystem::syncToLibrary() {
  const auto& entities = registry->getEntities();

  Debug::logTrace(Debug::Category::PHYSICS_SYNC,
      "PhysicsSystem::syncToLibrary: processing ", entities.size(), " entities");

  std::unordered_set<Entity> currentValid;
  currentValid.reserve(entities.size());
  for (Entity e : entities) {
    if (!registry->getComponent<ColliderComponent>(e)) continue;
    if (!registry->getComponent<TransformComponent>(e)) continue;
    auto* phys = registry->getComponent<SimulatedComponent>(e);
    if (phys && networkManager && !networkManager->isLocallyOwned(e)) continue;
    currentValid.insert(e);
  }

  std::vector<Entity> toRemove;
  for (Entity e : trackedEntities) {
    if (!currentValid.count(e)) toRemove.push_back(e);
  }
  bool anyRemoved = false;
  for (Entity e : toRemove) {
    auto* obj = registry->getPhysicsObjectPtr(e);
    if (obj) world.removeObject(obj);
    trackedEntities.erase(e);
    staticCache.erase(e);
    anyRemoved = true;
  }
  if (anyRemoved) rebuildWorldObjects();

  int addedCount = 0;
  for (Entity e : currentValid) {
    auto* collider  = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    auto* phys      = registry->getComponent<SimulatedComponent>(e);

    const bool isNew = !trackedEntities.count(e);

    if (isNew) {
      registry->addPhysicsObject(e);               // insert into dense store
      auto* obj = registry->getPhysicsObjectPtr(e);
      configurePhysicsObject(obj, phys, collider, transform);

      trackedEntities.insert(e);
      rebuildWorldObjects();

      if (!phys) {
        staticCache[e] = {transform->position, transform->rotation, collider->type};
      }
      ++addedCount;

    } else if (!phys) {
      auto& cached = staticCache[e];
      if (transform->position  != cached.position     ||
          transform->rotation  != cached.rotation     ||
          collider->type       != cached.colliderType) {
        auto* obj = registry->getPhysicsObjectPtr(e);
        configurePhysicsObject(obj, nullptr, collider, transform);
        cached = {transform->position, transform->rotation, collider->type};
      }

    } else {
      auto* obj = registry->getPhysicsObjectPtr(e);

      if (dirtyDynamic.count(e)) {
        obj->setPosition(transform->position);
        obj->setVelocity(phys->velocity);
        obj->setAngularVelocity(phys->angularVelocity);
        obj->setOrientation(transform->rotation);
        obj->wakeUp();
      }

      glm::vec3 acceleration = phys->acceleration;
      if (environmentSettings &&
          environmentSettings->windAffects == WindAffectsMode::AllObjects) {
        acceleration += jphys::computeWindAcceleration(
            environmentSettings->wind, phys->velocity, environmentSettings->windDrag);
      }
      obj->setAcceleration(acceleration);

      if (phys->constantTorque != glm::vec3(0.0f))
        obj->addTorque(phys->constantTorque);
    }
  }

  dirtyDynamic.clear();

  Debug::logVerbose(Debug::Category::PHYSICS_SYNC,
      "PhysicsSystem::syncToLibrary: tracked=", trackedEntities.size(),
      " added=", addedCount);
}

void PhysicsSystem::syncFromLibrary() {
  const auto& entities = registry->getEntities();
  std::vector<Entity> toDestroy;
  toDestroy.reserve(32);

  Debug::logTrace(Debug::Category::PHYSICS_SYNC,
      "PhysicsSystem::syncFromLibrary: entities=", entities.size());
  int writtenCount = 0;

  for (Entity e : entities) {
    auto* collider  = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    auto* phys      = registry->getComponent<SimulatedComponent>(e);
    if (!collider || !transform) continue;

    // Only write back locally-owned dynamic objects; remote ones are updated
    // by NetworkManager::applyRemoteStates() instead.
    if (!phys) continue;
    if (networkManager && !networkManager->isLocallyOwned(e)) continue;

    const auto* obj = registry->getPhysicsObjectPtr(e);
    if (!obj) continue;

    transform->position = obj->getPosition();
    transform->rotation = obj->getOrientation();
    phys->velocity        = obj->getVelocity();
    phys->angularVelocity = obj->getAngularVelocity();

    if (killboxEnabled && transform->position.y < killboxY)
      toDestroy.push_back(e);

    ++writtenCount;
    Debug::logTrace(Debug::Category::PHYSICS_SYNC,
        "PhysicsSystem::syncFromLibrary: entity ", e,
        " pos=(", transform->position.x, ",", transform->position.y, ",", transform->position.z, ")",
        " vel=(", phys->velocity.x, ",", phys->velocity.y, ",", phys->velocity.z, ")");
  }

  Debug::logVerbose(Debug::Category::PHYSICS_SYNC,
      "PhysicsSystem::syncFromLibrary: wrote back ", writtenCount, " dynamic objects");

  for (Entity e : toDestroy) {
    auto* obj = registry->getPhysicsObjectPtr(e);
    if (obj) world.removeObject(obj);   // remove before the data is moved
    trackedEntities.erase(e);
    staticCache.erase(e);
    registry->destroyEntity(e);         // swap-remove happens here
  }
  if (!toDestroy.empty()) rebuildWorldObjects();
}
