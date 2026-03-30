#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <vector>

#include "Resources/MeshManager.h"
#include "Util/RenderUtils.h"

class MainPipeline final {
 public:
  enum class ShadingMode { Phong, Gouraud };

  MainPipeline(VkDevice inDevice, VkFormat inSwapChainFormat,
               VkFormat inDepthFormat)
      : device(inDevice),
        swapChainFormat(inSwapChainFormat),
        depthFormat(inDepthFormat),
        pipeline(VK_NULL_HANDLE),
        pipelineLayout(VK_NULL_HANDLE),
        descriptorSetLayout(VK_NULL_HANDLE),
        materialDescriptorSetLayout(VK_NULL_HANDLE),
        shadowDescriptorSetLayout(VK_NULL_HANDLE),
        instanceDescriptorSetLayout(VK_NULL_HANDLE),
        currentPolygonMode(VK_POLYGON_MODE_FILL),
        currentShadingMode(ShadingMode::Phong) {}

  ~MainPipeline() {
    try {
      cleanup();
    } catch (...) {
    }
  }

  MainPipeline(const MainPipeline&) = delete;
  MainPipeline& operator=(const MainPipeline&) = delete;

  void create(VkDescriptorSetLayout inDescriptorSetLayout,
              VkDescriptorSetLayout inMaterialDescriptorSetLayout,
              VkDescriptorSetLayout inShadowDescriptorSetLayout,
              VkDescriptorSetLayout inInstanceDescriptorSetLayout) {
    descriptorSetLayout = inDescriptorSetLayout;
    materialDescriptorSetLayout = inMaterialDescriptorSetLayout;
    shadowDescriptorSetLayout = inShadowDescriptorSetLayout;
    instanceDescriptorSetLayout = inInstanceDescriptorSetLayout;
    createPipeline();
  }

  void recreate() {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    createPipeline();
  }

  void cleanup() {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }
  }

  VkPipeline getPipeline() const { return pipeline; }
  VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
  void setPolygonMode(VkPolygonMode mode) { currentPolygonMode = mode; }
  VkPolygonMode getPolygonMode() const { return currentPolygonMode; }
  void setShadingMode(ShadingMode mode) { currentShadingMode = mode; }
  ShadingMode getShadingMode() const { return currentShadingMode; }

 private:
  VkDevice device;
  VkFormat swapChainFormat;
  VkFormat depthFormat;
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSetLayout materialDescriptorSetLayout;
  VkDescriptorSetLayout shadowDescriptorSetLayout;
  VkDescriptorSetLayout instanceDescriptorSetLayout;
  VkPolygonMode currentPolygonMode;
  ShadingMode currentShadingMode;

  void createPipeline() {
    std::vector<char> vertShaderCode;
    RenderUtils::readFile("shaders/vert.spv", vertShaderCode);
    std::vector<char> fragShaderCode;
    RenderUtils::readFile("shaders/frag.spv", fragShaderCode);

    const VkShaderModule vertShaderModule =
        RenderUtils::createShaderModule(device, vertShaderCode);
    const VkShaderModule fragShaderModule =
        RenderUtils::createShaderModule(device, fragShaderCode);

    uint32_t shadingModeValue =
        (currentShadingMode == ShadingMode::Phong) ? 0 : 1;
    VkSpecializationMapEntry specializationMapEntry{};
    specializationMapEntry.constantID = 0;
    specializationMapEntry.offset = 0;
    specializationMapEntry.size = sizeof(uint32_t);
    VkSpecializationInfo specializationInfo{};
    specializationInfo.mapEntryCount = 1;
    specializationInfo.pMapEntries = &specializationMapEntry;
    specializationInfo.dataSize = sizeof(uint32_t);
    specializationInfo.pData = &shadingModeValue;

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = RenderUtils::ENTRY_POINT_MAIN;
    vertShaderStageInfo.pSpecializationInfo = &specializationInfo;

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = RenderUtils::ENTRY_POINT_MAIN;
    fragShaderStageInfo.pSpecializationInfo = &specializationInfo;

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = currentPolygonMode;
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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    RenderUtils::createPipelineColorBlendStateCreateInfo(colorBlending,
                                                         colorBlendAttachment);

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // set=0: frame UBO+lights, set=1: material, set=2: shadow maps,
    // set=3: per-instance SSBO (replaces push constants for the main pass)
    std::array<VkDescriptorSetLayout, 4> layouts = {
        descriptorSetLayout, materialDescriptorSetLayout,
        shadowDescriptorSetLayout, instanceDescriptorSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (pipelineLayout == VK_NULL_HANDLE) {
      if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                 &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
      }
    }

    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChainFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertShaderStageInfo, fragShaderStageInfo};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    RenderUtils::createInputAssemblyState(inputAssembly,
                                          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    RenderUtils::createGraphicsPipeline(
        device, pipelineLayout, VK_NULL_HANDLE, 2, shaderStages.data(),
        &vertexInputInfo, &inputAssembly, &viewportState, &rasterizer,
        &multisampling, &depthStencil, &colorBlending, &dynamicState, &pipeline,
        &renderingCreateInfo);

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
  }
};