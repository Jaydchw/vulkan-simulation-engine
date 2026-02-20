#include "PhysicsSystem.h"

#include <cmath>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

PhysicsSystem::PhysicsSystem() {
  Debug::log(Debug::Category::PHYSICS, "PhysicsSystem: Created");
}

void PhysicsSystem::setRegistry(Registry* reg) {
  registry = reg;
  clearSnapshots();
  initialSnapshotValid = false;
}

void PhysicsSystem::update(float deltaTime) {
  if (!registry || deltaTime <= 0.0f) return;
  integrate(deltaTime);
  resolveCollisions();
}

FrameSnapshot PhysicsSystem::captureFrame() const {
  FrameSnapshot snap;
  if (!registry) return snap;
  for (const auto& [entity, phys] : registry->allPhysics()) {
    const auto* transform = registry->getComponent<TransformComponent>(entity);
    if (!transform) continue;
    snap[entity] = {transform->position, phys.velocity};
  }
  return snap;
}

void PhysicsSystem::applyFrame(const FrameSnapshot& snap) {
  if (!registry) return;
  for (const auto& [entity, state] : snap) {
    auto* transform = registry->getComponent<TransformComponent>(entity);
    auto* phys = registry->getComponent<PhysicsComponent>(entity);
    if (transform) transform->position = state.position;
    if (phys) phys->velocity = state.velocity;
  }
}

void PhysicsSystem::saveInitialSnapshot() {
  initialSnapshot = captureFrame();
  initialSnapshotValid = true;
}

void PhysicsSystem::restoreInitialSnapshot() {
  if (!initialSnapshotValid) return;
  applyFrame(initialSnapshot);
}

void PhysicsSystem::saveSnapshot() {
  snapshots.push_back(captureFrame());
  framesSinceCondense++;
  if (framesSinceCondense >= condensePassInterval) {
    condenseOldFrames();
    framesSinceCondense = 0;
  }
}

void PhysicsSystem::restoreSnapshot(int index) {
  if (!registry || index < 0 ||
      index >= static_cast<int>(snapshots.size()))
    return;
  applyFrame(snapshots[index]);
}

void PhysicsSystem::truncateAfter(int index) {
  if (index < 0 || index >= static_cast<int>(snapshots.size())) return;
  snapshots.erase(snapshots.begin() + index + 1, snapshots.end());
}

void PhysicsSystem::clearSnapshots() {
  snapshots.clear();
  framesSinceCondense = 0;
}

int PhysicsSystem::getSnapshotCount() const {
  return static_cast<int>(snapshots.size());
}

void PhysicsSystem::condenseOldFrames() {
  int total = static_cast<int>(snapshots.size());
  if (total <= fullResFrames) return;

  int oldCount = total - fullResFrames;
  if (oldCount < 4) return;

  std::deque<FrameSnapshot> condensed;
  int step = 2;
  if (oldCount > 3600) step = 8;
  else if (oldCount > 1800) step = 4;

  for (int i = 0; i < oldCount; i += step) {
    condensed.push_back(std::move(snapshots[i]));
  }

  for (int i = oldCount; i < total; i++) {
    condensed.push_back(std::move(snapshots[i]));
  }

  snapshots = std::move(condensed);
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
        glm::vec3 relPos = transformA->position - transformB->position;
        float dist = glm::dot(relPos, normal);
        float penetration = colliderA->radius - dist;

        if (penetration > 0.0f) {
          if (colliderB->finite) {
            glm::vec3 contact = transformA->position - normal * dist;
            glm::vec3 localContact = contact - transformB->position;
            glm::vec3 he = colliderB->halfExtents * transformB->scale;
            glm::vec3 absN = glm::abs(normal);
            glm::vec3 tangent1, tangent2;
            if (absN.y > 0.5f) {
              tangent1 = glm::vec3(1.0f, 0.0f, 0.0f);
              tangent2 = glm::vec3(0.0f, 0.0f, 1.0f);
            } else if (absN.x > 0.5f) {
              tangent1 = glm::vec3(0.0f, 1.0f, 0.0f);
              tangent2 = glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
              tangent1 = glm::vec3(1.0f, 0.0f, 0.0f);
              tangent2 = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            float proj1 = glm::dot(localContact, tangent1);
            float proj2 = glm::dot(localContact, tangent2);
            float limit1 = glm::dot(he, glm::abs(tangent1));
            float limit2 = glm::dot(he, glm::abs(tangent2));
            if (std::abs(proj1) > limit1 || std::abs(proj2) > limit2)
              continue;
          }

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
            float impulse = -(1.0f + e) * velAlongNormal / totalMass;

            physA->velocity += normal * (impulse * physB->mass);
            physB->velocity -= normal * (impulse * physA->mass);
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
