#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
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

    void tickReceive();
    void tickSend();

    void assignObjectOwnership();
    bool isLocallyOwned(Entity e) const;
    uint8_t getOwnerPeerID(Entity e) const;

    void sendLoadScene(const std::string& scenePath);
    void sendSimState(bool isPaused, float timeSpeed,
                      int32_t historyIndex = -1, bool stepForward = false,
                      int8_t reversePlay = -1, int8_t colorByOwner = -1);

    bool pollPendingSceneLoad(std::string& outPath);
    bool pollPendingSimState(PendingSimState& outState);
    bool pollNewPeerConnected();

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

    bool colorByOwner = false;

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

    mutable std::mutex             remoteStatesMutex;
    std::vector<RemoteObjectState> pendingRemoteStates;

    mutable std::mutex           commandMutex;
    std::vector<std::string>     pendingSceneLoads;
    std::vector<PendingSimState> pendingSimStates;

    void networkThreadFunc();

    bool initUDP();
    bool initTCPServer();
    void sendUDPHello();
    void receiveUDPHellos();
    void acceptTCPConnections();
    void connectToPeer(const std::string& ip, uint16_t port);
    void receiveFromPeer(PeerInfo& peer);

    void handleObjectStatesBatch(const std::vector<uint8_t>& payload);
    void handleLoadScene(const std::vector<uint8_t>& payload);
    void handleSimState(const std::vector<uint8_t>& payload);

    void applyRemoteStates();
    void sendOwnedObjectStates();

    void recomputePeerIDs();
    bool isKnownInstance(uint32_t instId) const;

    std::string getLocalIPAddress();

    void broadcastTCP(PacketType type, const void* payload, uint32_t payloadSize);
};
