#pragma once
#include <cstdint>

constexpr uint16_t NETWORK_UDP_PORT = 45000;
constexpr uint16_t NETWORK_TCP_PORT = 45001;
constexpr uint16_t NETWORK_TCP_MAX_PORT = 45020;
constexpr uint32_t NETWORK_MAGIC = 0x56534D31;
constexpr uint8_t NETWORK_MAX_PEERS = 4;
constexpr float OWNERSHIP_LOSS_ISOLATION_PCT = 80.0f;

enum class PacketType : uint8_t {
  HELLO = 0x01,
  OBJECT_STATES = 0x02,
  PING = 0x03,
  PONG = 0x04,
  LOAD_SCENE = 0x05,
  SIM_STATE = 0x06,
  HANDSHAKE = 0x07,
  OBJECT_PROPERTIES = 0x08,
  SPAWN_ENTITY = 0x09,
};

#pragma pack(push, 1)

struct UDPHelloPacket {
  uint32_t magic = NETWORK_MAGIC;
  uint8_t type = static_cast<uint8_t>(PacketType::HELLO);
  uint16_t tcpPort = NETWORK_TCP_PORT;
  uint8_t pad = 0;
  uint32_t instanceId = 0;
};

struct TCPHeader {
  uint8_t type;
  uint32_t payloadSize;
};

struct ObjectBatchHeader {
  uint8_t senderPeerId;
  uint32_t count;
};

struct ObjectFastStateEntry {
  uint32_t entityId;
  float posX, posY, posZ;        // position
  float rotW, rotX, rotY, rotZ;  // orientation quaternion
  float velX, velY, velZ;        // linear velocity
};

struct ObjectPropertyEntry {
  uint32_t entityId;

  float mass;
  float restitution;
  float damping;
  uint8_t useGravity;

  uint8_t colliderType;
  uint8_t pad[2];
  float radius;
  float height;
  float halfExtX, halfExtY, halfExtZ;
  float normalX, normalY, normalZ;
  uint8_t finite;
  uint8_t pad2[3];
};

struct SimStatePayload {
  uint8_t isPaused;
  float timeSpeed;
  int32_t historyIndex;
  uint8_t stepForward;
  uint8_t reversePlay;
  uint8_t colorByOwner;
};

struct HandshakePayload {
  uint32_t instanceId;
  uint16_t tcpPort;
};

struct SpawnEntityPacket {
  uint32_t entityId;

  float posX, posY, posZ;
  float rotW, rotX, rotY, rotZ;
  float scaleX, scaleY, scaleZ;

  float velX, velY, velZ;
  float angVelX, angVelY, angVelZ;
  float mass;
  float restitution;
  float damping;
  uint8_t useGravity;

  uint8_t colliderType;
  float radius;
  float height;
  float halfExtX, halfExtY, halfExtZ;
  float normalX, normalY, normalZ;
  uint8_t finite;

  uint8_t hasRender;
  uint8_t ownerPeerId;
  uint8_t pad[1];
  uint32_t meshId;
  uint32_t materialId;

  char name[32];
};

#pragma pack(pop)
