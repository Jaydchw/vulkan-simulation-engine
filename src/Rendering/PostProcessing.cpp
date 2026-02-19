#include "PostProcessing.h"

#include <array>
#include <stdexcept>

#include "Rendering/RenderDevice.h"
#include "Util/Debug.h"
#include "Util/RenderUtils.h"

PostProcessing::PostProcessing(RenderDevice* renderDeviceParam,
                               VkDevice deviceParam,
                               VkFormat swapchainFormatParam)
    : renderDevice(renderDeviceParam),
      device(deviceParam),
      swapchainFormat(swapchainFormatParam),
      depthFormat(VK_FORMAT_D32_SFLOAT) {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Constructor called");
}

PostProcessing::~PostProcessing() noexcept {
  try {
    Debug::log(Debug::Category::POSTPROCESSING,
               "PostProcessing: Destructor called");
  } catch (...) {
  }
}

void PostProcessing::init(VkDescriptorPool descriptorPool, uint32_t frameWidth,
                          uint32_t frameHeight) {
  Debug::log(Debug::Category::POSTPROCESSING, "PostProcessing: Initializing");
  this->width = frameWidth;
  this->height = frameHeight;
  createOffscreenResources();
  createDepthResources();
  createDescriptorSetLayout();
  createPipelines();
  createDescriptorSets(descriptorPool);
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Initialization complete");
}

void PostProcessing::cleanup() {
  Debug::log(Debug::Category::POSTPROCESSING, "PostProcessing: Cleaning up");
  cleanupOffscreenResources();
  cleanupDepthResources();
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (toonPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, toonPipeline, nullptr);
    toonPipeline = VK_NULL_HANDLE;
  }
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }
  if (descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    descriptorSetLayout = VK_NULL_HANDLE;
  }
  if (offscreenSampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, offscreenSampler, nullptr);
    offscreenSampler = VK_NULL_HANDLE;
  }
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Cleanup complete");
}

void PostProcessing::resize(uint32_t newWidth, uint32_t newHeight,
                            VkDescriptorPool) {
  Debug::log(Debug::Category::POSTPROCESSING, "PostProcessing: Resizing to ",
             newWidth, "x", newHeight);
  this->width = newWidth;
  this->height = newHeight;
  cleanupOffscreenResources();
  cleanupDepthResources();
  createOffscreenResources();
  createDepthResources();
  updateDescriptorSets();
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Resize complete");
}

void PostProcessing::beginOffscreenPass(VkCommandBuffer commandBuffer,
                                        VkImageView,
                                        const VkExtent2D& extent) const {
  VkImageMemoryBarrier2 offscreenBarrier{};
  offscreenBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  offscreenBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  offscreenBarrier.srcAccessMask = 0;
  offscreenBarrier.dstStageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  offscreenBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  offscreenBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  offscreenBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  offscreenBarrier.image = offscreenImage;
  offscreenBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkImageMemoryBarrier2 depthBarrier{};
  depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  depthBarrier.srcAccessMask = 0;
  depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthBarrier.image = this->depthImage;
  depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  depthBarrier.subresourceRange.baseMipLevel = 0;
  depthBarrier.subresourceRange.levelCount = 1;
  depthBarrier.subresourceRange.baseArrayLayer = 0;
  depthBarrier.subresourceRange.layerCount = 1;

  std::array<VkImageMemoryBarrier2, 2> barriers = {offscreenBarrier,
                                                   depthBarrier};

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount =
      static_cast<uint32_t>(barriers.size());
  dependencyInfo.pImageMemoryBarriers = barriers.data();

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = offscreenImageView;
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

  VkRenderingAttachmentInfo depthAttachment{};
  depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachment.imageView = this->depthImageView;
  depthAttachment.imageLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachment.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {{0, 0}, extent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = &depthAttachment;

  vkCmdBeginRendering(commandBuffer, &renderingInfo);
}

void PostProcessing::endOffscreenPass(VkCommandBuffer commandBuffer) const {
  vkCmdEndRendering(commandBuffer);

  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.image = offscreenImage;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &barrier;

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void PostProcessing::render(VkCommandBuffer commandBuffer,
                            VkImageView targetImageView,
                            const VkExtent2D& extent,
                            uint32_t frameIndex) const {
  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = targetImageView;
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {{0, 0}, extent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;

  vkCmdBeginRendering(commandBuffer, &renderingInfo);

  const VkPipeline pipelineToBind = useToonShader ? toonPipeline : pipeline;
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineToBind);

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.extent = extent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0,
                          nullptr);

  struct PushConstants {
    float temperature;
    float humidity;
  } pushConstants;
  pushConstants.temperature = 20.0f;
  pushConstants.humidity = 0.5f;

  vkCmdPushConstants(commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants),
                     &pushConstants);

  vkCmdDraw(commandBuffer, 3, 1, 0, 0);
  vkCmdEndRendering(commandBuffer);
}

