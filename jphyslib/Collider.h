#pragma once
#include <glm/glm.hpp>

namespace jphys {

enum class ColliderType { Sphere, AABB, Plane, Cylinder };

class Collider {
 public:
  Collider() = default;

  static Collider createSphere(float radius) {
    Collider c;
    c.type = ColliderType::Sphere;
    c.radius = radius;
    return c;
  }

  static Collider createAABB(const glm::vec3& halfExtents) {
    Collider c;
    c.type = ColliderType::AABB;
    c.halfExtents = halfExtents;
    return c;
  }

  static Collider createPlane(const glm::vec3& normal) {
    Collider c;
    c.type = ColliderType::Plane;
    c.normal = glm::normalize(normal);
    c.finite = false;
    return c;
  }

  static Collider createFinitePlane(const glm::vec3& normal,
                                     const glm::vec3& halfExtents) {
    Collider c;
    c.type = ColliderType::Plane;
    c.normal = glm::normalize(normal);
    c.halfExtents = halfExtents;
    c.finite = true;
    return c;
  }

  static Collider createCylinder(float radius, float height) {
    Collider c;
    c.type = ColliderType::Cylinder;
    c.radius = radius;
    c.height = height;
    return c;
  }

  ColliderType getType() const { return type; }
  float getRadius() const { return radius; }
  float getHeight() const { return height; }
  const glm::vec3& getHalfExtents() const { return halfExtents; }
  const glm::vec3& getNormal() const { return normal; }
  bool isFinite() const { return finite; }

  void setRadius(float r) { radius = r; }
  void setHalfExtents(const glm::vec3& he) { halfExtents = he; }
  void setNormal(const glm::vec3& n) { normal = glm::normalize(n); }

 private:
  ColliderType type = ColliderType::Sphere;
  float radius = 1.0f;
  float height = 1.0f;
  glm::vec3 halfExtents = glm::vec3(0.5f);
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  bool finite = false;
};

}  // namespace jphys
