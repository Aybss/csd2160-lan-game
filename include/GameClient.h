#pragma once
#include "Network.h"
#include "Common.h"
#include "Tank.h"          // for Obstacle struct
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
    //  Network 
    ClientNet   m_net;
    std::string m_username;
    uint8_t     m_pid = 0xFF;

    //  Phase 
    ClientPhase m_phase = ClientPhase::CONNECTING;

    //  Lobby 
    PktLobbyState m_lobby{};
    bool          m_ready   = false;
    uint8_t       m_selSkin = 0;

    //  Shop 
    PktProfileUpdate m_profile{};
    int              m_shopCursor = 0;

    //  In-game 
    PktGameState  m_gameState{};
    uint32_t      m_lastSeq  = 0;
    uint32_t      m_inputSeq = 0;
    uint32_t      m_mapSeed  = 0;
    std::vector<Obstacle> m_obstacles;

    // Interpolation
    PktGameState  m_prevState{};
    float         m_interpT  = 0.f;
    float         m_stateAge = 0.f;

    //  End of match 
    PktMatchOver  m_matchOver{};

    //  Chat 
    std::deque<std::string> m_chatHistory;
    std::string             m_chatInput;
    bool                    m_chatActive = false;

    //  Round wins 
    uint8_t m_roundWins[MAX_PLAYERS]{};

    //  SFML assets 
    sf::Font        m_font;
    bool            m_fontLoaded = false;

    // SFML 3: sf::Sound has no default constructor - use optional
    sf::SoundBuffer              m_sbShoot, m_sbHit, m_sbDead;
    std::optional<sf::Sound>     m_sndShoot, m_sndHit, m_sndDead;
    sf::Music                    m_bgm;
    bool                         m_audioOk = false;

    //  Timing 
    float m_dt = 0.f;

    //  Methods 
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

    void drawTank     (sf::RenderWindow& w, const PlayerState& ps, uint8_t pid);
    void drawBullet   (sf::RenderWindow& w, const BulletState& bs);
    void drawHUD      (sf::RenderWindow& w);
    void drawChat     (sf::RenderWindow& w);
    void drawObstacles(sf::RenderWindow& w);

    void generateObstacles(uint32_t seed);
    void loadAssets();

    sf::Color   skinColor(uint8_t skin) const;
    std::string pidName(uint8_t pid) const;
    sf::Text    makeText(const std::string& s, unsigned size, sf::Color col = sf::Color::White);
};
