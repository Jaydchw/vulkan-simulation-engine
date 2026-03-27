#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Collider.h"

namespace jphys {

class PhysicsObject {
 public:
  PhysicsObject() { recomputeWorldInvI(); }

  PhysicsObject(const glm::vec3& position, const Collider& collider)
      : position(position), collider(collider) { recomputeWorldInvI(); }

  const glm::vec3& getPosition() const { return position; }
  void setPosition(const glm::vec3& pos) { position = pos; }

  const glm::vec3& getScale() const { return scale; }
  void setScale(const glm::vec3& s) { scale = s; }

  const glm::quat& getOrientation() const { return orientation; }
  void setOrientation(const glm::quat& q) { orientation = q; recomputeWorldInvI(); }

  const glm::vec3& getVelocity() const { return velocity; }
  void setVelocity(const glm::vec3& vel) { velocity = vel; }

  const glm::vec3& getAcceleration() const { return acceleration; }
  void setAcceleration(const glm::vec3& accel) { acceleration = accel; }

  const glm::vec3& getAngularVelocity() const { return angularVelocity; }
  void setAngularVelocity(const glm::vec3& av) { angularVelocity = av; }

  void addTorque(const glm::vec3& t) { torqueAccumulator += t; }
  const glm::vec3& getTorque() const { return torqueAccumulator; }
  void clearTorque() { torqueAccumulator = glm::vec3(0.0f); }

  void applyForceAtPoint(const glm::vec3& force,
                         const glm::vec3& applicationPoint) {
    glm::vec3 r = applicationPoint - position;
    torqueAccumulator += glm::cross(r, force);
  }

  float getMomentOfInertia() const {
    if (collider.getType() == ColliderType::Sphere) {
      float r = collider.getRadius();
      return (2.0f / 5.0f) * mass * r * r;
    }
    return mass;
  }

  glm::vec3 getBodyInverseInertia() const {
    if (collider.getType() == ColliderType::Sphere) {
      float r = collider.getRadius();
      float I = (2.0f / 5.0f) * mass * r * r;
      float inv = (I > 0.0f) ? 1.0f / I : 0.0f;
      return glm::vec3(inv, inv, inv);
    }
    if (collider.getType() == ColliderType::Cylinder) {
      float r = collider.getRadius();
      float h = collider.getHeight();
      float Ixx = (1.0f / 12.0f) * mass * (3.0f * r * r + h * h);
      float Izz = 0.5f * mass * r * r;
      float invXX = (Ixx > 0.0f) ? 1.0f / Ixx : 0.0f;
      float invZZ = (Izz > 0.0f) ? 1.0f / Izz : 0.0f;
      return glm::vec3(invXX, invXX, invZZ);
    }
    if (collider.getType() == ColliderType::AABB) {
      glm::vec3 he = collider.getHalfExtents();
      float a = he.x * 2.0f;
      float b = he.y * 2.0f;
      float c = he.z * 2.0f;
      float Ixx = (1.0f / 12.0f) * mass * (b * b + c * c);
      float Iyy = (1.0f / 12.0f) * mass * (a * a + c * c);
      float Izz = (1.0f / 12.0f) * mass * (a * a + b * b);
      float invXX = (Ixx > 0.0f) ? 1.0f / Ixx : 0.0f;
      float invYY = (Iyy > 0.0f) ? 1.0f / Iyy : 0.0f;
      float invZZ = (Izz > 0.0f) ? 1.0f / Izz : 0.0f;
      return glm::vec3(invXX, invYY, invZZ);
    }
    if (collider.getType() == ColliderType::Capsule) {
      float r = collider.getRadius();
      float h = collider.getHeight();
      float Ixx = (1.0f / 12.0f) * mass * (3.0f * r * r + h * h);
      float Izz = 0.5f * mass * r * r;
      float invXX = (Ixx > 0.0f) ? 1.0f / Ixx : 0.0f;
      float invZZ = (Izz > 0.0f) ? 1.0f / Izz : 0.0f;
      return glm::vec3(invXX, invXX, invZZ);
    }
    if (collider.getType() == ColliderType::Cone) {
      float r = collider.getRadius();
      float h = collider.getHeight();
      float Izz = (3.0f / 10.0f) * mass * r * r;
      float Ixx = (3.0f / 80.0f) * mass * (4.0f * r * r + h * h);
      float invXX = (Ixx > 0.0f) ? 1.0f / Ixx : 0.0f;
      float invZZ = (Izz > 0.0f) ? 1.0f / Izz : 0.0f;
      return glm::vec3(invXX, invXX, invZZ);
    }
    float inv = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    return glm::vec3(inv, inv, inv);
  }

  const glm::mat3& getWorldInverseInertiaTensor() const { return cachedWorldInvI; }

  float getMass() const { return mass; }
  void setMass(float m) { mass = m; recomputeWorldInvI(); }

  float getRestitution() const { return restitution; }
  void setRestitution(float r) { restitution = r; }

  float getDamping() const { return damping; }
  void setDamping(float d) { damping = d; }

  bool getUseGravity() const { return useGravity; }
  void setUseGravity(bool g) { useGravity = g; }

  bool isStatic() const { return staticBody; }
  void setStatic(bool s) { staticBody = s; }

  Collider& getCollider() { return collider; }
  const Collider& getCollider() const { return collider; }
  void setCollider(const Collider& c) { collider = c; recomputeWorldInvI(); }

 private:
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(1.0f);
  glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 velocity = glm::vec3(0.0f);
  glm::vec3 acceleration = glm::vec3(0.0f);
  glm::vec3 angularVelocity = glm::vec3(0.0f);
  glm::vec3 torqueAccumulator = glm::vec3(0.0f);

  float mass = 1.0f;
  float restitution = 0.5f;
  float damping = 0.99f;
  bool useGravity = true;
  bool staticBody = false;

  Collider collider;

  glm::mat3 cachedWorldInvI = glm::mat3(1.0f);

  void recomputeWorldInvI() {
    glm::vec3 invI = getBodyInverseInertia();
    glm::mat3 R = glm::mat3_cast(orientation);
    glm::mat3 RD;
    RD[0] = R[0] * invI.x;
    RD[1] = R[1] * invI.y;
    RD[2] = R[2] * invI.z;
    cachedWorldInvI = RD * glm::transpose(R);
  }
};

}  // namespace jphys
