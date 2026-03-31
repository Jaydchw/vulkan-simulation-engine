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

  // Creates a buffer and a sub-allocation from the VMA pool.
  // Pass mappedData != nullptr to enable persistent host-mapped access
  // (equivalent to the old vkMapMemory pattern).
  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer& buffer, VmaAllocation& allocation,
                    void** mappedData = nullptr) const;

  // Destroys a buffer and releases its VMA allocation.
  void destroyBuffer(VkBuffer buffer, VmaAllocation allocation) const;

  // Creates an image and a sub-allocation from the VMA pool.
  void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                   VkFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                   VkImage& image, VmaAllocation& allocation,
                   uint32_t arrayLayers = 1,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                   VkImageCreateFlags flags = 0) const;

  // Destroys an image and releases its VMA allocation.
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
