#pragma once
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <stdexcept>
#include <vector>

#include "../Rendering/RenderDevice.h"
#include "../Util/Debug.h"
#include "ECS/Components.h"

constexpr uint32_t SHADOW_MAP_SIZE = 16384;
constexpr uint32_t MAX_SHADOW_CASTERS = 4;

struct ShadowMapData {
  glm::mat4 lightSpaceMatrix;
  VkImage image;
  VmaAllocation allocation;
  VkImageView imageView;
  VkSampler sampler;
  uint32_t lightIndex;

  ShadowMapData()
      : lightSpaceMatrix(1.0f),
        image(VK_NULL_HANDLE),
        allocation(VK_NULL_HANDLE),
        imageView(VK_NULL_HANDLE),
        sampler(VK_NULL_HANDLE),
        lightIndex(0) {}

  ShadowMapData(const ShadowMapData&) = default;
  ShadowMapData& operator=(const ShadowMapData&) = default;
};

class ShadowSystem final {
 public:
  explicit ShadowSystem(RenderDevice* device);
  ~ShadowSystem();

  ShadowSystem(const ShadowSystem&) = delete;
  ShadowSystem& operator=(const ShadowSystem&) = delete;

  void init();
  uint32_t createShadowMap(uint32_t lightIndex);
  void resetShadowMaps();
  void cleanup();

  glm::mat4 calculateLightSpaceMatrix(const LightComponent& light,
                                       const TransformComponent& transform,
                                       const glm::vec3& sceneCenter,
                                       float sceneRadius) const;

  void updateLightSpaceMatrix(uint32_t shadowMapIndex, const glm::mat4& matrix);

  inline glm::mat4 getLightSpaceMatrix(uint32_t shadowMapIndex) const {
    if (shadowMapIndex < shadowMaps.size()) {
      return shadowMaps[shadowMapIndex].lightSpaceMatrix;
    }
    return glm::mat4(1.0f);
  }

  void getShadowMaps(std::vector<ShadowMapData>& outMaps) const {
    outMaps = shadowMaps;
  }

  inline uint32_t getShadowMapCount() const {
    return static_cast<uint32_t>(shadowMaps.size());
  }

  inline VkDescriptorSetLayout getShadowDescriptorSetLayout() const {
    return shadowDescriptorSetLayout;
  }

  inline VkDescriptorSet getShadowDescriptorSet() const {
    return shadowDescriptorSet;
  }

 private:
  ShadowMapData dummyShadowMap;
  std::vector<ShadowMapData> shadowMaps;
  RenderDevice* renderDevice;
  VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool shadowDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet shadowDescriptorSet = VK_NULL_HANDLE;

  void createDescriptorSetLayout();
  void createDescriptorPool();
  void createDescriptorSet();
  void createDummyShadowMap();
  void updateDescriptorSet();
  void transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                             VkImageLayout newLayout) const;
};