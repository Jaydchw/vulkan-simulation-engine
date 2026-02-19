#include "Shadow.h"

#include <glm/gtc/matrix_transform.hpp>

ShadowSystem::ShadowSystem(RenderDevice* device) : renderDevice(device) {
  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Constructor called");
}

ShadowSystem::~ShadowSystem() {
  try {
    Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Destructor called");
  } catch (...) {
  }
}

void ShadowSystem::init() {
  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Initializing");
  createDescriptorSetLayout();
  createDescriptorPool();
  createDescriptorSet();
  createDummyShadowMap();
  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Initialization complete");
}

uint32_t ShadowSystem::createShadowMap(uint32_t lightIndex) {
  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Creating shadow map for light index ", lightIndex);

  if (shadowMaps.size() >= MAX_SHADOW_CASTERS) {
    Debug::log(Debug::Category::SHADOWS,
               "ShadowSystem: Maximum shadow casters reached!");
    return UINT32_MAX;
  }

  ShadowMapData shadowMap{};
  shadowMap.lightIndex = lightIndex;
  shadowMap.lightSpaceMatrix = glm::mat4(1.0f);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = SHADOW_MAP_SIZE;
  imageInfo.extent.height = SHADOW_MAP_SIZE;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_D32_SFLOAT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(renderDevice->getDevice(), &imageInfo, nullptr,
                    &shadowMap.image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shadow map image!");
  }

  Debug::log(Debug::Category::SHADOWS, "  - Created shadow map image (",
             SHADOW_MAP_SIZE, "x", SHADOW_MAP_SIZE, ")");

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(renderDevice->getDevice(), shadowMap.image,
                               &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = renderDevice->findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(renderDevice->getDevice(), &allocInfo, nullptr,
                       &shadowMap.memory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate shadow map memory!");
  }

  vkBindImageMemory(renderDevice->getDevice(), shadowMap.image,
                    shadowMap.memory, 0);

  Debug::log(Debug::Category::SHADOWS, "  - Allocated shadow map memory");

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = shadowMap.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_D32_SFLOAT;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(renderDevice->getDevice(), &viewInfo, nullptr,
                        &shadowMap.imageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shadow map image view!");
  }

  Debug::log(Debug::Category::SHADOWS, "  - Created shadow map image view");

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(renderDevice->getDevice(), &samplerInfo, nullptr,
                      &shadowMap.sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shadow map sampler!");
  }

  Debug::log(Debug::Category::SHADOWS, "  - Created shadow map sampler");

  transitionImageLayout(shadowMap.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

  Debug::log(Debug::Category::SHADOWS, "  - Transitioned to read-only layout");

  shadowMaps.push_back(shadowMap);
  updateDescriptorSet();

  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Shadow map created successfully, total maps: ",
             shadowMaps.size());
  return static_cast<uint32_t>(shadowMaps.size() - 1);
}

void ShadowSystem::cleanup() {
  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Cleaning up ",
             shadowMaps.size(), " shadow maps");

  for (auto& shadowMap : shadowMaps) {
    if (shadowMap.sampler != VK_NULL_HANDLE) {
      vkDestroySampler(renderDevice->getDevice(), shadowMap.sampler, nullptr);
    }
    if (shadowMap.imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(renderDevice->getDevice(), shadowMap.imageView,
                         nullptr);
    }
    if (shadowMap.image != VK_NULL_HANDLE) {
      vkDestroyImage(renderDevice->getDevice(), shadowMap.image, nullptr);
    }
    if (shadowMap.memory != VK_NULL_HANDLE) {
      vkFreeMemory(renderDevice->getDevice(), shadowMap.memory, nullptr);
    }
  }
  shadowMaps.clear();

  if (dummyShadowMap.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(renderDevice->getDevice(), dummyShadowMap.sampler,
                     nullptr);
  }
  if (dummyShadowMap.imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(renderDevice->getDevice(), dummyShadowMap.imageView,
                       nullptr);
  }
  if (dummyShadowMap.image != VK_NULL_HANDLE) {
    vkDestroyImage(renderDevice->getDevice(), dummyShadowMap.image, nullptr);
  }
  if (dummyShadowMap.memory != VK_NULL_HANDLE) {
    vkFreeMemory(renderDevice->getDevice(), dummyShadowMap.memory, nullptr);
  }

  if (shadowDescriptorSet != VK_NULL_HANDLE) {
    shadowDescriptorSet = VK_NULL_HANDLE;
  }
  if (shadowDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(renderDevice->getDevice(), shadowDescriptorPool,
                            nullptr);
    shadowDescriptorPool = VK_NULL_HANDLE;
  }
  if (shadowDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(renderDevice->getDevice(),
                                 shadowDescriptorSetLayout, nullptr);
    shadowDescriptorSetLayout = VK_NULL_HANDLE;
  }

  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Cleanup complete");
}

