#include "pch.h"

#include "PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace jphys {

PhysicsWorld::PhysicsWorld() {}

void PhysicsWorld::addObject(PhysicsObject* obj) {
  if (obj) {
    objects.push_back(obj);
  }
}

void PhysicsWorld::removeObject(PhysicsObject* obj) {
  objects.erase(std::remove(objects.begin(), objects.end(), obj),
                objects.end());
}

void PhysicsWorld::clear() { objects.clear(); }

void PhysicsWorld::step(float deltaTime) {
  if (deltaTime <= 0.0f) return;
  integrate(deltaTime);
  resolveCollisions();
}

void PhysicsWorld::integrate(float deltaTime) {
  for (auto* obj : objects) {
    if (!obj || obj->isStatic()) continue;

    glm::vec3 vel = obj->getVelocity();

    if (obj->getUseGravity()) {
      vel += gravity * deltaTime;
    }

    vel += obj->getAcceleration() * deltaTime;
    vel *= std::pow(obj->getDamping(), deltaTime);

    obj->setVelocity(vel);
    obj->setPosition(obj->getPosition() + vel * deltaTime);
  }
}

void PhysicsWorld::resolveCollisions() {
  for (size_t i = 0; i < objects.size(); ++i) {
    PhysicsObject* objA = objects[i];
    if (!objA || objA->isStatic()) continue;

    const Collider& colA = objA->getCollider();

    for (size_t j = 0; j < objects.size(); ++j) {
      if (i == j) continue;
      PhysicsObject* objB = objects[j];
      if (!objB) continue;

      const Collider& colB = objB->getCollider();

      // Sphere vs Plane
      if (colA.getType() == ColliderType::Sphere &&
          colB.getType() == ColliderType::Plane) {
        CollisionResult result = testSpherePlane(*objA, *objB);
        if (result.collided) {
          resolveSpherePlane(*objA, *objB, result);
        }
      }

      // Sphere vs Sphere
      if (colA.getType() == ColliderType::Sphere &&
          colB.getType() == ColliderType::Sphere) {
        CollisionResult result = testSphereSphere(*objA, *objB);
        if (result.collided) {
          resolveSphereSphere(*objA, *objB, result);
        }
      }

      // Sphere vs AABB
      if (colA.getType() == ColliderType::Sphere &&
          colB.getType() == ColliderType::AABB) {
        CollisionResult result = testSphereAABB(*objA, *objB);
        if (result.collided) {
          resolveSphereAABB(*objA, *objB, result);
        }
      }
    }
  }
}

CollisionResult PhysicsWorld::testSphereSphere(const PhysicsObject& a,
                                               const PhysicsObject& b) {
  CollisionResult result;

  glm::vec3 diff = a.getPosition() - b.getPosition();
  float dist = glm::length(diff);
  float minDist = a.getCollider().getRadius() + b.getCollider().getRadius();

  if (dist < minDist && dist > 0.0001f) {
    result.collided = true;
    result.normal = diff / dist;
    result.penetration = minDist - dist;
    result.contactPoint =
        a.getPosition() - result.normal * a.getCollider().getRadius();
  }

  return result;
}

CollisionResult PhysicsWorld::testSpherePlane(const PhysicsObject& sphere,
                                              const PhysicsObject& plane) {
  CollisionResult result;

  glm::vec3 normal = plane.getCollider().getNormal();
  glm::vec3 relPos = sphere.getPosition() - plane.getPosition();
  float dist = glm::dot(relPos, normal);
  float penetration = sphere.getCollider().getRadius() - dist;

  if (penetration > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact = sphere.getPosition() - normal * dist;
      glm::vec3 localContact = contact - plane.getPosition();
      glm::vec3 he = plane.getCollider().getHalfExtents() * plane.getScale();
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
      if (std::abs(proj1) > limit1 || std::abs(proj2) > limit2) {
        return result;
      }
    }

    result.collided = true;
    result.normal = normal;
    result.penetration = penetration;
    result.contactPoint = sphere.getPosition() - normal * dist;
  }

  return result;
}

CollisionResult PhysicsWorld::testSphereAABB(const PhysicsObject& sphere,
                                             const PhysicsObject& aabb) {
  CollisionResult result;

  glm::vec3 boxMin = aabb.getPosition() -
                     aabb.getCollider().getHalfExtents() * aabb.getScale();
  glm::vec3 boxMax = aabb.getPosition() +
                     aabb.getCollider().getHalfExtents() * aabb.getScale();
  glm::vec3 closest = glm::clamp(sphere.getPosition(), boxMin, boxMax);
  glm::vec3 diff = sphere.getPosition() - closest;
  float dist = glm::length(diff);

  if (dist < sphere.getCollider().getRadius() && dist > 0.0001f) {
    result.collided = true;
    result.normal = diff / dist;
    result.penetration = sphere.getCollider().getRadius() - dist;
    result.contactPoint = closest;
  }

  return result;
}

void PhysicsWorld::resolveSpherePlane(PhysicsObject& sphere,
                                      PhysicsObject& plane,
                                      const CollisionResult& result) {
  sphere.setPosition(sphere.getPosition() + result.normal * result.penetration);
  float velAlongNormal = glm::dot(sphere.getVelocity(), result.normal);
  if (velAlongNormal < 0.0f) {
    sphere.setVelocity(sphere.getVelocity() -
                       result.normal * velAlongNormal *
                           (1.0f + sphere.getRestitution()));
  }
}

void PhysicsWorld::resolveSphereSphere(PhysicsObject& a, PhysicsObject& b,
                                       const CollisionResult& result) {
  if (!b.isStatic()) {
    float totalMass = a.getMass() + b.getMass();
    a.setPosition(a.getPosition() + result.normal * result.penetration *
                                        (b.getMass() / totalMass));
    b.setPosition(b.getPosition() - result.normal * result.penetration *
                                        (a.getMass() / totalMass));

    float velAlongNormal =
        glm::dot(a.getVelocity() - b.getVelocity(), result.normal);
    if (velAlongNormal > 0.0f) return;

    float e = glm::min(a.getRestitution(), b.getRestitution());
    float impulse = -(1.0f + e) * velAlongNormal / totalMass;

    a.setVelocity(a.getVelocity() + result.normal * (impulse * b.getMass()));
    b.setVelocity(b.getVelocity() - result.normal * (impulse * a.getMass()));
  } else {
    a.setPosition(a.getPosition() + result.normal * result.penetration);
    float velAlongNormal = glm::dot(a.getVelocity(), result.normal);
    if (velAlongNormal < 0.0f) {
      a.setVelocity(a.getVelocity() - result.normal * velAlongNormal *
                                          (1.0f + a.getRestitution()));
    }
  }
}

void PhysicsWorld::resolveSphereAABB(PhysicsObject& sphere, PhysicsObject& aabb,
                                     const CollisionResult& result) {
  sphere.setPosition(sphere.getPosition() + result.normal * result.penetration);
  float velAlongNormal = glm::dot(sphere.getVelocity(), result.normal);
  if (velAlongNormal < 0.0f) {
    sphere.setVelocity(sphere.getVelocity() -
                       result.normal * velAlongNormal *
                           (1.0f + sphere.getRestitution()));
  }
}

}  // namespace jphys
