#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "PhysicsObject.h"

namespace jphys {

struct ClothParticle {
  glm::vec3 position;
  glm::vec3 prevPosition;
  float invMass;
};

struct ClothConstraint {
  int a, b;
  float restLength;
  bool isBending;
  bool broken;
};

class ClothSim {
 public:
  ClothSim(int resX, int resZ, float width, float height,
           const glm::vec3& origin,
           glm::mat3 orientation = glm::mat3(1.0f));

  void setStructuralStiffness(float s) { structuralStiffness = s; }
  void setBendingStiffness(float s)    { bendingStiffness = s; }
  void setDamping(float d)             { damping = d; }
  void setUseGravity(bool g)           { useGravity = g; }
  void setGravity(const glm::vec3& g)  { gravity = g; }
  void setWind(const glm::vec3& w)     { wind = w; }
  void setWindDrag(float d)            { windDrag = d; }
  void setMaxVelocity(float v)         { maxVelocity = v; }
  void setTearability(float t)         { tearability = t; }
  void setParticleMass(float m);
  void setSolverIterations(int n) { solverIterations = n; }
  void setTwoWayCoupling(bool b)       { twoWayCoupling = b; }
  void pinParticle(int index);

  void step(float dt, const std::vector<PhysicsObject*>& obstacles);

  const std::vector<ClothParticle>& getParticles() const { return particles; }
  const std::vector<ClothConstraint>& getConstraints() const { return constraints; }
  int getResolutionX() const { return resX; }
  int getResolutionZ() const { return resZ; }
  int particleIndex(int x, int z) const { return z * resX + x; }

  // Returns true if any constraint tore since the last call to clearTornFlag().
  bool hasTorn() const { return tornSinceLastQuery; }
  void clearTornFlag() { tornSinceLastQuery = false; }

 private:
  int resX, resZ;
  float structuralStiffness = 0.8f;
  float bendingStiffness    = 0.1f;
  float damping             = 0.99f;
  float maxVelocity         = 25.0f;
  bool useGravity           = true;
  glm::vec3 gravity         = glm::vec3(0.0f, -9.81f, 0.0f);
  glm::vec3 wind            = glm::vec3(0.0f);
  float windDrag            = 0.2f;
  float tearability         = 0.0f;
  int solverIterations      = 8;
  bool tornSinceLastQuery   = false;
  bool twoWayCoupling       = false;
  float stepDt              = 0.016f;
  bool applyImpulseThisIter = false;

  std::vector<ClothParticle>   particles;
  std::vector<ClothConstraint> constraints;

  void integrate(float dt);
  void satisfyConstraints();
  void resolveCollisions(const std::vector<PhysicsObject*>& obstacles);
  void resolveVsObject(ClothParticle& p, PhysicsObject& obj);
  void resolveVsSphere(ClothParticle& p, PhysicsObject& obj);
  void resolveVsAABB(ClothParticle& p, PhysicsObject& obj);
  void resolveVsPlane(ClothParticle& p, PhysicsObject& obj);
  void resolveVsCylinder(ClothParticle& p, PhysicsObject& obj);
  void resolveVsCapsule(ClothParticle& p, PhysicsObject& obj);
  void resolveTrianglesVsObject(PhysicsObject& obj);
  void applyContactImpulse(ClothParticle& p, PhysicsObject& obj,
                            const glm::vec3& n, const glm::vec3& contactPt);
};

}  // namespace jphys
