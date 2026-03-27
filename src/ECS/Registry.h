#pragma once
#include <optional>
#include <unordered_map>
#include <vector>

#include "ECS/Components.h"
#include "ECS/Entity.h"
#include <PhysicsObject.h>

class Registry final {
 public:
  Registry() : nextEntity(1) {}

  Entity createEntity() { return nextEntity++; }

  void destroyEntity(Entity entity) {
    names.erase(entity);
    transforms.erase(entity);
    meshes.erase(entity);
    materials.erase(entity);
    renders.erase(entity);
    lights.erase(entity);
    physics.erase(entity);
    colliders.erase(entity);
    physicsObjects.erase(entity);
    spawners.erase(entity);
  }

  // Creates an entity with a specific ID (used when replicating remote spawns).
  // Advances nextEntity past specificId to avoid future conflicts.
  Entity createEntityWithId(Entity specificId) {
    if (specificId >= nextEntity) nextEntity = specificId + 1;
    return specificId;
  }

  template <typename T>
  void addComponent(Entity entity, const T& component);

  template <typename T>
  T* getComponent(Entity entity);

  template <typename T>
  const T* getComponent(Entity entity) const;

  template <typename T>
  bool hasComponent(Entity entity) const;

  const std::unordered_map<Entity, NameComponent>& allNames() const {
    return names;
  }
  const std::unordered_map<Entity, TransformComponent>& allTransforms() const {
    return transforms;
  }
  const std::unordered_map<Entity, MeshComponent>& allMeshes() const {
    return meshes;
  }
  const std::unordered_map<Entity, MaterialComponent>& allMaterials() const {
    return materials;
  }
  const std::unordered_map<Entity, RenderComponent>& allRenders() const {
    return renders;
  }
  const std::unordered_map<Entity, LightComponent>& allLights() const {
    return lights;
  }
  const std::unordered_map<Entity, PhysicsComponent>& allPhysics() const {
    return physics;
  }
  std::unordered_map<Entity, PhysicsComponent>& allPhysicsMut() {
    return physics;
  }
  const std::unordered_map<Entity, ColliderComponent>& allColliders() const {
    return colliders;
  }
  const std::unordered_map<Entity, SpawnerComponent>& allSpawners() const {
    return spawners;
  }
  std::unordered_map<Entity, SpawnerComponent>& allSpawnersMut() {
    return spawners;
  }
  jphys::PhysicsObject& getPhysicsObject(Entity entity) {
    return physicsObjects[entity];
  }
  const jphys::PhysicsObject& getPhysicsObject(Entity entity) const {
    return physicsObjects.at(entity);
  }
  // Returns nullptr if the entity has no PhysicsObject (safe alternative to at())
  jphys::PhysicsObject* getPhysicsObjectPtr(Entity entity) {
    auto it = physicsObjects.find(entity);
    return it != physicsObjects.end() ? &it->second : nullptr;
  }

  std::vector<Entity> getEntities() const {
    std::vector<Entity> result;
    for (const auto& [entity, _] : names) {
      result.push_back(entity);
    }
    return result;
  }

 private:
  Entity nextEntity;
  std::unordered_map<Entity, NameComponent> names;
  std::unordered_map<Entity, TransformComponent> transforms;
  std::unordered_map<Entity, MeshComponent> meshes;
  std::unordered_map<Entity, MaterialComponent> materials;
  std::unordered_map<Entity, RenderComponent> renders;
  std::unordered_map<Entity, LightComponent> lights;
  std::unordered_map<Entity, PhysicsComponent> physics;
  std::unordered_map<Entity, ColliderComponent> colliders;
  std::unordered_map<Entity, jphys::PhysicsObject> physicsObjects;
  std::unordered_map<Entity, SpawnerComponent> spawners;
};

template <>
inline void Registry::addComponent<NameComponent>(Entity entity,
                                                  const NameComponent& c) {
  names[entity] = c;
}
template <>
inline void Registry::addComponent<TransformComponent>(
    Entity entity, const TransformComponent& c) {
  transforms[entity] = c;
}
template <>
inline void Registry::addComponent<MeshComponent>(Entity entity,
                                                  const MeshComponent& c) {
  meshes[entity] = c;
}
template <>
inline void Registry::addComponent<MaterialComponent>(
    Entity entity, const MaterialComponent& c) {
  materials[entity] = c;
}
template <>
inline void Registry::addComponent<RenderComponent>(Entity entity,
                                                    const RenderComponent& c) {
  renders[entity] = c;
}
template <>
inline void Registry::addComponent<LightComponent>(Entity entity,
                                                   const LightComponent& c) {
  lights[entity] = c;
}
template <>
inline void Registry::addComponent<PhysicsComponent>(
    Entity entity, const PhysicsComponent& c) {
  physics[entity] = c;
}
template <>
inline void Registry::addComponent<ColliderComponent>(
    Entity entity, const ColliderComponent& c) {
  colliders[entity] = c;
}

