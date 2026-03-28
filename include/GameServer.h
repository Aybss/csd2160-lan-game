#pragma once
#include "Network.h"
#include "Tank.h"
#include "Bullet.h"
#include "Persistence.h"
#include "Common.h"
#include <vector>
#include <array>
#include <string>
#include <chrono>

enum class ServerPhase { LOBBY, IN_GAME, ROUND_OVER, MATCH_OVER };

class GameServer
{
public:
    GameServer(uint16_t port);
    void run();

private:
    ServerNet   m_net;
    Persistence m_db;

    ServerPhase m_phase = ServerPhase::LOBBY;
    float       m_now   = 0.f;
    float       m_announceTimer = 0.f;  // LAN broadcast timer

    struct LobbyPlayer {
        bool    active = false;
        char    name[16]{};
        bool    ready  = false;
        uint8_t skin   = 0;
        bool    isBot  = false;
        bool isAnonymous = false; // true = do NOT persist stats at match end
    };
    std::array<LobbyPlayer, MAX_PLAYERS> m_lobby{};
    uint8_t m_hostPid = 0xFF;   // 0xFF = no host yet

    // Server long-term Curve25519 keypair (loaded/generated on startup, stored in server.cfg)
    uint8_t m_serverPk[32]{};
    uint8_t m_serverSk[32]{};

    // Pending auth state: clients that have done key exchange but not yet registered
    struct PendingAuth
    {
        sockaddr_in addr{};
        char name[16]{};
        AuthMode mode = AuthMode::ANONYMOUS;
        uint8_t challengeNonce[32]{};
        uint8_t sessionRxKey[32]{}; // server rx = messages FROM this client
        uint8_t sessionTxKey[32]{}; // server tx = messages TO this client
        uint64_t nonceTx = 0;       // for outgoing encrypted-auth packets
        float timestamp = 0.f;
    };
    std::vector<PendingAuth> m_pendingAuth;

    //  Bot AI state 
    struct BotState {
        bool    active    = false;
        float   thinkTimer = 0.f;   // seconds until next decision
        uint8_t targetPid  = 0xFF;  // which player to chase
        float   aimAngle   = 0.f;   // desired heading
    };
    std::array<BotState, MAX_PLAYERS> m_bots{};

    // A*
    struct AStarNode {
        int r, c;
        float gCost; // Distance from start
        float hCost; // Heuristic (distance to target)
        float fCost() const { return gCost + hCost; }
        bool operator>(const AStarNode& other) const { return fCost() > other.fCost(); }
    };

    // Match
    std::array<uint8_t, MAX_PLAYERS> m_roundWins{};
    std::array<Tank,    MAX_PLAYERS> m_tanks{};
    std::vector<Bullet>              m_bullets;
    std::vector<Obstacle>            m_obstacles;
    int      m_aliveCount = 0;
    float    m_roundTimer = 0.f;
    float    m_stateTimer = 0.f;
    uint32_t m_stateSeq   = 0;
    uint32_t m_mapSeed    = 0;

    std::array<PktInput, MAX_PLAYERS> m_inputs{};

    // Powerups
    std::array<PowerupState, MAX_POWERUPS> m_powerups{};
    std::array<float, MAX_POWERUPS>        m_powerupRespawn{};

    // Explosive barrels
    std::array<BarrelState, MAX_BARRELS>   m_barrels{};

    void handlePacket(const Envelope& e);
    void handleConnect(const Envelope& e);
    void handlePlayerReady(const PktPlayerReady& p, uint8_t pid);
    void handleInput(const PktInput& p);
    void handleChat(const PktChat& p);
    void handleBuySkin(const PktBuySkin& p);
    void handleDisconnect(uint8_t pid);
    void handleVoice(const Envelope& e);
    void handleAddBot(const PktAddBot& p);
    void handleKickBot(const PktKickBot& p);

    // Auth handshake handlers
    void loadOrGenServerKeypair();
    void handleKeyExchange(const Envelope &e);
    void handleAuthResponse(const Envelope &e, PendingAuth pa, const std::vector<uint8_t> &decrypted);
    void sendEncryptedToPending(PendingAuth &pa, const void *plaintext, int len);
    bool decryptFromPending(const PendingAuth &pa, const Envelope &e, std::vector<uint8_t> &out);

    // Shared lobby-slot setup used by both handleConnect and handleAuthResponse.
    // Registers the client with the network layer, fills the lobby slot, and
    // promotes them to host if the lobby was empty.  Returns 0xFF on failure.
    uint8_t registerPlayerInLobby(const sockaddr_in &addr, const char *name,
                                   bool isAnonymous, float nowSec);

    void promoteNextHost();          // reassign hostPid after host leaves
    void updateBots(float dt);       // simple AI tick

    void broadcastLobbyState();
    bool allReady() const;
    void startGame();
    void announcePresence(const sockaddr_in* replyTo = nullptr);  // UDP broadcast/unicast for LAN discovery
    void handlePlayerListReq(const sockaddr_in& from);           // respond with registered player records

    void updateGame(float dt);
    void checkBulletCollisions();
    void checkPowerupCollisions();
    void checkRoundEnd();
    void endRound(uint8_t winner);
    void endMatch(uint8_t winner);
    void resetRound();
    void spawnPowerups();
    void spawnBarrels();
    void checkBarrelCollisions();
    void explodeBarrel(int idx);
    void generateMap(uint32_t seed);
    void broadcastGameState();
    void sendProfileUpdate(uint8_t pid);

    static std::array<std::pair<float,float>, MAX_PLAYERS> spawnPositions();
};