void ShadowSystem::transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                                         VkImageLayout newLayout) const {
  const VkCommandBuffer commandBuffer = renderDevice->beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::runtime_error("Unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  renderDevice->endSingleTimeCommands(commandBuffer);
}

void ShadowSystem::createDescriptorSetLayout() {
  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Creating descriptor set layout");

  VkDescriptorSetLayoutBinding shadowMapBinding{};
  shadowMapBinding.binding = 0;
  shadowMapBinding.descriptorCount = MAX_SHADOW_CASTERS;
  shadowMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  shadowMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &shadowMapBinding;

  if (vkCreateDescriptorSetLayout(renderDevice->getDevice(), &layoutInfo,
                                  nullptr,
                                  &shadowDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shadow descriptor set layout!");
  }

  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Descriptor set layout created");
}

void ShadowSystem::createDescriptorPool() {
  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Creating descriptor pool");

  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = MAX_SHADOW_CASTERS;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  if (vkCreateDescriptorPool(renderDevice->getDevice(), &poolInfo, nullptr,
                             &shadowDescriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shadow descriptor pool!");
  }

  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Descriptor pool created");
}

void ShadowSystem::createDescriptorSet() {
  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Creating descriptor set");

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = shadowDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &shadowDescriptorSetLayout;

  if (vkAllocateDescriptorSets(renderDevice->getDevice(), &allocInfo,
                               &shadowDescriptorSet) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate shadow descriptor set!");
  }

  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Descriptor set created");
}

void ShadowSystem::createDummyShadowMap() {
  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Creating dummy shadow map");

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = 1;
  imageInfo.extent.height = 1;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_D32_SFLOAT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(renderDevice->getDevice(), &imageInfo, nullptr,
                    &dummyShadowMap.image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create dummy shadow map image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(renderDevice->getDevice(), dummyShadowMap.image,
                               &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = renderDevice->findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(renderDevice->getDevice(), &allocInfo, nullptr,
                       &dummyShadowMap.memory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate dummy shadow map memory!");
  }

  vkBindImageMemory(renderDevice->getDevice(), dummyShadowMap.image,
                    dummyShadowMap.memory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = dummyShadowMap.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_D32_SFLOAT;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(renderDevice->getDevice(), &viewInfo, nullptr,
                        &dummyShadowMap.imageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create dummy shadow map image view!");
  }

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  if (vkCreateSampler(renderDevice->getDevice(), &samplerInfo, nullptr,
                      &dummyShadowMap.sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create dummy shadow map sampler!");
  }

  transitionImageLayout(dummyShadowMap.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Dummy shadow map created");
}

void ShadowSystem::updateDescriptorSet() {
  Debug::log(Debug::Category::SHADOWS,
             "ShadowSystem: Updating descriptor set with ", shadowMaps.size(),
             " shadow maps");

  std::vector<VkDescriptorImageInfo> imageInfos(MAX_SHADOW_CASTERS);

  for (size_t i = 0; i < MAX_SHADOW_CASTERS; i++) {
    if (i < shadowMaps.size()) {
      imageInfos[i].imageLayout =
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      imageInfos[i].imageView = shadowMaps[i].imageView;
      imageInfos[i].sampler = shadowMaps[i].sampler;
      Debug::log(Debug::Category::SHADOWS, "  - Binding shadow map ", i,
                 " to descriptor set");
    } else {
      imageInfos[i].imageLayout =
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      imageInfos[i].imageView = dummyShadowMap.imageView;
      imageInfos[i].sampler = dummyShadowMap.sampler;
      Debug::log(Debug::Category::SHADOWS,
                 "  - Binding dummy shadow map to slot ", i);
    }
  }

  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = shadowDescriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = MAX_SHADOW_CASTERS;
  descriptorWrite.pImageInfo = imageInfos.data();

  vkUpdateDescriptorSets(renderDevice->getDevice(), 1, &descriptorWrite, 0,
                         nullptr);

  Debug::log(Debug::Category::SHADOWS, "ShadowSystem: Descriptor set updated");
}

void ShadowSystem::updateLightSpaceMatrix(uint32_t shadowMapIndex,
                                          const glm::mat4& matrix) {
  if (shadowMapIndex < shadowMaps.size()) {
    shadowMaps[shadowMapIndex].lightSpaceMatrix = matrix;
  } else {
    Debug::log(Debug::Category::SHADOWS,
               "ShadowSystem: WARNING - Invalid shadow map index ",
               shadowMapIndex, " for light space matrix update");
  }
}

glm::mat4 ShadowSystem::calculateLightSpaceMatrix(const Light& light,
                                                  const glm::vec3& sceneCenter,
                                                  float sceneRadius) const {
  glm::mat4 lightProjection;
  glm::mat4 lightView;

  if (light.getType() == LightType::Sun) {
    const float orthoSize = sceneRadius * 1.2f;
    const float nearPlane = 1.0f;
    const float farPlane = sceneRadius * 6.0f;

    glm::vec3 lightDir;
    light.getDirection(lightDir);
    const glm::vec3 lightPos =
        sceneCenter -
        glm::normalize(lightDir) * (sceneRadius * 3.0f);

    glm::vec3 upVector = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(glm::normalize(lightDir), upVector)) >
        0.99f) {
      upVector = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    lightView = glm::lookAt(lightPos, sceneCenter, upVector);
    lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize,
                                 nearPlane, farPlane);
    lightProjection[1][1] *= -1;

  } else if (light.getType() == LightType::Point) {
    constexpr float fov = glm::radians(120.0f);
    const float aspect = 1.0f;
    const float nearPlane = 0.1f;

    const float maxDistance =
        glm::sqrt(light.getIntensity() / (light.getQuadratic() * 0.01f));
    const float farPlane = glm::min(maxDistance, 500.0f);

    glm::vec3 targetPos = sceneCenter;
    glm::vec3 lightPos;
    light.getPosition(lightPos);
    const glm::vec3 toScene = targetPos - lightPos;
    if (glm::length(toScene) > 0.001f) {
      targetPos = lightPos + glm::normalize(toScene) * 10.0f;
    }

    glm::vec3 upVector = glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = glm::normalize(targetPos - lightPos);
    if (glm::abs(glm::dot(forward, upVector)) > 0.99f) {
      upVector = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    lightView = glm::lookAt(lightPos, targetPos, upVector);
    lightProjection = glm::perspective(fov, aspect, nearPlane, farPlane);
    lightProjection[1][1] *= -1;

    Debug::log(Debug::Category::SHADOWS, "Point light shadow - Pos: (",
               lightPos.x, ", ", lightPos.y, ", ",
               lightPos.z, ") Target: (", targetPos.x, ", ",
               targetPos.y, ", ", targetPos.z, ") Far: ", farPlane);
  }

  return lightProjection * lightView;
}