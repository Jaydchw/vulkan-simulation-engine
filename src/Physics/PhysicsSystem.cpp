#include "PhysicsSystem.h"

#include <cmath>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

PhysicsSystem::PhysicsSystem() {
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Created");
}

void PhysicsSystem::setRegistry(Registry* reg) { registry = reg; }

void PhysicsSystem::update(float deltaTime) {
  if (!registry || deltaTime <= 0.0f) return;
  integrate(deltaTime);
  resolveCollisions();
}

void PhysicsSystem::integrate(float deltaTime) {
  for (auto& [entity, phys] : registry->allPhysicsMut()) {
    auto* transform = registry->getComponent<TransformComponent>(entity);
    if (!transform) continue;

    if (phys.useGravity) {
      phys.velocity += gravity * deltaTime;
    }

    phys.velocity += phys.acceleration * deltaTime;
    phys.velocity *= std::pow(phys.damping, deltaTime);
    transform->position += phys.velocity * deltaTime;
  }
}

void PhysicsSystem::resolveCollisions() {
  auto entities = registry->getEntities();

  for (size_t i = 0; i < entities.size(); ++i) {
    Entity entityA = entities[i];
    if (!registry->hasComponent<PhysicsComponent>(entityA)) continue;
    if (!registry->hasComponent<ColliderComponent>(entityA)) continue;

    auto* transformA = registry->getComponent<TransformComponent>(entityA);
    auto* physA = registry->getComponent<PhysicsComponent>(entityA);
    const auto* colliderA = registry->getComponent<ColliderComponent>(entityA);
    if (!transformA || !physA || !colliderA) continue;

    for (size_t j = 0; j < entities.size(); ++j) {
      if (i == j) continue;
      Entity entityB = entities[j];
      if (!registry->hasComponent<ColliderComponent>(entityB)) continue;

      auto* transformB = registry->getComponent<TransformComponent>(entityB);
      const auto* colliderB =
          registry->getComponent<ColliderComponent>(entityB);
      if (!transformB || !colliderB) continue;

      // Sphere vs Plane
      if (colliderA->type == ColliderType::Sphere &&
          colliderB->type == ColliderType::Plane) {
        glm::vec3 normal = colliderB->normal;
        float dist =
            glm::dot(transformA->position - transformB->position, normal);
        float penetration = colliderA->radius - dist;

        if (penetration > 0.0f) {
          transformA->position += normal * penetration;
          float velAlongNormal = glm::dot(physA->velocity, normal);
          if (velAlongNormal < 0.0f) {
            physA->velocity -=
                normal * velAlongNormal * (1.0f + physA->restitution);
          }
        }
      }

      // Sphere vs Sphere
      if (colliderA->type == ColliderType::Sphere &&
          colliderB->type == ColliderType::Sphere) {
        glm::vec3 diff = transformA->position - transformB->position;
        float dist = glm::length(diff);
        float minDist = colliderA->radius + colliderB->radius;

        if (dist < minDist && dist > 0.0001f) {
          glm::vec3 normal = diff / dist;
          float penetration = minDist - dist;

          auto* physB = registry->getComponent<PhysicsComponent>(entityB);
          if (physB) {
            float totalMass = physA->mass + physB->mass;
            transformA->position +=
                normal * penetration * (physB->mass / totalMass);
            transformB->position -=
                normal * penetration * (physA->mass / totalMass);

            float velAlongNormal =
                glm::dot(physA->velocity - physB->velocity, normal);
            if (velAlongNormal > 0.0f) continue;

            float e =
                glm::min(physA->restitution, physB->restitution);
            float j = -(1.0f + e) * velAlongNormal / totalMass;

            physA->velocity += normal * (j * physB->mass);
            physB->velocity -= normal * (j * physA->mass);
          } else {
            transformA->position += normal * penetration;
            float velAlongNormal = glm::dot(physA->velocity, normal);
            if (velAlongNormal < 0.0f) {
              physA->velocity -=
                  normal * velAlongNormal * (1.0f + physA->restitution);
            }
          }
        }
      }

      // Sphere vs AABB
      if (colliderA->type == ColliderType::Sphere &&
          colliderB->type == ColliderType::AABB) {
        glm::vec3 boxMin =
            transformB->position - colliderB->halfExtents * transformB->scale;
        glm::vec3 boxMax =
            transformB->position + colliderB->halfExtents * transformB->scale;
        glm::vec3 closest = glm::clamp(transformA->position, boxMin, boxMax);
        glm::vec3 diff = transformA->position - closest;
        float dist = glm::length(diff);

        if (dist < colliderA->radius && dist > 0.0001f) {
          glm::vec3 normal = diff / dist;
          float penetration = colliderA->radius - dist;

          transformA->position += normal * penetration;
          float velAlongNormal = glm::dot(physA->velocity, normal);
          if (velAlongNormal < 0.0f) {
            physA->velocity -=
                normal * velAlongNormal * (1.0f + physA->restitution);
          }
        }
      }
    }
  }
}
