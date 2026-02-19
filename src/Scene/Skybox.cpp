#include "Skybox.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

#include "../stb_image.h"
#include "Resources/Object.h"
#include "Util/Debug.h"

void Skybox::render(VkCommandBuffer const commandBuffer,
                    VkDescriptorSet cameraDescriptorSet,
                    const VkExtent2D& extent, const Object* domeObject,
                    float timeOfDay, float sunIntensity) const {
  if (domeObject && !domeObject->isVisible()) return;

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.extent = extent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  std::array<VkDescriptorSet, 2> descriptorSets = {cameraDescriptorSet,
                                                   descriptorSet};
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 2, descriptorSets.data(), 0,
                          nullptr);

  struct SkyboxPushConstants {
    alignas(16) glm::mat4 model;
    alignas(16) glm::vec3 domeCenter;
    alignas(4) float domeRadiusSquared;
    alignas(4) float timeOfDay;
    alignas(4) float sunIntensity;
    alignas(4) float padding1;
    alignas(4) float padding2;
  } pushConstants;

  pushConstants.model = glm::mat4(1.0f);
  pushConstants.domeCenter = glm::vec3(0.0f, 0.0f, 0.0f);
  pushConstants.domeRadiusSquared = SKYBOX_RADIUS * SKYBOX_RADIUS;
  pushConstants.timeOfDay = timeOfDay;
  pushConstants.sunIntensity = sunIntensity;
  pushConstants.padding1 = 0.0f;
  pushConstants.padding2 = 0.0f;

  vkCmdPushConstants(commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(SkyboxPushConstants), &pushConstants);

  std::array<VkBuffer, 1> vertexBuffers = {vertexBuffer};
  std::array<VkDeviceSize, 1> offsets = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers.data(),
                         offsets.data());
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0,
                   0, 0);
}

void Skybox::loadCubemap(const std::string& folderPath) {
  Debug::log(Debug::Category::SKYBOX, "Skybox: Loading cubemap from ",
             folderPath);

  const std::array<std::string, 6> faceFiles = {
      folderPath + "/px.jpg", folderPath + "/nx.jpg", folderPath + "/py.jpg",
      folderPath + "/ny.jpg", folderPath + "/nz.jpg", folderPath + "/pz.jpg"};

  int width = 0, height = 0;
  std::vector<unsigned char*> faceData(6);

  for (size_t i = 0; i < 6; i++) {
    int w, h, c;
    faceData[i] = stbi_load(faceFiles[i].c_str(), &w, &h, &c, STBI_rgb_alpha);

    if (!faceData[i]) {
      for (size_t j = 0; j < i; j++) {
        stbi_image_free(faceData[j]);
      }
      throw std::runtime_error("Failed to load skybox face: " + faceFiles[i]);
    }

    if (i == 0) {
      width = w;
      height = h;
    } else if (w != width || h != height) {
      for (size_t j = 0; j <= i; j++) {
        stbi_image_free(faceData[j]);
      }
      throw std::runtime_error("Skybox faces have inconsistent dimensions!");
    }
  }

  Debug::log(Debug::Category::SKYBOX, "  Cubemap dimensions: ", width, "x",
             height);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = static_cast<uint32_t>(width);
  imageInfo.extent.height = static_cast<uint32_t>(height);
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 6;
  imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

  if (vkCreateImage(device, &imageInfo, nullptr, &cubemapImage) != VK_SUCCESS) {
    for (auto* const data : faceData) stbi_image_free(data);
    throw std::runtime_error("Failed to create cubemap image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, cubemapImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &cubemapImageMemory) !=
      VK_SUCCESS) {
    for (auto* const data : faceData) stbi_image_free(data);
    throw std::runtime_error("Failed to allocate cubemap image memory!");
  }

  vkBindImageMemory(device, cubemapImage, cubemapImageMemory, 0);

  const VkDeviceSize layerSize = width * height * 4;
  const VkDeviceSize totalSize = layerSize * 6;

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  renderDevice->createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingBuffer, stagingBufferMemory);

  void* mappedData = nullptr;
  vkMapMemory(device, stagingBufferMemory, 0, totalSize, 0, &mappedData);
  for (size_t i = 0; i < 6; i++) {
    memcpy(static_cast<char*>(mappedData) + (layerSize * i), faceData[i],
           layerSize);
    stbi_image_free(faceData[i]);
  }
  vkUnmapMemory(device, stagingBufferMemory);

  const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  barrier.srcAccessMask = 0;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.image = cubemapImage;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 6;

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  std::vector<VkBufferImageCopy> copyRegions(6);
  for (size_t i = 0; i < 6; i++) {
    copyRegions[i].bufferOffset = layerSize * i;
    copyRegions[i].bufferRowLength = 0;
    copyRegions[i].bufferImageHeight = 0;
    copyRegions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegions[i].imageSubresource.mipLevel = 0;
    copyRegions[i].imageSubresource.baseArrayLayer = static_cast<uint32_t>(i);
    copyRegions[i].imageSubresource.layerCount = 1;
    copyRegions[i].imageOffset = {0, 0, 0};
    copyRegions[i].imageExtent = {static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height), 1};
  }

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, cubemapImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6,
                         copyRegions.data());

  barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  endSingleTimeCommands(commandBuffer);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  Debug::log(Debug::Category::SKYBOX, "Skybox: Cubemap loaded successfully");
}

void Skybox::createSkyboxGeometry() {
  const int segments = 64;
  const int rings = 32;
  const float radius = SKYBOX_RADIUS;

  vertices.clear();
  indices.clear();

  for (int ring = 0; ring <= rings; ++ring) {
    const float phi =
        static_cast<float>(ring) / static_cast<float>(rings) * 3.14159f;
    const float y = cos(phi);
    const float ringRadius = sin(phi);

    for (int seg = 0; seg <= segments; ++seg) {
      const float theta = static_cast<float>(seg) /
                          static_cast<float>(segments) * 2.0f * 3.14159f;
      const float x = ringRadius * cos(theta);
      const float z = ringRadius * sin(theta);

      vertices.push_back(glm::vec3(x * radius, y * radius, z * radius));
    }
  }

  for (int ring = 0; ring < rings; ++ring) {
    for (int seg = 0; seg < segments; ++seg) {
      int current = ring * (segments + 1) + seg;
      int next = current + segments + 1;

      indices.push_back(current);
      indices.push_back(current + 1);
      indices.push_back(next);

      indices.push_back(current + 1);
      indices.push_back(next + 1);
      indices.push_back(next);
    }
  }

  const VkDeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
  renderDevice->createBuffer(vertexBufferSize,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             vertexBuffer, vertexBufferMemory);

  void* vertexData = nullptr;
  vkMapMemory(device, vertexBufferMemory, 0, vertexBufferSize, 0, &vertexData);
  memcpy(vertexData, vertices.data(), vertexBufferSize);
  vkUnmapMemory(device, vertexBufferMemory);

  const VkDeviceSize indexBufferSize = sizeof(uint16_t) * indices.size();
  renderDevice->createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             indexBuffer, indexBufferMemory);

  void* indexData = nullptr;
  vkMapMemory(device, indexBufferMemory, 0, indexBufferSize, 0, &indexData);
  memcpy(indexData, indices.data(), indexBufferSize);
  vkUnmapMemory(device, indexBufferMemory);
}

uint32_t Skybox::findMemoryType(uint32_t typeFilter,
                                VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(renderDevice->getPhysicalDevice(),
                                      &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type!");
}