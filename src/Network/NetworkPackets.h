#pragma once
#include <cstdint>

constexpr uint16_t NETWORK_UDP_PORT = 45000;
constexpr uint16_t NETWORK_TCP_PORT = 45001;
constexpr uint16_t NETWORK_TCP_MAX_PORT = 45020;
constexpr uint32_t NETWORK_MAGIC = 0x56534D31;
constexpr uint8_t NETWORK_MAX_PEERS = 4;

enum class PacketType : uint8_t {
  HELLO = 0x01,
  OBJECT_STATES = 0x02,
  PING = 0x03,
  PONG = 0x04,
  LOAD_SCENE = 0x05,
  SIM_STATE = 0x06,
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

struct ObjectStateEntry {
  uint32_t entityId;
  float posX, posY, posZ;
  float rotW, rotX, rotY, rotZ;
};

struct ObjectStatesBatchHeader {
  uint8_t senderPeerId;
  uint32_t count;
};

struct SimStatePayload {
  uint8_t isPaused;
  float timeSpeed;
  int32_t historyIndex;
  uint8_t stepForward;
  uint8_t reversePlay;
  uint8_t colorByOwner;
};

#pragma pack(pop)
