#pragma once
#include "Network.h"
#include "Common.h"
#include "Tank.h"
#include "VoiceChat.h"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <optional>

enum class ClientPhase { CONNECTING, KEY_EXCHANGE, AUTHENTICATING, LOBBY, SHOP, IN_GAME, ROUND_OVER, MATCH_OVER, DISCONNECTED };

// Authentication info supplied from the menu before connecting
struct AuthInfo
{
    AuthMode    mode = AuthMode::ANONYMOUS;
    std::string password;
};

class GameClient
{
public:
    GameClient(const std::string& serverIp, uint16_t port,
               const std::string& username, const AuthInfo& auth);
    void run(sf::RenderWindow& window);

    bool        getAuthFailed() const { return m_authFailed; }
    std::string getAuthError()  const { return m_authError; }

private:
    ClientNet   m_net;
    std::string m_username;
    uint8_t     m_pid = 0xFF;

    // Auth
    AuthInfo    m_auth;
    uint8_t     m_clientPk[32]{};    // ephemeral Curve25519 public key
    uint8_t     m_clientSk[32]{};    // ephemeral Curve25519 secret key
    bool        m_authFailed  = false;
    std::string m_authError;

    ClientPhase m_phase = ClientPhase::CONNECTING;

    PktLobbyState    m_lobby{};
    bool             m_ready   = false;
    uint8_t          m_selSkin = 0;

    PktProfileUpdate m_profile{};
    int              m_shopCursor = 0;

    PktGameState  m_gameState{};
    uint32_t      m_lastSeq  = 0;
    uint32_t      m_inputSeq = 0;
    uint32_t      m_mapSeed  = 0;
    std::vector<Obstacle> m_obstacles;

    PktGameState  m_prevState{};
    float         m_stateAge = 0.f;

    PktMatchOver  m_matchOver{};

    std::deque<std::string> m_chatHistory;
    std::string             m_chatInput;
    bool                    m_chatActive = false;

    uint8_t m_roundWins[MAX_PLAYERS]{};

    //  SFML assets 
    sf::Font        m_font;
    bool            m_fontLoaded = false;

    sf::SoundBuffer              m_sbShoot, m_sbHit, m_sbDead, m_sbPowerup;
    std::optional<sf::Sound>     m_sndShoot, m_sndHit, m_sndDead, m_sndPowerup;
    sf::Music                    m_bgm;
    bool                         m_audioOk = false;

    //  Voice chat 
    VoiceChat m_voice;
    bool      m_voiceInit = false;

    //  Explosion particles 
    struct Explosion {
        float x, y;       // centre
        float timer;      // counts down from EXPLOSION_DURATION
        float maxTimer;
    };
    std::vector<Explosion> m_explosions;
    float m_gameTime = 0.f;  // for shield pulse

    //  Skin textures (procedurally generated at startup) 
    // Each skin has a body texture and a turret texture
    sf::RenderTexture m_skinBodyRT[SKIN_COUNT];
    sf::RenderTexture m_skinTurretRT[SKIN_COUNT];
    sf::Texture       m_skinBodyTex[SKIN_COUNT];
    sf::Texture       m_skinTurretTex[SKIN_COUNT];
    bool              m_texturesGenerated = false;

    float m_dt = 0.f;

    uint8_t m_spectateTarget = 0xFF; // 0xFF means ALL (whole map). for spectating

    bool  m_returnToMenu = false;
    sf::View m_lbView;   // letterbox view, updated on resize, used by HUD draws

    //  Pause menu 
    bool  m_pauseOpen  = false;
    float m_volume     = 50.f;   // 0–100  music + SFX
    float m_voiceVolume = 80.f;  // 0–100  voice chat
    float m_keepaliveTimer = 0.f;
    std::string m_disconnectMsg;  // shown on DISCONNECTED screen
    bool  m_isAdmin    = false;   // true when we are the host/admin

    void processPackets();
    void sendInput(sf::RenderWindow& w);

    void updateLobby(sf::RenderWindow& w);
    void updateShop (sf::RenderWindow& w);

    void drawConnecting(sf::RenderWindow& w);
    void drawLobby     (sf::RenderWindow& w);
    void drawShop      (sf::RenderWindow& w);
    void drawInGame    (sf::RenderWindow& w);
    void drawRoundOver (sf::RenderWindow& w);
    void drawMatchOver (sf::RenderWindow& w);
    void drawPauseMenu (sf::RenderWindow& w);
    void drawDisconnected(sf::RenderWindow& w);
    void drawBackground(sf::RenderWindow& w);
    void drawBorder    (sf::RenderWindow& w);
    void drawScreenBorder(sf::RenderWindow& w, float margin);

    void drawTank      (sf::RenderWindow& w, const PlayerState& ps, uint8_t pid);
    void drawBullet    (sf::RenderWindow& w, const BulletState& bs);
    void drawPowerup   (sf::RenderWindow& w, const PowerupState& ps);
    void drawBarrel    (sf::RenderWindow& w, const BarrelState& bs);
    void drawExplosions(sf::RenderWindow& w);
    void drawHUD       (sf::RenderWindow& w);
    void drawMinimap   (sf::RenderWindow& w);
    void drawChat      (sf::RenderWindow& w);
    void drawObstacles (sf::RenderWindow& w);

    void generateObstacles(uint32_t seed);
    void loadAssets();
    void generateSkinTextures();

    struct BulletSnapshot {
        float x, y;
        bool active;
    };

    struct Snapshot {
        float x[MAX_PLAYERS];
        float y[MAX_PLAYERS];
        float angle[MAX_PLAYERS];
        bool alive[MAX_PLAYERS];
        BulletSnapshot bullets[MAX_BULLETS];
    };

    std::vector<Snapshot> m_killCamBuffer;
    bool m_playingKillCam = false;
    float m_killCamTimer = 0.f;
    int m_killCamWinnerPid = -1;

    // Control settings
    const float REPLAY_DURATION = 7.5f; // Total time for the sequence
    const float PLAYBACK_SPEED = 0.5f;   // 0.5 = Half speed (Slow motion)
    const float POST_REPLAY_PAUSE = 1.5f; // Extra time to freeze on the final frame

    sf::Color   skinColor(uint8_t skin) const;
    sf::Color   powerupColor(PowerupType t) const;
    std::string powerupName(PowerupType t) const;
    std::string pidName(uint8_t pid) const;
    sf::Text    makeText(const std::string& s, unsigned size, sf::Color col = sf::Color::White);
};
