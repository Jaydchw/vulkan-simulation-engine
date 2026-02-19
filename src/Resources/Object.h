#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <utility>

#include "Resources/Material.h"
#include "Util/Debug.h"

using MeshID = uint32_t;
constexpr MeshID INVALID_MESH_ID = 0;

class Object final {
 public:
  Object()
      : name("Unnamed Object"),
        position(0.0f, 0.0f, 0.0f),
        rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
        scale(1.0f, 1.0f, 1.0f),
        meshID(INVALID_MESH_ID),
        materialID(INVALID_MATERIAL_ID),
        visible(true),
        layerMask(0xFFFFFFFF) {}

  glm::mat4 getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = model * glm::mat4_cast(rotation);
    model = glm::scale(model, scale);
    return model;
  }

  void setName(const std::string& n) { name = n; }

  void getName(std::string& outName) const { outName = name; }

  void getPosition(glm::vec3& outPosition) const { outPosition = position; }

  void getRotation(glm::quat& outRotation) const { outRotation = rotation; }

  void getScale(glm::vec3& outScale) const { outScale = scale; }

  void setPosition(const glm::vec3& pos) { position = pos; }
  void setPosition(float x, float y, float z) { position = glm::vec3(x, y, z); }

  void setRotation(const glm::quat& rot) { rotation = rot; }
  void setRotationEuler(float pitch, float yaw, float roll) {
    rotation = glm::quat(
        glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
  }
  void setRotationEuler(const glm::vec3& euler) {
    rotation = glm::quat(glm::radians(euler));
  }

  void setScale(const glm::vec3& s) { scale = s; }
  void setScale(float x, float y, float z) { scale = glm::vec3(x, y, z); }
  void setScale(float uniform) { scale = glm::vec3(uniform); }

  void setMesh(MeshID mesh) { meshID = mesh; }
  MeshID getMeshID() const { return meshID; }

  void setMaterial(MaterialID material) { materialID = material; }
  MaterialID getMaterialID() const { return materialID; }

  void setVisible(bool v) { visible = v; }
  bool isVisible() const { return visible; }

  void setLayerMask(uint32_t mask) { layerMask = mask; }
  uint32_t getLayerMask() const { return layerMask; }

 private:
  std::string name;
  glm::vec3 position;
  glm::quat rotation;
  glm::vec3 scale;
  MeshID meshID;
  MaterialID materialID;
  bool visible;
  uint32_t layerMask;
};

class ObjectBuilder final {
 public:
  ObjectBuilder() : object() {}

  ObjectBuilder& name(const std::string& n) {
    object.setName(n);
    return *this;
  }

  ObjectBuilder& position(const glm::vec3& pos) {
    object.setPosition(pos);
    return *this;
  }

  ObjectBuilder& position(float x, float y, float z) {
    object.setPosition(x, y, z);
    return *this;
  }

  ObjectBuilder& rotation(const glm::quat& rot) {
    object.setRotation(rot);
    return *this;
  }

  ObjectBuilder& rotationEuler(float pitch, float yaw, float roll) {
    object.setRotationEuler(pitch, yaw, roll);
    return *this;
  }

  ObjectBuilder& rotationEuler(const glm::vec3& euler) {
    object.setRotationEuler(euler);
    return *this;
  }

  ObjectBuilder& scale(const glm::vec3& s) {
    object.setScale(s);
    return *this;
  }

  ObjectBuilder& scale(float x, float y, float z) {
    object.setScale(x, y, z);
    return *this;
  }

  ObjectBuilder& scale(float uniform) {
    object.setScale(uniform);
    return *this;
  }

  ObjectBuilder& mesh(MeshID meshID) {
    object.setMesh(meshID);
    return *this;
  }

  ObjectBuilder& material(MaterialID materialID) {
    object.setMaterial(materialID);
    return *this;
  }

  ObjectBuilder& visible(bool isVisible) {
    object.setVisible(isVisible);
    return *this;
  }

  ObjectBuilder& layerMask(uint32_t mask) {
    object.setLayerMask(mask);
    return *this;
  }

  void build(Object& outObject) const {
    std::string currentName;
    object.getName(currentName);

    if (object.getMeshID() == INVALID_MESH_ID) {
      Debug::log(Debug::Category::OBJECTS,
                 "ObjectBuilder: Warning - building object '", currentName,
                 "' with invalid mesh ID");
    }
    if (object.getMaterialID() == INVALID_MATERIAL_ID) {
      Debug::log(Debug::Category::OBJECTS,
                 "ObjectBuilder: Warning - building object '", currentName,
                 "' with invalid material ID");
    }
    Debug::log(Debug::Category::OBJECTS, "ObjectBuilder: Built object '",
               currentName, "' (Mesh: ", object.getMeshID(),
               ", Material: ", object.getMaterialID(), ")");

    outObject = object;
  }

 private:
  Object object;
};