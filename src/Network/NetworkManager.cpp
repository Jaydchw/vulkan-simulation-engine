#include "NetworkManager.h"

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>

#include "../ECS/Components.h"
#include "../ECS/Registry.h"

NetworkManager::NetworkManager() {
  WSADATA wsaData{};
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    std::cerr << "[Network] WSAStartup failed: " << WSAGetLastError() << "\n";

  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
  myInstanceId = dist(rng);
  localPeerID = 1;
}

NetworkManager::~NetworkManager() {
  shutdown();
  WSACleanup();
}

void NetworkManager::init(Registry* reg) {
  registry = reg;

  if (running) {
    std::lock_guard<std::mutex> lk(remoteStatesMutex);
    pendingRemoteStates.clear();
    return;
  }

  localIP = getLocalIPAddress();

  {
    std::lock_guard<std::mutex> lk(peersMutex);
    allKnownInstances.clear();
    allKnownInstances.push_back({myInstanceId, localIP, 0});
    localPeerID = 1;
  }

  if (!initUDP()) return;
  if (!initTCPServer()) return;

  {
    std::lock_guard<std::mutex> lk(peersMutex);
    allKnownInstances[0].tcpPort = localTCPPort;
  }

  running = true;
  networkThread = std::thread(&NetworkManager::networkThreadFunc, this);

  std::cout << "[Network] Started. IP=" << localIP << "  TCP=" << localTCPPort
            << "  instanceId=" << myInstanceId
            << "  PeerID=" << (int)localPeerID << "\n";
}

void NetworkManager::shutdown() {
  running = false;

  if (networkThread.joinable()) networkThread.join();

  {
    std::lock_guard<std::mutex> lk(peersMutex);
    for (auto& p : peers) {
      if (p.socket != INVALID_SOCKET) {
        closesocket(p.socket);
        p.socket = INVALID_SOCKET;
        p.connected = false;
      }
    }
  }

  if (udpSocket != INVALID_SOCKET) {
    closesocket(udpSocket);
    udpSocket = INVALID_SOCKET;
  }
  if (tcpListenSocket != INVALID_SOCKET) {
    closesocket(tcpListenSocket);
    tcpListenSocket = INVALID_SOCKET;
  }
}

void NetworkManager::tickReceive() {
  if (!running) return;
  applyRemoteStates();
}

void NetworkManager::tickSend() {
  if (!running) return;
  sendOwnedObjectStates();
}

void NetworkManager::assignObjectOwnership() {
  if (!registry) return;

  std::vector<Entity> dynamicEntities;
  for (const auto& [e, _] : registry->allPhysics())
    dynamicEntities.push_back(e);
  std::sort(dynamicEntities.begin(), dynamicEntities.end());

  uint8_t peerCount;
  {
    std::lock_guard<std::mutex> lk(peersMutex);
    peerCount = static_cast<uint8_t>(allKnownInstances.size());
    if (peerCount == 0) peerCount = 1;
    if (peerCount > NETWORK_MAX_PEERS) peerCount = NETWORK_MAX_PEERS;
  }

  {
    std::lock_guard<std::mutex> lk(ownershipMutex);
    ownershipMap.clear();
    for (size_t i = 0; i < dynamicEntities.size(); ++i) {
      Entity e = dynamicEntities[i];
      uint8_t owner = static_cast<uint8_t>((i % peerCount) + 1);
      ownershipMap[e] = owner;
    }
  }

  {
    std::lock_guard<std::mutex> lkp(peersMutex);
    for (auto& p : peers) p.ownedObjects = 0;
  }
  {
    std::lock_guard<std::mutex> lko(ownershipMutex);
    std::lock_guard<std::mutex> lkp(peersMutex);
    for (auto& [e, ownerId] : ownershipMap)
      for (auto& p : peers)
        if (p.id == ownerId) {
          ++p.ownedObjects;
          break;
        }
  }

  std::cout << "[Network] Assigned " << dynamicEntities.size() << " objects to "
            << (int)peerCount << " peers (I am peer " << (int)localPeerID
            << ")\n";
}

bool NetworkManager::isLocallyOwned(Entity e) const {
  if (!running || localPeerID == 0) return true;

  std::lock_guard<std::mutex> lk(ownershipMutex);
  auto it = ownershipMap.find(e);
  if (it == ownershipMap.end()) return true;
  return it->second == localPeerID;
}

uint8_t NetworkManager::getOwnerPeerID(Entity e) const {
  std::lock_guard<std::mutex> lk(ownershipMutex);
  auto it = ownershipMap.find(e);
  return (it != ownershipMap.end()) ? it->second : 0;
}

