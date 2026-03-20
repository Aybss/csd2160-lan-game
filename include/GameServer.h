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

    struct LobbyPlayer {
        bool    active = false;
        char    name[16]{};
        bool    ready  = false;
        uint8_t skin   = 0;
    };
    std::array<LobbyPlayer, MAX_PLAYERS> m_lobby{};
    uint8_t m_hostPid = 0;

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
    std::array<float, MAX_POWERUPS>        m_powerupRespawn{};  // countdown to respawn

    void handlePacket(const Envelope& e);
    void handleConnect(const Envelope& e);
    void handlePlayerReady(const PktPlayerReady& p, uint8_t pid);
    void handleInput(const PktInput& p);
    void handleChat(const PktChat& p);
    void handleBuySkin(const PktBuySkin& p);
    void handleDisconnect(uint8_t pid);
    void handleVoice(const Envelope& e);

    void broadcastLobbyState();
    bool allReady() const;
    void startGame();

    void updateGame(float dt);
    void checkBulletCollisions();
    void checkPowerupCollisions();
    void checkRoundEnd();
    void endRound(uint8_t winner);
    void endMatch(uint8_t winner);
    void resetRound();
    void spawnPowerups();
    void generateMap(uint32_t seed);
    void broadcastGameState();
    void sendProfileUpdate(uint8_t pid);

    static std::array<std::pair<float,float>, MAX_PLAYERS> spawnPositions();
};
