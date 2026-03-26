#pragma once
#include <cstdint>
#include <string>
#include <array>

//  Window 
constexpr int   WIN_W           = 1024;
constexpr int   WIN_H           = 768;
constexpr int   MAP_W           = 2400;   // large playfield
constexpr int   MAP_H           = 1800;
constexpr int   MAP_OFFSET_X    = 0;      // no fixed margin; camera follows player
constexpr int   MAP_OFFSET_Y    = 0;

//  Gameplay 
constexpr int   MAX_PLAYERS     = 6;
constexpr int   MAX_BULLETS     = 18; // cap for server and client
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

//  Skin definitions 
constexpr int SKIN_COUNT = 6;
// Prices in coins (index 0 = default, free)
constexpr int SKIN_PRICES[SKIN_COUNT] = { 0, 40, 80, 120, 160, 200 };

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
constexpr int OBS_COLS = 20;
constexpr int OBS_ROWS = 15;

//  Explosive barrels 
constexpr int   MAX_BARRELS        = 6;
constexpr float BARREL_RADIUS      = 14.f;
constexpr float BARREL_EXPLODE_R   = 80.f;  // explosion damage radius
constexpr float EXPLOSION_DURATION = 0.6f;  // client-side visual duration


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

    // Barrels
    BARREL_EXPLODE  = 55,  // server->all: barrel exploded

    // Voice chat
    VOICE_DATA      = 60,  // client-server-others: compressed audio chunk

    // Utility
    PING            = 40,
    PONG_PKT        = 41,
    ACK             = 42,

    // LAN Discovery
    SERVER_ANNOUNCE = 70,   // server-broadcast: "I exist"
    SERVER_QUERY    = 71,   // client-broadcast: "who's there?"

    // Admin / bot
    ADD_BOT         = 80,   // host-server: add a bot player
    KICK_BOT = 81,

    // Auth / Key Exchange
    KEY_EXCHANGE = 90,     // client->server: ephemeral Curve25519 public key
    KEY_EXCHANGE_ACK = 91, // server->client: server long-term public key
    AUTH_CHALLENGE = 92,   // server->client (encrypted): random nonce + salt
    AUTH_RESPONSE = 93,    // client->server (encrypted): auth data
    AUTH_RESULT = 94,      // server->client (encrypted): result + assigned pid
    ENCRYPTED = 95,        // wrapper: [PktType][nonce:24][ciphertext]
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
    uint8_t  isBot   = 0;   // 1 if this slot is a server-controlled bot
    uint8_t  isAnonymous = 0; // 1 if this slot is a guest (stats not saved)
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

struct BarrelState
{
    uint8_t active = 0;
    float   x = 0, y = 0;
};

struct PktBarrelExplode
{
    PktType type  = PktType::BARREL_EXPLODE;
    uint8_t idx   = 0;    // which barrel slot
    float   x = 0, y = 0; // explosion centre
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
    BarrelState  barrels[MAX_BARRELS]{};
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

//  Admin / Bot 
struct PktAddBot
{
    PktType type       = PktType::ADD_BOT;
    uint8_t requestPid = 0;   // must match server's hostPid to be accepted
};

struct PktKickBot {
    PktType type = PktType::KICK_BOT;
    uint8_t requestPid = 0; // Must be host
    uint8_t botPid = 0;     // Target bot
};

//  LAN Discovery 
struct PktServerAnnounce
{
    PktType  type         = PktType::SERVER_ANNOUNCE;
    uint16_t port         = NET_PORT;
    uint8_t  playerCount  = 0;
    uint8_t  maxPlayers   = MAX_PLAYERS;
    uint8_t  inGame       = 0;          // 1 if game in progress (not joinable)
    char     playerNames[MAX_PLAYERS][16]{};  // names of connected players
};

struct PktServerQuery
{
    PktType type = PktType::SERVER_QUERY;
};

//  Auth mode
enum class AuthMode : uint8_t
{
    ANONYMOUS = 0,
    LOGIN = 1,
    REGISTER = 2
};

//  Key exchange (plaintext — public keys are not secret)
//  name is sent early so the server can look up the salt before sending the challenge.
struct PktKeyExchange
{
    PktType type = PktType::KEY_EXCHANGE;
    char name[16]{}; // username announcing itself
    AuthMode mode = AuthMode::ANONYMOUS;
    uint8_t clientPk[32]{}; // client ephemeral Curve25519 public key
};
struct PktKeyExchangeAck
{
    PktType type = PktType::KEY_EXCHANGE_ACK;
    uint8_t serverPk[32]{}; // server long-term Curve25519 public key
};

//  Encrypted envelope header
//  Full wire layout: [PktType::ENCRYPTED (1 byte)] [nonce (24 bytes)] [ciphertext (payload + 16 MAC)]
struct EncryptedEnvelopeHdr
{
    PktType type = PktType::ENCRYPTED;
    uint8_t nonce[24]{};
    // ciphertext bytes follow immediately in the packet buffer
};

//  Auth inner packets (sent encrypted inside EncryptedEnvelope)
struct PktAuthChallenge
{
    PktType type = PktType::AUTH_CHALLENGE;
    uint8_t challengeNonce[32]{}; // random per-session challenge for HMAC signing
    char saltHex[33]{};           // LOGIN: stored per-user Argon2id salt (hex); others: empty
};
struct PktAuthResponse
{
    PktType type = PktType::AUTH_RESPONSE;
    AuthMode mode = AuthMode::ANONYMOUS;
    char name[16]{};
    char saltHex[33]{};     // REGISTER: client-generated salt (hex of 16 bytes); others: unused
    uint8_t authData[32]{}; // LOGIN:    HMAC-SHA256(Argon2id(pw,salt), challengeNonce)
                            // REGISTER: raw 32-byte Argon2id(pw, clientSalt) stored as authKey in DB
                            // ANONYMOUS: all zeros
};
struct PktAuthResult
{
    PktType type = PktType::AUTH_RESULT;
    uint8_t result = 0; // 0=ok, 1=bad_credentials, 2=name_taken, 3=game_in_progress, 4=lobby_full, 5=guest_name_in_use
    uint8_t pid = 0xFF; // assigned pid on success
    char reason[32]{};
};

#pragma pack(pop)
