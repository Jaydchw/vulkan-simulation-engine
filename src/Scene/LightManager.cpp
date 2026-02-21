#include "LightManager.h"

LightManager::LightManager(RenderDevice* rd)
    : registry(nullptr),
      shadowSystem(nullptr),
      renderDevice(rd),
      lightBufferMapped(nullptr),
      lightBuffer(VK_NULL_HANDLE),
      lightBufferMemory(VK_NULL_HANDLE),
      lightCount(0) {
  Debug::log(Debug::Category::LIGHTS, "LightManager: Constructor called");
}

LightManager::~LightManager() {
  try {
    Debug::log(Debug::Category::LIGHTS, "LightManager: Destructor called");
    cleanup();
  } catch (...) {
  }
}

void LightManager::init() {
  Debug::log(Debug::Category::LIGHTS, "LightManager: Initializing");
  createLightBuffer();
  shadowSystem = std::make_unique<ShadowSystem>(renderDevice);
  shadowSystem->init();
  Debug::log(Debug::Category::LIGHTS, "LightManager: Initialization complete");
}

void LightManager::setRegistry(Registry* reg) {
  registry = reg;
  lightCount = 0;
}

void LightManager::resetForNewScene() {
  Debug::log(Debug::Category::LIGHTS, "LightManager: Resetting for new scene");
  if (shadowSystem) {
    shadowSystem->resetShadowMaps();
  }
  lightCount = 0;
  LightBufferObject lbo{};
  memcpy(lightBufferMapped, &lbo, sizeof(LightBufferObject));
}

void LightManager::syncLights() {
  if (!registry) return;

  const auto& lightMap = registry->allLights();
  if (static_cast<int>(lightMap.size()) == lightCount) return;

  std::vector<ShadowMapData> shadowMaps;
  shadowSystem->getShadowMaps(shadowMaps);

  size_t idx = 0;
  for (auto& [entity, light] : lightMap) {
    if (idx >= static_cast<size_t>(lightCount)) {
      auto* lc = registry->getComponent<LightComponent>(entity);
      if (lc && lc->castsShadows && shadowMaps.size() < MAX_SHADOW_CASTERS) {
        uint32_t shadowMapIndex =
            shadowSystem->createShadowMap(static_cast<uint32_t>(idx));
        lc->shadowMapIndex = shadowMapIndex;

        const auto* nameComp = registry->getComponent<NameComponent>(entity);
        std::string name = nameComp ? nameComp->name : "Unknown";
        Debug::log(Debug::Category::LIGHTS,
                   "LightManager: Created shadow map for light '", name,
                   "' with index: ", shadowMapIndex);
        shadowSystem->getShadowMaps(shadowMaps);
      }
    }
    idx++;
  }

  lightCount = static_cast<int>(lightMap.size());
  updateLightBuffer();
}

void LightManager::updateLightBuffer() {
  if (!registry) return;

  LightBufferObject lbo{};
  const auto& lightMap = registry->allLights();
  lbo.numLights = static_cast<int>(lightMap.size());

  std::vector<ShadowMapData> shadowMaps;
  shadowSystem->getShadowMaps(shadowMaps);
  lbo.numShadowMaps = static_cast<int>(shadowMaps.size());

  static bool logged = false;
  if (!logged) {
    Debug::log(Debug::Category::LIGHTS,
               "LightManager: Updating light buffer with ", lbo.numLights,
               " lights and ", lbo.numShadowMaps, " shadow maps");
    Debug::log(Debug::Category::LIGHTS,
               "LightManager: Light buffer size: ", sizeof(LightBufferObject),
               " bytes");
    Debug::log(Debug::Category::LIGHTS,
               "LightManager: Light data size: ", sizeof(LightData), " bytes");
    logged = true;
  }

  size_t i = 0;
  for (const auto& [entity, light] : lightMap) {
    if (i >= MAX_LIGHTS) break;

    const auto* transform = registry->getComponent<TransformComponent>(entity);
    glm::vec3 pos = transform ? transform->position : glm::vec3(0.0f);

    lbo.lights[i].position = glm::vec4(pos, 1.0f);
    lbo.lights[i].direction = (light.type == LightType::Sun)
                                  ? glm::vec4(light.direction, 0.0f)
                                  : glm::vec4(0.0f);
    lbo.lights[i].color = glm::vec4(light.color, 1.0f);
    lbo.lights[i].intensity = light.intensity;
    lbo.lights[i].constant = light.constant;
    lbo.lights[i].linear = light.linear;
    lbo.lights[i].quadratic = light.quadratic;
    lbo.lights[i].cutOff = light.cutOff;
    lbo.lights[i].outerCutOff = light.outerCutOff;
    lbo.lights[i].type = static_cast<int>(light.type);
    lbo.lights[i].castsShadows = light.castsShadows ? 1 : 0;
    lbo.lights[i].shadowMapIndex = static_cast<int>(light.shadowMapIndex);

    if (light.shadowMapIndex != UINT32_MAX) {
      lbo.lights[i].lightSpaceMatrix =
          shadowSystem->getLightSpaceMatrix(light.shadowMapIndex);
    } else {
      lbo.lights[i].lightSpaceMatrix = glm::mat4(1.0f);
    }

    i++;
  }

  memcpy(lightBufferMapped, &lbo, sizeof(LightBufferObject));
}

