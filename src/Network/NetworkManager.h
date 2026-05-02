#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "NetworkPackets.h"
#include "../ECS/Entity.h"

class Registry;

struct RemoteObjectState {
    uint32_t  entityId;
    glm::vec3 position;
    glm::quat orientation;
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
};

struct RemoteInterpolationState {
    glm::vec3 position;
    glm::quat orientation;
    glm::vec3 velocity;
    std::chrono::steady_clock::time_point receivedAt;
    bool valid = false;
};

struct RemoteObjectProperties {
    uint32_t  entityId;
    float     mass        = 1.0f;
    float     restitution = 0.5f;
    float     damping     = 0.99f;
    bool      useGravity  = true;
    uint8_t   colliderType = 0;
    float     radius      = 1.0f;
    float     height      = 1.0f;
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    glm::vec3 normal      = {0.0f, 1.0f, 0.0f};
    bool      finite      = false;
};

struct PeerInfo {
    uint8_t     id           = 0;
    uint32_t    instanceId   = 0;
    std::string ip;
    uint16_t    tcpPort      = NETWORK_TCP_PORT;
    SOCKET      socket       = INVALID_SOCKET;
    bool        connected    = false;
    int         ownedObjects = 0;
    float       latencyMs    = 0.0f;
    uint64_t    bytesSent    = 0;
    uint64_t    bytesReceived = 0;
    std::vector<uint8_t> recvBuf;
};

struct PendingSimState {
    bool    isPaused;
    float   timeSpeed;
    int32_t historyIndex = -1;
    bool    stepForward  = false;
    int8_t  reversePlay  = -1;
    int8_t  colorByOwner = -1;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    void init(Registry* registry);
    void shutdown();
    void setNetworkEnabled(bool enabled);

    void tickReceive();
    void tickSend();

    void assignObjectOwnership();
    bool isLocallyOwned(Entity e) const;
    uint8_t getOwnerPeerID(Entity e) const;

    void setEntityOwner(Entity e, uint8_t peerID);

    std::vector<uint8_t> getActivePeerIDs() const;

    void sendLoadScene(const std::string& scenePath);
    void sendOwnedObjectProperties();
    void sendSimState(bool isPaused, float timeSpeed,
                      int32_t historyIndex = -1, bool stepForward = false,
                      int8_t reversePlay = -1, int8_t colorByOwner = -1);

    void stepInterpolation(float dt);
    void flushInterpolation();

    bool pollPendingSceneLoad(std::string& outPath);
    bool pollPendingSimState(PendingSimState& outState);
    bool pollNewPeerConnected();
    bool pollPeerDropped();

    void broadcastSpawnEntity(Entity e);
    bool pollPendingSpawnedEntity(SpawnEntityPacket& out);

    uint8_t     getLocalPeerID()        const { return localPeerID; }
    uint32_t    getInstanceId()         const { return myInstanceId; }
    std::string getLocalIP()            const { return localIP; }
    uint16_t    getLocalTCPPort()       const { return localTCPPort; }
    bool        isRunning()             const { return running.load(); }
    bool        isConnected()           const;
    int         getConnectedPeerCount() const;

    std::vector<PeerInfo> getPeerSnapshot() const;
    int getLocalOwnedCount()   const;
    int getTotalManagedCount() const;

    bool  colorByOwner  = false;
    float networkSendHz = 120.0f;

    float simPacketLossPercent  = 0.0f;   // 0-100: per-peer drop chance per send
    float simExtraLatencyMs     = 0.0f;   // 0-500: artificial delay before send
    float simBandwidthLimitKBps = 0.0f;   // 0 = unlimited, else max KB/s outbound

    float interpLerpSpeed = 15.0f;


private:
    Registry* registry = nullptr;

    uint32_t    myInstanceId = 0;
    uint8_t     localPeerID  = 1;
    std::string localIP;
    uint16_t    localTCPPort = 0;

    struct InstanceInfo {
        uint32_t    instanceId;
        std::string ip;
        uint16_t    tcpPort;
    };
    std::vector<InstanceInfo> allKnownInstances;

    SOCKET udpSocket       = INVALID_SOCKET;
    SOCKET tcpListenSocket = INVALID_SOCKET;

    mutable std::mutex    peersMutex;
    std::vector<PeerInfo> peers;

    mutable std::mutex                   ownershipMutex;
    std::unordered_map<Entity, uint8_t>  ownershipMap;

    std::thread       networkThread;
    std::atomic<bool> running{false};
    std::atomic<bool> newPeerConnectedFlag{false};
    std::atomic<bool> peerDroppedFlag{false};

    mutable std::mutex                   remoteStatesMutex;
    std::vector<RemoteObjectState>       pendingRemoteStates;
    std::vector<RemoteObjectProperties>  pendingRemoteProperties;

    std::unordered_map<Entity, RemoteInterpolationState> remoteInterpStates;

    mutable std::mutex           commandMutex;
    std::vector<std::string>     pendingSceneLoads;
    std::vector<PendingSimState> pendingSimStates;

    std::mt19937 rng;

    struct DeferredSend {
        std::chrono::steady_clock::time_point readyAt;
        SOCKET sock;
        std::vector<uint8_t> data;
    };
    std::deque<DeferredSend> deferredSends;
    std::mutex               deferredMutex;

    uint64_t bwBytesThisSecond = 0;
    std::chrono::steady_clock::time_point bwWindowStart;

    void networkThreadFunc();

    bool initUDP();
    bool initTCPServer();
    void sendUDPHello();
    void receiveUDPHellos();
    void acceptTCPConnections();
    void connectToPeer(const std::string& ip, uint16_t port);
    void receiveFromPeer(PeerInfo& peer);

    void sendHandshake(SOCKET s);
    void handleHandshake(PeerInfo& peer, const std::vector<uint8_t>& payload);
    void handleObjectStatesBatch(const std::vector<uint8_t>& payload);
    void handleObjectPropertiesBatch(const std::vector<uint8_t>& payload);
    void handleLoadScene(const std::vector<uint8_t>& payload);
    void handleSimState(const std::vector<uint8_t>& payload);
    void handleSpawnEntity(const std::vector<uint8_t>& payload);

    mutable std::mutex              spawnedEntitiesMutex;
    std::vector<SpawnEntityPacket>  pendingSpawnedEntities;

    void applyRemoteStates();
    void applyRemoteProperties();
    void sendOwnedObjectStates();

    void queueSimulatedSend(SOCKET sock, const std::vector<uint8_t>& data);
    void flushDeferredSends();

    void recomputePeerIDs();
    bool isKnownInstance(uint32_t instId) const;

    std::string getLocalIPAddress();

    void broadcastTCP(PacketType type, const void* payload, uint32_t payloadSize);
};
