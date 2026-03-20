#pragma once
#include <cstdint>
#include <string>
#include <array>

//  Window 
constexpr int   WIN_W           = 1024;
constexpr int   WIN_H           = 768;
constexpr int   MAP_W           = 1024;
constexpr int   MAP_H           = 768;

//  Gameplay 
constexpr int   MAX_PLAYERS     = 6;
constexpr int   MIN_PLAYERS     = 2;   // min to start
constexpr float TANK_SPEED      = 160.f;
constexpr float TANK_ROT_SPEED  = 140.f;  // degrees/sec
constexpr float BULLET_SPEED    = 400.f;
constexpr float TANK_RADIUS     = 18.f;
constexpr float BULLET_RADIUS   = 5.f;
constexpr float BULLET_LIFETIME = 3.f;   // seconds
constexpr int   TANK_MAX_HP     = 3;
constexpr int   ROUNDS_TO_WIN   = 3;
constexpr int   XP_PER_KILL     = 50;
constexpr int   XP_PER_WIN      = 100;
constexpr int   COINS_PER_KILL  = 20;
constexpr int   COINS_PER_WIN   = 50;
constexpr int   XP_PER_LEVEL    = 200;
constexpr uint16_t NET_PORT     = 54100;

//  Skin definitions (index - colour packed RGBA) 
constexpr int SKIN_COUNT = 5;
// Prices in coins (index 0 = default, free)
constexpr int SKIN_PRICES[SKIN_COUNT] = { 0, 40, 80, 120, 200 };

//  Map obstacle
struct Obstacle { float x, y, w, h; };

//  Powerups 
constexpr int   MAX_POWERUPS     = 4;
constexpr float POWERUP_RADIUS   = 14.f;
constexpr float POWERUP_DURATION = 6.f;   // seconds buff lasts

enum class PowerupType : uint8_t
{
    SPEED      = 0,   // 1.8x movement speed
    RAPIDFIRE  = 1,   // 0.15s cooldown instead of 0.5s
    SHIELD     = 2,   // absorbs next hit
};

//  Voice 
constexpr int VOICE_FRAME_MS    = 20;    // Opus frame size in ms
constexpr int VOICE_SAMPLE_RATE = 16000; // 16kHz mono
constexpr int VOICE_MAX_BYTES   = 400;   // max compressed bytes per frame

//  Chat 
constexpr int MAX_CHAT_MSG  = 48;
constexpr int MAX_CHAT_HIST = 12;

//  Obstacle grid 
constexpr int OBS_COLS = 8;
constexpr int OBS_ROWS = 6;

// Packet types
enum class PktType : uint8_t
{
    // Lobby
    CONNECT         = 0,
    CONNECT_ACK     = 1,
    LOBBY_STATE     = 2,   // server-all: full lobby snapshot
    PLAYER_READY    = 3,   // client-server
    GAME_START      = 4,   // server-all
    DISCONNECT      = 5,

    // In-game
    INPUT           = 10,
    GAME_STATE      = 11,  // server-all: full game snapshot
    BULLET_SPAWN    = 12,
    PLAYER_HIT      = 13,
    PLAYER_DEAD     = 14,
    ROUND_OVER      = 15,
    MATCH_OVER      = 16,

    // Chat
    CHAT            = 20,

    // Shop / profile
    BUY_SKIN        = 30,
    BUY_SKIN_ACK    = 31,
    PROFILE_UPDATE  = 32,  // server-client: updated coins/xp/skins

    // Powerups
    POWERUP_STATE   = 50,  // server-all: powerup positions/types in game state
    POWERUP_COLLECT = 51,  // server-all: a player collected a powerup

    // Voice chat
    VOICE_DATA      = 60,  // client-server-others: compressed audio chunk

    // Utility
    PING            = 40,
    PONG_PKT        = 41,
    ACK             = 42,
};

// Packet structs  (all packed – safe over loopback/LAN)
#pragma pack(push,1)

//  Lobby 
struct PktConnect
{
    PktType type = PktType::CONNECT;
    char    name[16]{};   // username
};

struct PktConnectAck
{
    PktType type    = PktType::CONNECT_ACK;
    uint8_t pid     = 0;
    uint8_t denied  = 0;  // 1 = lobby full / game in progress
};

struct LobbySlot
{
    uint8_t  active  = 0;
    char     name[16]{};
    uint8_t  ready   = 0;
    uint8_t  skin    = 0;
    uint16_t level   = 0;
    uint16_t wins    = 0;
};

struct PktLobbyState
{
    PktType    type = PktType::LOBBY_STATE;
    LobbySlot  slots[MAX_PLAYERS]{};
    uint8_t    hostPid = 0;
};