void NetworkManager::sendLoadScene(const std::string& path) {
  if (!running) return;
  broadcastTCP(PacketType::LOAD_SCENE, path.data(),
               static_cast<uint32_t>(path.size()));
}

void NetworkManager::sendSimState(bool isPaused, float timeSpeed,
                                  int32_t historyIndex, bool stepForward,
                                  int8_t reversePlay, int8_t colorByOwner) {
  if (!running) return;
  SimStatePayload payload{};
  payload.isPaused = isPaused ? 1 : 0;
  payload.timeSpeed = timeSpeed;
  payload.historyIndex = historyIndex;
  payload.stepForward = stepForward ? 1 : 0;
  payload.reversePlay =
      static_cast<uint8_t>(reversePlay < 0 ? 0xFF : reversePlay);
  payload.colorByOwner =
      static_cast<uint8_t>(colorByOwner < 0 ? 0xFF : colorByOwner);
  broadcastTCP(PacketType::SIM_STATE, &payload, sizeof(payload));
}

bool NetworkManager::pollPendingSceneLoad(std::string& outPath) {
  std::lock_guard<std::mutex> lk(commandMutex);
  if (pendingSceneLoads.empty()) return false;
  outPath = std::move(pendingSceneLoads.front());
  pendingSceneLoads.erase(pendingSceneLoads.begin());
  return true;
}

bool NetworkManager::pollPendingSimState(PendingSimState& outState) {
  std::lock_guard<std::mutex> lk(commandMutex);
  if (pendingSimStates.empty()) return false;
  outState = pendingSimStates.back();
  pendingSimStates.clear();
  return true;
}

bool NetworkManager::pollNewPeerConnected() {
  bool expected = true;
  return newPeerConnectedFlag.compare_exchange_strong(expected, false);
}

bool NetworkManager::isConnected() const {
  std::lock_guard<std::mutex> lk(peersMutex);
  for (const auto& p : peers)
    if (p.connected) return true;
  return false;
}

int NetworkManager::getConnectedPeerCount() const {
  std::lock_guard<std::mutex> lk(peersMutex);
  int n = 0;
  for (const auto& p : peers)
    if (p.connected) ++n;
  return n;
}

std::vector<PeerInfo> NetworkManager::getPeerSnapshot() const {
  std::lock_guard<std::mutex> lk(peersMutex);
  return peers;
}

int NetworkManager::getLocalOwnedCount() const {
  std::lock_guard<std::mutex> lk(ownershipMutex);
  int n = 0;
  for (const auto& [e, owner] : ownershipMap)
    if (owner == localPeerID) ++n;
  return n;
}

int NetworkManager::getTotalManagedCount() const {
  std::lock_guard<std::mutex> lk(ownershipMutex);
  return static_cast<int>(ownershipMap.size());
}

void NetworkManager::networkThreadFunc() {
  DWORD lastHelloMs = GetTickCount();
  constexpr DWORD HELLO_INTERVAL_MS = 1000;

  while (running) {
    DWORD now = GetTickCount();
    if (now - lastHelloMs >= HELLO_INTERVAL_MS) {
      sendUDPHello();
      lastHelloMs = now;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    if (udpSocket != INVALID_SOCKET) FD_SET(udpSocket, &readSet);
    if (tcpListenSocket != INVALID_SOCKET) FD_SET(tcpListenSocket, &readSet);
    {
      std::lock_guard<std::mutex> lk(peersMutex);
      for (auto& p : peers)
        if (p.socket != INVALID_SOCKET && p.connected)
          FD_SET(p.socket, &readSet);
    }

    timeval tv{0, 50000};
    if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) continue;

    if (udpSocket != INVALID_SOCKET && FD_ISSET(udpSocket, &readSet))
      receiveUDPHellos();
    if (tcpListenSocket != INVALID_SOCKET &&
        FD_ISSET(tcpListenSocket, &readSet))
      acceptTCPConnections();
    {
      std::lock_guard<std::mutex> lk(peersMutex);
      for (auto& p : peers)
        if (p.socket != INVALID_SOCKET && p.connected &&
            FD_ISSET(p.socket, &readSet))
          receiveFromPeer(p);
    }
  }
}

bool NetworkManager::initUDP() {
  udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpSocket == INVALID_SOCKET) {
    std::cerr << "[Network] UDP socket failed\n";
    return false;
  }

  BOOL bcast = TRUE, reuse = TRUE;
  setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST,
             reinterpret_cast<const char*>(&bcast), sizeof(bcast));
  setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(NETWORK_UDP_PORT);

  if (bind(udpSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    std::cerr << "[Network] UDP bind failed: " << WSAGetLastError() << "\n";
    closesocket(udpSocket);
    udpSocket = INVALID_SOCKET;
    return false;
  }
  u_long nb = 1;
  ioctlsocket(udpSocket, FIONBIO, &nb);
  return true;
}

