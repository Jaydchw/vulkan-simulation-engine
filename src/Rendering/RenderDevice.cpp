#include "RenderDevice.h"

#include <stdexcept>

RenderDevice::RenderDevice(VkInstance instance, VkDevice inDevice,
                           VkPhysicalDevice inPhysicalDevice,
                           VkCommandPool inCommandPool,
                           VkQueue inGraphicsQueue)
    : device(inDevice),
      physicalDevice(inPhysicalDevice),
      commandPool(inCommandPool),
      graphicsQueue(inGraphicsQueue) {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance       = instance;
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device         = device;
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

  if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
    throw std::runtime_error("Failed to create VMA allocator!");
}

RenderDevice::~RenderDevice() {
  if (allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(allocator);
    allocator = VK_NULL_HANDLE;
  }
}

void RenderDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkBuffer& buffer, VmaAllocation& allocation,
                                void** mappedData) const {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size        = size;
  bufferInfo.usage       = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.requiredFlags = properties;
  if (mappedData) {
    // Persistent host-visible mapping: VMA keeps the pointer alive for the
    // lifetime of the allocation (replaces the old vkMapMemory pattern).
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  VmaAllocationInfo info{};
  if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                      &buffer, &allocation, &info) != VK_SUCCESS)
    throw std::runtime_error("Failed to create VMA buffer!");

  if (mappedData) *mappedData = info.pMappedData;
}

void RenderDevice::destroyBuffer(VkBuffer buffer,
                                 VmaAllocation allocation) const {
  vmaDestroyBuffer(allocator, buffer, allocation);
}

void RenderDevice::createImage(uint32_t width, uint32_t height,
                               uint32_t mipLevels, VkFormat format,
                               VkImageTiling tiling, VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkImage& image, VmaAllocation& allocation,
                               uint32_t arrayLayers,
                               VkSampleCountFlagBits samples,
                               VkImageCreateFlags flags) const {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType     = VK_IMAGE_TYPE_2D;
  imageInfo.extent        = {width, height, 1};
  imageInfo.mipLevels     = mipLevels;
  imageInfo.arrayLayers   = arrayLayers;
  imageInfo.format        = format;
  imageInfo.tiling        = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage         = usage;
  imageInfo.samples       = samples;
  imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.flags         = flags;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.requiredFlags = properties;

  if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                     &image, &allocation, nullptr) != VK_SUCCESS)
    throw std::runtime_error("Failed to create VMA image!");
}

void RenderDevice::destroyImage(VkImage image,
                                VmaAllocation allocation) const {
  vmaDestroyImage(allocator, image, allocation);
}

void RenderDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer,
                              VkDeviceSize size) const {
  VkCommandBuffer const commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

uint32_t RenderDevice::findMemoryType(uint32_t typeFilter,
                                      VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1u << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
      return i;
  }
  throw std::runtime_error("Failed to find suitable memory type!");
}

VkCommandBuffer RenderDevice::beginSingleTimeCommands() const {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool        = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void RenderDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers    = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
