#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

class RenderDevice;

class DebugRenderer {
 public:
  struct Vertex {
    glm::vec3 pos;    // location 0 — VK_FORMAT_R32G32B32_SFLOAT
    glm::vec4 color;  // location 1 — VK_FORMAT_R32G32B32A32_SFLOAT
  };

  static constexpr uint32_t MAX_VERTICES = 65536;

  void init(VkDevice device, RenderDevice* renderDevice,
            VkFormat colorFormat, int framesInFlight);
  void cleanup();

  void begin(uint32_t frameIndex);

  void addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

  void addWireSphere(const glm::vec3& center, float radius,
                     const glm::quat& orientation, const glm::vec4& color,
                     int segs = 24);

  void addWireBox(const glm::vec3& center, const glm::vec3& halfExtents,
                  const glm::quat& orientation, const glm::vec4& color);

  void addWireCapsule(const glm::vec3& center, float radius, float halfHeight,
                      const glm::quat& orientation, const glm::vec4& color,
                      int segs = 16);

  void addWireCylinder(const glm::vec3& center, float radius, float halfHeight,
                       const glm::quat& orientation, const glm::vec4& color,
                       int segs = 16);

  void addArrow(const glm::vec3& from, const glm::vec3& to,
                const glm::vec4& color);

  void render(VkCommandBuffer cmd, const glm::mat4& viewProj,
              uint32_t frameIndex, VkExtent2D extent);

 private:
  VkDevice       device       = VK_NULL_HANDLE;
  RenderDevice*  renderDevice = nullptr;
  VkPipeline     pipeline     = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

  std::vector<VkBuffer>      vertexBuffers;
  std::vector<VmaAllocation> vertexAllocs;
  std::vector<Vertex*>       mappedPtrs;
  std::vector<uint32_t>      vertexCounts;

  uint32_t currentFrame = 0;
};