void NetworkManager::sendUDPHello() {
  if (udpSocket == INVALID_SOCKET) return;

  UDPHelloPacket pkt{};
  pkt.tcpPort = localTCPPort;
  pkt.instanceId = myInstanceId;

  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = INADDR_BROADCAST;
  dest.sin_port = htons(NETWORK_UDP_PORT);

  sendto(udpSocket, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
         reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

void NetworkManager::receiveUDPHellos() {
  sockaddr_in sender{};
  int senderLen = sizeof(sender);
  UDPHelloPacket pkt{};

  int ret = recvfrom(udpSocket, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0,
                     reinterpret_cast<sockaddr*>(&sender), &senderLen);
  if (ret < static_cast<int>(sizeof(pkt))) return;
  if (pkt.magic != NETWORK_MAGIC) return;
  if (pkt.type != static_cast<uint8_t>(PacketType::HELLO)) return;
  if (pkt.instanceId == myInstanceId) return;

  char ipBuf[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET, &sender.sin_addr, ipBuf, sizeof(ipBuf));
  std::string senderIP(ipBuf);

  {
    std::lock_guard<std::mutex> lk(peersMutex);
    if (isKnownInstance(pkt.instanceId)) return;

    std::cout << "[Network] Discovered peer inst=" << pkt.instanceId
              << " ip=" << senderIP << " tcp=" << pkt.tcpPort << "\n";

    bool merged = false;
    for (auto& p : peers) {
      if (p.ip == senderIP && p.instanceId == 0) {
        p.instanceId = pkt.instanceId;
        p.tcpPort = pkt.tcpPort;
        merged = true;
        break;
      }
    }
    if (!merged) {
      PeerInfo newPeer{};
      newPeer.instanceId = pkt.instanceId;
      newPeer.ip = senderIP;
      newPeer.tcpPort = pkt.tcpPort;
      peers.push_back(newPeer);
    }

    allKnownInstances.push_back({pkt.instanceId, senderIP, pkt.tcpPort});
    std::sort(allKnownInstances.begin(), allKnownInstances.end(),
              [](const InstanceInfo& a, const InstanceInfo& b) {
                return a.instanceId < b.instanceId;
              });
    recomputePeerIDs();
  }

  connectToPeer(senderIP, pkt.tcpPort);
}

bool NetworkManager::initTCPServer() {
  for (uint16_t port = NETWORK_TCP_PORT; port <= NETWORK_TCP_MAX_PORT; ++port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) continue;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
      closesocket(s);
      continue;
    }

    listen(s, SOMAXCONN);
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    tcpListenSocket = s;
    localTCPPort = port;
    std::cout << "[Network] TCP server on port " << port << "\n";
    return true;
  }
  std::cerr << "[Network] Could not bind any TCP port in range "
            << NETWORK_TCP_PORT << "-" << NETWORK_TCP_MAX_PORT << "\n";
  return false;
}

void NetworkManager::acceptTCPConnections() {
  sockaddr_in clientAddr{};
  int addrLen = sizeof(clientAddr);
  SOCKET clientSock = accept(
      tcpListenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
  if (clientSock == INVALID_SOCKET) return;

  char ipBuf[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
  std::string clientIP(ipBuf);

  u_long nb = 1;
  ioctlsocket(clientSock, FIONBIO, &nb);

  std::cout << "[Network] Incoming TCP from " << clientIP << "\n";

  std::lock_guard<std::mutex> lk(peersMutex);
  for (auto& p : peers) {
    if (p.ip == clientIP && !p.connected) {
      if (p.socket != INVALID_SOCKET) closesocket(p.socket);
      p.socket = clientSock;
      p.connected = true;
      newPeerConnectedFlag = true;
      return;
    }
  }

  PeerInfo np{};
  np.ip = clientIP;
  np.socket = clientSock;
  np.connected = true;
  peers.push_back(np);
  newPeerConnectedFlag = true;
}

void NetworkManager::connectToPeer(const std::string& ip, uint16_t port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    std::cerr << "[Network] TCP connect to " << ip << ":" << port
              << " failed: " << WSAGetLastError() << "\n";
    closesocket(s);
    return;
  }

  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);

  std::lock_guard<std::mutex> lk(peersMutex);
  for (auto& p : peers) {
    if (p.ip == ip && p.tcpPort == port) {
      if (p.socket != INVALID_SOCKET) closesocket(p.socket);
      p.socket = s;
      p.connected = true;
      newPeerConnectedFlag = true;
      std::cout << "[Network] TCP connected to " << ip << ":" << port << "\n";
      return;
    }
  }
  closesocket(s);
}