void LightManager::cleanup() {
  Debug::log(Debug::Category::LIGHTS, "LightManager: Cleaning up");

  if (shadowSystem) {
    shadowSystem->cleanup();
    shadowSystem.reset();
  }

  if (lightBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(renderDevice->getDevice(), lightBuffer, nullptr);
    lightBuffer = VK_NULL_HANDLE;
  }
  if (lightBufferMemory != VK_NULL_HANDLE) {
    vkFreeMemory(renderDevice->getDevice(), lightBufferMemory, nullptr);
    lightBufferMemory = VK_NULL_HANDLE;
  }

  Debug::log(Debug::Category::LIGHTS, "LightManager: Cleanup complete");
}

void LightManager::updateAllShadowMatrices(const glm::vec3& sceneCenter,
                                           float sceneRadius) {
  if (!registry) return;

  for (const auto& [entity, light] : registry->allLights()) {
    if (light.castsShadows && light.shadowMapIndex != UINT32_MAX) {
      const auto* transform =
          registry->getComponent<TransformComponent>(entity);
      TransformComponent defaultTransform;
      const auto& t = transform ? *transform : defaultTransform;

      const glm::mat4 lightSpaceMatrix =
          shadowSystem->calculateLightSpaceMatrix(light, t, sceneCenter,
                                                  sceneRadius);
      shadowSystem->updateLightSpaceMatrix(light.shadowMapIndex,
                                           lightSpaceMatrix);
    }
  }

  updateLightBuffer();
}

void LightManager::debugPrintLightInfo() const {
  if (!registry) return;

  const auto& lightMap = registry->allLights();
  Debug::log(Debug::Category::LIGHTS, "=== Light System Debug Info ===");
  Debug::log(Debug::Category::LIGHTS, "Total lights: ", lightMap.size());

  size_t i = 0;
  for (const auto& [entity, light] : lightMap) {
    const auto* nameComp = registry->getComponent<NameComponent>(entity);
    std::string name = nameComp ? nameComp->name : "Unknown";
    Debug::log(Debug::Category::LIGHTS, "Light ", i, ": ", name);
    Debug::log(Debug::Category::LIGHTS, " Type: ",
               (light.type == LightType::Sun ? "Sun" : "Point"));

    const auto* transform = registry->getComponent<TransformComponent>(entity);
    if (transform) {
      Debug::log(Debug::Category::LIGHTS, " Position: (", transform->position.x,
                 ", ", transform->position.y, ", ", transform->position.z, ")");
    }

    if (light.type == LightType::Sun) {
      Debug::log(Debug::Category::LIGHTS, " Direction: (", light.direction.x,
                 ", ", light.direction.y, ", ", light.direction.z, ")");
    }
    Debug::log(Debug::Category::LIGHTS, " Intensity: ", light.intensity);
    Debug::log(Debug::Category::LIGHTS,
               " Casts Shadows: ", (light.castsShadows ? "Yes" : "No"));
    Debug::log(Debug::Category::LIGHTS,
               " Shadow Map Index: ", light.shadowMapIndex);
    i++;
  }

  Debug::log(Debug::Category::LIGHTS,
             "Shadow maps: ", shadowSystem->getShadowMapCount());
}

void LightManager::createLightBuffer() {
  Debug::log(Debug::Category::LIGHTS, "LightManager: Creating light buffer");

  const VkDeviceSize bufferSize = sizeof(LightBufferObject);

  renderDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             lightBuffer, lightBufferMemory);

  vkMapMemory(renderDevice->getDevice(), lightBufferMemory, 0, bufferSize, 0,
              &lightBufferMapped);

  LightBufferObject lbo{};
  lbo.numLights = 0;
  lbo.numShadowMaps = 0;
  memcpy(lightBufferMapped, &lbo, sizeof(LightBufferObject));

  Debug::log(Debug::Category::LIGHTS, "LightManager: Light buffer created");
}