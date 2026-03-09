#include "PhysicsSystem.h"

#include <chrono>
#include <vector>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

PhysicsSystem::PhysicsSystem() {
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Created");
}

void PhysicsSystem::setRegistry(Registry* reg) {
  registry = reg;
}

void PhysicsSystem::update(float deltaTime) {
  if (!registry || deltaTime <= 0.0f) return;

  syncToLibrary();
  world.step(deltaTime);
  syncFromLibrary();
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

void PhysicsSystem::syncToLibrary() {
  world.clear();

  auto entities = registry->getEntities();
  for (Entity e : entities) {
    auto* phys = registry->getComponent<PhysicsComponent>(e);
    auto* collider = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    if (!collider || !transform) continue;

    auto* obj = &registry->getPhysicsObject(e);

    // Sync transform -> physics object
    obj->setPosition(transform->position);
    obj->setScale(transform->scale);

    // Sync collider
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
    }

    // Sync physics properties
    if (phys) {
      obj->setVelocity(phys->velocity);
      obj->setAcceleration(phys->acceleration);
      obj->setMass(phys->mass);
      obj->setRestitution(phys->restitution);
      obj->setDamping(phys->damping);
      obj->setUseGravity(phys->useGravity);
      obj->setStatic(false);
    } else {
      obj->setStatic(true);
    }

    world.addObject(obj);
  }
}

void PhysicsSystem::syncFromLibrary() {
  auto entities = registry->getEntities();
  for (Entity e : entities) {
    auto* collider = registry->getComponent<ColliderComponent>(e);
    auto* transform = registry->getComponent<TransformComponent>(e);
    if (!collider || !transform) continue;

    const auto& obj = registry->getPhysicsObject(e);

    transform->position = obj.getPosition();

    auto* phys = registry->getComponent<PhysicsComponent>(e);
    if (phys) {
      phys->velocity = obj.getVelocity();
    }
  }
}
