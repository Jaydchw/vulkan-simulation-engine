#include "NetworkManager.h"

#pragma comment(lib, "ws2_32.lib")

#include "../Util/ThreadAffinity.h"

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

  rng = std::mt19937(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
  myInstanceId = dist(rng);
  localPeerID = 1;
  bwWindowStart = std::chrono::steady_clock::now();
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
    pendingRemoteProperties.clear();
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
  ThreadAffinity::setThread(networkThread, ThreadAffinity::NETWORKING_MASK, "Networking");

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

void NetworkManager::setNetworkEnabled(bool enabled) {
  if (enabled == running.load()) return;

  if (!enabled) {
    shutdown();
  } else {
    // Clear all stale per-session state so we start fresh
    {
      std::lock_guard<std::mutex> lk(peersMutex);
      peers.clear();
    }
    {
      std::lock_guard<std::mutex> lk(remoteStatesMutex);
      pendingRemoteStates.clear();
      pendingRemoteProperties.clear();
    }
    {
      std::lock_guard<std::mutex> lk(ownershipMutex);
      ownershipMap.clear();
    }
    {
      std::lock_guard<std::mutex> lk(commandMutex);
      pendingSceneLoads.clear();
      pendingSimStates.clear();
    }
    {
      std::lock_guard<std::mutex> lk(deferredMutex);
      deferredSends.clear();
    }
    init(registry);  // registry is still valid from before shutdown
  }
}

void NetworkManager::tickReceive() {
  if (!running) return;
  applyRemoteProperties();  // slow channel — usually a no-op, cheap when empty
  applyRemoteStates();      // fast channel — applied every frame
}

void NetworkManager::tickSend() {
  if (!running) return;
  sendOwnedObjectStates();
}

void NetworkManager::assignObjectOwnership() {
  if (!registry) return;

  std::vector<Entity> dynamicEntities;
  for (const auto& [e, _] : registry->allSimulated())
    dynamicEntities.push_back(e);
  std::sort(dynamicEntities.begin(), dynamicEntities.end());

  // Build the list of active peer IDs: only connected peers + self.
  // If simulated packet loss is at or above the isolation threshold, this
  // instance runs in solo mode and claims all objects locally.
  std::vector<uint8_t> activePeerIds;
  {
    std::lock_guard<std::mutex> lk(peersMutex);
    if (simPacketLossPercent >= OWNERSHIP_LOSS_ISOLATION_PCT) {
      activePeerIds.push_back(localPeerID);
    } else {
      activePeerIds.push_back(localPeerID);
      for (const auto& p : peers)
        if (p.connected && p.id != 0) activePeerIds.push_back(p.id);
      std::sort(activePeerIds.begin(), activePeerIds.end());
      activePeerIds.erase(
          std::unique(activePeerIds.begin(), activePeerIds.end()),
          activePeerIds.end());
    }
  }

  const uint8_t activePeerCount = static_cast<uint8_t>(
      std::min(activePeerIds.size(), size_t(NETWORK_MAX_PEERS)));

  {
    std::lock_guard<std::mutex> lk(ownershipMutex);
    ownershipMap.clear();
    for (size_t i = 0; i < dynamicEntities.size(); ++i)
      ownershipMap[dynamicEntities[i]] = activePeerIds[i % activePeerCount];
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

  std::cout << "[Network] Ownership assigned: " << dynamicEntities.size()
            << " objects across " << (int)activePeerCount << " active peers"
            << (simPacketLossPercent >= OWNERSHIP_LOSS_ISOLATION_PCT
                    ? " [isolated: high loss]"
                    : "")
            << " (I am peer " << (int)localPeerID << ")\n";
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

bool NetworkManager::pollPeerDropped() {
  bool expected = true;
  return peerDroppedFlag.compare_exchange_strong(expected, false);
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
  ULONGLONG lastHelloMs = GetTickCount64();
  constexpr ULONGLONG HELLO_INTERVAL_MS = 1000;

  while (running) {
    ULONGLONG now = GetTickCount64();
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
    if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) {
      flushDeferredSends();
      continue;
    }

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
    flushDeferredSends();
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

    PeerInfo newPeer{};
    newPeer.instanceId = pkt.instanceId;
    newPeer.ip = senderIP;
    newPeer.tcpPort = pkt.tcpPort;
    peers.push_back(newPeer);

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

  // Send our handshake first (socket is still blocking — small packet completes
  // immediately)
  sendHandshake(clientSock);

  u_long nb = 1;
  ioctlsocket(clientSock, FIONBIO, &nb);

  std::cout << "[Network] Incoming TCP from " << clientIP << "\n";

  // Don't match by IP — multiple instances share the same IP on one machine.
  // Identity is resolved when we receive their HANDSHAKE packet.
  std::lock_guard<std::mutex> lk(peersMutex);
  PeerInfo np{};
  np.ip = clientIP;
  np.socket = clientSock;
  np.connected = true;
  // instanceId=0 until handleHandshake fills it in
  peers.push_back(np);
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

  // Send our handshake while socket is still blocking — small packet, completes
  // immediately
  sendHandshake(s);

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
  // Read all available bytes into the peer's accumulation buffer.
  // Heap-allocated to avoid a large (65 KB) stack frame.
  std::vector<uint8_t> tmp(65536);
  int r = recv(peer.socket, reinterpret_cast<char*>(tmp.data()), static_cast<int>(tmp.size()), 0);
  if (r == 0 || (r == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
    closesocket(peer.socket);
    peer.socket = INVALID_SOCKET;
    peer.connected = false;
    peer.recvBuf.clear();
    peerDroppedFlag = true;
    std::cout << "[Network] Peer " << peer.ip << " disconnected\n";
    return;
  }
  if (r > 0) {
    peer.bytesReceived += static_cast<uint64_t>(r);
    peer.recvBuf.insert(peer.recvBuf.end(), tmp.data(), tmp.data() + r);
  }

  // Process every complete packet in the buffer
  while (peer.recvBuf.size() >= sizeof(TCPHeader)) {
    TCPHeader hdr{};
    std::memcpy(&hdr, peer.recvBuf.data(), sizeof(hdr));

    if (hdr.payloadSize > 4 * 1024 * 1024) {
      // Protocol error — disconnect rather than letting garbage accumulate
      closesocket(peer.socket);
      peer.socket = INVALID_SOCKET;
      peer.connected = false;
      peer.recvBuf.clear();
      std::cout << "[Network] Peer " << peer.ip
                << " protocol error, disconnecting\n";
      return;
    }

    size_t needed = sizeof(TCPHeader) + hdr.payloadSize;
    if (peer.recvBuf.size() < needed) break;  // wait for the rest

    std::vector<uint8_t> payload(peer.recvBuf.begin() + sizeof(TCPHeader),
                                 peer.recvBuf.begin() + needed);
    peer.recvBuf.erase(peer.recvBuf.begin(), peer.recvBuf.begin() + needed);

    switch (static_cast<PacketType>(hdr.type)) {
      case PacketType::HANDSHAKE:
        handleHandshake(peer, payload);
        if (!peer.connected) return;  // duplicate was dropped
        break;
      case PacketType::OBJECT_STATES:
        handleObjectStatesBatch(payload);
        break;
      case PacketType::OBJECT_PROPERTIES:
        handleObjectPropertiesBatch(payload);
        break;
      case PacketType::LOAD_SCENE:
        handleLoadScene(payload);
        break;
      case PacketType::SIM_STATE:
        handleSimState(payload);
        break;
      case PacketType::SPAWN_ENTITY:
        handleSpawnEntity(payload);
        break;
      case PacketType::PING: {
        TCPHeader pong{};
        pong.type = static_cast<uint8_t>(PacketType::PONG);
        pong.payloadSize = 0;
        send(peer.socket, reinterpret_cast<const char*>(&pong), sizeof(pong),
             0);
        break;
      }
      default:
        break;
    }
  }
}

void NetworkManager::sendHandshake(SOCKET s) {
  HandshakePayload hs{};
  hs.instanceId = myInstanceId;
  hs.tcpPort = localTCPPort;

  TCPHeader hdr{};
  hdr.type = static_cast<uint8_t>(PacketType::HANDSHAKE);
  hdr.payloadSize = sizeof(hs);

  std::vector<uint8_t> buf(sizeof(hdr) + sizeof(hs));
  std::memcpy(buf.data(), &hdr, sizeof(hdr));
  std::memcpy(buf.data() + sizeof(hdr), &hs, sizeof(hs));
  send(s, reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()), 0);
}

void NetworkManager::handleHandshake(PeerInfo& peer,
                                     const std::vector<uint8_t>& payload) {
  // peersMutex is already held by the caller (networkThreadFunc)
  if (payload.size() < sizeof(HandshakePayload)) return;
  HandshakePayload hs{};
  std::memcpy(&hs, payload.data(), sizeof(hs));

  // Check if we already have a live connection to this instance via the
  // outbound socket. If so, this is the duplicate incoming connection — drop
  // it.
  for (auto& p : peers) {
    if (&p == &peer) continue;
    if (p.instanceId == hs.instanceId && p.connected &&
        p.socket != INVALID_SOCKET) {
      closesocket(peer.socket);
      peer.socket = INVALID_SOCKET;
      peer.connected = false;
      peer.recvBuf.clear();
      std::cout << "[Network] Dropped duplicate connection from inst="
                << hs.instanceId << "\n";
      return;
    }
  }

  // Identify this peer
  peer.instanceId = hs.instanceId;
  peer.tcpPort = hs.tcpPort;

  // Add to known instances if not already there (can happen if TCP arrives
  // before the UDP hello is processed)
  if (!isKnownInstance(hs.instanceId)) {
    allKnownInstances.push_back({hs.instanceId, peer.ip, hs.tcpPort});
    std::sort(allKnownInstances.begin(), allKnownInstances.end(),
              [](const InstanceInfo& a, const InstanceInfo& b) {
                return a.instanceId < b.instanceId;
              });
    recomputePeerIDs();
  }

  // Ensure this peer has its ID assigned
  for (size_t i = 0; i < allKnownInstances.size(); ++i) {
    if (allKnownInstances[i].instanceId == hs.instanceId) {
      peer.id = static_cast<uint8_t>(i + 1);
      break;
    }
  }

  newPeerConnectedFlag = true;
  std::cout << "[Network] Handshake: inst=" << hs.instanceId
            << " -> PeerID=" << (int)peer.id << "\n";
}

void NetworkManager::handleObjectStatesBatch(
    const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(ObjectBatchHeader)) return;

  ObjectBatchHeader batchHdr{};
  std::memcpy(&batchHdr, payload.data(), sizeof(batchHdr));

  size_t offset = sizeof(batchHdr);
  size_t expectedSize = offset + batchHdr.count * sizeof(ObjectFastStateEntry);
  if (payload.size() < expectedSize) return;

  std::lock_guard<std::mutex> lk(remoteStatesMutex);
  for (uint32_t i = 0; i < batchHdr.count; ++i) {
    ObjectFastStateEntry entry{};
    std::memcpy(&entry, payload.data() + offset, sizeof(entry));
    offset += sizeof(entry);

    RemoteObjectState state{};
    state.entityId = entry.entityId;
    state.position = {entry.posX, entry.posY, entry.posZ};
    state.orientation =
        glm::quat(entry.rotW, entry.rotX, entry.rotY, entry.rotZ);
    state.velocity = {entry.velX, entry.velY, entry.velZ};
    pendingRemoteStates.push_back(state);
  }
}

void NetworkManager::handleObjectPropertiesBatch(
    const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(ObjectBatchHeader)) return;

  ObjectBatchHeader batchHdr{};
  std::memcpy(&batchHdr, payload.data(), sizeof(batchHdr));

  size_t offset = sizeof(batchHdr);
  size_t expectedSize = offset + batchHdr.count * sizeof(ObjectPropertyEntry);
  if (payload.size() < expectedSize) return;

  std::lock_guard<std::mutex> lk(remoteStatesMutex);
  for (uint32_t i = 0; i < batchHdr.count; ++i) {
    ObjectPropertyEntry entry{};
    std::memcpy(&entry, payload.data() + offset, sizeof(entry));
    offset += sizeof(entry);

    RemoteObjectProperties props{};
    props.entityId = entry.entityId;
    props.mass = entry.mass;
    props.restitution = entry.restitution;
    props.damping = entry.damping;
    props.useGravity = entry.useGravity != 0;
    props.colliderType = entry.colliderType;
    props.radius = entry.radius;
    props.height = entry.height;
    props.halfExtents = {entry.halfExtX, entry.halfExtY, entry.halfExtZ};
    props.normal = {entry.normalX, entry.normalY, entry.normalZ};
    props.finite = entry.finite != 0;
    pendingRemoteProperties.push_back(props);
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

    // Keep velocity current for cross-peer collision resolution
    auto* phys = registry->getComponent<SimulatedComponent>(e);
    if (phys) phys->velocity = state.velocity;

    auto* physObj = registry->getPhysicsObjectPtr(e);
    if (physObj) {
      physObj->setPosition(state.position);
      physObj->setOrientation(state.orientation);
    }
  }
}

void NetworkManager::applyRemoteProperties() {
  std::vector<RemoteObjectProperties> snapshot;
  {
    std::lock_guard<std::mutex> lk(remoteStatesMutex);
    if (pendingRemoteProperties.empty()) return;
    snapshot.swap(pendingRemoteProperties);
  }
  if (!registry) return;

  for (const auto& props : snapshot) {
    Entity e = static_cast<Entity>(props.entityId);

    auto* phys = registry->getComponent<SimulatedComponent>(e);
    if (phys) {
      phys->mass = props.mass;
      phys->restitution = props.restitution;
      phys->damping = props.damping;
      phys->useGravity = props.useGravity;
    }

    auto* collider = registry->getComponent<ColliderComponent>(e);
    if (collider) {
      collider->type = static_cast<ColliderType>(props.colliderType);
      collider->radius = props.radius;
      collider->height = props.height;
      collider->halfExtents = props.halfExtents;
      collider->normal = props.normal;
      collider->finite = props.finite;
    }
  }
}

void NetworkManager::sendOwnedObjectStates() {
  if (!registry) return;

  std::vector<ObjectFastStateEntry> entries;
  for (const auto& [e, phys] : registry->allSimulated()) {
    if (!isLocallyOwned(e)) continue;
    const auto* transform = registry->getComponent<TransformComponent>(e);
    if (!transform) continue;

    ObjectFastStateEntry entry{};
    entry.entityId = static_cast<uint32_t>(e);
    entry.posX = transform->position.x;
    entry.posY = transform->position.y;
    entry.posZ = transform->position.z;
    entry.rotW = transform->rotation.w;
    entry.rotX = transform->rotation.x;
    entry.rotY = transform->rotation.y;
    entry.rotZ = transform->rotation.z;
    entry.velX = phys.velocity.x;
    entry.velY = phys.velocity.y;
    entry.velZ = phys.velocity.z;
    entries.push_back(entry);
  }
  if (entries.empty()) return;

  ObjectBatchHeader batchHdr{};
  batchHdr.senderPeerId = localPeerID;
  batchHdr.count = static_cast<uint32_t>(entries.size());

  uint32_t payloadSize =
      sizeof(batchHdr) +
      static_cast<uint32_t>(entries.size() * sizeof(ObjectFastStateEntry));

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
              entries.size() * sizeof(ObjectFastStateEntry));

  // Bandwidth budget reset (1-second window)
  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration<float>(now - bwWindowStart).count() >= 1.0f) {
    bwBytesThisSecond = 0;
    bwWindowStart = now;
  }

  std::uniform_real_distribution<float> lossDist(0.0f, 100.0f);

  std::lock_guard<std::mutex> lk(peersMutex);
  for (auto& peer : peers) {
    if (!peer.connected || peer.socket == INVALID_SOCKET) continue;

    // Per-peer packet loss simulation
    if (simPacketLossPercent > 0.0f && lossDist(rng) < simPacketLossPercent)
      continue;

    // Bandwidth cap: drop if this send would exceed the budget
    if (simBandwidthLimitKBps > 0.0f) {
      uint64_t limitBytes =
          static_cast<uint64_t>(simBandwidthLimitKBps * 1024.0f);
      if (bwBytesThisSecond + buf.size() > limitBytes) continue;
      bwBytesThisSecond += buf.size();
    }

    peer.bytesSent += buf.size();
    queueSimulatedSend(peer.socket, buf);
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

void NetworkManager::sendOwnedObjectProperties() {
  if (!registry) return;

  std::vector<ObjectPropertyEntry> entries;
  for (const auto& [e, phys] : registry->allSimulated()) {
    if (!isLocallyOwned(e)) continue;
    const auto* collider = registry->getComponent<ColliderComponent>(e);
    if (!collider) continue;

    ObjectPropertyEntry entry{};
    entry.entityId = static_cast<uint32_t>(e);
    entry.mass = phys.mass;
    entry.restitution = phys.restitution;
    entry.damping = phys.damping;
    entry.useGravity = phys.useGravity ? 1 : 0;
    entry.colliderType = static_cast<uint8_t>(collider->type);
    entry.radius = collider->radius;
    entry.height = collider->height;
    entry.halfExtX = collider->halfExtents.x;
    entry.halfExtY = collider->halfExtents.y;
    entry.halfExtZ = collider->halfExtents.z;
    entry.normalX = collider->normal.x;
    entry.normalY = collider->normal.y;
    entry.normalZ = collider->normal.z;
    entry.finite = collider->finite ? 1 : 0;
    entries.push_back(entry);
  }
  if (entries.empty()) return;

  ObjectBatchHeader batchHdr{};
  batchHdr.senderPeerId = localPeerID;
  batchHdr.count = static_cast<uint32_t>(entries.size());

  const uint32_t payloadSize =
      sizeof(batchHdr) +
      static_cast<uint32_t>(entries.size() * sizeof(ObjectPropertyEntry));

  std::vector<uint8_t> payload(payloadSize);
  size_t off = 0;
  std::memcpy(payload.data() + off, &batchHdr, sizeof(batchHdr));
  off += sizeof(batchHdr);
  std::memcpy(payload.data() + off, entries.data(),
              entries.size() * sizeof(ObjectPropertyEntry));

  // Sent via the reliable control path — not subject to loss/BW simulation.
  broadcastTCP(PacketType::OBJECT_PROPERTIES, payload.data(), payloadSize);
}

void NetworkManager::queueSimulatedSend(SOCKET sock,
                                        const std::vector<uint8_t>& data) {
  if (simExtraLatencyMs <= 0.0f) {
    send(sock, reinterpret_cast<const char*>(data.data()),
         static_cast<int>(data.size()), 0);
    return;
  }

  auto readyAt = std::chrono::steady_clock::now() +
                 std::chrono::microseconds(
                     static_cast<int64_t>(simExtraLatencyMs * 1000.0f));
  std::lock_guard<std::mutex> lk(deferredMutex);
  deferredSends.push_back({readyAt, sock, data});
}

void NetworkManager::flushDeferredSends() {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(deferredMutex);
  while (!deferredSends.empty() && deferredSends.front().readyAt <= now) {
    auto& d = deferredSends.front();
    send(d.sock, reinterpret_cast<const char*>(d.data.data()),
         static_cast<int>(d.data.size()), 0);
    deferredSends.pop_front();
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

void NetworkManager::broadcastSpawnEntity(Entity e) {
  if (!running || !registry) return;

  const auto* transform = registry->getComponent<TransformComponent>(e);
  const auto* phys      = registry->getComponent<SimulatedComponent>(e);
  const auto* collider  = registry->getComponent<ColliderComponent>(e);
  const auto* nameComp  = registry->getComponent<NameComponent>(e);
  const auto* mesh      = registry->getComponent<MeshComponent>(e);
  const auto* material  = registry->getComponent<MaterialComponent>(e);
  const auto* render    = registry->getComponent<RenderComponent>(e);

  if (!transform || !phys || !collider) return;

  SpawnEntityPacket pkt{};
  pkt.entityId = static_cast<uint32_t>(e);

  pkt.posX = transform->position.x;
  pkt.posY = transform->position.y;
  pkt.posZ = transform->position.z;
  pkt.rotW = transform->rotation.w;
  pkt.rotX = transform->rotation.x;
  pkt.rotY = transform->rotation.y;
  pkt.rotZ = transform->rotation.z;
  pkt.scaleX = transform->scale.x;
  pkt.scaleY = transform->scale.y;
  pkt.scaleZ = transform->scale.z;

  pkt.velX    = phys->velocity.x;
  pkt.velY    = phys->velocity.y;
  pkt.velZ    = phys->velocity.z;
  pkt.angVelX = phys->angularVelocity.x;
  pkt.angVelY = phys->angularVelocity.y;
  pkt.angVelZ = phys->angularVelocity.z;
  pkt.mass        = phys->mass;
  pkt.restitution = phys->restitution;
  pkt.damping     = phys->damping;
  pkt.useGravity  = phys->useGravity ? 1 : 0;

  pkt.colliderType = static_cast<uint8_t>(collider->type);
  pkt.radius       = collider->radius;
  pkt.height       = collider->height;
  pkt.halfExtX     = collider->halfExtents.x;
  pkt.halfExtY     = collider->halfExtents.y;
  pkt.halfExtZ     = collider->halfExtents.z;
  pkt.normalX      = collider->normal.x;
  pkt.normalY      = collider->normal.y;
  pkt.normalZ      = collider->normal.z;
  pkt.finite       = collider->finite ? 1 : 0;

  pkt.hasRender  = (render && mesh && material) ? 1 : 0;
  pkt.meshId     = mesh     ? mesh->meshID         : 0;
  pkt.materialId = material ? material->materialID : 0;

  if (nameComp) {
    strncpy_s(pkt.name, sizeof(pkt.name), nameComp->name.c_str(), _TRUNCATE);
  }

  broadcastTCP(PacketType::SPAWN_ENTITY, &pkt, sizeof(pkt));
}

void NetworkManager::handleSpawnEntity(const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(SpawnEntityPacket)) return;

  SpawnEntityPacket pkt{};
  std::memcpy(&pkt, payload.data(), sizeof(pkt));

  std::lock_guard<std::mutex> lk(spawnedEntitiesMutex);
  pendingSpawnedEntities.push_back(pkt);
}

bool NetworkManager::pollPendingSpawnedEntity(SpawnEntityPacket& out) {
  std::lock_guard<std::mutex> lk(spawnedEntitiesMutex);
  if (pendingSpawnedEntities.empty()) return false;
  out = pendingSpawnedEntities.front();
  pendingSpawnedEntities.erase(pendingSpawnedEntities.begin());
  return true;
}
