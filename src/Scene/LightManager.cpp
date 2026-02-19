#include "LightManager.h"

LightManager::LightManager(RenderDevice* rd)
    : lights(),
      shadowSystem(nullptr),
      renderDevice(rd),
      lightBufferMapped(nullptr),
      lightBuffer(VK_NULL_HANDLE),
      lightBufferMemory(VK_NULL_HANDLE) {
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

LightID LightManager::addLight(const Light& light) {
  if (lights.size() >= MAX_LIGHTS) {
    Debug::log(Debug::Category::LIGHTS,
               "LightManager: Maximum light count reached!");
    return INVALID_LIGHT_ID;
  }

  std::string name;
  light.getName(name);
  Debug::log(Debug::Category::LIGHTS, "LightManager: Adding light '", name,
             "'");

  LightID id = static_cast<LightID>(lights.size() + 1);
  lights.push_back(std::make_unique<Light>(light));

  std::vector<ShadowMapData> shadowMaps;
  shadowSystem->getShadowMaps(shadowMaps);

  if (light.getCastsShadows() && shadowMaps.size() < MAX_SHADOW_CASTERS) {
    uint32_t shadowMapIndex =
        shadowSystem->createShadowMap(static_cast<uint32_t>(lights.size() - 1));
    lights.back()->setShadowMapIndex(shadowMapIndex);
    Debug::log(Debug::Category::LIGHTS,
               " - Created shadow map with index: ", shadowMapIndex);
  }

  updateLightBuffer();

  Debug::log(Debug::Category::LIGHTS,
             "LightManager: Successfully added light with ID: ", id);
  return id;
}

Light* LightManager::getLight(LightID id) {
  if (id == INVALID_LIGHT_ID || id > lights.size()) {
    return nullptr;
  }
  return lights[id - 1].get();
}

void LightManager::updateLight(LightID id, const Light& light) {
  if (id == INVALID_LIGHT_ID || id > lights.size()) {
    return;
  }
  *lights[id - 1] = light;
  updateLightBuffer();
}

void LightManager::removeLight(LightID id) {
  if (id == INVALID_LIGHT_ID || id > lights.size()) {
    return;
  }
  lights.erase(lights.begin() + (id - 1));
  updateLightBuffer();
}

void LightManager::updateLightBuffer() {
  LightBufferObject lbo{};
  lbo.numLights = static_cast<int>(lights.size());

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

  for (size_t i = 0; i < lights.size(); i++) {
    const auto& l = *lights[i];

    glm::vec3 pos;
    l.getPosition(pos);
    lbo.lights[i].position = glm::vec4(pos, 1.0f);

    glm::vec3 dir;
    l.getDirection(dir);
    lbo.lights[i].direction = glm::vec4(dir, 0.0f);

    glm::vec3 color;
    l.getColor(color);
    lbo.lights[i].color = glm::vec4(color, 1.0f);

    lbo.lights[i].intensity = l.getIntensity();
    lbo.lights[i].constant = l.getConstant();
    lbo.lights[i].linear = l.getLinear();
    lbo.lights[i].quadratic = l.getQuadratic();

    lbo.lights[i].cutOff = l.getCutOff();
    lbo.lights[i].outerCutOff = l.getOuterCutOff();
    lbo.lights[i].type = static_cast<int>(l.getType());
    lbo.lights[i].castsShadows = l.getCastsShadows() ? 1 : 0;
    lbo.lights[i].shadowMapIndex = static_cast<int>(l.getShadowMapIndex());

    if (l.getShadowMapIndex() != UINT32_MAX) {
      lbo.lights[i].lightSpaceMatrix =
          shadowSystem->getLightSpaceMatrix(l.getShadowMapIndex());
    } else {
      lbo.lights[i].lightSpaceMatrix = glm::mat4(1.0f);
    }
  }

  memcpy(lightBufferMapped, &lbo, sizeof(LightBufferObject));
}

void LightManager::updateLightSpaceMatrix(LightID id, const glm::mat4& matrix) {
  if (id == INVALID_LIGHT_ID || id > lights.size()) {
    return;
  }

  Light* const light = lights[id - 1].get();
  if (light->getShadowMapIndex() != UINT32_MAX) {
    shadowSystem->updateLightSpaceMatrix(light->getShadowMapIndex(), matrix);
    updateLightBuffer();
  }
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

  lights.clear();

  Debug::log(Debug::Category::LIGHTS, "LightManager: Cleanup complete");
}

void LightManager::updateAllShadowMatrices(const glm::vec3& sceneCenter,
                                           float sceneRadius) {
  for (size_t i = 0; i < lights.size(); i++) {
    Light* const light = lights[i].get();

    if (light->getCastsShadows() && light->getShadowMapIndex() != UINT32_MAX) {
      const glm::mat4 lightSpaceMatrix =
          shadowSystem->calculateLightSpaceMatrix(*light, sceneCenter,
                                                  sceneRadius);

      shadowSystem->updateLightSpaceMatrix(light->getShadowMapIndex(),
                                           lightSpaceMatrix);
    }
  }

  updateLightBuffer();
}

void LightManager::setShadowCastingEnabled(LightID id, bool enabled) {
  if (id == INVALID_LIGHT_ID || id > lights.size()) {
    return;
  }

  Light* const light = lights[id - 1].get();
  const bool wasEnabled = light->getCastsShadows();
  light->setCastsShadows(enabled);

  if (enabled && !wasEnabled && light->getShadowMapIndex() == UINT32_MAX) {
    std::vector<ShadowMapData> shadowMaps;
    shadowSystem->getShadowMaps(shadowMaps);
    if (shadowMaps.size() < MAX_SHADOW_CASTERS) {
      uint32_t shadowMapIndex =
          shadowSystem->createShadowMap(static_cast<uint32_t>(id - 1));
      light->setShadowMapIndex(shadowMapIndex);
      std::string name;
      light->getName(name);
      Debug::log(Debug::Category::LIGHTS,
                 "LightManager: Enabled shadows for light '", name,
                 "' with shadow map index: ", shadowMapIndex);
    }
  } else if (!enabled && wasEnabled) {
    light->setShadowMapIndex(UINT32_MAX);
    std::string name;
    light->getName(name);
    Debug::log(Debug::Category::LIGHTS,
               "LightManager: Disabled shadows for light '", name, "'");
  }

  updateLightBuffer();
}

void LightManager::debugPrintLightInfo() const {
  Debug::log(Debug::Category::LIGHTS, "=== Light System Debug Info ===");
  Debug::log(Debug::Category::LIGHTS, "Total lights: ", lights.size());

  for (size_t i = 0; i < lights.size(); i++) {
    const Light* light = lights[i].get();
    std::string name;
    light->getName(name);
    Debug::log(Debug::Category::LIGHTS, "Light ", i, ": ", name);
    Debug::log(Debug::Category::LIGHTS, " Type: ",
               (light->getType() == LightType::Sun ? "Sun" : "Point"));
    glm::vec3 pos;
    light->getPosition(pos);
    Debug::log(Debug::Category::LIGHTS, " Position: (", pos.x, ", ", pos.y,
               ", ", pos.z, ")");
    glm::vec3 dir;
    light->getDirection(dir);
    Debug::log(Debug::Category::LIGHTS, " Direction: (", dir.x, ", ", dir.y,
               ", ", dir.z, ")");
    Debug::log(Debug::Category::LIGHTS, " Intensity: ", light->getIntensity());
    Debug::log(Debug::Category::LIGHTS,
               " Casts Shadows: ", (light->getCastsShadows() ? "Yes" : "No"));
    Debug::log(Debug::Category::LIGHTS,
               " Shadow Map Index: ", light->getShadowMapIndex());
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