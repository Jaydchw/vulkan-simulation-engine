#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Rendering/RenderDevice.h"
#include "ECS/Components.h"

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 texCoord;
  glm::vec3 normal;
  static const VkVertexInputBindingDescription& getBindingDescription();
  static const std::array<VkVertexInputAttributeDescription, 4>&
  getAttributeDescriptions();
};

enum class MeshType { Cube, Sphere, Plane, Cylinder, Custom };

class Mesh final {
 public:
  Mesh() = default;
  ~Mesh() = default;
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&&) = default;
  Mesh& operator=(Mesh&&) = default;

  void getVertices(std::vector<Vertex>& outVertices) const {
    outVertices = vertices;
  }
  void setVertices(const std::vector<Vertex>& v) { vertices = v; }

  void getIndices(std::vector<uint16_t>& outIndices) const {
    outIndices = indices;
  }
  void setIndices(const std::vector<uint16_t>& i) { indices = i; }

  void getName(std::string& outName) const { outName = name; }
  void setName(const std::string& n) { name = n; }

  VkBuffer getVertexBuffer() const { return vertexBuffer; }
  void setVertexBuffer(VkBuffer buffer) { vertexBuffer = buffer; }

  VmaAllocation getVertexBufferAllocation() const { return vertexBufferAllocation; }
  void setVertexBufferAllocation(VmaAllocation alloc) { vertexBufferAllocation = alloc; }

  void* getMappedVertexData() const { return mappedVertexData; }
  void setMappedVertexData(void* ptr) { mappedVertexData = ptr; }

  VkBuffer getIndexBuffer() const { return indexBuffer; }
  void setIndexBuffer(VkBuffer buffer) { indexBuffer = buffer; }

  VmaAllocation getIndexBufferAllocation() const { return indexBufferAllocation; }
  void setIndexBufferAllocation(VmaAllocation alloc) { indexBufferAllocation = alloc; }

  MeshType getType() const { return type; }
  void setType(MeshType t) { type = t; }

  float getBoundingRadius() const { return boundingRadius; }
  void setBoundingRadius(float r) { boundingRadius = r; }

  uint32_t getIndexCount() const {
    return static_cast<uint32_t>(indices.size());
  }

 private:
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  std::string name;

  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
  void* mappedVertexData = nullptr;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;

  MeshType type = MeshType::Custom;
  float boundingRadius = 0.0f;
};

class MeshManager final {
 public:
  explicit MeshManager(RenderDevice* renderDev);
  ~MeshManager();
  MeshManager(const MeshManager&) = delete;
  MeshManager& operator=(const MeshManager&) = delete;

  MeshID createCube(float size = 1.0f);
  MeshID createSphere(float radius = 1.0f, uint32_t segments = 32);
  MeshID createPlane(float width = 1.0f, float height = 1.0f);
  MeshID createCylinder(float radius = 1.0f, float height = 2.0f,
                        uint32_t segments = 32);
  MeshID createParticleQuad();
  MeshID createPyramid(float baseSize = 0.5f, float height = 1.0f);
  MeshID createCone(float radius = 0.5f, float height = 1.0f,
                    uint32_t segments = 32);
  MeshID createCapsule(float radius = 0.5f, float height = 2.0f,
                       uint32_t segments = 32);
  MeshID loadFromOBJ(const std::string& filepath);
  MeshID createDynamicMesh(const std::vector<Vertex>& vertices,
                           const std::vector<uint16_t>& indices);
  void updateDynamicMeshVertices(MeshID id, const std::vector<Vertex>& vertices);
  Mesh* getMesh(MeshID id);
  const Mesh* getMesh(MeshID id) const;
  MeshID getDefaultCube() const { return defaultCubeID; }
  void cleanup();

 private:
  std::unordered_map<std::string, MeshID> filepathToID;
  std::vector<std::unique_ptr<Mesh>> meshes;
  RenderDevice* renderDevice;
  MeshID defaultCubeID;

  MeshID registerMesh(Mesh* mesh);
  void createBuffers(Mesh* mesh) const;
  void createDefaultMeshes();
};