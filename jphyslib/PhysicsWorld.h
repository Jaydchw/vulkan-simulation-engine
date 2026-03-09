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

 // Returns per-pair collision stats from the last step, in insertion order
 const std::vector<CollisionPairStat>& getLastCollisionStats() const { return lastCollisionStats; }

 // Convenience totals across all pairs
 long long getLastCollisionChecks() const;
 long long getLastCollisionsResolved() const;

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

 std::vector<CollisionPairStat> lastCollisionStats;
 // Index map for O(1) lookup by pair name during a step
 std::unordered_map<std::string, size_t> pairIndex;

 void integrate(float deltaTime);
 void resolveCollisions();

 // Records a check (and optionally a resolve) for a named pair
 void recordCollision(const char* pairName, bool resolved);

 static void resolveSpherePlane(PhysicsObject& sphere,
                                  PhysicsObject& plane,
                                  const CollisionResult& result);
 static void resolveSphereSphere(PhysicsObject& a, PhysicsObject& b,
                                   const CollisionResult& result);
 static void resolveSphereAABB(PhysicsObject& sphere, PhysicsObject& aabb,
                                const CollisionResult& result);
};

}  // namespace jphys
