#pragma once
#include <vulkan/vulkan.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace RenderUtils {

constexpr const char* ENTRY_POINT_MAIN = "main";

inline void readFile(const std::string& filename, std::vector<char>& buffer) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + filename);
  }

  const size_t fileSize = static_cast<size_t>(file.tellg());
  buffer.resize(fileSize);

  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
  file.close();
}

inline VkShaderModule createShaderModule(VkDevice device,
                                         const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module!");
  }
  return shaderModule;
}

inline void createImageCreateInfo(
    VkImageCreateInfo& imageInfo, uint32_t width, uint32_t height,
    VkFormat format, VkImageUsageFlags usage, uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) {
  imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = arrayLayers;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = samples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
}

inline VkImageView createImageView(VkDevice device, VkImage image,
                                   VkFormat format,
                                   VkImageAspectFlags aspectMask,
                                   uint32_t mipLevels = 1,
                                   uint32_t layerCount = 1) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = layerCount;

  VkImageView imageView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image view!");
  }

  return imageView;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);

inline void createImageWithMemory(
    VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width,
    uint32_t height, VkFormat format, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage& image,
    VkDeviceMemory& imageMemory, uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    VkImageCreateFlags flags = 0) {
  VkImageCreateInfo imageInfo{};
  createImageCreateInfo(imageInfo, width, height, format, usage, mipLevels,
                        arrayLayers, samples, tiling);
  imageInfo.flags = flags;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      physicalDevice, memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}

inline void createGraphicsPipelineCreateInfo(
    VkGraphicsPipelineCreateInfo& pipelineInfo, VkPipelineLayout layout,
    VkRenderPass renderPass, uint32_t stageCount,
    const VkPipelineShaderStageCreateInfo* pStages,
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState,
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState,
    const VkPipelineViewportStateCreateInfo* pViewportState,
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState,
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState,
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState,
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState,
    const VkPipelineDynamicStateCreateInfo* pDynamicState,
    const VkPipelineRenderingCreateInfo* pNextRenderingInfo = nullptr) {
  pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = pNextRenderingInfo;
  pipelineInfo.stageCount = stageCount;
  pipelineInfo.pStages = pStages;
  pipelineInfo.pVertexInputState = pVertexInputState;
  pipelineInfo.pInputAssemblyState = pInputAssemblyState;
  pipelineInfo.pViewportState = pViewportState;
  pipelineInfo.pRasterizationState = pRasterizationState;
  pipelineInfo.pMultisampleState = pMultisampleState;
  pipelineInfo.pDepthStencilState = pDepthStencilState;
  pipelineInfo.pColorBlendState = pColorBlendState;
  pipelineInfo.pDynamicState = pDynamicState;
  pipelineInfo.layout = layout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;
}

inline void createInputAssemblyState(
    VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
    VkPrimitiveTopology topology) {
  inputAssembly = {};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = topology;
  inputAssembly.primitiveRestartEnable = VK_FALSE;
}

inline void createColorBlendAttachment(
    VkPipelineColorBlendAttachmentState& colorBlendAttachment) {
  colorBlendAttachment = {};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}

inline void createPipelineColorBlendStateCreateInfo(
    VkPipelineColorBlendStateCreateInfo& colorBlending,
    const VkPipelineColorBlendAttachmentState& colorBlendAttachment) {
  colorBlending = {};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
}

inline void createSamplerLayoutBinding(
    VkDescriptorSetLayoutBinding& samplerLayoutBinding, uint32_t binding,
    VkShaderStageFlags stageFlags) {
  samplerLayoutBinding = {};
  samplerLayoutBinding.binding = binding;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.stageFlags = stageFlags;
}

inline void createGraphicsPipeline(
    VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass,
    uint32_t stageCount, const VkPipelineShaderStageCreateInfo* pStages,
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState,
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState,
    const VkPipelineViewportStateCreateInfo* pViewportState,
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState,
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState,
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState,
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState,
    const VkPipelineDynamicStateCreateInfo* pDynamicState, VkPipeline* pipeline,
    const VkPipelineRenderingCreateInfo* pNextRenderingInfo = nullptr) {
  VkGraphicsPipelineCreateInfo pipelineInfo{};
  createGraphicsPipelineCreateInfo(
      pipelineInfo, layout, renderPass, stageCount, pStages, pVertexInputState,
      pInputAssemblyState, pViewportState, pRasterizationState,
      pMultisampleState, pDepthStencilState, pColorBlendState, pDynamicState,
      pNextRenderingInfo);

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }
}

}  // namespace RenderUtils
