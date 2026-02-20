#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

class EntityBuilder final {
 public:
  EntityBuilder() = default;

  EntityBuilder& name(const std::string& n) {
    nameComp.name = n;
    return *this;
  }

  EntityBuilder& position(const glm::vec3& pos) {
    transformComp.position = pos;
    return *this;
  }

  EntityBuilder& position(float x, float y, float z) {
    transformComp.position = glm::vec3(x, y, z);
    return *this;
  }

  EntityBuilder& rotation(const glm::quat& rot) {
    transformComp.rotation = rot;
    return *this;
  }

  EntityBuilder& rotationEuler(float pitch, float yaw, float roll) {
    transformComp.rotation = glm::quat(
        glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
    return *this;
  }

  EntityBuilder& rotationEuler(const glm::vec3& euler) {
    transformComp.rotation = glm::quat(glm::radians(euler));
    return *this;
  }

  EntityBuilder& scale(const glm::vec3& s) {
    transformComp.scale = s;
    return *this;
  }

  EntityBuilder& scale(float x, float y, float z) {
    transformComp.scale = glm::vec3(x, y, z);
    return *this;
  }

  EntityBuilder& scale(float uniform) {
    transformComp.scale = glm::vec3(uniform);
    return *this;
  }

  EntityBuilder& mesh(MeshID meshID) {
    meshComp.meshID = meshID;
    return *this;
  }

  EntityBuilder& material(MaterialID materialID) {
    materialComp.materialID = materialID;
    return *this;
  }

  EntityBuilder& visible(bool v) {
    renderComp.visible = v;
    return *this;
  }

  EntityBuilder& layerMask(uint32_t mask) {
    renderComp.layerMask = mask;
    return *this;
  }

  EntityBuilder& lightType(LightType t) {
    hasLight = true;
    lightComp.type = t;
    return *this;
  }

  EntityBuilder& direction(const glm::vec3& dir) {
    hasLight = true;
    lightComp.direction = glm::normalize(dir);
    return *this;
  }

  EntityBuilder& direction(float x, float y, float z) {
    hasLight = true;
    lightComp.direction = glm::normalize(glm::vec3(x, y, z));
    return *this;
  }

  EntityBuilder& color(const glm::vec3& c) {
    hasLight = true;
    lightComp.color = c;
    return *this;
  }

  EntityBuilder& color(float r, float g, float b) {
    hasLight = true;
    lightComp.color = glm::vec3(r, g, b);
    return *this;
  }

  EntityBuilder& intensity(float i) {
    hasLight = true;
    lightComp.intensity = i;
    return *this;
  }

  EntityBuilder& attenuation(float constant, float linear, float quadratic) {
    hasLight = true;
    lightComp.constant = constant;
    lightComp.linear = linear;
    lightComp.quadratic = quadratic;
    return *this;
  }

  EntityBuilder& castsShadows(bool shadows) {
    hasLight = true;
    lightComp.castsShadows = shadows;
    return *this;
  }

  Entity build(Registry& registry) const {
    if (!hasLight) {
      if (meshComp.meshID == INVALID_MESH_ID) {
        Debug::log(Debug::Category::OBJECTS,
                   "EntityBuilder: Warning - building entity '", nameComp.name,
                   "' with invalid mesh ID");
      }
      if (materialComp.materialID == INVALID_MATERIAL_ID) {
        Debug::log(Debug::Category::OBJECTS,
                   "EntityBuilder: Warning - building entity '", nameComp.name,
                   "' with invalid material ID");
      }
    }

    Entity entity = registry.createEntity();
    registry.addComponent<NameComponent>(entity, nameComp);
    registry.addComponent<TransformComponent>(entity, transformComp);

    if (hasLight) {
      registry.addComponent<LightComponent>(entity, lightComp);
      Debug::log(Debug::Category::LIGHTS, "EntityBuilder: Built light entity '",
                 nameComp.name, "'");
    } else {
      registry.addComponent<MeshComponent>(entity, meshComp);
      registry.addComponent<MaterialComponent>(entity, materialComp);
      registry.addComponent<RenderComponent>(entity, renderComp);
      Debug::log(Debug::Category::OBJECTS, "EntityBuilder: Built entity '",
                 nameComp.name, "' (Mesh: ", meshComp.meshID,
                 ", Material: ", materialComp.materialID, ")");
    }

    return entity;
  }

 private:
  NameComponent nameComp;
  TransformComponent transformComp;
  MeshComponent meshComp;
  MaterialComponent materialComp;
  RenderComponent renderComp;
  LightComponent lightComp;
  bool hasLight = false;
};
