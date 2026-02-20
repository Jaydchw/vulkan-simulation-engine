#include "MeshManager.h"

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

#include "Util/Debug.h"

const VkVertexInputBindingDescription& Vertex::getBindingDescription() {
  static VkVertexInputBindingDescription const bindingDescription = {
      0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  return bindingDescription;
}

const std::array<VkVertexInputAttributeDescription, 4>&
Vertex::getAttributeDescriptions() {
  static std::array<VkVertexInputAttributeDescription, 4> const
      attributeDescriptions = {
          {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
           {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
           {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)},
           {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}}};
  return attributeDescriptions;
}

MeshManager::MeshManager(RenderDevice* renderDev)
    : renderDevice(renderDev), defaultCubeID(0) {
  Debug::log(Debug::Category::MESH, "MeshManager: Constructor called");
  meshes.push_back(std::unique_ptr<Mesh>(nullptr));
  createDefaultMeshes();
  Debug::log(Debug::Category::MESH, "MeshManager: Initialization complete");
}

MeshManager::~MeshManager() noexcept {
  try {
    Debug::log(Debug::Category::MESH, "MeshManager: Destructor called");
    cleanup();
  } catch (...) {
  }
}

MeshID MeshManager::createCube(float size) {
  Debug::log(Debug::Category::MESH, "MeshManager: Creating cube (size: ", size,
             ")");
  Mesh* const mesh = new Mesh();
  mesh->setName("Cube");
  mesh->setType(MeshType::Cube);

  const float h = size * 0.5f;

  const std::vector<Vertex> vertices = {
      {{-h, -h, h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
      {{h, -h, h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
      {{h, h, h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
      {{-h, h, h}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},

      {{h, -h, -h}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
      {{-h, -h, -h}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
      {{-h, h, -h}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
      {{h, h, -h}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},

      {{-h, -h, -h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
      {{-h, -h, h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
      {{-h, h, h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
      {{-h, h, -h}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},

      {{h, -h, h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
      {{h, -h, -h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
      {{h, h, -h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
      {{h, h, h}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},

      {{-h, h, h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
      {{h, h, h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
      {{h, h, -h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
      {{-h, h, -h}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},

      {{-h, -h, -h}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
      {{h, -h, -h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
      {{h, -h, h}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
      {{-h, -h, h}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}};

  mesh->setVertices(vertices);

  const std::vector<uint16_t> indices = {
      0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
      12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};
  mesh->setIndices(indices);

  createBuffers(mesh);
  const MeshID id = registerMesh(mesh);
  Debug::log(Debug::Category::MESH, "MeshManager: Created cube with ID: ", id);
  return id;
}

MeshID MeshManager::createSphere(float radius, uint32_t segments) {
  Debug::log(Debug::Category::MESH,
             "MeshManager: Creating sphere (radius: ", radius,
             ", segments: ", segments, ")");
  Mesh* const mesh = new Mesh();
  mesh->setName("Sphere");
  mesh->setType(MeshType::Sphere);

  const uint32_t sliceCount = segments;
  const uint32_t stackCount = segments;

  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;

  Vertex topVertex{};
  topVertex.pos = glm::vec3(0.0f, radius, 0.0f);
  topVertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
  topVertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
  topVertex.texCoord = glm::vec2(0.0f, 0.0f);
  vertices.push_back(topVertex);

  const float phiStep = glm::pi<float>() / stackCount;
  const float thetaStep = 2.0f * glm::pi<float>() / sliceCount;

  for (uint32_t i = 1; i <= stackCount - 1; ++i) {
    const float phi = i * phiStep;
    for (uint32_t j = 0; j <= sliceCount; ++j) {
      const float theta = j * thetaStep;
      Vertex v{};
      v.pos.x = radius * sinf(phi) * cosf(theta);
      v.pos.y = radius * cosf(phi);
      v.pos.z = radius * sinf(phi) * sinf(theta);
      v.color = glm::vec3(1.0f, 1.0f, 1.0f);
      v.normal = glm::normalize(v.pos);
      v.texCoord = glm::vec2(static_cast<float>(j) / sliceCount,
                             static_cast<float>(i) / stackCount);
      vertices.push_back(v);
    }
  }

  Vertex bottomVertex{};
  bottomVertex.pos = glm::vec3(0.0f, -radius, 0.0f);
  bottomVertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
  bottomVertex.normal = glm::vec3(0.0f, -1.0f, 0.0f);
  bottomVertex.texCoord = glm::vec2(0.0f, 1.0f);
  vertices.push_back(bottomVertex);

  for (uint32_t i = 1; i <= sliceCount; ++i) {
    indices.push_back(0);
    indices.push_back(i + 1);
    indices.push_back(i);
  }

  const uint32_t baseIndex = 1;
  const uint32_t ringVertexCount = sliceCount + 1;
  for (uint32_t i = 0; i < stackCount - 2; ++i) {
    for (uint32_t j = 0; j < sliceCount; ++j) {
      indices.push_back(baseIndex + i * ringVertexCount + j);
      indices.push_back(baseIndex + i * ringVertexCount + j + 1);
      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
      indices.push_back(baseIndex + i * ringVertexCount + j + 1);
      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
    }
  }

  const uint32_t southPoleIndex = static_cast<uint32_t>(vertices.size()) - 1;
  const uint32_t southBaseIndex = southPoleIndex - ringVertexCount;
  for (uint32_t i = 0; i < sliceCount; ++i) {
    indices.push_back(southPoleIndex);
    indices.push_back(southBaseIndex + i);
    indices.push_back(southBaseIndex + i + 1);
  }

  mesh->setVertices(vertices);
  mesh->setIndices(indices);
  createBuffers(mesh);
  const MeshID id = registerMesh(mesh);
  Debug::log(Debug::Category::MESH,
             "MeshManager: Created sphere with ID: ", id);
  return id;
}

MeshID MeshManager::createPlane(float width, float height) {
  Debug::log(Debug::Category::MESH,
             "MeshManager: Creating plane (width: ", width,
             ", height: ", height, ")");
  Mesh* const mesh = new Mesh();
  mesh->setName("Plane");
  mesh->setType(MeshType::Plane);

  const float halfW = width * 0.5f;
  const float halfH = height * 0.5f;

  const std::vector<Vertex> vertices = {
      {{-halfW, 0.0f, -halfH},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f},
       {0.0f, 1.0f, 0.0f}},
      {{halfW, 0.0f, -halfH},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f},
       {0.0f, 1.0f, 0.0f}},
      {{halfW, 0.0f, halfH},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f}},
      {{-halfW, 0.0f, halfH},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f},
       {0.0f, 1.0f, 0.0f}},
  };

  mesh->setVertices(vertices);
  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};
  mesh->setIndices(indices);

  createBuffers(mesh);
  const MeshID id = registerMesh(mesh);
  Debug::log(Debug::Category::MESH, "MeshManager: Created plane with ID: ", id);
  return id;
}

MeshID MeshManager::createCylinder(float radius, float height,
                                   uint32_t segments) {
  Debug::log(Debug::Category::MESH, "MeshManager: Creating cylinder");
  Mesh* const mesh = new Mesh();
  mesh->setName("Cylinder");
  mesh->setType(MeshType::Cylinder);

  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;

  const uint32_t ringCount = 2;
  for (uint32_t i = 0; i < ringCount; ++i) {
    float y = -0.5f * height + i * height;
    for (uint32_t j = 0; j <= segments; ++j) {
      float theta = 2.0f * 3.14159f * float(j) / float(segments);
      float x = radius * cos(theta);
      float z = radius * sin(theta);
      Vertex v{};
      v.pos = glm::vec3(x, y, z);
      v.normal = glm::normalize(glm::vec3(x, 0.0f, z));
      v.texCoord = glm::vec2(float(j) / segments, float(i));
      v.color = glm::vec3(1.0f);
      vertices.push_back(v);
    }
  }

  for (uint32_t j = 0; j < segments; ++j) {
    indices.push_back(j);
    indices.push_back(j + segments + 1);
    indices.push_back(j + 1);
    indices.push_back(j + 1);
    indices.push_back(j + segments + 1);
    indices.push_back(j + segments + 2);
  }

  mesh->setVertices(vertices);
  mesh->setIndices(indices);
  createBuffers(mesh);
  return registerMesh(mesh);
}

MeshID MeshManager::createParticleQuad() {
  Debug::log(Debug::Category::MESH, "MeshManager: Creating particle quad");
  Mesh* const mesh = new Mesh();
  mesh->setName("Particle Quad");
  mesh->setType(MeshType::Plane);

  const std::vector<Vertex> vertices = {
      {{-0.5f, -0.5f, 0.0f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.0f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f},
       {0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, 0.0f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.0f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f},
       {0.0f, 0.0f, 1.0f}},
  };
  mesh->setVertices(vertices);
  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};
  mesh->setIndices(indices);

  createBuffers(mesh);
  const MeshID id = registerMesh(mesh);
  return id;
}

MeshID MeshManager::createPyramid(float baseSize, float height) {
  Debug::log(Debug::Category::MESH, "MeshManager: Creating pyramid");
  Mesh* const mesh = new Mesh();
  mesh->setName("Pyramid");
  mesh->setType(MeshType::Custom);

  const float h = height;
  const float b = baseSize * 0.5f;

  // Apex at top, base centered at origin
  glm::vec3 apex(0.0f, h, 0.0f);
  glm::vec3 bl(-b, 0.0f, -b);
  glm::vec3 br( b, 0.0f, -b);
  glm::vec3 fr( b, 0.0f,  b);
  glm::vec3 fl(-b, 0.0f,  b);

  auto makeTri = [](glm::vec3 a, glm::vec3 b2, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b2 - a, c - a));
    Vertex va{}; va.pos = a; va.normal = n; va.color = glm::vec3(1.0f); va.texCoord = {0.5f, 0.0f};
    Vertex vb{}; vb.pos = b2; vb.normal = n; vb.color = glm::vec3(1.0f); vb.texCoord = {0.0f, 1.0f};
    Vertex vc{}; vc.pos = c; vc.normal = n; vc.color = glm::vec3(1.0f); vc.texCoord = {1.0f, 1.0f};
    return std::array<Vertex, 3>{va, vb, vc};
  };

  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  uint16_t idx = 0;

  auto addTri = [&](glm::vec3 a, glm::vec3 b2, glm::vec3 c) {
    auto tri = makeTri(a, b2, c);
    vertices.push_back(tri[0]); vertices.push_back(tri[1]); vertices.push_back(tri[2]);
    indices.push_back(idx); indices.push_back(idx + 1); indices.push_back(idx + 2);
    idx += 3;
  };

  // 4 side faces
  addTri(apex, bl, br);
  addTri(apex, br, fr);
  addTri(apex, fr, fl);
  addTri(apex, fl, bl);
  // 2 base triangles
  addTri(bl, fr, br);
  addTri(bl, fl, fr);

  mesh->setVertices(vertices);
  mesh->setIndices(indices);
  createBuffers(mesh);
  return registerMesh(mesh);
}

MeshID MeshManager::loadFromOBJ(const std::string& filepath) {
  auto it = filepathToID.find(filepath);
  if (it != filepathToID.end()) return it->second;

  Debug::log(Debug::Category::MESH, "MeshManager: Loading OBJ: ", filepath);
  std::ifstream file(filepath);
  if (!file.is_open()) return defaultCubeID;

  Mesh* const mesh = new Mesh();
  mesh->setName(filepath);
  mesh->setType(MeshType::Custom);

  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> texCoords;
  std::vector<Vertex> meshVertices;
  std::vector<uint16_t> meshIndices;

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string prefix;
    iss >> prefix;
    if (prefix == "v") {
      glm::vec3 p;
      iss >> p.x >> p.y >> p.z;
      positions.push_back(p);
    } else if (prefix == "vn") {
      glm::vec3 n;
      iss >> n.x >> n.y >> n.z;
      normals.push_back(n);
    } else if (prefix == "vt") {
      glm::vec2 t;
      iss >> t.x >> t.y;
      texCoords.push_back(t);
    } else if (prefix == "f") {
      std::string vStr;
      std::vector<uint16_t> faceIndices;
      while (iss >> vStr) {
        std::istringstream viss(vStr);
        std::string segment;
        int pIdx = 0, tIdx = 0, nIdx = 0;
        if (std::getline(viss, segment, '/')) pIdx = std::stoi(segment) - 1;
        if (std::getline(viss, segment, '/')) {
          if (!segment.empty()) tIdx = std::stoi(segment) - 1;
        }
        if (std::getline(viss, segment, '/')) {
          if (!segment.empty()) nIdx = std::stoi(segment) - 1;
        }

        Vertex v{};
        v.pos = positions[pIdx];
        v.color = glm::vec3(1.0f);
        if (!texCoords.empty()) v.texCoord = texCoords[tIdx];
        if (!normals.empty()) v.normal = normals[nIdx];
        meshVertices.push_back(v);
        faceIndices.push_back(static_cast<uint16_t>(meshVertices.size() - 1));
      }
      for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
        meshIndices.push_back(faceIndices[0]);
        meshIndices.push_back(faceIndices[i]);
        meshIndices.push_back(faceIndices[i + 1]);
      }
    }
  }
  mesh->setVertices(meshVertices);
  mesh->setIndices(meshIndices);
  createBuffers(mesh);
  const MeshID id = registerMesh(mesh);
  filepathToID[filepath] = id;
  return id;
}

Mesh* MeshManager::getMesh(MeshID id) {
  if (id >= meshes.size() || meshes[id] == nullptr)
    return meshes[defaultCubeID].get();
  return meshes[id].get();
}

const Mesh* MeshManager::getMesh(MeshID id) const {
  if (id >= meshes.size() || meshes[id] == nullptr)
    return meshes[defaultCubeID].get();
  return meshes[id].get();
}

void MeshManager::cleanup() {
  const VkDevice device = renderDevice->getDevice();
  for (auto& mesh : meshes) {
    if (mesh) {
      if (mesh->getVertexBuffer() != VK_NULL_HANDLE)
        vkDestroyBuffer(device, mesh->getVertexBuffer(), nullptr);
      if (mesh->getVertexBufferMemory() != VK_NULL_HANDLE)
        vkFreeMemory(device, mesh->getVertexBufferMemory(), nullptr);
      if (mesh->getIndexBuffer() != VK_NULL_HANDLE)
        vkDestroyBuffer(device, mesh->getIndexBuffer(), nullptr);
      if (mesh->getIndexBufferMemory() != VK_NULL_HANDLE)
        vkFreeMemory(device, mesh->getIndexBufferMemory(), nullptr);
    }
  }
  meshes.clear();
}

MeshID MeshManager::registerMesh(Mesh* mesh) {
  if (!mesh) return defaultCubeID;
  const MeshID id = static_cast<MeshID>(meshes.size());
  meshes.push_back(std::unique_ptr<Mesh>(mesh));
  return id;
}

void MeshManager::createBuffers(Mesh* mesh) const {
  const VkDevice device = renderDevice->getDevice();
  std::vector<Vertex> vertices;
  mesh->getVertices(vertices);
  std::vector<uint16_t> indices;
  mesh->getIndices(indices);

  const VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  renderDevice->createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
  memcpy(data, vertices.data(), static_cast<size_t>(vertexBufferSize));
  vkUnmapMemory(device, stagingBufferMemory);

  VkBuffer vBuf;
  VkDeviceMemory vMem;
  renderDevice->createBuffer(
      vertexBufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vBuf, vMem);
  renderDevice->copyBuffer(stagingBuffer, vBuf, vertexBufferSize);
  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
  mesh->setVertexBuffer(vBuf);
  mesh->setVertexBufferMemory(vMem);

  const VkDeviceSize indexBufferSize = sizeof(uint16_t) * indices.size();
  renderDevice->createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingBuffer, stagingBufferMemory);

  vkMapMemory(device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
  memcpy(data, indices.data(), static_cast<size_t>(indexBufferSize));
  vkUnmapMemory(device, stagingBufferMemory);

  VkBuffer iBuf;
  VkDeviceMemory iMem;
  renderDevice->createBuffer(
      indexBufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, iBuf, iMem);
  renderDevice->copyBuffer(stagingBuffer, iBuf, indexBufferSize);
  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
  mesh->setIndexBuffer(iBuf);
  mesh->setIndexBufferMemory(iMem);
}

void MeshManager::createDefaultMeshes() { defaultCubeID = createCube(1.0f); }