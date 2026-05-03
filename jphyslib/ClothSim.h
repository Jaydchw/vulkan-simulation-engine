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
  // For bending constraints: indices of the two structural constraints that span
  // this bending constraint (a-mid and mid-b). -1 if not applicable.
  // When either structural dependency tears, this bending constraint is cascade-broken.
  int dep0 = -1, dep1 = -1;
};

class ClothSim {
 public:
  ClothSim(int resX, int resZ, float width, float height,
           const glm::vec3& origin,
           glm::mat3 orientation = glm::mat3(1.0f));

  void setStructuralStiffness(float s)    { structuralStiffness = s; }
  void setBendingStiffness(float s)       { bendingStiffness = s; }
  void setDamping(float d)               { damping = d; }
  void setUseGravity(bool g)             { useGravity = g; }
  void setGravity(const glm::vec3& g)    { gravity = g; }
  void setWind(const glm::vec3& w)       { wind = w; }
  void setWindDrag(float d)              { windDrag = d; }
  void setMaxVelocity(float v)           { maxVelocity = v; }
  void setTearability(float t)           { tearability = t; }
  void setParticleMass(float m);
  void setSolverIterations(int n)        { solverIterations = n; }
  void setTwoWayCoupling(bool b)         { twoWayCoupling = b; }
  // Particles adjacent to a tear that have <= this many intact structural constraints
  // are auto-torn free. Cascades until no more dangling particles remain.
  // 0 = disabled, 2 = remove single-thread strands (recommended).
  void setWeakConnectionThreshold(int t) { weakConnectionThreshold = t; }
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
  int weakConnectionThreshold = 2;

  std::vector<ClothParticle>   particles;
  std::vector<ClothConstraint> constraints;

  // Grid-indexed lookups into the constraints vector for horizontal and vertical
  // structural constraints. horzIdx[z*(resX-1)+x] is the index of the (x,z)-(x+1,z)
  // constraint; vertIdx[z*resX+x] is (x,z)-(x,z+1). Used for O(1) torn-triangle
  // checks in collision and for the auto-tear weak-connection pass.
  std::vector<int> horzIdx;  // size (resX-1)*resZ
  std::vector<int> vertIdx;  // size resX*(resZ-1)

  // Pre-allocated scratch buffers to avoid heap allocation during autoTearWeak.
  std::vector<int>     structCountScratch;
  std::vector<uint8_t> tearAdjacentScratch;

  // Returns true if the horizontal/vertical structural constraint is broken.
  // Callers must ensure x/z are in valid range.
  bool horzBroken(int x, int z) const {
    int i = horzIdx[z * (resX - 1) + x];
    return i >= 0 && constraints[i].broken;
  }
  bool vertBroken(int x, int z) const {
    int i = vertIdx[z * resX + x];
    return i >= 0 && constraints[i].broken;
  }

  void autoTearWeak();
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