void NetworkManager::receiveFromPeer(PeerInfo& peer) {
  TCPHeader hdr{};
  int ret = recv(peer.socket, reinterpret_cast<char*>(&hdr), sizeof(hdr), 0);

  if (ret == 0 ||
      (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
    closesocket(peer.socket);
    peer.socket = INVALID_SOCKET;
    peer.connected = false;
    std::cout << "[Network] Peer " << peer.ip << " disconnected\n";
    return;
  }
  if (ret != sizeof(hdr)) return;
  if (hdr.payloadSize == 0) return;
  if (hdr.payloadSize > 4 * 1024 * 1024) return;

  std::vector<uint8_t> payload(hdr.payloadSize);
  int totalRead = 0;
  while (totalRead < static_cast<int>(hdr.payloadSize)) {
    int r =
        recv(peer.socket, reinterpret_cast<char*>(payload.data()) + totalRead,
             hdr.payloadSize - totalRead, 0);
    if (r <= 0) break;
    totalRead += r;
    peer.bytesReceived += static_cast<uint64_t>(r);
  }
  if (totalRead != static_cast<int>(hdr.payloadSize)) return;

  switch (static_cast<PacketType>(hdr.type)) {
    case PacketType::OBJECT_STATES:
      handleObjectStatesBatch(payload);
      break;
    case PacketType::LOAD_SCENE:
      handleLoadScene(payload);
      break;
    case PacketType::SIM_STATE:
      handleSimState(payload);
      break;
    case PacketType::PING: {
      TCPHeader pong{};
      pong.type = static_cast<uint8_t>(PacketType::PONG);
      pong.payloadSize = 0;
      send(peer.socket, reinterpret_cast<const char*>(&pong), sizeof(pong), 0);
      break;
    }
    default:
      break;
  }
}

void NetworkManager::handleObjectStatesBatch(
    const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(ObjectStatesBatchHeader)) return;

  ObjectStatesBatchHeader batchHdr{};
  std::memcpy(&batchHdr, payload.data(), sizeof(batchHdr));

  size_t offset = sizeof(batchHdr);
  size_t expectedSize = offset + batchHdr.count * sizeof(ObjectStateEntry);
  if (payload.size() < expectedSize) return;

  std::lock_guard<std::mutex> lk(remoteStatesMutex);
  for (uint32_t i = 0; i < batchHdr.count; ++i) {
    ObjectStateEntry entry{};
    std::memcpy(&entry, payload.data() + offset, sizeof(entry));
    offset += sizeof(entry);

    RemoteObjectState state{};
    state.entityId = entry.entityId;
    state.position = {entry.posX, entry.posY, entry.posZ};
    state.orientation =
        glm::quat(entry.rotW, entry.rotX, entry.rotY, entry.rotZ);
    pendingRemoteStates.push_back(state);
  }
}

void NetworkManager::handleLoadScene(const std::vector<uint8_t>& payload) {
  if (payload.empty()) return;
  std::string path(reinterpret_cast<const char*>(payload.data()),
                   payload.size());
  std::lock_guard<std::mutex> lk(commandMutex);
  pendingSceneLoads.push_back(std::move(path));
}

void NetworkManager::handleSimState(const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(SimStatePayload)) return;
  SimStatePayload p{};
  std::memcpy(&p, payload.data(), sizeof(p));
  PendingSimState state{};
  state.isPaused = p.isPaused != 0;
  state.timeSpeed = p.timeSpeed;
  state.historyIndex = p.historyIndex;
  state.stepForward = p.stepForward != 0;
  state.reversePlay =
      (p.reversePlay == 0xFF) ? -1 : static_cast<int8_t>(p.reversePlay);
  state.colorByOwner =
      (p.colorByOwner == 0xFF) ? -1 : static_cast<int8_t>(p.colorByOwner);
  std::lock_guard<std::mutex> lk(commandMutex);
  pendingSimStates.push_back(state);
}

void NetworkManager::applyRemoteStates() {
  if (!registry) return;

  std::vector<RemoteObjectState> snapshot;
  {
    std::lock_guard<std::mutex> lk(remoteStatesMutex);
    snapshot.swap(pendingRemoteStates);
  }

  for (const auto& state : snapshot) {
    Entity e = static_cast<Entity>(state.entityId);

    auto* transform = registry->getComponent<TransformComponent>(e);
    if (!transform) continue;
    transform->position = state.position;
    transform->rotation = state.orientation;

    auto* physObj = registry->getPhysicsObjectPtr(e);
    if (physObj) {
      physObj->setPosition(state.position);
      physObj->setOrientation(state.orientation);
    }
  }
}

