#include "Rendering/DebugRenderer.h"
#include "Rendering/RenderDevice.h"
#include "Util/RenderUtils.h"

#include <array>
#include <cmath>
#include <stdexcept>

static constexpr float kTau = 6.28318530f;
static constexpr float kPi  = 3.14159265f;

// ─────────────────────────────────────────────────────────────────────────────
// init / cleanup
// ─────────────────────────────────────────────────────────────────────────────

void DebugRenderer::init(VkDevice dev, RenderDevice* rd,
                         VkFormat colorFormat, int framesInFlight) {
  device       = dev;
  renderDevice = rd;

  // ── Per-frame host-visible vertex buffers ───────────────────────────────
  vertexBuffers.resize(framesInFlight, VK_NULL_HANDLE);
  vertexAllocs .resize(framesInFlight, VK_NULL_HANDLE);
  mappedPtrs   .resize(framesInFlight, nullptr);
  vertexCounts .resize(framesInFlight, 0);

  const VkDeviceSize bufSize = sizeof(Vertex) * MAX_VERTICES;
  for (int i = 0; i < framesInFlight; ++i) {
    void* mapped = nullptr;
    renderDevice->createBuffer(
        bufSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexBuffers[i], vertexAllocs[i], &mapped);
    mappedPtrs[i] = static_cast<Vertex*>(mapped);
  }

  // ── Pipeline layout: one push constant (mat4 viewProj = 64 bytes) ───────
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushRange.offset     = 0;
  pushRange.size       = sizeof(glm::mat4);

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges    = &pushRange;
  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("DebugRenderer: failed to create pipeline layout");

  // ── Shaders ──────────────────────────────────────────────────────────────
  std::vector<char> vertCode, fragCode;
  RenderUtils::readFile("shaders/debug_vert.spv", vertCode);
  RenderUtils::readFile("shaders/debug_frag.spv", fragCode);
  VkShaderModule vertMod = RenderUtils::createShaderModule(device, vertCode);
  VkShaderModule fragMod = RenderUtils::createShaderModule(device, fragCode);

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertMod;
  stages[0].pName  = "main";
  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragMod;
  stages[1].pName  = "main";

  // ── Vertex input ─────────────────────────────────────────────────────────
  VkVertexInputBindingDescription binding{};
  binding.binding   = 0;
  binding.stride    = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attrs[2]{};
  attrs[0].location = 0; attrs[0].binding = 0;
  attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset   = offsetof(Vertex, pos);
  attrs[1].location = 1; attrs[1].binding = 0;
  attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
  attrs[1].offset   = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   = 1;
  vertexInput.pVertexBindingDescriptions      = &binding;
  vertexInput.vertexAttributeDescriptionCount = 2;
  vertexInput.pVertexAttributeDescriptions    = attrs;

  // ── Input assembly: line list ─────────────────────────────────────────────
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

  // ── Viewport (dynamic) ───────────────────────────────────────────────────
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount  = 1;

  // ── Rasterizer ───────────────────────────────────────────────────────────
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth   = 1.0f;  // wideLines is optional; 1.0 is always valid
  rasterizer.cullMode    = VK_CULL_MODE_NONE;
  rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  // ── Multisampling ────────────────────────────────────────────────────────
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // ── Depth stencil: test yes, write no (overlay on scene geometry) ────────
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable  = VK_TRUE;
  depthStencil.depthWriteEnable = VK_FALSE;
  depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

  // ── Color blend: alpha blend ─────────────────────────────────────────────
  VkPipelineColorBlendAttachmentState blendAttach{};
  blendAttach.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blendAttach.blendEnable         = VK_TRUE;
  blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
  blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments    = &blendAttach;

  // ── Dynamic states ───────────────────────────────────────────────────────
  std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                  VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates    = dynamicStates.data();

  // ── Dynamic rendering attachment info ────────────────────────────────────
  // Must match PostProcessing's offscreen pass (swapchainFormat color,
  // VK_FORMAT_D32_SFLOAT depth — hardcoded in PostProcessing.cpp).
  VkPipelineRenderingCreateInfo renderingCI{};
  renderingCI.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCI.colorAttachmentCount    = 1;
  renderingCI.pColorAttachmentFormats = &colorFormat;
  renderingCI.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

  // ── Assemble pipeline ─────────────────────────────────────────────────────
  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext               = &renderingCI;
  pipelineInfo.stageCount          = 2;
  pipelineInfo.pStages             = stages;
  pipelineInfo.pVertexInputState   = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState      = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState   = &multisampling;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.pColorBlendState    = &colorBlending;
  pipelineInfo.pDynamicState       = &dynamicState;
  pipelineInfo.layout              = pipelineLayout;
  pipelineInfo.renderPass          = VK_NULL_HANDLE;  // dynamic rendering

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &pipeline) != VK_SUCCESS)
    throw std::runtime_error("DebugRenderer: failed to create pipeline");

  vkDestroyShaderModule(device, vertMod, nullptr);
  vkDestroyShaderModule(device, fragMod, nullptr);
}

