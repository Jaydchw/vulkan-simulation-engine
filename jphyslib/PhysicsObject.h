#pragma once
#include <glm/glm.hpp>

#include "Collider.h"

namespace jphys {

class PhysicsObject {
 public:
  PhysicsObject() = default;

  PhysicsObject(const glm::vec3& position, const Collider& collider)
      : position(position), collider(collider) {}

  // Transform
  const glm::vec3& getPosition() const { return position; }
  void setPosition(const glm::vec3& pos) { position = pos; }

  const glm::vec3& getScale() const { return scale; }
  void setScale(const glm::vec3& s) { scale = s; }

  // Velocity and acceleration
  const glm::vec3& getVelocity() const { return velocity; }
  void setVelocity(const glm::vec3& vel) { velocity = vel; }

  const glm::vec3& getAcceleration() const { return acceleration; }
  void setAcceleration(const glm::vec3& accel) { acceleration = accel; }

  // Physical properties
  float getMass() const { return mass; }
  void setMass(float m) { mass = m; }

  float getRestitution() const { return restitution; }
  void setRestitution(float r) { restitution = r; }

  float getDamping() const { return damping; }
  void setDamping(float d) { damping = d; }

  bool getUseGravity() const { return useGravity; }
  void setUseGravity(bool g) { useGravity = g; }

  bool isStatic() const { return staticBody; }
  void setStatic(bool s) { staticBody = s; }

  // Collider
  Collider& getCollider() { return collider; }
  const Collider& getCollider() const { return collider; }
  void setCollider(const Collider& c) { collider = c; }

 private:
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(1.0f);
  glm::vec3 velocity = glm::vec3(0.0f);
  glm::vec3 acceleration = glm::vec3(0.0f);

  float mass = 1.0f;
  float restitution = 0.5f;
  float damping = 0.99f;
  bool useGravity = true;
  bool staticBody = false;

  Collider collider;
};

}  // namespace jphys
