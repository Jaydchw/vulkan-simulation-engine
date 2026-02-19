#pragma once
#include <vulkan/vulkan.h>

#include <vector>

namespace Vulkan {

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice,
                             const std::vector<VkFormat>& candidates,
                             VkImageTiling tiling,
                             VkFormatFeatureFlags features);

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

}  // namespace Vulkan