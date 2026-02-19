#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

#include "Rendering/RenderDevice.h"
#include "Resources/Material.h"
#include "Resources/TextureManager.h"

class MaterialManager final {
 public:
  MaterialManager(RenderDevice* rd, TextureManager* tm);
  ~MaterialManager();

  MaterialManager(const MaterialManager&) = delete;
  MaterialManager& operator=(const MaterialManager&) = delete;

  void init(VkDescriptorSetLayout descSetLayout, VkDescriptorPool descPool);

  MaterialID registerMaterial(Material* material);
  MaterialID registerMaterial(const MaterialBuilder& builder);
  Material* getMaterial(MaterialID id);
  const Material* getMaterial(MaterialID id) const;

  MaterialID getDefaultMaterial() const { return defaultMaterialID; }

  void updateMaterialProperties(MaterialID id,
                                const MaterialProperties& properties);
  void cleanup();
  MaterialID loadFromMTL(const std::string& mtlFilepath);

 private:
  // 1. Large Containers (ordered by approximate size: Map > Vector)
  std::unordered_map<std::string, MaterialID> mtlFilepathToID;
  std::unordered_map<std::string, MaterialID> materialNameToID;
  std::vector<std::unique_ptr<Material>> materials;

  // 2. Pointers & Handles (8 bytes)
  RenderDevice* renderDevice;
  TextureManager* textureManager;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;

  // 3. Small Scalars (4 bytes)
  MaterialID defaultMaterialID;

  void createDescriptorSet(Material* material) const;
  void updateDescriptorSet(const Material* material) const;
  void createDefaultMaterial();
};