template <>
inline NameComponent* Registry::getComponent<NameComponent>(Entity entity) {
  auto it = names.find(entity);
  return it != names.end() ? &it->second : nullptr;
}
template <>
inline TransformComponent* Registry::getComponent<TransformComponent>(
    Entity entity) {
  auto it = transforms.find(entity);
  return it != transforms.end() ? &it->second : nullptr;
}
template <>
inline MeshComponent* Registry::getComponent<MeshComponent>(Entity entity) {
  auto it = meshes.find(entity);
  return it != meshes.end() ? &it->second : nullptr;
}
template <>
inline MaterialComponent* Registry::getComponent<MaterialComponent>(
    Entity entity) {
  auto it = materials.find(entity);
  return it != materials.end() ? &it->second : nullptr;
}
template <>
inline RenderComponent* Registry::getComponent<RenderComponent>(
    Entity entity) {
  auto it = renders.find(entity);
  return it != renders.end() ? &it->second : nullptr;
}
template <>
inline LightComponent* Registry::getComponent<LightComponent>(Entity entity) {
  auto it = lights.find(entity);
  return it != lights.end() ? &it->second : nullptr;
}
template <>
inline PhysicsComponent* Registry::getComponent<PhysicsComponent>(
    Entity entity) {
  auto it = physics.find(entity);
  return it != physics.end() ? &it->second : nullptr;
}
template <>
inline ColliderComponent* Registry::getComponent<ColliderComponent>(
    Entity entity) {
  auto it = colliders.find(entity);
  return it != colliders.end() ? &it->second : nullptr;
}

template <>
inline const NameComponent* Registry::getComponent<NameComponent>(
    Entity entity) const {
  auto it = names.find(entity);
  return it != names.end() ? &it->second : nullptr;
}
template <>
inline const TransformComponent* Registry::getComponent<TransformComponent>(
    Entity entity) const {
  auto it = transforms.find(entity);
  return it != transforms.end() ? &it->second : nullptr;
}
template <>
inline const MeshComponent* Registry::getComponent<MeshComponent>(
    Entity entity) const {
  auto it = meshes.find(entity);
  return it != meshes.end() ? &it->second : nullptr;
}
template <>
inline const MaterialComponent* Registry::getComponent<MaterialComponent>(
    Entity entity) const {
  auto it = materials.find(entity);
  return it != materials.end() ? &it->second : nullptr;
}
template <>
inline const RenderComponent* Registry::getComponent<RenderComponent>(
    Entity entity) const {
  auto it = renders.find(entity);
  return it != renders.end() ? &it->second : nullptr;
}
template <>
inline const LightComponent* Registry::getComponent<LightComponent>(
    Entity entity) const {
  auto it = lights.find(entity);
  return it != lights.end() ? &it->second : nullptr;
}
template <>
inline const PhysicsComponent* Registry::getComponent<PhysicsComponent>(
    Entity entity) const {
  auto it = physics.find(entity);
  return it != physics.end() ? &it->second : nullptr;
}
template <>
inline const ColliderComponent* Registry::getComponent<ColliderComponent>(
    Entity entity) const {
  auto it = colliders.find(entity);
  return it != colliders.end() ? &it->second : nullptr;
}

template <>
inline bool Registry::hasComponent<NameComponent>(Entity entity) const {
  return names.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<TransformComponent>(Entity entity) const {
  return transforms.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<MeshComponent>(Entity entity) const {
  return meshes.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<MaterialComponent>(Entity entity) const {
  return materials.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<RenderComponent>(Entity entity) const {
  return renders.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<LightComponent>(Entity entity) const {
  return lights.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<PhysicsComponent>(Entity entity) const {
  return physics.count(entity) > 0;
}
template <>
inline bool Registry::hasComponent<ColliderComponent>(Entity entity) const {
  return colliders.count(entity) > 0;
}

template <>
inline void Registry::addComponent<SpawnerComponent>(Entity entity,
                                                     const SpawnerComponent& c) {
  spawners[entity] = c;
}
template <>
inline SpawnerComponent* Registry::getComponent<SpawnerComponent>(Entity entity) {
  auto it = spawners.find(entity);
  return it != spawners.end() ? &it->second : nullptr;
}
template <>
inline const SpawnerComponent* Registry::getComponent<SpawnerComponent>(
    Entity entity) const {
  auto it = spawners.find(entity);
  return it != spawners.end() ? &it->second : nullptr;
}
template <>
inline bool Registry::hasComponent<SpawnerComponent>(Entity entity) const {
  return spawners.count(entity) > 0;
}