struct PktPlayerReady
{
    PktType type    = PktType::PLAYER_READY;
    uint8_t pid     = 0;
    uint8_t ready   = 0;
    uint8_t skin    = 0;
};

struct PktGameStart
{
    PktType type = PktType::GAME_START;
    // obstacle seed so all clients generate same map
    uint32_t mapSeed = 0;
};

struct PktDisconnect
{
    PktType type = PktType::DISCONNECT;
    uint8_t pid  = 0;
};

//  In-game 
struct PktInput
{
    PktType  type    = PktType::INPUT;
    uint8_t  pid     = 0;
    uint8_t  forward = 0;
    uint8_t  back    = 0;
    uint8_t  left    = 0;
    uint8_t  right   = 0;
    uint8_t  fire    = 0;
    uint32_t seq     = 0;
};

struct PlayerState
{
    float   x = 0, y = 0;
    float   angle = 0;
    uint8_t hp    = TANK_MAX_HP;
    uint8_t alive = 1;
    uint8_t skin  = 0;
    int16_t kills = 0;
};

struct BulletState
{
    uint8_t  active = 0;
    uint8_t  owner  = 0;
    float    x = 0, y = 0;
    float    vx = 0, vy = 0;
    float    life = BULLET_LIFETIME;
};

// PowerupState must be declared before PktGameState which embeds it
struct PowerupState
{
    uint8_t     active = 0;
    PowerupType ptype  = PowerupType::SPEED;
    float       x = 0, y = 0;
};

struct PktGameState
{
    PktType      type = PktType::GAME_STATE;
    uint32_t     seq  = 0;
    PlayerState  players[MAX_PLAYERS]{};
    BulletState  bullets[MAX_PLAYERS * 3]{};
    PowerupState powerups[MAX_POWERUPS]{};
    // Per-player active buff bitmask: bit0=speed, bit1=rapidfire, bit2=shield
    uint8_t      buffs[MAX_PLAYERS]{};
};

struct PktBulletSpawn
{
    PktType type  = PktType::BULLET_SPAWN;
    uint8_t owner = 0;
    float   x = 0, y = 0, vx = 0, vy = 0;
};

struct PktPlayerHit
{
    PktType type     = PktType::PLAYER_HIT;
    uint8_t victim   = 0;
    uint8_t attacker = 0;
    uint8_t hpLeft   = 0;
};

struct PktPlayerDead
{
    PktType type     = PktType::PLAYER_DEAD;
    uint8_t victim   = 0;
    uint8_t attacker = 0;
};

struct PktRoundOver
{
    PktType type      = PktType::ROUND_OVER;
    uint8_t winner    = 0xFF;  // 0xFF = draw
    uint8_t roundWins[MAX_PLAYERS]{};
};

struct PktMatchOver
{
    PktType  type      = PktType::MATCH_OVER;
    uint8_t  winner    = 0;
    int16_t  kills[MAX_PLAYERS]{};
    uint16_t xpGained[MAX_PLAYERS]{};
    uint16_t coinsGained[MAX_PLAYERS]{};
    // Top-5 leaderboard (by total wins, server-side)
    char     lbName[5][16]{};
    uint16_t lbWins[5]{};
    uint16_t lbLevel[5]{};
};

//  Chat 
struct PktChat
{
    PktType type = PktType::CHAT;
    uint8_t pid  = 0;
    char    msg[MAX_CHAT_MSG]{};
};

//  Shop 
struct PktBuySkin
{
    PktType type    = PktType::BUY_SKIN;
    uint8_t pid     = 0;
    uint8_t skinIdx = 0;
};

struct PktBuySkinAck
{
    PktType type    = PktType::BUY_SKIN_ACK;
    uint8_t success = 0;
    uint8_t skinIdx = 0;
    uint32_t coinsLeft = 0;
};

struct PktProfileUpdate
{
    PktType  type         = PktType::PROFILE_UPDATE;
    uint32_t xp           = 0;
    uint16_t level        = 0;
    uint32_t coins        = 0;
    uint8_t  ownedSkins   = 0;  // bitmask
    uint16_t totalWins    = 0;
};

//  Powerups 
struct PktPowerupCollect
{
    PktType     type  = PktType::POWERUP_COLLECT;
    uint8_t     pid   = 0;        // who collected it
    uint8_t     idx   = 0;        // which powerup slot
    PowerupType ptype = PowerupType::SPEED;
};

//  Voice 
struct PktVoiceData
{
    PktType  type    = PktType::VOICE_DATA;
    uint8_t  pid     = 0;
    uint16_t length  = 0;                      // bytes of compressed data
    uint8_t  data[VOICE_MAX_BYTES]{};
};

//  Utility 
struct PktAck
{
    PktType  type = PktType::ACK;
    uint32_t seq  = 0;
};

#pragma pack(pop)
