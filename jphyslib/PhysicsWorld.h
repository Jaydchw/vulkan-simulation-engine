#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "Collider.h"
#include "PhysicsObject.h"

namespace jphys {

struct CollisionResult {
  bool collided = false;
  glm::vec3 normal = glm::vec3(0.0f);
  float penetration = 0.0f;
  glm::vec3 contactPoint = glm::vec3(0.0f);
};

class PhysicsWorld {
 public:
  PhysicsWorld();

  void setGravity(const glm::vec3& g) { gravity = g; }
  glm::vec3 getGravity() const { return gravity; }

  void addObject(PhysicsObject* obj);
  void removeObject(PhysicsObject* obj);
  void clear();

  void step(float deltaTime);

  // Collision detection (static utility methods for testing)
  static CollisionResult testSphereSphere(const PhysicsObject& a,
                                           const PhysicsObject& b);
  static CollisionResult testSpherePlane(const PhysicsObject& sphere,
                                          const PhysicsObject& plane);
  static CollisionResult testSphereAABB(const PhysicsObject& sphere,
                                         const PhysicsObject& aabb);

  const std::vector<PhysicsObject*>& getObjects() const { return objects; }

 private:
  glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
  std::vector<PhysicsObject*> objects;

  void integrate(float deltaTime);
  void resolveCollisions();

  static void resolveSpherePlane(PhysicsObject& sphere,
                                  PhysicsObject& plane,
                                  const CollisionResult& result);
  static void resolveSphereSphere(PhysicsObject& a, PhysicsObject& b,
                                   const CollisionResult& result);
  static void resolveSphereAABB(PhysicsObject& sphere, PhysicsObject& aabb,
                                 const CollisionResult& result);
};

}  // namespace jphys
