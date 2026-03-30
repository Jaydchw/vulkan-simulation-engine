#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

#include "Rendering/RenderDevice.h"
#include "Resources/RenderMaterial.h"
#include "Resources/TextureManager.h"

class RenderMaterialManager final {
 public:
  RenderMaterialManager(RenderDevice* rd, TextureManager* tm);
  ~RenderMaterialManager();

  RenderMaterialManager(const RenderMaterialManager&) = delete;
  RenderMaterialManager& operator=(const RenderMaterialManager&) = delete;

  void init(VkDescriptorSetLayout descSetLayout);
  void resetForNewScene();

  RenderMaterialID registerMaterial(RenderMaterial* material);
  RenderMaterialID registerMaterial(const RenderMaterialBuilder& builder);
  RenderMaterial* getMaterial(RenderMaterialID id);
  const RenderMaterial* getMaterial(RenderMaterialID id) const;

  RenderMaterialID getDefaultMaterial() const { return defaultRenderMaterialID; }

  void updateRenderMaterialProperties(RenderMaterialID id,
                                const RenderMaterialProperties& properties);
  void cleanup();
  RenderMaterialID loadFromMTL(const std::string& mtlFilepath);

 private:
  std::unordered_map<std::string, RenderMaterialID> mtlFilepathToID;
  std::unordered_map<std::string, RenderMaterialID> materialNameToID;
  std::vector<std::unique_ptr<RenderMaterial>> materials;

  RenderDevice* renderDevice;
  TextureManager* textureManager;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;

  RenderMaterialID defaultRenderMaterialID;

  void createDescriptorSet(RenderMaterial* material) const;
  void updateDescriptorSet(const RenderMaterial* material) const;
  void createDefaultMaterial();
};
