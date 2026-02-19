#pragma once
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <vector>

#include "VulkanDevice.h"

namespace Vulkan {

inline VkCommandPool createCommandPool(VkDevice device,
                                       VkPhysicalDevice physicalDevice,
                                       VkSurfaceKHR surface) {
  QueueFamilyIndices queueFamilyIndices;
  findQueueFamilies(physicalDevice, surface, queueFamilyIndices);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  VkCommandPool commandPool;
  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool!");
  }

  return commandPool;
}

inline void createCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                 uint32_t maxFramesInFlight,
                                 std::vector<VkCommandBuffer>& commandBuffers) {
  commandBuffers.resize(maxFramesInFlight);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffers!");
  }
}

}  // namespace Vulkan
