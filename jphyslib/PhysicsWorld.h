#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
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

struct CollisionPairStat {
  std::string pairName;
  long long checks = 0;
  long long resolved = 0;
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

 const std::vector<CollisionPairStat>& getLastCollisionStats() const { return lastCollisionStats; }

 long long getLastCollisionChecks() const;
 long long getLastCollisionsResolved() const;

 static CollisionResult testSphereSphere(const PhysicsObject& a,
                                          const PhysicsObject& b);
 static CollisionResult testSpherePlane(const PhysicsObject& sphere,
                                         const PhysicsObject& plane);
 static CollisionResult testSphereAABB(const PhysicsObject& sphere,
                                        const PhysicsObject& aabb);
 static CollisionResult testSphereCylinder(const PhysicsObject& sphere,
                                            const PhysicsObject& cylinder);
 static CollisionResult testCylinderPlane(const PhysicsObject& cylinder,
                                           const PhysicsObject& plane);
 static CollisionResult testCylinderCylinder(const PhysicsObject& a,
                                              const PhysicsObject& b);
 static CollisionResult testAABBPlane(const PhysicsObject& aabb,
                                       const PhysicsObject& plane);
 static CollisionResult testAABBAABB(const PhysicsObject& a,
                                      const PhysicsObject& b);
 static CollisionResult testAABBCylinder(const PhysicsObject& aabb,
                                          const PhysicsObject& cylinder);
 static CollisionResult testCapsulePlane(const PhysicsObject& capsule,
                                          const PhysicsObject& plane);
 static CollisionResult testCapsuleSphere(const PhysicsObject& capsule,
                                           const PhysicsObject& sphere);
 static CollisionResult testCapsuleAABB(const PhysicsObject& capsule,
                                         const PhysicsObject& aabb);
 static CollisionResult testCapsuleCylinder(const PhysicsObject& capsule,
                                             const PhysicsObject& cylinder);
 static CollisionResult testCapsuleCapsule(const PhysicsObject& a,
                                            const PhysicsObject& b);
 static CollisionResult testConePlane(const PhysicsObject& cone,
                                       const PhysicsObject& plane);
 static CollisionResult testConeSphere(const PhysicsObject& cone,
                                        const PhysicsObject& sphere);

 const std::vector<PhysicsObject*>& getObjects() const { return objects; }

private:
 glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
 std::vector<PhysicsObject*> objects;

 std::vector<CollisionPairStat> lastCollisionStats;
 std::unordered_map<std::string, size_t> pairIndex;

 void integrate(float deltaTime);
 void resolveCollisions();

 void recordCollision(const char* pairName, bool resolved);

 static void resolveImpulse(PhysicsObject& a, PhysicsObject& b,
                             const CollisionResult& result);
 static void resolveSpherePlane(PhysicsObject& sphere,
                                  PhysicsObject& plane,
                                  const CollisionResult& result);
 static void resolveSphereSphere(PhysicsObject& a, PhysicsObject& b,
                                   const CollisionResult& result);
 static void resolveSphereAABB(PhysicsObject& sphere, PhysicsObject& aabb,
                                const CollisionResult& result);
 static void resolveCylinderPlane(PhysicsObject& cylinder,
                                   PhysicsObject& plane,
                                   const CollisionResult& result);
};

}  // namespace jphys