void PostProcessing::createOffscreenResources() {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Creating offscreen resources");
  RenderUtils::createImageWithMemory(
      device, renderDevice->getPhysicalDevice(), width, height, swapchainFormat,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreenImage,
      offscreenImageMemory);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = offscreenImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = swapchainFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &offscreenImageView) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create offscreen image view!");
  }

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(device, &samplerInfo, nullptr, &offscreenSampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create offscreen sampler!");
  }
}

void PostProcessing::createDepthResources() {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Creating depth resources");
  RenderUtils::createImageWithMemory(
      device, renderDevice->getPhysicalDevice(), width, height, depthFormat,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
  depthImageView = RenderUtils::createImageView(device, depthImage, depthFormat,
                                                VK_IMAGE_ASPECT_DEPTH_BIT);
}

void PostProcessing::createDescriptorSetLayout() {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Creating descriptor set layout");
  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  RenderUtils::createSamplerLayoutBinding(samplerLayoutBinding, 0,
                                          VK_SHADER_STAGE_FRAGMENT_BIT);

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &samplerLayoutBinding;

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                  &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error(
        "failed to create post-process descriptor set layout!");
  }
}

void PostProcessing::createPipelines() {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Creating pipelines");
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 0;
  vertexInputInfo.vertexAttributeDescriptionCount = 0;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  RenderUtils::createInputAssemblyState(inputAssembly,
                                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  RenderUtils::createColorBlendAttachment(colorBlendAttachment);

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  RenderUtils::createPipelineColorBlendStateCreateInfo(colorBlending,
                                                       colorBlendAttachment);

  std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(float) * 2;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create post-process pipeline layout!");
  }

  VkPipelineRenderingCreateInfo renderingCreateInfo{};
  renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCreateInfo.colorAttachmentCount = 1;
  renderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = &renderingCreateInfo;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = VK_NULL_HANDLE;

  auto createPipelineInstance = [&](const std::string& vertPath,
                                    const std::string& fragPath,
                                    VkPipeline& outPipeline) {
    std::vector<char> vertCode;
    RenderUtils::readFile(vertPath, vertCode);
    std::vector<char> fragCode;
    RenderUtils::readFile(fragPath, fragCode);
    const VkShaderModule vertModule = createShaderModule(vertCode);
    const VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = RenderUtils::ENTRY_POINT_MAIN;

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = RenderUtils::ENTRY_POINT_MAIN;

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage,
                                                                   fragStage};
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &outPipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline for " +
                               vertPath);
    }
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
  };

  createPipelineInstance("shaders/postprocess_vert.spv",
                         "shaders/postprocess_frag.spv", pipeline);
  createPipelineInstance("shaders/toon_vert.spv", "shaders/toon_frag.spv",
                         toonPipeline);
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Pipelines created");
}

void PostProcessing::createDescriptorSets(VkDescriptorPool descriptorPool) {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Creating descriptor sets");
  std::vector<VkDescriptorSetLayout> layouts(2, descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
  allocInfo.pSetLayouts = layouts.data();

  descriptorSets.resize(2);
  if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "failed to allocate post-process descriptor sets!");
  }
  updateDescriptorSets();
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Descriptor sets created");
}

void PostProcessing::updateDescriptorSets() {
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Updating descriptor sets");
  for (size_t i = 0; i < descriptorSets.size(); i++) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = offscreenImageView;
    imageInfo.sampler = offscreenSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSets[i];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
  }
  Debug::log(Debug::Category::POSTPROCESSING,
             "PostProcessing: Descriptor sets updated");
}

void PostProcessing::cleanupOffscreenResources() {
  if (offscreenImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, offscreenImageView, nullptr);
    offscreenImageView = VK_NULL_HANDLE;
  }
  if (offscreenImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, offscreenImage, nullptr);
    offscreenImage = VK_NULL_HANDLE;
  }
  if (offscreenImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, offscreenImageMemory, nullptr);
    offscreenImageMemory = VK_NULL_HANDLE;
  }
}

void PostProcessing::cleanupDepthResources() {
  if (depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, depthImageView, nullptr);
    depthImageView = VK_NULL_HANDLE;
  }
  if (depthImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, depthImage, nullptr);
    depthImage = VK_NULL_HANDLE;
  }
  if (depthImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, depthImageMemory, nullptr);
    depthImageMemory = VK_NULL_HANDLE;
  }
}

VkShaderModule PostProcessing::createShaderModule(
    const std::vector<char>& code) const {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
  return shaderModule;
}