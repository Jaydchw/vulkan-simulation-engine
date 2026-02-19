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
  if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
  if (toonPipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(device, toonPipeline, nullptr);
  if (pipelineLayout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  if (descriptorSetLayout != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  if (offscreenSampler != VK_NULL_HANDLE)
    vkDestroySampler(device, offscreenSampler, nullptr);
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
                                        const VkExtent2D& extent,
                                        const glm::vec4& clearColor) const {
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
  VkClearColorValue cv;
  cv.float32[0] = clearColor.r;
  cv.float32[1] = clearColor.g;
  cv.float32[2] = clearColor.b;
  cv.float32[3] = clearColor.a;
  colorAttachment.clearValue.color = cv;

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

  const VkPipeline pipelineToBind = config.useToon ? toonPipeline : pipeline;
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
    float hue;
    float saturation;
    float contrast;
  } pushConstants;
  pushConstants.hue = config.hue;
  pushConstants.saturation = config.saturation;
  pushConstants.contrast = config.contrast;

  vkCmdPushConstants(commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants),
                     &pushConstants);
  vkCmdDraw(commandBuffer, 3, 1, 0, 0);
  vkCmdEndRendering(commandBuffer);
}

void PostProcessing::createOffscreenResources() {
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
  viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCreateImageView(device, &viewInfo, nullptr, &offscreenImageView);
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(device, &samplerInfo, nullptr, &offscreenSampler);
}

void PostProcessing::createDepthResources() {
  RenderUtils::createImageWithMemory(
      device, renderDevice->getPhysicalDevice(), width, height, depthFormat,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
  depthImageView = RenderUtils::createImageView(device, depthImage, depthFormat,
                                                VK_IMAGE_ASPECT_DEPTH_BIT);
}

void PostProcessing::createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  RenderUtils::createSamplerLayoutBinding(samplerLayoutBinding, 0,
                                          VK_SHADER_STAGE_FRAGMENT_BIT);
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &samplerLayoutBinding;
  vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                              &descriptorSetLayout);
}

void PostProcessing::createPipelines() {
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  RenderUtils::createInputAssemblyState(inputAssembly,
                                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  VkPipelineViewportStateCreateInfo viewportState{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rasterizer{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  VkPipelineMultisampleStateCreateInfo multisampling{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depthStencil{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  RenderUtils::createColorBlendAttachment(colorBlendAttachment);
  VkPipelineColorBlendStateCreateInfo colorBlending{};
  RenderUtils::createPipelineColorBlendStateCreateInfo(colorBlending,
                                                       colorBlendAttachment);
  std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.size = sizeof(float) * 3;
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
  VkPipelineRenderingCreateInfo renderingCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  renderingCreateInfo.colorAttachmentCount = 1;
  renderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
  VkGraphicsPipelineCreateInfo pipelineInfo{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
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
  auto createPipelineInstance = [&](const std::string& vertPath,
                                    const std::string& fragPath,
                                    VkPipeline& outPipeline) {
    std::vector<char> vertCode;
    RenderUtils::readFile(vertPath, vertCode);
    std::vector<char> fragCode;
    RenderUtils::readFile(fragPath, fragCode);
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);
    VkPipelineShaderStageCreateInfo vertStage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragStage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                              &outPipeline);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
  };
  createPipelineInstance("shaders/postprocess_vert.spv",
                         "shaders/postprocess_frag.spv", pipeline);
  createPipelineInstance("shaders/toon_vert.spv", "shaders/toon_frag.spv",
                         toonPipeline);
}

void PostProcessing::createDescriptorSets(VkDescriptorPool descriptorPool) {
  std::vector<VkDescriptorSetLayout> layouts(2, descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 2;
  allocInfo.pSetLayouts = layouts.data();
  descriptorSets.resize(2);
  vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data());
  updateDescriptorSets();
}

void PostProcessing::updateDescriptorSets() {
  for (size_t i = 0; i < descriptorSets.size(); i++) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = offscreenImageView;
    imageInfo.sampler = offscreenSampler;
    VkWriteDescriptorSet descriptorWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = descriptorSets[i];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
  }
}

void PostProcessing::cleanupOffscreenResources() {
  if (offscreenImageView) {
    vkDestroyImageView(device, offscreenImageView, nullptr);
    offscreenImageView = VK_NULL_HANDLE;
  }
  if (offscreenImage) {
    vkDestroyImage(device, offscreenImage, nullptr);
    offscreenImage = VK_NULL_HANDLE;
  }
  if (offscreenImageMemory) {
    vkFreeMemory(device, offscreenImageMemory, nullptr);
    offscreenImageMemory = VK_NULL_HANDLE;
  }
}

void PostProcessing::cleanupDepthResources() {
  if (depthImageView) {
    vkDestroyImageView(device, depthImageView, nullptr);
    depthImageView = VK_NULL_HANDLE;
  }
  if (depthImage) {
    vkDestroyImage(device, depthImage, nullptr);
    depthImage = VK_NULL_HANDLE;
  }
  if (depthImageMemory) {
    vkFreeMemory(device, depthImageMemory, nullptr);
    depthImageMemory = VK_NULL_HANDLE;
  }
}

VkShaderModule PostProcessing::createShaderModule(
    const std::vector<char>& code) const {
  VkShaderModuleCreateInfo createInfo{
      VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule shaderModule;
  vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
  return shaderModule;
}