#pragma once
#include <glm/glm.hpp>
#include <string>
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

 void setSolverIterations(int n) { solverIterations = (std::max)(1, n); }
 int getSolverIterations() const { return solverIterations; }

 void setSleepThresholds(float linearSpeed, float angularSpeed, float delay) {
   sleepLinearThreshSq  = linearSpeed * linearSpeed;
   sleepAngularThreshSq = angularSpeed * angularSpeed;
   sleepDelay           = delay;
 }

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
 // Indexed by (typeA * 6 + typeB); -1 = no stat entry yet this frame.
 // ColliderType enum: Sphere=0, AABB=1, Plane=2, Cylinder=3, Capsule=4, Cone=5.
 int pairStatIndex[36];

 int   solverIterations     = 4;
 float sleepLinearThreshSq  = 0.0025f;  // (0.05 m/s)^2
 float sleepAngularThreshSq = 0.01f;    // (0.10 rad/s)^2
 float sleepDelay           = 0.3f;

 void integrate(float deltaTime);
 void resolveCollisions();

 void recordCollision(int pairIdx, bool resolved);

 static void applyPositionCorrection(PhysicsObject& a, PhysicsObject& b,
                                     const CollisionResult& result);
 static void applyVelocityImpulse(PhysicsObject& a, PhysicsObject& b,
                                  const CollisionResult& result);
};

}  // namespace jphys
