#pragma once
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

#include "ECS/Registry.h"
#include "Rendering/RenderDevice.h"
#include "Shadow.h"
#include "Util/Debug.h"

constexpr uint32_t MAX_LIGHTS = 8;

struct LightData {
  alignas(16) glm::mat4 lightSpaceMatrix;
  alignas(16) glm::vec4 position;
  alignas(16) glm::vec4 direction;
  alignas(16) glm::vec4 color;
  alignas(4) float intensity;
  alignas(4) float constant;
  alignas(4) float linear;
  alignas(4) float quadratic;
  alignas(4) float cutOff;
  alignas(4) float outerCutOff;
  alignas(4) int type;
  alignas(4) int castsShadows;
  alignas(4) int shadowMapIndex;
  alignas(4) float padding1;
  alignas(4) float padding2;
  alignas(4) float padding3;
};

struct LightBufferObject {
  std::array<LightData, MAX_LIGHTS> lights;
  alignas(4) int numLights;
  alignas(4) int numShadowMaps;
  alignas(4) float padding1;
  alignas(4) float padding2;
};

class LightManager final {
 public:
  explicit LightManager(RenderDevice* rd);
  ~LightManager();

  LightManager(const LightManager&) = delete;
  LightManager& operator=(const LightManager&) = delete;

  void init();
  void setRegistry(Registry* reg);
  void syncLights();
  void updateLightBuffer();
  void cleanup();

  VkBuffer getLightBuffer() const { return lightBuffer; }
  int getLightCount() const { return lightCount; }

  VkDescriptorSetLayout getShadowDescriptorSetLayout() const {
    return shadowSystem->getShadowDescriptorSetLayout();
  }

  VkDescriptorSet getShadowDescriptorSet() const {
    return shadowSystem->getShadowDescriptorSet();
  }

  void getShadowMaps(std::vector<ShadowMapData>& outMaps) const {
    shadowSystem->getShadowMaps(outMaps);
  }

  int getShadowMapCount() const {
    return static_cast<int>(shadowSystem->getShadowMapCount());
  }

  ShadowSystem* getShadowSystem() const { return shadowSystem.get(); }

  void updateAllShadowMatrices(const glm::vec3& sceneCenter, float sceneRadius);
  void debugPrintLightInfo() const;

 private:
  Registry* registry = nullptr;
  std::unique_ptr<ShadowSystem> shadowSystem;
  RenderDevice* renderDevice;
  void* lightBufferMapped;
  VkBuffer lightBuffer;
  VkDeviceMemory lightBufferMemory;
  int lightCount = 0;

  void createLightBuffer();
};