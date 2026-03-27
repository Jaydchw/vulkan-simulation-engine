#include "PhysicsSystem.h"

#include <chrono>
#include <cmath>
#include <vector>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Network/NetworkManager.h"
#include "Util/Debug.h"

PhysicsSystem::PhysicsSystem() {
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Created");
}

void PhysicsSystem::setRegistry(Registry* reg) {
  // Clear stale raw pointers into the old registry before it is destroyed.
  // syncToLibrary() also calls world.clear() at its start, but the old registry
  // may already be deallocated by then, leaving dangling pointers in the world.
  world.clear();
  registry = reg;
}

void PhysicsSystem::update(float deltaTime) {
  if (!registry || deltaTime <= 0.0f) return;

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
  return t;
}

void PhysicsSystem::resolveCrossPeerCollisions() {
  // Only runs in networked sessions with more than one peer.
  if (!registry || !networkManager) return;
  if (networkManager->getConnectedPeerCount() == 0) return;

  auto entities = registry->getEntities();

  for (Entity local : entities) {
    auto* localPhys      = registry->getComponent<PhysicsComponent>(local);
    auto* localCollider  = registry->getComponent<ColliderComponent>(local);
    auto* localTransform = registry->getComponent<TransformComponent>(local);
    if (!localPhys || !localCollider || !localTransform) continue;
    if (!networkManager->isLocallyOwned(local)) continue;
    if (localCollider->type != ColliderType::Sphere) continue;

    const float localRadius = localCollider->radius * localTransform->scale.x;
    glm::vec3   localPos    = localTransform->position;
    glm::vec3   localVel    = localPhys->velocity;
    const float localMass   = (localPhys->mass > 0.0f) ? localPhys->mass : 1.0f;

    for (Entity remote : entities) {
      if (remote == local) continue;
      auto* remotePhys      = registry->getComponent<PhysicsComponent>(remote);
      auto* remoteCollider  = registry->getComponent<ColliderComponent>(remote);
      auto* remoteTransform = registry->getComponent<TransformComponent>(remote);
      if (!remotePhys || !remoteCollider || !remoteTransform) continue;
      if (networkManager->isLocallyOwned(remote)) continue;
      if (remoteCollider->type != ColliderType::Sphere) continue;

      const float remoteRadius = remoteCollider->radius * remoteTransform->scale.x;
      const glm::vec3 remotePos = remoteTransform->position;
      const glm::vec3 remoteVel = remotePhys->velocity;
      const float     remoteMass = (remotePhys->mass > 0.0f) ? remotePhys->mass : 1.0f;

      const glm::vec3 diff    = localPos - remotePos;
      const float     distSq  = glm::dot(diff, diff);
      const float     minDist = localRadius + remoteRadius;

      if (distSq >= minDist * minDist || distSq < 1e-10f) continue;

      const float     dist   = std::sqrt(distSq);
      const glm::vec3 normal = diff / dist;

      // Relative velocity along the collision normal
      const float relVel = glm::dot(localVel - remoteVel, normal);
      if (relVel < 0.0f) {
        // Elastic impulse — each peer only applies its own half
        const float j = -(1.0f + localPhys->restitution) * relVel /
                        (1.0f / localMass + 1.0f / remoteMass);
        localVel += (j / localMass) * normal;
      }

      // Push local object out of overlap (50% — remote does the other 50%)
      localPos += normal * ((minDist - dist) * 0.5f);
    }

    localPhys->velocity        = localVel;
    localTransform->position   = localPos;
    auto* physObj = registry->getPhysicsObjectPtr(local);
    if (physObj) {
      physObj->setVelocity(localVel);
      physObj->setPosition(localPos);
    }
  }
}

void PhysicsSystem::syncToLibrary() {
  world.clear();

  auto entities = registry->getEntities();
  for (Entity e : entities) {
    auto* phys     = registry->getComponent<PhysicsComponent>(e);
    auto* collider = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    if (!collider || !transform) continue;

    // Dynamic (has PhysicsComponent) but not locally owned → skip simulation.
    // Static objects (no PhysicsComponent) are always added so that locally-
    // owned objects can collide with them.
    if (phys && networkManager && !networkManager->isLocallyOwned(e))
      continue;

    auto* obj = &registry->getPhysicsObject(e);

    obj->setPosition(transform->position);
    obj->setScale(transform->scale);

    switch (collider->type) {
      case ColliderType::Sphere:
        obj->setCollider(jphys::Collider::createSphere(collider->radius));
        break;
      case ColliderType::AABB:
        obj->setCollider(jphys::Collider::createAABB(collider->halfExtents));
        break;
      case ColliderType::Plane:
        if (collider->finite)
          obj->setCollider(jphys::Collider::createFinitePlane(
              collider->normal, collider->halfExtents));
        else
          obj->setCollider(jphys::Collider::createPlane(collider->normal));
        break;
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
      obj->setDamping(phys->damping);
      obj->setUseGravity(phys->useGravity);
      obj->setAngularVelocity(phys->angularVelocity);
      obj->setOrientation(transform->rotation);
      obj->setStatic(false);

      if (phys->constantTorque != glm::vec3(0.0f))
        obj->addTorque(phys->constantTorque);
    } else {
      obj->setStatic(true);
    }

    world.addObject(obj);
  }
}

void PhysicsSystem::syncFromLibrary() {
  auto entities = registry->getEntities();
  for (Entity e : entities) {
    auto* collider  = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    auto* phys      = registry->getComponent<PhysicsComponent>(e);
    if (!collider || !transform) continue;

    // Only write back locally-owned dynamic objects; remote ones are updated
    // by NetworkManager::applyRemoteStates() instead.
    if (phys && networkManager && !networkManager->isLocallyOwned(e))
      continue;

    const auto& obj = registry->getPhysicsObject(e);

    transform->position = obj.getPosition();
    transform->rotation = obj.getOrientation();

    if (phys) {
      phys->velocity        = obj.getVelocity();
      phys->angularVelocity = obj.getAngularVelocity();
    }
  }
}