void NetworkManager::sendOwnedObjectStates() {
  if (!registry) return;

  std::vector<ObjectStateEntry> entries;
  for (const auto& [e, phys] : registry->allPhysics()) {
    if (!isLocallyOwned(e)) continue;
    const auto* transform = registry->getComponent<TransformComponent>(e);
    if (!transform) continue;

    ObjectStateEntry entry{};
    entry.entityId = static_cast<uint32_t>(e);
    entry.posX = transform->position.x;
    entry.posY = transform->position.y;
    entry.posZ = transform->position.z;
    entry.rotW = transform->rotation.w;
    entry.rotX = transform->rotation.x;
    entry.rotY = transform->rotation.y;
    entry.rotZ = transform->rotation.z;
    entries.push_back(entry);
  }
  if (entries.empty()) return;

  ObjectStatesBatchHeader batchHdr{};
  batchHdr.senderPeerId = localPeerID;
  batchHdr.count = static_cast<uint32_t>(entries.size());

  uint32_t payloadSize =
      sizeof(batchHdr) +
      static_cast<uint32_t>(entries.size() * sizeof(ObjectStateEntry));

  TCPHeader hdr{};
  hdr.type = static_cast<uint8_t>(PacketType::OBJECT_STATES);
  hdr.payloadSize = payloadSize;

  std::vector<uint8_t> buf(sizeof(hdr) + payloadSize);
  size_t off = 0;
  std::memcpy(buf.data() + off, &hdr, sizeof(hdr));
  off += sizeof(hdr);
  std::memcpy(buf.data() + off, &batchHdr, sizeof(batchHdr));
  off += sizeof(batchHdr);
  std::memcpy(buf.data() + off, entries.data(),
              entries.size() * sizeof(ObjectStateEntry));

  std::lock_guard<std::mutex> lk(peersMutex);
  for (auto& peer : peers) {
    if (!peer.connected || peer.socket == INVALID_SOCKET) continue;
    int sent = send(peer.socket, reinterpret_cast<const char*>(buf.data()),
                    static_cast<int>(buf.size()), 0);
    if (sent > 0) peer.bytesSent += static_cast<uint64_t>(sent);
  }
}

void NetworkManager::recomputePeerIDs() {
  for (size_t i = 0; i < allKnownInstances.size(); ++i) {
    if (allKnownInstances[i].instanceId == myInstanceId) {
      localPeerID = static_cast<uint8_t>(i + 1);
      break;
    }
  }

  for (auto& p : peers) {
    for (size_t i = 0; i < allKnownInstances.size(); ++i) {
      if (allKnownInstances[i].instanceId == p.instanceId) {
        p.id = static_cast<uint8_t>(i + 1);
        break;
      }
    }
  }
}

bool NetworkManager::isKnownInstance(uint32_t instId) const {
  for (const auto& info : allKnownInstances)
    if (info.instanceId == instId) return true;
  return false;
}

void NetworkManager::broadcastTCP(PacketType type, const void* payload,
                                  uint32_t payloadSize) {
  TCPHeader hdr{};
  hdr.type = static_cast<uint8_t>(type);
  hdr.payloadSize = payloadSize;

  std::vector<uint8_t> buf(sizeof(hdr) + payloadSize);
  std::memcpy(buf.data(), &hdr, sizeof(hdr));
  if (payload && payloadSize > 0)
    std::memcpy(buf.data() + sizeof(hdr), payload, payloadSize);

  std::lock_guard<std::mutex> lk(peersMutex);
  for (auto& peer : peers) {
    if (!peer.connected || peer.socket == INVALID_SOCKET) continue;
    int sent = send(peer.socket, reinterpret_cast<const char*>(buf.data()),
                    static_cast<int>(buf.size()), 0);
    if (sent > 0) peer.bytesSent += static_cast<uint64_t>(sent);
  }
}

std::string NetworkManager::getLocalIPAddress() {
  char hostname[256]{};
  gethostname(hostname, sizeof(hostname));

  addrinfo hints{}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) return "127.0.0.1";

  std::string best = "127.0.0.1";
  for (addrinfo* p = result; p; p = p->ai_next) {
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr,
              buf, sizeof(buf));
    if (std::string(buf) != "127.0.0.1") {
      best = buf;
      break;
    }
  }
  freeaddrinfo(result);
  return best;
}
