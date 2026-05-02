#pragma once
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"

class RenderDevice final {
 public:
  RenderDevice(VkInstance instance, VkDevice inDevice,
               VkPhysicalDevice inPhysicalDevice,
               VkCommandPool inCommandPool, VkQueue inGraphicsQueue);
  ~RenderDevice();

  RenderDevice(const RenderDevice&) = delete;
  RenderDevice& operator=(const RenderDevice&) = delete;

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer, VmaAllocation& allocation,
                    void** mappedData = nullptr) const;

  void destroyBuffer(VkBuffer buffer, VmaAllocation allocation) const;

  void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                   VkFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                   VkImage& image, VmaAllocation& allocation,
                   uint32_t arrayLayers = 1,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                   VkImageCreateFlags flags = 0) const;

  void destroyImage(VkImage image, VmaAllocation allocation) const;

  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties) const;

  VkCommandBuffer beginSingleTimeCommands() const;
  void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

  VkDevice         getDevice()         const { return device; }
  VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
  VkCommandPool    getCommandPool()    const { return commandPool; }
  VkQueue          getGraphicsQueue()  const { return graphicsQueue; }
  VmaAllocator     getAllocator()      const { return allocator; }

 private:
  VkDevice         device;
  VkPhysicalDevice physicalDevice;
  VkCommandPool    commandPool;
  VkQueue          graphicsQueue;
  VmaAllocator     allocator = VK_NULL_HANDLE;
};
