#pragma once
#include <optional>
#include <vector>

#include "ECS/ComponentStore.h"
#include "ECS/Components.h"
#include "ECS/Entity.h"
#include <PhysicsObject.h>

// All component types except PhysicsObject (which is managed separately by the
// physics library) are stored in a ComponentStore<T> — a dense parallel-vector
// structure that keeps each component type contiguous in memory.  This ensures
// that hot iteration paths (renderer, physics, timeline) read linearly from
// cache-friendly memory rather than chasing scattered heap-node pointers.

class Registry final {
 public:
  Registry() : nextEntity(1) {}

  Entity createEntity() { return nextEntity++; }

  void destroyEntity(Entity entity) {
    names.erase(entity);
    transforms.erase(entity);
    meshes.erase(entity);
    materials.erase(entity);
    physicsMaterials.erase(entity);
    renders.erase(entity);
    lights.erase(entity);
    simulated.erase(entity);
    colliders.erase(entity);
    physicsObjects.erase(entity);
    spawners.erase(entity);
    cameras.erase(entity);
    animations.erase(entity);
    cloths.erase(entity);
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

  // Returns a reference to the flat entity list; no heap allocation per call.
  const std::vector<Entity>& getEntities() const {
    return names.entityList();
  }

  // ── Per-component-type accessors ────────────────────────────────────────────
  const ComponentStore<NameComponent>&             allNames()             const { return names; }
  const ComponentStore<TransformComponent>&        allTransforms()        const { return transforms; }
  const ComponentStore<MeshComponent>&             allMeshes()            const { return meshes; }
  const ComponentStore<RenderMaterialComponent>&   allMaterials()         const { return materials; }
  const ComponentStore<PhysicsMaterialComponent>&  allPhysicsMaterials()  const { return physicsMaterials; }
  const ComponentStore<RenderComponent>&           allRenders()           const { return renders; }
  const ComponentStore<LightComponent>&            allLights()            const { return lights; }
  const ComponentStore<SimulatedComponent>&        allSimulated()         const { return simulated; }
  ComponentStore<SimulatedComponent>&              allSimulatedMut()            { return simulated; }
  const ComponentStore<ColliderComponent>&         allColliders()         const { return colliders; }
  const ComponentStore<SpawnerComponent>&          allSpawners()          const { return spawners; }
  ComponentStore<SpawnerComponent>&                allSpawnersMut()             { return spawners; }
  const ComponentStore<CameraComponent>&           allCameras()           const { return cameras; }
  const ComponentStore<AnimationComponent>&        allAnimations()        const { return animations; }
  ComponentStore<AnimationComponent>&              allAnimationsMut()           { return animations; }
  const ComponentStore<ClothComponent>&            allCloths()            const { return cloths; }
  ComponentStore<ClothComponent>&                  allClothsMut()               { return cloths; }

  jphys::PhysicsObject& getPhysicsObject(Entity entity) {
    return physicsObjects[entity];
  }
  const jphys::PhysicsObject& getPhysicsObject(Entity entity) const {
    return physicsObjects.at(entity);
  }
  jphys::PhysicsObject* getPhysicsObjectPtr(Entity entity) {
    auto it = physicsObjects.find(entity);
    return it != physicsObjects.end() ? &it->second : nullptr;
  }

 private:
  Entity nextEntity;

  ComponentStore<NameComponent>            names;
  ComponentStore<TransformComponent>       transforms;
  ComponentStore<MeshComponent>            meshes;
  ComponentStore<RenderMaterialComponent>  materials;
  ComponentStore<PhysicsMaterialComponent> physicsMaterials;
  ComponentStore<RenderComponent>          renders;
  ComponentStore<LightComponent>           lights;
  ComponentStore<SimulatedComponent>       simulated;
  ComponentStore<ColliderComponent>        colliders;
  ComponentStore<SpawnerComponent>         spawners;
  ComponentStore<CameraComponent>          cameras;
  ComponentStore<AnimationComponent>       animations;
  ComponentStore<ClothComponent>           cloths;

  // PhysicsObject is owned by the physics library and uses a map directly.
  std::unordered_map<Entity, jphys::PhysicsObject> physicsObjects;
};

// ── addComponent specialisations ────────────────────────────────────────────
template <> inline void Registry::addComponent<NameComponent>(Entity e, const NameComponent& c)            { names.insert(e, c); }
template <> inline void Registry::addComponent<TransformComponent>(Entity e, const TransformComponent& c)  { transforms.insert(e, c); }
template <> inline void Registry::addComponent<MeshComponent>(Entity e, const MeshComponent& c)            { meshes.insert(e, c); }
template <> inline void Registry::addComponent<RenderMaterialComponent>(Entity e, const RenderMaterialComponent& c) { materials.insert(e, c); }
template <> inline void Registry::addComponent<PhysicsMaterialComponent>(Entity e, const PhysicsMaterialComponent& c) { physicsMaterials.insert(e, c); }
template <> inline void Registry::addComponent<RenderComponent>(Entity e, const RenderComponent& c)        { renders.insert(e, c); }
template <> inline void Registry::addComponent<LightComponent>(Entity e, const LightComponent& c)          { lights.insert(e, c); }
template <> inline void Registry::addComponent<SimulatedComponent>(Entity e, const SimulatedComponent& c)  { simulated.insert(e, c); }
template <> inline void Registry::addComponent<ColliderComponent>(Entity e, const ColliderComponent& c)    { colliders.insert(e, c); }
template <> inline void Registry::addComponent<SpawnerComponent>(Entity e, const SpawnerComponent& c)      { spawners.insert(e, c); }
template <> inline void Registry::addComponent<CameraComponent>(Entity e, const CameraComponent& c)        { cameras.insert(e, c); }
template <> inline void Registry::addComponent<AnimationComponent>(Entity e, const AnimationComponent& c)  { animations.insert(e, c); }
template <> inline void Registry::addComponent<ClothComponent>(Entity e, const ClothComponent& c)          { cloths.insert(e, c); }

// ── getComponent specialisations ────────────────────────────────────────────
template <> inline NameComponent*            Registry::getComponent<NameComponent>(Entity e)            { return names.get(e); }
template <> inline TransformComponent*       Registry::getComponent<TransformComponent>(Entity e)       { return transforms.get(e); }
template <> inline MeshComponent*            Registry::getComponent<MeshComponent>(Entity e)            { return meshes.get(e); }
template <> inline RenderMaterialComponent*  Registry::getComponent<RenderMaterialComponent>(Entity e)  { return materials.get(e); }
template <> inline PhysicsMaterialComponent* Registry::getComponent<PhysicsMaterialComponent>(Entity e) { return physicsMaterials.get(e); }
template <> inline RenderComponent*          Registry::getComponent<RenderComponent>(Entity e)          { return renders.get(e); }
template <> inline LightComponent*           Registry::getComponent<LightComponent>(Entity e)           { return lights.get(e); }
template <> inline SimulatedComponent*       Registry::getComponent<SimulatedComponent>(Entity e)       { return simulated.get(e); }
template <> inline ColliderComponent*        Registry::getComponent<ColliderComponent>(Entity e)        { return colliders.get(e); }
template <> inline SpawnerComponent*         Registry::getComponent<SpawnerComponent>(Entity e)         { return spawners.get(e); }
template <> inline CameraComponent*          Registry::getComponent<CameraComponent>(Entity e)          { return cameras.get(e); }
template <> inline AnimationComponent*       Registry::getComponent<AnimationComponent>(Entity e)       { return animations.get(e); }
template <> inline ClothComponent*           Registry::getComponent<ClothComponent>(Entity e)           { return cloths.get(e); }

// ── const getComponent specialisations ──────────────────────────────────────
template <> inline const NameComponent*            Registry::getComponent<NameComponent>(Entity e)            const { return names.get(e); }
template <> inline const TransformComponent*       Registry::getComponent<TransformComponent>(Entity e)       const { return transforms.get(e); }
template <> inline const MeshComponent*            Registry::getComponent<MeshComponent>(Entity e)            const { return meshes.get(e); }
template <> inline const RenderMaterialComponent*  Registry::getComponent<RenderMaterialComponent>(Entity e)  const { return materials.get(e); }
template <> inline const PhysicsMaterialComponent* Registry::getComponent<PhysicsMaterialComponent>(Entity e) const { return physicsMaterials.get(e); }
template <> inline const RenderComponent*          Registry::getComponent<RenderComponent>(Entity e)          const { return renders.get(e); }
template <> inline const LightComponent*           Registry::getComponent<LightComponent>(Entity e)           const { return lights.get(e); }
template <> inline const SimulatedComponent*       Registry::getComponent<SimulatedComponent>(Entity e)       const { return simulated.get(e); }
template <> inline const ColliderComponent*        Registry::getComponent<ColliderComponent>(Entity e)        const { return colliders.get(e); }
template <> inline const SpawnerComponent*         Registry::getComponent<SpawnerComponent>(Entity e)         const { return spawners.get(e); }
template <> inline const CameraComponent*          Registry::getComponent<CameraComponent>(Entity e)          const { return cameras.get(e); }
template <> inline const AnimationComponent*       Registry::getComponent<AnimationComponent>(Entity e)       const { return animations.get(e); }
template <> inline const ClothComponent*           Registry::getComponent<ClothComponent>(Entity e)           const { return cloths.get(e); }

// ── hasComponent specialisations ────────────────────────────────────────────
template <> inline bool Registry::hasComponent<NameComponent>(Entity e)            const { return names.has(e); }
template <> inline bool Registry::hasComponent<TransformComponent>(Entity e)       const { return transforms.has(e); }
template <> inline bool Registry::hasComponent<MeshComponent>(Entity e)            const { return meshes.has(e); }
template <> inline bool Registry::hasComponent<RenderMaterialComponent>(Entity e)  const { return materials.has(e); }
template <> inline bool Registry::hasComponent<PhysicsMaterialComponent>(Entity e) const { return physicsMaterials.has(e); }
template <> inline bool Registry::hasComponent<RenderComponent>(Entity e)          const { return renders.has(e); }
template <> inline bool Registry::hasComponent<LightComponent>(Entity e)           const { return lights.has(e); }
template <> inline bool Registry::hasComponent<SimulatedComponent>(Entity e)       const { return simulated.has(e); }
template <> inline bool Registry::hasComponent<ColliderComponent>(Entity e)        const { return colliders.has(e); }
template <> inline bool Registry::hasComponent<SpawnerComponent>(Entity e)         const { return spawners.has(e); }
template <> inline bool Registry::hasComponent<CameraComponent>(Entity e)          const { return cameras.has(e); }
template <> inline bool Registry::hasComponent<AnimationComponent>(Entity e)       const { return animations.has(e); }
template <> inline bool Registry::hasComponent<ClothComponent>(Entity e)           const { return cloths.has(e); }