void DebugRenderer::cleanup() {
  if (pipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(device, pipeline, nullptr);
  if (pipelineLayout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  for (size_t i = 0; i < vertexBuffers.size(); ++i)
    if (vertexBuffers[i] != VK_NULL_HANDLE)
      renderDevice->destroyBuffer(vertexBuffers[i], vertexAllocs[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry builders
// ─────────────────────────────────────────────────────────────────────────────

void DebugRenderer::begin(uint32_t frameIndex) {
  currentFrame              = frameIndex;
  vertexCounts[frameIndex]  = 0;
}

void DebugRenderer::addLine(const glm::vec3& a, const glm::vec3& b,
                             const glm::vec4& color) {
  const uint32_t fi = currentFrame;
  if (vertexCounts[fi] + 2 > MAX_VERTICES) return;
  mappedPtrs[fi][vertexCounts[fi]++] = {a, color};
  mappedPtrs[fi][vertexCounts[fi]++] = {b, color};
}

void DebugRenderer::addWireSphere(const glm::vec3& center, float radius,
                                   const glm::quat& orientation,
                                   const glm::vec4& color, int segs) {
  const glm::mat3 rot(orientation);
  // Three great-circle rings: XZ (equator), XY (front), YZ (side)
  for (int plane = 0; plane < 3; ++plane) {
    glm::vec3 prev{};
    bool first = true;
    for (int i = 0; i <= segs; ++i) {
      const float a  = static_cast<float>(i) / segs * kTau;
      const float ca = std::cos(a), sa = std::sin(a);
      glm::vec3 local;
      if      (plane == 0) local = {ca, 0.f, sa};   // XZ
      else if (plane == 1) local = {ca, sa, 0.f};   // XY
      else                 local = {0.f, ca, sa};   // YZ
      const glm::vec3 p = center + rot * (local * radius);
      if (!first) addLine(prev, p, color);
      prev  = p;
      first = false;
    }
  }
}

void DebugRenderer::addWireBox(const glm::vec3& center,
                                const glm::vec3& halfExtents,
                                const glm::quat& orientation,
                                const glm::vec4& color) {
  const glm::mat3 rot(orientation);
  const glm::vec3& he = halfExtents;
  glm::vec3 c[8] = {
    center + rot * glm::vec3(-he.x, -he.y, -he.z),
    center + rot * glm::vec3(+he.x, -he.y, -he.z),
    center + rot * glm::vec3(+he.x, +he.y, -he.z),
    center + rot * glm::vec3(-he.x, +he.y, -he.z),
    center + rot * glm::vec3(-he.x, -he.y, +he.z),
    center + rot * glm::vec3(+he.x, -he.y, +he.z),
    center + rot * glm::vec3(+he.x, +he.y, +he.z),
    center + rot * glm::vec3(-he.x, +he.y, +he.z),
  };
  static constexpr int kEdges[12][2] = {
    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
  };
  for (const auto& e : kEdges) addLine(c[e[0]], c[e[1]], color);
}

void DebugRenderer::addWireCapsule(const glm::vec3& center, float radius,
                                    float halfHeight, const glm::quat& orientation,
                                    const glm::vec4& color, int segs) {
  const glm::mat3 rot(orientation);
  const glm::vec3 up  = rot * glm::vec3(0.f, 1.f, 0.f);
  const glm::vec3 top = center + up * halfHeight;
  const glm::vec3 bot = center - up * halfHeight;

  // Top and bottom circles
  for (int cap = 0; cap < 2; ++cap) {
    const glm::vec3& cc = (cap == 0) ? top : bot;
    glm::vec3 prev{};
    bool first = true;
    for (int i = 0; i <= segs; ++i) {
      const float a = static_cast<float>(i) / segs * kTau;
      const glm::vec3 p = cc + rot * glm::vec3(std::cos(a) * radius, 0.f, std::sin(a) * radius);
      if (!first) addLine(prev, p, color);
      prev  = p;
      first = false;
    }
  }

  // 4 vertical connecting lines
  for (int i = 0; i < 4; ++i) {
    const float a      = static_cast<float>(i) / 4.f * kTau;
    const glm::vec3 off = rot * glm::vec3(std::cos(a) * radius, 0.f, std::sin(a) * radius);
    addLine(top + off, bot + off, color);
  }

  // Hemisphere arcs at each cap (2 perpendicular arcs per cap)
  for (int cap = 0; cap < 2; ++cap) {
    const glm::vec3& cc  = (cap == 0) ? top : bot;
    const float sign     = (cap == 0) ? 1.f : -1.f;
    for (int plane = 0; plane < 2; ++plane) {
      glm::vec3 prev{};
      bool first = true;
      for (int i = 0; i <= segs / 2; ++i) {
        const float t  = static_cast<float>(i) / (segs / 2);
        const float a  = t * kPi;
        const float ca = std::cos(a) * sign;
        const float sa = std::sin(a);
        glm::vec3 local;
        if (plane == 0) local = {sa * radius, ca * radius, 0.f};
        else            local = {0.f, ca * radius, sa * radius};
        const glm::vec3 p = cc + rot * local;
        if (!first) addLine(prev, p, color);
        prev  = p;
        first = false;
      }
    }
  }
}

void DebugRenderer::addWireCylinder(const glm::vec3& center, float radius,
                                     float halfHeight, const glm::quat& orientation,
                                     const glm::vec4& color, int segs) {
  const glm::mat3 rot(orientation);
  const glm::vec3 up  = rot * glm::vec3(0.f, 1.f, 0.f);
  const glm::vec3 top = center + up * halfHeight;
  const glm::vec3 bot = center - up * halfHeight;

  // Top and bottom flat circles
  for (int cap = 0; cap < 2; ++cap) {
    const glm::vec3& cc = (cap == 0) ? top : bot;
    glm::vec3 prev{};
    bool first = true;
    for (int i = 0; i <= segs; ++i) {
      const float a = static_cast<float>(i) / segs * kTau;
      const glm::vec3 p = cc + rot * glm::vec3(std::cos(a) * radius, 0.f, std::sin(a) * radius);
      if (!first) addLine(prev, p, color);
      prev  = p;
      first = false;
    }
  }

  // 4 vertical edges
  for (int i = 0; i < 4; ++i) {
    const float a      = static_cast<float>(i) / 4.f * kTau;
    const glm::vec3 off = rot * glm::vec3(std::cos(a) * radius, 0.f, std::sin(a) * radius);
    addLine(top + off, bot + off, color);
  }
}

void DebugRenderer::addArrow(const glm::vec3& from, const glm::vec3& to,
                              const glm::vec4& color) {
  addLine(from, to, color);
  const glm::vec3 dir = to - from;
  const float len = glm::length(dir);
  if (len < 0.0001f) return;
  const glm::vec3 n = dir / len;
  // Build a perpendicular vector for the arrowhead fins
  glm::vec3 perp = (std::abs(n.x) < 0.9f) ? glm::vec3(1.f, 0.f, 0.f)
                                            : glm::vec3(0.f, 1.f, 0.f);
  perp = glm::normalize(glm::cross(n, perp));
  const float headSize = len * 0.18f;
  const glm::vec3 head = to - n * headSize;
  addLine(to, head + perp * headSize, color);
  addLine(to, head - perp * headSize, color);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void DebugRenderer::render(VkCommandBuffer cmd, const glm::mat4& viewProj,
                            uint32_t frameIndex, VkExtent2D extent) {
  const uint32_t count = vertexCounts[frameIndex];
  if (count == 0 || pipeline == VK_NULL_HANDLE) return;

  // Re-set viewport/scissor to match the offscreen pass dimensions
  VkViewport vp{0.f, 0.f,
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                0.f, 1.f};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  VkRect2D scissor{{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                     0, sizeof(glm::mat4), &viewProj);

  VkBuffer     vbs[]     = {vertexBuffers[frameIndex]};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
  vkCmdDraw(cmd, count, 1, 0, 0);
}
