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

// Fast-channel data applied every tick.
struct RemoteObjectState {
    uint32_t  entityId;
    glm::vec3 position;
    glm::quat orientation;
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
};

// Slow-channel data applied once per connect/scene-load.
// Mirrors ObjectPropertyEntry but uses engine types.
// colliderType uses the same values as the ColliderType enum (uint8_t).
struct RemoteObjectProperties {
    uint32_t  entityId;
    float     mass        = 1.0f;
    float     restitution = 0.5f;
    float     damping     = 0.99f;
    bool      useGravity  = true;
    uint8_t   colliderType = 0;   // 0=Sphere 1=AABB 2=Plane 3=Cylinder
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
    // Gracefully stops or restarts all networking. When disabled, disconnects
    // all sockets and stops discovery. When re-enabled, starts fresh.
    void setNetworkEnabled(bool enabled);

    void tickReceive();
    void tickSend();

    void assignObjectOwnership();
    bool isLocallyOwned(Entity e) const;
    uint8_t getOwnerPeerID(Entity e) const;

    // Explicitly pin one entity to a specific peer without redistributing all others.
    void setEntityOwner(Entity e, uint8_t peerID);

    // Returns the sorted list of currently active peer IDs (local + connected).
    // Used by SpawnerSystem for SEQUENTIAL round-robin ownership assignment.
    std::vector<uint8_t> getActivePeerIDs() const;

    void sendLoadScene(const std::string& scenePath);
    // Sends static physics properties (mass, collider, etc.) to all connected peers.
    // Call after a new peer connects or after a scene reload.
    // Uses broadcastTCP directly — not subject to loss/BW simulation (it's setup data).
    void sendOwnedObjectProperties();
    void sendSimState(bool isPaused, float timeSpeed,
                      int32_t historyIndex = -1, bool stepForward = false,
                      int8_t reversePlay = -1, int8_t colorByOwner = -1);

    bool pollPendingSceneLoad(std::string& outPath);
    bool pollPendingSimState(PendingSimState& outState);
    bool pollNewPeerConnected();
    // Returns true (once) when any peer drops. Triggers ownership reallocation.
    bool pollPeerDropped();

    // Called by SpawnerSystem after creating a new entity locally.
    // Reads entity components from the registry and broadcasts a SPAWN_ENTITY packet to all peers.
    void broadcastSpawnEntity(Entity e);
    // Returns true if a remote peer spawned an entity; out is filled with the full description.
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

    // Network condition simulation (main-thread access only)
    float simPacketLossPercent  = 0.0f;   // 0-100: per-peer drop chance per send
    float simExtraLatencyMs     = 0.0f;   // 0-500: artificial delay before send
    float simBandwidthLimitKBps = 0.0f;   // 0 = unlimited, else max KB/s outbound

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

    mutable std::mutex           commandMutex;
    std::vector<std::string>     pendingSceneLoads;
    std::vector<PendingSimState> pendingSimStates;

    // Network condition simulation internals
    std::mt19937 rng;

    struct DeferredSend {
        std::chrono::steady_clock::time_point readyAt;
        SOCKET sock;
        std::vector<uint8_t> data;
    };
    std::deque<DeferredSend> deferredSends;
    std::mutex               deferredMutex;

    // Bandwidth tracking (main-thread only)
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

    // Queues a packet for a single socket, applying latency/bandwidth simulation.
    // Called from main thread; deferredSends flushed by network thread.
    void queueSimulatedSend(SOCKET sock, const std::vector<uint8_t>& data);
    void flushDeferredSends();

    void recomputePeerIDs();
    bool isKnownInstance(uint32_t instId) const;

    std::string getLocalIPAddress();

    void broadcastTCP(PacketType type, const void* payload, uint32_t payloadSize);
};
