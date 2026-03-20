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

enum class ClientPhase { CONNECTING, LOBBY, SHOP, IN_GAME, ROUND_OVER, MATCH_OVER };

class GameClient
{
public:
    GameClient(const std::string& serverIp, uint16_t port, const std::string& username);
    void run();

private:
    ClientNet   m_net;
    std::string m_username;
    uint8_t     m_pid = 0xFF;

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

    // ── SFML assets ───────────────────────────────────────────────────────
    sf::Font        m_font;
    bool            m_fontLoaded = false;

    sf::SoundBuffer              m_sbShoot, m_sbHit, m_sbDead, m_sbPowerup;
    std::optional<sf::Sound>     m_sndShoot, m_sndHit, m_sndDead, m_sndPowerup;
    sf::Music                    m_bgm;
    bool                         m_audioOk = false;

    // ── Voice chat ────────────────────────────────────────────────────────
    VoiceChat m_voice;
    bool      m_voiceInit = false;

    // ── Explosion particles ───────────────────────────────────────────────
    struct Explosion {
        float x, y;       // centre
        float timer;      // counts down from EXPLOSION_DURATION
        float maxTimer;
    };
    std::vector<Explosion> m_explosions;
    float m_gameTime = 0.f;  // for shield pulse

    // ── Skin textures (procedurally generated at startup) ─────────────────
    // Each skin has a body texture and a turret texture
    sf::RenderTexture m_skinBodyRT[SKIN_COUNT];
    sf::RenderTexture m_skinTurretRT[SKIN_COUNT];
    sf::Texture       m_skinBodyTex[SKIN_COUNT];
    sf::Texture       m_skinTurretTex[SKIN_COUNT];
    bool              m_texturesGenerated = false;

    float m_dt = 0.f;

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

    void drawTank      (sf::RenderWindow& w, const PlayerState& ps, uint8_t pid);
    void drawBullet    (sf::RenderWindow& w, const BulletState& bs);
    void drawPowerup   (sf::RenderWindow& w, const PowerupState& ps);
    void drawBarrel    (sf::RenderWindow& w, const BarrelState& bs);
    void drawExplosions(sf::RenderWindow& w);
    void drawHUD       (sf::RenderWindow& w);
    void drawChat      (sf::RenderWindow& w);
    void drawObstacles (sf::RenderWindow& w);

    void generateObstacles(uint32_t seed);
    void loadAssets();
    void generateSkinTextures();

    sf::Color   skinColor(uint8_t skin) const;
    sf::Color   powerupColor(PowerupType t) const;
    std::string powerupName(PowerupType t) const;
    std::string pidName(uint8_t pid) const;
    sf::Text    makeText(const std::string& s, unsigned size, sf::Color col = sf::Color::White);
};
