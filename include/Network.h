#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <optional>
#include <queue>
#include <cstdint>
#include "Common.h"

//  Raw UDP socket (non-blocking) 
class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();
    bool bind(uint16_t port);
    bool sendTo(const void* data, int len, const sockaddr_in& addr);
    // returns bytes read, 0 = would-block, -1 = error
    int  recvFrom(void* buf, int maxLen, sockaddr_in& addr);
private:
    SOCKET m_sock = INVALID_SOCKET;
};

//  Per-client record (server side) 
struct ClientRecord
{
    sockaddr_in addr{};
    uint8_t     pid       = 0;
    char        name[16]  = {};
    bool        active    = false;
    float       lastSeen  = 0.f;   // seconds since epoch (use clock)
};

//  Inbound envelope 
struct Envelope
{
    uint8_t     buf[2048]{};
    int         len  = 0;
    sockaddr_in from{};
};

// ServerNet
class ServerNet
{
public:
    bool init(uint16_t port);
    // Call every frame; fills internal queues
    void poll(float nowSec);

    int  clientCount() const { return (int)m_clients.size(); }
    bool hasClient(uint8_t pid) const;

    // Send to one client
    void sendTo(uint8_t pid, const void* data, int len);
    // Broadcast to all active clients
    void broadcast(const void* data, int len);
    // Broadcast except one
    void broadcastExcept(uint8_t exceptPid, const void* data, int len);

    // Queue accessors
    bool pollEnvelope(Envelope& out);

    // Timeout check – returns pids that timed out (removes them internally)
    std::vector<uint8_t> checkTimeouts(float nowSec, float timeoutSec = 30.f);

    // Register a new client; returns assigned pid or 0xFF on failure
    uint8_t registerClient(const sockaddr_in& addr, const char* name, float nowSec);

    const ClientRecord* getClient(uint8_t pid) const;
    std::vector<uint8_t> activePids() const;

private:
    UdpSocket                m_sock;
    std::vector<ClientRecord> m_clients;
    std::queue<Envelope>     m_queue;
    uint8_t                  m_nextPid = 0;
};

// ClientNet
class ClientNet
{
public:
    bool init(const std::string& serverIp, uint16_t port);
    void poll();

    bool    connected()  const { return m_connected; }
    uint8_t playerId()   const { return m_pid; }

    void send(const void* data, int len);

    bool pollEnvelope(Envelope& out);

private:
    UdpSocket   m_sock;
    sockaddr_in m_server{};
    bool        m_connected = false;
    uint8_t     m_pid       = 0xFF;
    std::queue<Envelope> m_queue;

    friend class GameClient;  // GameClient sets m_connected/m_pid on ack
};

// ── LAN discovery ──────────────────────────────────────────────────────────────
struct DiscoveredServer
{
    std::string ip;
    uint16_t    port        = NET_PORT;
    uint8_t     playerCount = 0;
    uint8_t     maxPlayers  = MAX_PLAYERS;
    bool        inGame      = false;
    char        playerNames[MAX_PLAYERS][16]{};
};

// Sends a broadcast query and collects responses for ~waitMs milliseconds
std::vector<DiscoveredServer> scanLAN(uint16_t port = NET_PORT, int waitMs = 800);
