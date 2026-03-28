#include "GameClient.h"
#include "VoiceChat.h"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>

static constexpr float DEG2RAD = 3.14159265f / 180.f;

//  Skin base colours (used as fallback and for HUD tints) 
static const sf::Color SKIN_COLORS[SKIN_COUNT] = {
    sf::Color(80,160,80),     // 0 Army
    sf::Color(110,90,60),     // 1 Camo
    sf::Color(200,170,110),   // 2 Desert
    sf::Color(220,230,240),   // 3 Arctic
    sf::Color(50,55,60),      // 4 Stealth
    sf::Color(210,175,55),    // 5 Gold
};
static const std::string SKIN_NAMES[SKIN_COUNT] = {
    "Army   (free)",
    "Camo   (40c)",
    "Desert (80c)",
    "Arctic (120c)",
    "Stealth(160c)",
    "Gold   (200c)"
};

GameClient::GameClient(const std::string& serverIp, uint16_t port, const std::string& username)
    : m_username(username)
{
    m_net.init(serverIp, port);
    loadAssets();
}

void GameClient::loadAssets()
{
    const char* fonts[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf"
    };
    for(auto& f : fonts)
        if(m_font.openFromFile(f)){ m_fontLoaded=true; break; }

    // Sounds – graceful fallback if files missing
    // SFML 3: SoundBuffer uses openFromFile; Sound requires buffer in constructor
    if(m_sbShoot.loadFromFile("assets/shoot.wav"))    m_sndShoot.emplace(m_sbShoot);
    if(m_sbHit.loadFromFile("assets/hit.wav"))        m_sndHit.emplace(m_sbHit);
    if(m_sbDead.loadFromFile("assets/dead.wav"))      m_sndDead.emplace(m_sbDead);
    if(m_sbPowerup.loadFromFile("assets/powerup.wav")) m_sndPowerup.emplace(m_sbPowerup);
    if(m_bgm.openFromFile("assets/bgm.ogg"))
    {
        m_bgm.setLooping(true);
        m_bgm.setVolume(35.f);
        m_bgm.play();
        m_audioOk = true;
    }

    generateSkinTextures();
}

sf::Text GameClient::makeText(const std::string& s, unsigned size, sf::Color col)
{
    sf::Text t(m_font, s, size);
    t.setFillColor(col);
    return t;
}

sf::Color GameClient::skinColor(uint8_t skin) const
{
    return SKIN_COLORS[skin % SKIN_COUNT];
}

sf::Color GameClient::powerupColor(PowerupType t) const
{
    switch(t){
        case PowerupType::SPEED:     return sf::Color(0, 220, 255);
        case PowerupType::RAPIDFIRE: return sf::Color(255, 100, 0);
        case PowerupType::SHIELD:    return sf::Color(100, 255, 100);
        default:                     return sf::Color::White;
    }
}

std::string GameClient::powerupName(PowerupType t) const
{
    switch(t){
        case PowerupType::SPEED:     return "Speed Boost";
        case PowerupType::RAPIDFIRE: return "Rapid Fire";
        case PowerupType::SHIELD:    return "Shield";
        default:                     return "Unknown";
    }
}

std::string GameClient::pidName(uint8_t pid) const
{
    if(pid < MAX_PLAYERS && m_lobby.slots[pid].active)
        return std::string(m_lobby.slots[pid].name);
    return "P" + std::to_string((int)pid);
}

void GameClient::generateSkinTextures()
{
    // Tank body size in texture space
    const unsigned TW = 64;   // texture width
    const unsigned TH = 52;   // texture height (body)
    const unsigned BW = 44;   // barrel width
    const unsigned BH = 14;   // barrel height

    auto drawRivet = [](sf::RenderTexture& rt, float x, float y){
        sf::CircleShape r(3.f);
        r.setOrigin({3.f,3.f});
        r.setPosition({x,y});
        r.setFillColor(sf::Color(30,30,30,180));
        rt.draw(r);
    };

    for(int s=0;s<SKIN_COUNT;s++)
    {
        //  Body texture 
        if (!m_skinBodyRT[s].resize({ TW, TH })) continue;
        m_skinBodyRT[s].clear(sf::Color::Transparent);

        sf::Color base = SKIN_COLORS[s];
        sf::Color dark(
            (uint8_t)(base.r*0.6f),
            (uint8_t)(base.g*0.6f),
            (uint8_t)(base.b*0.6f));
        sf::Color light(
            (uint8_t)std::min(255,(int)(base.r*1.3f)),
            (uint8_t)std::min(255,(int)(base.g*1.3f)),
            (uint8_t)std::min(255,(int)(base.b*1.3f)));

        // Base body rectangle
        sf::RectangleShape body({(float)TW,(float)TH});
        body.setFillColor(base);
        m_skinBodyRT[s].draw(body);

        // Track strips (left and right edges)
        sf::RectangleShape track({10.f,(float)TH});
        track.setFillColor(dark);
        track.setPosition({0.f,0.f}); m_skinBodyRT[s].draw(track);
        track.setPosition({(float)TW-10.f,0.f}); m_skinBodyRT[s].draw(track);

        // Track lines
        for(int y=4;y<(int)TH;y+=8){
            sf::RectangleShape line({10.f,3.f});
            line.setFillColor(sf::Color(base.r/3,base.g/3,base.b/3));
            line.setPosition({0.f,(float)y});   m_skinBodyRT[s].draw(line);
            line.setPosition({(float)TW-10.f,(float)y}); m_skinBodyRT[s].draw(line);
        }

        // Skin-specific detail pattern
        switch(s)
        {
            case 0: // Army - simple rivets
                drawRivet(m_skinBodyRT[s], 18, 10);
                drawRivet(m_skinBodyRT[s], 46, 10);
                drawRivet(m_skinBodyRT[s], 18, 42);
                drawRivet(m_skinBodyRT[s], 46, 42);
                break;

            case 1: // Camo - irregular dark blobs
            {
                srand(42);
                for(int i=0;i<8;i++){
                    sf::CircleShape blob((float)(8+rand()%10));
                    blob.setFillColor(sf::Color(
                        (uint8_t)(base.r*0.55f),
                        (uint8_t)(base.g*0.7f),
                        (uint8_t)(base.b*0.4f), 180));
                    blob.setPosition({(float)(10+rand()%44),(float)(rand()%52)});
                    m_skinBodyRT[s].draw(blob);
                }
                break;
            }
            case 2: // Desert - diagonal stripes
            {
                for(int i=-2;i<8;i++){
                    sf::RectangleShape stripe({(float)TW*2.f,6.f});
                    stripe.setFillColor(sf::Color(
                        (uint8_t)(base.r*0.75f),
                        (uint8_t)(base.g*0.65f),
                        (uint8_t)(base.b*0.5f),160));
                    stripe.setRotation(sf::degrees(40.f));
                    stripe.setPosition({(float)(i*14-10),0.f});
                    m_skinBodyRT[s].draw(stripe);
                }
                break;
            }
            case 3: // Arctic - crack lines
            {
                for(int i=0;i<5;i++){
                    sf::RectangleShape crack({(float)(15+i*4),2.f});
                    crack.setFillColor(sf::Color(140,160,180,160));
                    crack.setRotation(sf::degrees((float)(i*33)));
                    crack.setPosition({(float)(12+i*8),(float)(8+i*7)});
                    m_skinBodyRT[s].draw(crack);
                }
                break;
            }
            case 4: // Stealth - angular edge highlight
            {
                sf::RectangleShape edge({(float)TW-20.f,2.f});
                edge.setFillColor(sf::Color(90,95,100,200));
                edge.setPosition({10.f,8.f});  m_skinBodyRT[s].draw(edge);
                edge.setPosition({10.f,(float)TH-10.f}); m_skinBodyRT[s].draw(edge);
                sf::RectangleShape diag({30.f,2.f});
                diag.setFillColor(sf::Color(80,85,90,180));
                diag.setRotation(sf::degrees(30.f));
                diag.setPosition({14.f,14.f}); m_skinBodyRT[s].draw(diag);
                diag.setPosition({30.f,14.f}); m_skinBodyRT[s].draw(diag);
                break;
            }
            case 5: // Gold - metallic shine stripe
            {
                sf::RectangleShape shine({(float)TW-20.f,8.f});
                shine.setFillColor(sf::Color(255,240,160,120));
                shine.setPosition({10.f,16.f});
                m_skinBodyRT[s].draw(shine);
                sf::RectangleShape shine2({(float)TW-20.f,3.f});
                shine2.setFillColor(sf::Color(255,255,200,80));
                shine2.setPosition({10.f,28.f});
                m_skinBodyRT[s].draw(shine2);
                // Dark border lines
                sf::RectangleShape border({(float)TW-20.f,2.f});
                border.setFillColor(sf::Color(160,120,0,200));
                border.setPosition({10.f,10.f}); m_skinBodyRT[s].draw(border);
                border.setPosition({10.f,(float)TH-12.f}); m_skinBodyRT[s].draw(border);
                break;
            }
        }

        // Top highlight strip
        sf::RectangleShape highlight({(float)TW-20.f,3.f});
        highlight.setFillColor(sf::Color(255,255,255,40));
        highlight.setPosition({10.f,4.f});
        m_skinBodyRT[s].draw(highlight);

        m_skinBodyRT[s].display();
        m_skinBodyTex[s] = m_skinBodyRT[s].getTexture();

        //  Turret texture 
        if (!m_skinTurretRT[s].resize({ BW, BH })) continue;
        m_skinTurretRT[s].clear(sf::Color::Transparent);

        sf::RectangleShape barrel({(float)BW,(float)BH});
        barrel.setFillColor(dark);
        m_skinTurretRT[s].draw(barrel);

        // Barrel highlight
        sf::RectangleShape bhl({(float)BW,3.f});
        bhl.setFillColor(sf::Color(255,255,255,50));
        bhl.setPosition({0.f,2.f});
        m_skinTurretRT[s].draw(bhl);

        // Barrel tip band
        sf::RectangleShape tip({8.f,(float)BH});
        tip.setFillColor(sf::Color(
            (uint8_t)(dark.r*0.7f),
            (uint8_t)(dark.g*0.7f),
            (uint8_t)(dark.b*0.7f)));
        tip.setPosition({(float)BW-8.f,0.f});
        m_skinTurretRT[s].draw(tip);

        m_skinTurretRT[s].display();
        m_skinTurretTex[s] = m_skinTurretRT[s].getTexture();
    }

    m_texturesGenerated = true;
}

void GameClient::generateObstacles(uint32_t seed)
{
    m_obstacles.clear();
    srand(seed);

    float cellW = (float)MAP_W / OBS_COLS;
    float cellH = (float)MAP_H / OBS_ROWS;
    int halfR = OBS_ROWS / 2;
    int halfC = OBS_COLS / 2;

    for (int r = 1; r < halfR; r++)
    {
        for (int c = 1; c < halfC; c++)
        {
            if (r <= 2 && c <= 2) continue; // Spawn clearance

            // Slightly higher density (20%) to accommodate trees
            if (rand() % 100 > 20) continue;

            int roll = rand() % 10;
            float bw, bh;

            if (roll < 3) {
                // 30% Large Pillar (120-180px)
                bw = 120.f + rand() % 60;
                bh = 120.f + rand() % 60;
            }
            else if (roll < 6) {
                // 30% Tree (Small & Circular-ish)
                bw = 35.f + rand() % 15;
                bh = 35.f + rand() % 15;
            }
            else {
                // 40% Tactical Cover (60-100px)
                bw = 60.f + rand() % 40;
                bh = 60.f + rand() % 40;
            }

            float bx = c * cellW + (cellW - bw) * 0.5f;
            float by = r * cellH + (cellH - bh) * 0.5f;

            auto addSymmetric = [&](float x, float y, float w, float h) {
                m_obstacles.push_back({ x, y, w, h });
                m_obstacles.push_back({ (float)MAP_W - x - w, y, w, h });
                m_obstacles.push_back({ x, (float)MAP_H - y - h, w, h });
                m_obstacles.push_back({ (float)MAP_W - x - w, (float)MAP_H - y - h, w, h });
                };

            addSymmetric(bx, by, bw, bh);
        }
    }

    // Center Island
    m_obstacles.push_back({ (MAP_W - 200.f) * 0.5f, (MAP_H - 200.f) * 0.5f, 200.f, 200.f });
}

static sf::View makeLetterboxView(unsigned winW, unsigned winH)
{
    float logW = (float)WIN_W;
    float logH = (float)WIN_H;
    float winAR = (float)winW / (float)winH;
    float logAR = logW / logH;

    sf::FloatRect vp;
    if (winAR > logAR)
    {
        // Window wider than logical: black bars on left/right
        float scale = (float)winH / logH;
        float vpW   = (logW * scale) / (float)winW;
        vp = sf::FloatRect({ (1.f - vpW) * 0.5f, 0.f }, { vpW, 1.f });
    }
    else
    {
        // Window taller than logical: black bars on top/bottom
        float scale = (float)winW / logW;
        float vpH   = (logH * scale) / (float)winH;
        vp = sf::FloatRect({ 0.f, (1.f - vpH) * 0.5f }, { 1.f, vpH });
    }

    sf::View v(sf::FloatRect({ 0.f, 0.f }, { logW, logH }));
    v.setViewport(vp);
    return v;
}

// Main loop

void GameClient::run(sf::RenderWindow& window)
{
    window.setTitle("TankNet - " + m_username);
    auto sz = window.getSize();
    sf::View lbView = makeLetterboxView(sz.x, sz.y);
    m_lbView = lbView;
    window.setView(lbView);

    sf::Clock clock;
    PktConnect conn; strncpy(conn.name, m_username.c_str(), 15);
    m_net.send(&conn, sizeof(conn));

    bool shouldReturn = false;
    while (window.isOpen() && !shouldReturn)
    {
        while (const auto ev = window.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { window.close(); return; }
            if (const auto* rs = ev->getIf<sf::Event::Resized>())
            {
                lbView = makeLetterboxView(rs->size.x, rs->size.y);
                m_lbView = lbView;
                window.setView(lbView);
            }

            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
            {
                if (kp->code == sf::Keyboard::Key::Escape)
                {
                    if (m_phase == ClientPhase::DISCONNECTED)
                        shouldReturn = true;
                    else if (m_chatActive)
                    { m_chatActive = false; m_chatInput.clear(); }
                    else if (m_phase == ClientPhase::LOBBY || m_phase == ClientPhase::IN_GAME
                          || m_phase == ClientPhase::ROUND_OVER)
                        m_pauseOpen = !m_pauseOpen;
                }
            }

            if (!m_pauseOpen &&
                (m_phase == ClientPhase::LOBBY || m_phase == ClientPhase::IN_GAME))
            {
                if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                {
                    if (kp->code == sf::Keyboard::Key::Enter && m_phase == ClientPhase::LOBBY)
                    {
                        if (m_chatActive) {
                            if (!m_chatInput.empty()) {
                                PktChat c; c.pid = m_pid;
                                strncpy(c.msg, m_chatInput.c_str(), MAX_CHAT_MSG - 1);
                                m_net.send(&c, sizeof(c));
                                m_chatInput.clear();
                            }
                            m_chatActive = false;
                        } else m_chatActive = true;
                    }
                    if (kp->code == sf::Keyboard::Key::Backspace && m_chatActive && !m_chatInput.empty())
                        m_chatInput.pop_back();
                }
                if (m_chatActive)
                    if (const auto* te = ev->getIf<sf::Event::TextEntered>())
                    {
                        char c = (char)te->unicode;
                        if (c >= 32 && c < 127 && m_chatInput.size() < MAX_CHAT_MSG - 1)
                            m_chatInput += c;
                    }
            }

            // Spectate target cycling — keyboard arrows OR mouse click on < > buttons
            if (m_phase == ClientPhase::IN_GAME &&
                m_pid < MAX_PLAYERS && !m_gameState.players[m_pid].alive)
            {
                // Keyboard cycling
                if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                {
                    if (kp->code == sf::Keyboard::Key::Left || kp->code == sf::Keyboard::Key::Comma)
                    {
                        int start = (m_spectateTarget == 0xFF) ? MAX_PLAYERS - 1 : (int)m_spectateTarget - 1;
                        for (int i = start; i >= 0; i--)
                            if (m_lobby.slots[i].active && m_gameState.players[i].alive && i != (int)m_pid)
                            { m_spectateTarget = (uint8_t)i; break; }
                    }
                    if (kp->code == sf::Keyboard::Key::Right || kp->code == sf::Keyboard::Key::Period)
                    {
                        int start = (m_spectateTarget == 0xFF) ? 0 : (int)m_spectateTarget + 1;
                        for (int i = start; i < MAX_PLAYERS; i++)
                            if (m_lobby.slots[i].active && m_gameState.players[i].alive && i != (int)m_pid)
                            { m_spectateTarget = (uint8_t)i; break; }
                    }
                }

                // Mouse click on < > buttons — use m_lbView to convert to logical coords
                if (const auto* mb = ev->getIf<sf::Event::MouseButtonReleased>())
                {
                    if (mb->button == sf::Mouse::Button::Left)
                    {
                        sf::Vector2f lp = window.mapPixelToCoords(
                            sf::Vector2i(mb->position.x, mb->position.y), m_lbView);

                        // Button layout mirrors drawInGame
                        const float btnY = 47.f, btnW = 30.f, btnH = 30.f;
                        const float lX = (float)WIN_W * 0.5f - 165.f;
                        const float rX = (float)WIN_W * 0.5f + 135.f;

                        auto inBtn = [&](float bx) {
                            return lp.x >= bx && lp.x <= bx + btnW &&
                                   lp.y >= btnY && lp.y <= btnY + btnH;
                        };

                        if (inBtn(lX))  // < button
                        {
                            int start = (m_spectateTarget == 0xFF) ? MAX_PLAYERS - 1 : (int)m_spectateTarget - 1;
                            for (int i = start; i >= 0; i--)
                                if (m_lobby.slots[i].active && m_gameState.players[i].alive && i != (int)m_pid)
                                { m_spectateTarget = (uint8_t)i; break; }
                        }
                        if (inBtn(rX))  // > button
                        {
                            int start = (m_spectateTarget == 0xFF) ? 0 : (int)m_spectateTarget + 1;
                            for (int i = start; i < MAX_PLAYERS; i++)
                                if (m_lobby.slots[i].active && m_gameState.players[i].alive && i != (int)m_pid)
                                { m_spectateTarget = (uint8_t)i; break; }
                        }
                    }
                }
            }
        }

        m_dt = clock.restart().asSeconds();
        m_dt = std::min(m_dt, 0.05f);

        // Keepalive ping so server doesn't time us out when window is unfocused
        m_keepaliveTimer += m_dt;
        if (m_keepaliveTimer >= 5.f)
        {
            m_keepaliveTimer = 0.f;
            if (m_phase != ClientPhase::CONNECTING && m_phase != ClientPhase::DISCONNECTED)
            {
                uint8_t ping[2] = { (uint8_t)PktType::PING, m_pid };
                m_net.send(ping, 2);
            }
        }

        // Voice chat runs in all phases
        m_voice.setTalking(!m_pauseOpen && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::V));
        m_voice.tick();

        // Retry connect
        if (m_phase == ClientPhase::CONNECTING)
        {
            m_keepaliveTimer += m_dt;
            if (m_keepaliveTimer > 1.5f) {
                PktConnect conn2; strncpy(conn2.name, m_username.c_str(), 15);
                m_net.send(&conn2, sizeof(conn2));
                m_keepaliveTimer = 0.f;
            }
        }

        // --- KILL CAM RECORDING ---
        if (m_phase == ClientPhase::IN_GAME && !m_playingKillCam) {
            Snapshot sn;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                sn.x[i] = m_gameState.players[i].x;
                sn.y[i] = m_gameState.players[i].y;
                sn.angle[i] = m_gameState.players[i].angle;
                sn.alive[i] = m_gameState.players[i].alive;
            }

            // Record Bullet Data
            for (int i = 0; i < MAX_BULLETS; i++) {
                sn.bullets[i].x = m_gameState.bullets[i].x;
                sn.bullets[i].y = m_gameState.bullets[i].y;
                sn.bullets[i].active = m_gameState.bullets[i].active;
            }

            m_killCamBuffer.push_back(sn);
            if (m_killCamBuffer.size() > 180) m_killCamBuffer.erase(m_killCamBuffer.begin());
        }

        if (m_playingKillCam) {
            m_killCamTimer += m_dt;
            float totalSequenceTime = (3.0f / 0.5f) + 1.5f; // Duration/Speed + Pause
            if (m_killCamTimer > totalSequenceTime) {
                m_playingKillCam = false;
                m_killCamTimer = 0.f;
                m_killCamBuffer.clear();
            }
        }

        m_net.poll();
        processPackets();

        if (m_phase == ClientPhase::DISCONNECTED && m_returnToMenu) {
            m_returnToMenu = false;
            shouldReturn = true;
        }

        window.clear(sf::Color::Black);
        window.setView(lbView);

        sf::RectangleShape bg({ (float)WIN_W,(float)WIN_H });
        bg.setFillColor(sf::Color(15, 15, 25));
        window.draw(bg);

        switch (m_phase)
        {
        case ClientPhase::CONNECTING:   drawConnecting(window);  break;
        case ClientPhase::LOBBY:        updateLobby(window); drawLobby(window); break;
        case ClientPhase::SHOP:         updateShop(window);  drawShop(window);  break;
        case ClientPhase::IN_GAME:      if (!m_pauseOpen) sendInput(window); drawInGame(window); break;
        case ClientPhase::ROUND_OVER:   drawRoundOver(window); break;
        case ClientPhase::MATCH_OVER:   drawMatchOver(window);  break;
        case ClientPhase::DISCONNECTED: drawDisconnected(window); break;
        }

        window.setView(lbView);
        if (m_pauseOpen) drawPauseMenu(window);
        window.display();
    }

    PktDisconnect d; d.pid = m_pid;
    m_net.send(&d, sizeof(d));
}

// Packet processing

void GameClient::processPackets()
{
    Envelope e;
    while(m_net.pollEnvelope(e))
    {
        if(e.len<1) continue;
        PktType t=(PktType)e.buf[0];

        // Once disconnected, ignore everything that could change phase
        if(m_phase == ClientPhase::DISCONNECTED) continue;

        switch(t)
        {
            case PktType::CONNECT_ACK:
                if(e.len>=(int)sizeof(PktConnectAck)){
                    PktConnectAck a; memcpy(&a,e.buf,sizeof(a));
                    if(!a.denied){ m_pid=a.pid; m_net.m_pid=a.pid; m_net.m_connected=true;
                        m_phase=ClientPhase::LOBBY;
                        std::cout<<"[Client] Connected as pid "<<(int)m_pid<<"\n";
                        // Init voice chat now that we know our pid
                        if(!m_voiceInit){
                            m_voice.init(m_pid, [this](const void* d, int l){ m_net.send(d,l); });
                            m_voiceInit = true;
                        }
                    }
                } break;

            case PktType::LOBBY_STATE:
                if(e.len>=(int)sizeof(PktLobbyState)){
                    memcpy(&m_lobby,e.buf,sizeof(m_lobby));
                    m_isAdmin = (m_lobby.hostPid == m_pid);
                    // Server sent lobby state = we are back in lobby
                    if(m_phase==ClientPhase::MATCH_OVER || m_phase==ClientPhase::ROUND_OVER)
                    {
                        m_phase  = ClientPhase::LOBBY;
                        m_ready  = false;
                        memset(m_roundWins, 0, sizeof(m_roundWins));
                    }
                }
                break;

            case PktType::GAME_START:
                if(e.len>=(int)sizeof(PktGameStart)){
                    PktGameStart gs; memcpy(&gs,e.buf,sizeof(gs));
                    m_mapSeed=gs.mapSeed;
                    generateObstacles(m_mapSeed);
                    m_phase=ClientPhase::IN_GAME;
                    m_lastSeq   = 0;
                    m_gameState = PktGameState{};  // cleared; server sends immediate state
                    if(m_audioOk) m_bgm.play();
                } break;

            case PktType::GAME_STATE:
                if(e.len>=(int)sizeof(PktGameState)){
                    PktGameState gs; memcpy(&gs,e.buf,sizeof(gs));
                    if(gs.seq >= m_lastSeq){
                        m_prevState = m_gameState;
                        m_gameState = gs;
                        m_lastSeq   = gs.seq;
                        m_stateAge  = 0.f;
                    }
                } break;

            case PktType::BULLET_SPAWN:
                if(m_audioOk && m_sndShoot) m_sndShoot->play();
                break;

            case PktType::PLAYER_HIT:
                if(m_audioOk && m_sndHit)   m_sndHit->play();
                break;

            case PktType::PLAYER_DEAD:
                if(m_audioOk && m_sndDead)  m_sndDead->play();
                break;

            case PktType::ROUND_OVER:
                if (e.len >= (int)sizeof(PktRoundOver)) {
                    PktRoundOver ro; memcpy(&ro, e.buf, sizeof(ro));
                    for (int i = 0;i < MAX_PLAYERS;i++) m_roundWins[i] = ro.roundWins[i];

                    if (!m_killCamBuffer.empty()) {
                        m_playingKillCam = true;
                        m_killCamTimer = 0.f;
                        for (int i = 0; i < MAX_PLAYERS; i++) {
                            if (m_killCamBuffer.back().alive[i]) {
                                m_killCamWinnerPid = i;
                                break;
                            }
                        }
                    }
                    m_phase = ClientPhase::ROUND_OVER;
                } break;

            case PktType::MATCH_OVER:
                if(e.len>=(int)sizeof(PktMatchOver)){
                    memcpy(&m_matchOver,e.buf,sizeof(m_matchOver));
                    m_phase=ClientPhase::MATCH_OVER;
                    if(m_audioOk) m_bgm.stop();
                } break;

            case PktType::POWERUP_COLLECT:
                if(e.len>=(int)sizeof(PktPowerupCollect)){
                    PktPowerupCollect pc; memcpy(&pc,e.buf,sizeof(pc));
                    if(m_sndPowerup) m_sndPowerup->play();
                    // Add chat notification
                    std::string note = "*** " + pidName(pc.pid) + " picked up " + powerupName(pc.ptype) + "! ***";
                    m_chatHistory.push_back(note);
                    if((int)m_chatHistory.size()>MAX_CHAT_HIST) m_chatHistory.pop_front();
                } break;

            case PktType::BARREL_EXPLODE:
                if(e.len>=(int)sizeof(PktBarrelExplode)){
                    PktBarrelExplode be; memcpy(&be,e.buf,sizeof(be));
                    m_explosions.push_back({be.x, be.y, EXPLOSION_DURATION, EXPLOSION_DURATION});
                    if(m_sndDead) m_sndDead->play();  // reuse death sound for explosion
                } break;

            case PktType::VOICE_DATA:
                if(e.len>=(int)(sizeof(PktType)+sizeof(uint8_t)+sizeof(uint16_t))){
                    uint8_t  vpid = e.buf[1];
                    uint16_t vlen = 0; memcpy(&vlen, e.buf+2, 2);
                    if(vlen>0 && vlen<=VOICE_MAX_BYTES && e.len>=(int)(4+vlen))
                        m_voice.feedIncoming(vpid, e.buf+4, vlen);
                } break;

            case PktType::CHAT:
                if(e.len>=(int)sizeof(PktChat)){
                    PktChat c; memcpy(&c,e.buf,sizeof(c));
                    c.msg[MAX_CHAT_MSG-1]='\0';
                    std::string line = "[" + pidName(c.pid) + "] " + c.msg;
                    m_chatHistory.push_back(line);
                    if((int)m_chatHistory.size()>MAX_CHAT_HIST) m_chatHistory.pop_front();
                } break;

            case PktType::PROFILE_UPDATE:
                if(e.len>=(int)sizeof(PktProfileUpdate))
                    memcpy(&m_profile,e.buf,sizeof(m_profile));
                break;

            case PktType::BUY_SKIN_ACK:
                if(e.len>=(int)sizeof(PktBuySkinAck)){
                    PktBuySkinAck ack; memcpy(&ack,e.buf,sizeof(ack));
                    if(ack.success){
                        // Auto-equip the just-bought skin
                        m_selSkin = ack.skinIdx;
                        PktPlayerReady pr; pr.pid=m_pid; pr.ready=m_ready?1:0; pr.skin=m_selSkin;
                        m_net.send(&pr,sizeof(pr));
                    }
                }
                break;

            case PktType::DISCONNECT:
                if(e.len>=(int)sizeof(PktDisconnect)){
                    PktDisconnect d; memcpy(&d,e.buf,sizeof(d));
                    if(d.pid == m_pid)
                    {
                        // Server kicked US — show disconnected screen
                        m_disconnectMsg = "You were disconnected from the server.";
                        m_phase = ClientPhase::DISCONNECTED;
                        m_pauseOpen = false;
                        if(m_audioOk) m_bgm.stop();
                    }
                    else
                    {
                        std::string line = "*** " + pidName(d.pid) + " disconnected ***";
                        m_chatHistory.push_back(line);
                        if((int)m_chatHistory.size()>MAX_CHAT_HIST) m_chatHistory.pop_front();
                    }
                } break;

            default: break;
        }
    }
}


// Input
void GameClient::sendInput(sf::RenderWindow& w)
{
    if (m_chatActive) return;

    bool localAlive = (m_pid < MAX_PLAYERS && m_gameState.players[m_pid].alive);

    if (localAlive)
    {
        PktInput inp;
        inp.pid = m_pid;
        inp.seq = m_inputSeq++;
        inp.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) ? 1 : 0;
        inp.back = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) ? 1 : 0;
        inp.left = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) ? 1 : 0;
        inp.right = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) ? 1 : 0;
        inp.fire = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space) ? 1 : 0;
        m_net.send(&inp, sizeof(inp));
    }
    else
    {
        // Cycle spectator logic via screen buttons
        static bool lmbWasDown = false;
        bool lmbNow = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

        if (lmbNow && !lmbWasDown)
        {
            auto mp = w.mapPixelToCoords(sf::Mouse::getPosition(w), m_lbView);
            // Button geometry (must match drawing logic)
            const float btnY = 47.f, btnW = 30.f, btnH = 30.f;
            const float lX = WIN_W * 0.5f - 165.f;
            const float rX = WIN_W * 0.5f + 135.f;

            bool hitL = (mp.x >= lX && mp.x <= lX + btnW && mp.y >= btnY && mp.y <= btnY + btnH);
            bool hitR = (mp.x >= rX && mp.x <= rX + btnW && mp.y >= btnY && mp.y <= btnY + btnH);

            if (hitL || hitR)
            {
                // Gather list of valid targets: 0xFF (Map) + all living human players
                std::vector<uint8_t> targets = { 0xFF };
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (m_lobby.slots[i].active && m_gameState.players[i].alive)
                        targets.push_back((uint8_t)i);
                }

                // Find current index in valid target list
                int idx = 0;
                for (size_t i = 0; i < targets.size(); i++) {
                    if (targets[i] == m_spectateTarget) { idx = (int)i; break; }
                }

                // Cycle
                if (hitR) idx = (idx + 1) % targets.size();
                else idx = (idx - 1 + (int)targets.size()) % targets.size();

                m_spectateTarget = targets[idx];
            }
        }
        lmbWasDown = lmbNow;
    }

    m_stateAge += m_dt;
    m_gameTime += m_dt;
    for (auto& e : m_explosions) e.timer -= m_dt;
    m_explosions.erase(std::remove_if(m_explosions.begin(), m_explosions.end(),
        [](const Explosion& e) { return e.timer <= 0.f; }), m_explosions.end());
}

// Draw helpers

void GameClient::drawConnecting(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;
    auto t = makeText("Connecting to server...", 28);
    t.setPosition({WIN_W/2.f - 160.f, WIN_H/2.f});
    w.draw(t);
}

void GameClient::updateLobby(sf::RenderWindow& w)
{
    static bool rHeld=false;
    static bool sHeld=false;
    static bool pHeld=false;

    if(m_chatActive || m_pauseOpen){
        rHeld = true; sHeld = true; pHeld = true;
        (void)w; return;
    }

    // R = toggle ready
    bool rDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R);
    if(rDown && !rHeld){
        m_ready = !m_ready;
        PktPlayerReady pr; pr.pid=m_pid; pr.ready=m_ready?1:0; pr.skin=m_selSkin;
        m_net.send(&pr,sizeof(pr));
    }
    rHeld=rDown;

    // B = open shop (was O)
    bool sDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::B);
    if(sDown && !sHeld) m_phase=ClientPhase::SHOP;
    sHeld=sDown;

    // P = add bot (admin only)
    bool pDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::P);
    if(pDown && !pHeld && m_isAdmin)
    {
        PktAddBot ab; ab.requestPid=m_pid;
        m_net.send(&ab, sizeof(ab));
    }
    pHeld=pDown;
    (void)w;
}

void GameClient::drawLobby(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;

    // Title
    //  3D camo title 
    {
        const std::string titleStr = "TANK NET";
        const unsigned fontSize = 52;

        // Shadow layers (offset behind for 3D depth)
        for(int depth=6; depth>=1; depth--){
            sf::Text shadow(m_font, titleStr, fontSize);
            shadow.setStyle(sf::Text::Bold);
            uint8_t sv = (uint8_t)(20 + depth*8);
            shadow.setFillColor(sf::Color(sv, sv+4, sv, 255));
            auto sb = shadow.getLocalBounds();
            shadow.setOrigin({sb.size.x/2.f, sb.size.y/2.f});
            shadow.setPosition({WIN_W/2.f + depth*1.5f, 44.f + depth*1.5f});
            w.draw(shadow);
        }

        // Camo background behind text (draw into RenderTexture then sprite)
        // Approximate with coloured rectangles since we cant clip easily
        auto mainT = makeText(titleStr, fontSize, sf::Color(80,140,70));
        mainT.setStyle(sf::Text::Bold);
        auto mb = mainT.getLocalBounds();
        mainT.setOrigin({mb.size.x/2.f, mb.size.y/2.f});
        mainT.setPosition({WIN_W/2.f, 44.f});

        // Draw dark camo blobs as background accent (fixed positions, no flickering)
        static const float blobData[10][4] = {
            {-200,  8, 14, 0}, {-140, 20, 22, 1}, {-60, 4, 16, 2},
            {  20, 28, 18, 0}, { 80, 10, 12, 1}, {140, 24, 20, 2},
            { 200,  6, 16, 0}, {-100, 36, 10, 1}, {60, 38, 14, 2},
            {160, 34, 20, 0}
        };
        static const sf::Color blobCols[3] = {
            sf::Color(50,90,40,120), sf::Color(40,70,30,110), sf::Color(60,100,45,130)
        };
        for(auto& bd : blobData){
            sf::CircleShape blob(bd[2]);
            blob.setFillColor(blobCols[(int)bd[3]]);
            blob.setPosition({WIN_W/2.f + bd[0], 16.f + bd[1]});
            w.draw(blob);
        }

        // Main text in camo green
        w.draw(mainT);

        // Outline pass for crispness
        for(int ox=-1;ox<=1;ox++) for(int oy=-1;oy<=1;oy++){
            if(ox==0&&oy==0) continue;
            sf::Text outline(m_font, titleStr, fontSize);
            outline.setStyle(sf::Text::Bold);
            outline.setFillColor(sf::Color(30,50,25,180));
            outline.setOrigin({mb.size.x/2.f, mb.size.y/2.f});
            outline.setPosition({WIN_W/2.f+(float)ox, 44.f+(float)oy});
            w.draw(outline);
        }
        w.draw(mainT);

        // "- Lobby" subtitle
        auto sub = makeText("Lobby", 20, sf::Color(160,200,140));
        auto sbb = sub.getLocalBounds();
        sub.setOrigin({sbb.size.x/2.f, 0.f});
        sub.setPosition({WIN_W/2.f, 78.f});
        w.draw(sub);
    }

    // Player list
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        auto& sl = m_lobby.slots[i];
        if(!sl.active) continue;
        float y = 100.f + i*70.f;

        sf::RectangleShape bg({700.f,60.f});
        bg.setPosition({160.f,y});
        bg.setFillColor(sl.ready ? sf::Color(30,80,30) : sf::Color(40,40,60));
        w.draw(bg);

        // Mini tank preview
        if(m_texturesGenerated){
            uint8_t sk = sl.skin % SKIN_COUNT;
            sf::Sprite ms(m_skinBodyTex[sk]);
            auto tb = m_skinBodyTex[sk].getSize();
            ms.setOrigin({tb.x/2.f,tb.y/2.f});
            ms.setPosition({198.f,y+30.f});
            float sc = 38.f/(float)tb.x;
            ms.setScale({sc,sc});
            w.draw(ms);
        } else {
            sf::CircleShape dot(18.f);
            dot.setFillColor(skinColor(sl.skin));
            dot.setPosition({180.f, y+12.f});
            w.draw(dot);
        }

        std::string line = std::string(sl.name) +
            "  Lv." + std::to_string(sl.level) +
            "  Wins:" + std::to_string(sl.wins) +
            (sl.ready ? "  [READY]" : "") +
            (sl.isBot ? "  [BOT]" : "");
        // Crown icon for admin
        sf::Color nameCol = i==(int)m_pid ? sf::Color::Cyan : sf::Color::White;
        if((int)i == (int)m_lobby.hostPid)
        {
            auto crown = makeText("[HOST] ", 18, sf::Color(255,215,0));
            crown.setPosition({220.f, y+18.f});
            w.draw(crown);
            auto crownB = crown.getLocalBounds();
            auto t = makeText(line, 22, nameCol);
            t.setPosition({220.f + crownB.size.x + 2.f, y+16.f});
            w.draw(t);
        }
        else
        {
            auto t = makeText(line, 22, nameCol);
            t.setPosition({220.f, y+16.f});
            w.draw(t);
        }

        if (m_isAdmin && (uint8_t)i != m_pid) {
            float xX = 820.f; // Position to the right of the player entry
            float xY = y + 15.f;
            float xSize = 30.f;

            // Check hover/click
            auto mp = w.mapPixelToCoords(sf::Mouse::getPosition(w), m_lbView);
            bool hov = (mp.x >= xX && mp.x <= xX + xSize && mp.y >= xY && mp.y <= xY + xSize);

            sf::RectangleShape kickBtn({ xSize, xSize });
            kickBtn.setPosition({ xX, xY });
            kickBtn.setFillColor(hov ? sf::Color(255, 50, 50) : sf::Color(150, 0, 0));
            w.draw(kickBtn);

            auto xText = makeText("X", 20, sf::Color::White);
            xText.setPosition({ xX + 8.f, xY + 2.f });
            w.draw(xText);

            static bool lmbWasDown = false;
            bool lmbDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
            if (hov && lmbDown && !lmbWasDown) {
                PktKickBot kb;
                kb.requestPid = m_pid;
                kb.botPid = (uint8_t)i;
                m_net.send(&kb, sizeof(kb));
            }
            lmbWasDown = lmbDown;
        }
    }

    // Controls hint
    std::string lobbyHint = "[R] Ready  [B] Shop  [Enter] Chat  [Esc] Menu  [V] Voice";
    if(m_isAdmin) lobbyHint += "  [P] Add Bot";
    if(m_voice.isTalking()) lobbyHint += "  [MIC ON]";
    auto hint = makeText(lobbyHint, 16, m_voice.isTalking() ? sf::Color(100,255,100) : sf::Color(160,160,160));
    hint.setPosition({160.f, WIN_H-60.f});
    w.draw(hint);

    // Profile bar
    std::string prof = "Coins: " + std::to_string(m_profile.coins) +
                       "  XP: " + std::to_string(m_profile.xp) +
                       "  Lv." + std::to_string(m_profile.level);
    auto pt = makeText(prof, 20, sf::Color::Yellow);
    pt.setPosition({20.f, WIN_H-30.f});
    w.draw(pt);

    drawChat(w);
}

void GameClient::updateShop(sf::RenderWindow& w)
{
    static bool upH=false,dnH=false,bH=false,escH=false;
    bool up  = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W);      // W/S instead of arrows
    bool dn  = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S);
    bool buy = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Enter);
    bool esc = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape);

    if(up&&!upH) m_shopCursor=std::max(0,m_shopCursor-1);
    if(dn&&!dnH) m_shopCursor=std::min(SKIN_COUNT-1,m_shopCursor+1);
    if(buy&&!bH)
    {
        bool owned = (m_profile.ownedSkins >> m_shopCursor) & 1;
        if(owned)
        {
            // Equip: set local selection and send ready packet with new skin
            m_selSkin = (uint8_t)m_shopCursor;
            PktPlayerReady pr; pr.pid=m_pid; pr.ready=m_ready?1:0; pr.skin=m_selSkin;
            m_net.send(&pr,sizeof(pr));
        }
        else
        {
            // Buy
            PktBuySkin bs; bs.pid=m_pid; bs.skinIdx=(uint8_t)m_shopCursor;
            m_net.send(&bs,sizeof(bs));
        }
    }
    if(esc&&!escH) m_phase=ClientPhase::LOBBY;
    upH=up; dnH=dn; bH=buy; escH=esc;
    (void)w;
}

void GameClient::drawShop(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;
    auto title = makeText("SHOP - Tank Skins", 34, sf::Color::Yellow);
    title.setPosition({WIN_W/2.f-160.f,30.f});
    w.draw(title);

    auto coins = makeText("Coins: "+std::to_string(m_profile.coins), 22, sf::Color(255,215,0));
    coins.setPosition({30.f,30.f});
    w.draw(coins);

    for(int i=0;i<SKIN_COUNT;i++)
    {
        float y = 80.f + i*96.f;
        bool owned    = (m_profile.ownedSkins >> i) & 1;
        bool equipped = (i == (int)m_selSkin);
        bool selected = (i == m_shopCursor);

        // Row background
        sf::RectangleShape bg({700.f,82.f});
        bg.setPosition({162.f,y});
        bg.setFillColor(selected ? sf::Color(55,55,90) : sf::Color(28,28,48));
        if(selected){ bg.setOutlineThickness(2.f); bg.setOutlineColor(sf::Color::Cyan); }
        w.draw(bg);

        // Tank preview using skin texture
        if(m_texturesGenerated)
        {
            // Body sprite
            sf::Sprite bodySprite(m_skinBodyTex[i]);
            auto tb = m_skinBodyTex[i].getSize();
            bodySprite.setOrigin({tb.x/2.f,tb.y/2.f});
            bodySprite.setPosition({220.f, y+41.f});
            float sc = 52.f / (float)tb.x;
            bodySprite.setScale({sc,sc});
            w.draw(bodySprite);

            // Barrel sprite (pointing right)
            sf::Sprite barrelSprite(m_skinTurretTex[i]);
            auto tt = m_skinTurretTex[i].getSize();
            barrelSprite.setOrigin({0.f,tt.y/2.f});
            barrelSprite.setPosition({220.f+14.f, y+41.f});
            float bs = 18.f / (float)tt.y;
            barrelSprite.setScale({bs,bs});
            w.draw(barrelSprite);
        }
        else
        {
            sf::CircleShape dot(22.f);
            dot.setFillColor(skinColor((uint8_t)i));
            dot.setPosition({209.f,y+19.f});
            w.draw(dot);
        }

        // Name and status
        std::string status = owned ? (equipped ? " [EQUIPPED]" : " [OWNED]") : "";
        sf::Color textCol  = equipped ? sf::Color::Cyan
                           : owned    ? sf::Color::Green
                                      : sf::Color::White;
        auto t = makeText(SKIN_NAMES[i] + status, 22, textCol);
        t.setPosition({270.f, y+14.f});
        w.draw(t);

        // Price tag
        if(!owned){
            auto price = makeText(std::to_string(SKIN_PRICES[i])+" coins", 18, sf::Color(255,215,0));
            price.setPosition({270.f, y+46.f});
            w.draw(price);
        }
    }
    auto hint = makeText("[W/S] Select   [Enter] Buy/Equip   [Esc] Back",18,sf::Color(160,160,160));
    hint.setPosition({200.f,WIN_H-36.f});
    w.draw(hint);
}

void GameClient::drawObstacles(sf::RenderWindow& w)
{
    for (auto& o : m_obstacles)
    {
        sf::RectangleShape shape({ o.w, o.h });
        shape.setPosition({ o.x, o.y });

        // 1. Identify "Trees" (Small, square-ish obstacles)
        if (o.w < 55.f)
        {
            // Trunk
            sf::RectangleShape trunk({ o.w * 0.3f, o.h * 0.3f });
            trunk.setOrigin({ (o.w * 0.3f) / 2.f, (o.h * 0.3f) / 2.f });
            trunk.setPosition({ o.x + o.w / 2.f, o.y + o.h / 2.f });
            trunk.setFillColor(sf::Color(101, 67, 33)); // Brown

            // Foliage (Green Circle)
            sf::CircleShape leaf(o.w * 0.6f);
            leaf.setOrigin({ o.w * 0.6f, o.w * 0.6f });
            leaf.setPosition({ o.x + o.w / 2.f, o.y + o.h / 2.f });
            leaf.setFillColor(sf::Color(34, 139, 34, 230)); // Forest Green
            leaf.setOutlineThickness(2.f);
            leaf.setOutlineColor(sf::Color(20, 60, 20));

            w.draw(trunk);
            w.draw(leaf);
        }
        // 2. Identify "Large Boulders" (The Pillar type from generateMap)
        else if (o.w > 110.f)
        {
            shape.setFillColor(sf::Color(90, 95, 100)); // Granite Grey
            shape.setOutlineThickness(3.f);
            shape.setOutlineColor(sf::Color(40, 42, 45));

            // Add a "highlight" for a rock-like depth
            sf::RectangleShape detail({ o.w - 10.f, o.h - 10.f });
            detail.setPosition({ o.x + 5.f, o.y + 5.f });
            detail.setFillColor(sf::Color(110, 115, 120, 100));

            w.draw(shape);
            w.draw(detail);
        }
        // 3. Identify "Brick Walls / Tactical Cover" (Medium rectangles)
        else
        {
            shape.setFillColor(sf::Color(140, 70, 50)); // Brick Red
            shape.setOutlineThickness(2.f);
            shape.setOutlineColor(sf::Color(80, 30, 20));

            w.draw(shape);

            // Draw simple "Brick" lines
            sf::RectangleShape line({ o.w, 2.f });
            line.setFillColor(sf::Color(100, 50, 40, 150));
            for (float ly = 10.f; ly < o.h; ly += 15.f) {
                line.setPosition({ o.x, o.y + ly });
                w.draw(line);
            }
        }
    }
}

void GameClient::drawTank(sf::RenderWindow& w, const PlayerState& ps, uint8_t pid)
{
    if(!ps.alive) return;
    if(pid < MAX_PLAYERS && !m_lobby.slots[pid].active) return;

    uint8_t skin = ps.skin % SKIN_COUNT;

    if(m_texturesGenerated)
    {
        //  Textured body 
        sf::Sprite body(m_skinBodyTex[skin]);
        auto tb = m_skinBodyTex[skin].getSize();
        body.setOrigin({tb.x/2.f, tb.y/2.f});
        body.setPosition({ps.x, ps.y});
        body.setRotation(sf::degrees(ps.angle));
        float scaleX = (TANK_RADIUS*2.f) / (float)tb.x;
        float scaleY = (TANK_RADIUS*1.6f) / (float)tb.y;
        body.setScale({scaleX, scaleY});
        w.draw(body);

        //  Barrel(s): double barrel for rapid-fire buff 
        uint8_t buffsEarly = (pid < MAX_PLAYERS) ? m_gameState.buffs[pid] : 0;
        bool rapidFire = (buffsEarly & 0x02) != 0;

        auto drawBarrelSprite = [&](float sideOffset)
        {
            sf::Sprite barrel(m_skinTurretTex[skin]);
            auto tt = m_skinTurretTex[skin].getSize();
            barrel.setOrigin({0.f, tt.y/2.f});
            float bscale = (TANK_RADIUS*0.45f) / (float)tt.y;
            barrel.setScale({bscale, bscale});

            if(sideOffset == 0.f)
            {
                barrel.setPosition({ps.x, ps.y});
                barrel.setRotation(sf::degrees(ps.angle));
            }
            else
            {
                // Offset perpendicular to the barrel direction
                float rad = ps.angle * 3.14159265f / 180.f;
                float px = ps.x + std::cos(rad + 1.5708f) * sideOffset;
                float py = ps.y + std::sin(rad + 1.5708f) * sideOffset;
                barrel.setPosition({px, py});
                barrel.setRotation(sf::degrees(ps.angle));
            }
            w.draw(barrel);
        };

        if(rapidFire)
        {
            drawBarrelSprite(-5.f);   // left barrel
            drawBarrelSprite( 5.f);   // right barrel
        }
        else
        {
            drawBarrelSprite(0.f);    // single centred barrel
        }
    }
    else
    {
        // Fallback: plain shapes if textures not generated yet
        sf::Color col = skinColor(skin);
        sf::RectangleShape body({TANK_RADIUS*2.f, TANK_RADIUS*1.6f});
        body.setOrigin({TANK_RADIUS, TANK_RADIUS*0.8f});
        body.setPosition({ps.x, ps.y});
        body.setRotation(sf::degrees(ps.angle));
        body.setFillColor(col);
        w.draw(body);
    }

    // Shield: pulsing translucent blue dome
    uint8_t buffs = (pid < MAX_PLAYERS) ? m_gameState.buffs[pid] : 0;
    if(buffs & 0x04){
        float pulse = 0.5f + 0.5f*sinf(m_gameTime * 4.f);  // 0..1 oscillation
        uint8_t alpha = (uint8_t)(120 + 80*pulse);

        // Filled translucent blue
        sf::CircleShape dome(TANK_RADIUS+8.f);
        dome.setOrigin({TANK_RADIUS+8.f,TANK_RADIUS+8.f});
        dome.setPosition({ps.x,ps.y});
        dome.setFillColor(sf::Color(60,140,255,(uint8_t)(40+30*pulse)));
        dome.setOutlineThickness(3.f);
        dome.setOutlineColor(sf::Color(100,180,255,alpha));
        w.draw(dome);

        // Inner shimmer ring
        sf::CircleShape inner(TANK_RADIUS+2.f);
        inner.setOrigin({TANK_RADIUS+2.f,TANK_RADIUS+2.f});
        inner.setPosition({ps.x,ps.y});
        inner.setFillColor(sf::Color::Transparent);
        inner.setOutlineThickness(2.f);
        inner.setOutlineColor(sf::Color(180,220,255,(uint8_t)(80+60*pulse)));
        w.draw(inner);
    }

    // HP pips
    for(int h=0;h<ps.hp;h++){
        sf::CircleShape pip(4.f);
        pip.setFillColor(sf::Color::Red);
        pip.setPosition({ps.x-(TANK_MAX_HP*10.f)/2.f+h*10.f, ps.y-TANK_RADIUS-12.f});
        w.draw(pip);
    }

    // Speed/rapidfire buff icons (shield shown as ring above)
    float bx = ps.x - 10.f;
    for(int b=0;b<2;b++){
        if(buffs & (1<<b)){
            sf::CircleShape bi(5.f);
            bi.setOrigin({5.f,5.f});
            bi.setPosition({bx, ps.y+TANK_RADIUS+8.f});
            bi.setFillColor(powerupColor((PowerupType)b));
            w.draw(bi);
            bx += 14.f;
        }
    }

    // Name tag
    if(m_fontLoaded){
        auto tag = makeText(pidName(pid),14,sf::Color::White);
        tag.setPosition({ps.x-24.f, ps.y-TANK_RADIUS-26.f});
        w.draw(tag);
    }
}

void GameClient::drawBullet(sf::RenderWindow& w, const BulletState& bs)
{
    if(!bs.active) return;
    sf::CircleShape c(BULLET_RADIUS);
    c.setOrigin({BULLET_RADIUS,BULLET_RADIUS});
    c.setPosition({bs.x,bs.y});
    c.setFillColor(sf::Color::Yellow);
    w.draw(c);
}

void GameClient::drawPowerup(sf::RenderWindow& w, const PowerupState& ps)
{
    if(!ps.active) return;
    sf::Color col = powerupColor(ps.ptype);

    // Outer glow ring
    sf::CircleShape ring(POWERUP_RADIUS + 4.f);
    ring.setOrigin({POWERUP_RADIUS+4.f, POWERUP_RADIUS+4.f});
    ring.setPosition({ps.x, ps.y});
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineThickness(3.f);
    ring.setOutlineColor(sf::Color(col.r, col.g, col.b, 160));
    w.draw(ring);

    // Inner circle
    sf::CircleShape c(POWERUP_RADIUS);
    c.setOrigin({POWERUP_RADIUS, POWERUP_RADIUS});
    c.setPosition({ps.x, ps.y});
    c.setFillColor(sf::Color(col.r, col.g, col.b, 200));
    w.draw(c);

    // Letter label
    if(m_fontLoaded){
        std::string label;
        switch(ps.ptype){
            case PowerupType::SPEED:     label = "S"; break;
            case PowerupType::RAPIDFIRE: label = "R"; break;
            case PowerupType::SHIELD:    label = "P"; break;
        }
        sf::Text t(m_font, label, 14);
        t.setFillColor(sf::Color::White);
        auto b = t.getLocalBounds();
        t.setOrigin({b.size.x/2.f, b.size.y/2.f});
        t.setPosition({ps.x, ps.y - 4.f});
        w.draw(t);
    }
}

void GameClient::drawBarrel(sf::RenderWindow& w, const BarrelState& bs)
{
    if(!bs.active) return;
    float r = BARREL_RADIUS;

    // Dark shadow
    sf::CircleShape shadow(r+2.f);
    shadow.setOrigin({r+2.f,r+2.f});
    shadow.setPosition({bs.x+3.f,bs.y+3.f});
    shadow.setFillColor(sf::Color(0,0,0,80));
    w.draw(shadow);

    // Barrel body (red cylinder)
    sf::RectangleShape body({r*1.6f, r*2.f});
    body.setOrigin({r*0.8f,r});
    body.setPosition({bs.x,bs.y});
    body.setFillColor(sf::Color(180,40,30));
    body.setOutlineThickness(2.f);
    body.setOutlineColor(sf::Color(100,20,15));
    w.draw(body);

    // Lid top
    sf::RectangleShape lid({r*1.6f, r*0.28f});
    lid.setOrigin({r*0.8f, r*0.14f});
    lid.setPosition({bs.x, bs.y-r+r*0.14f});
    lid.setFillColor(sf::Color(140,30,22));
    w.draw(lid);

    // Lid bottom
    sf::RectangleShape lidB({r*1.6f, r*0.28f});
    lidB.setOrigin({r*0.8f, r*0.14f});
    lidB.setPosition({bs.x, bs.y+r-r*0.14f});
    lidB.setFillColor(sf::Color(140,30,22));
    w.draw(lidB);

    // Metal band stripes (dark red)
    for(int i=0;i<2;i++){
        sf::RectangleShape band({r*1.6f, 4.f});
        band.setOrigin({r*0.8f,2.f});
        band.setPosition({bs.x, bs.y - r*0.3f + i*r*0.6f});
        band.setFillColor(sf::Color(90,18,12));
        w.draw(band);
    }

    // Hazard X stripes
    sf::RectangleShape x1({r*1.4f, 4.f});
    x1.setOrigin({r*0.7f,2.f});
    x1.setPosition({bs.x,bs.y});
    x1.setRotation(sf::degrees(40.f));
    x1.setFillColor(sf::Color(230,200,0,180));
    w.draw(x1);
    x1.setRotation(sf::degrees(-40.f));
    w.draw(x1);

    // Shine highlight
    sf::RectangleShape shine({r*0.3f, r*1.2f});
    shine.setOrigin({r*0.15f,r*0.6f});
    shine.setPosition({bs.x-r*0.4f,bs.y});
    shine.setFillColor(sf::Color(255,100,90,80));
    w.draw(shine);
}

void GameClient::drawExplosions(sf::RenderWindow& w)
{
    for(auto& exp : m_explosions)
    {
        float t = 1.f - (exp.timer / exp.maxTimer);  // 0=start, 1=end

        // Outer ring (expands and fades)
        float outerR = BARREL_EXPLODE_R * t;
        sf::CircleShape outer(outerR);
        outer.setOrigin({outerR,outerR});
        outer.setPosition({exp.x,exp.y});
        outer.setFillColor(sf::Color::Transparent);
        outer.setOutlineThickness(4.f*(1.f-t));
        outer.setOutlineColor(sf::Color(255,100,0,(uint8_t)(180*(1.f-t))));
        w.draw(outer);

        // Inner fireball (shrinks and fades)
        float innerR = BARREL_EXPLODE_R * 0.5f * (1.f-t*0.7f);
        sf::CircleShape inner(innerR);
        inner.setOrigin({innerR,innerR});
        inner.setPosition({exp.x,exp.y});
        inner.setFillColor(sf::Color(
            255,
            (uint8_t)(200*(1.f-t)),
            0,
            (uint8_t)(220*(1.f-t))));
        w.draw(inner);

        // Core (bright white-yellow)
        float coreR = BARREL_EXPLODE_R * 0.2f * (1.f-t);
        if(coreR > 1.f){
            sf::CircleShape core(coreR);
            core.setOrigin({coreR,coreR});
            core.setPosition({exp.x,exp.y});
            core.setFillColor(sf::Color(255,240,180,(uint8_t)(255*(1.f-t))));
            w.draw(core);
        }

        // Debris particles (8 directions)
        for(int i=0;i<8;i++){
            float angle = i * 3.14159f * 0.25f;
            float dist  = BARREL_EXPLODE_R * 0.7f * t;
            float px    = exp.x + cosf(angle)*dist;
            float py    = exp.y + sinf(angle)*dist;
            sf::CircleShape debris(4.f*(1.f-t)+1.f);
            debris.setFillColor(sf::Color(200,80,0,(uint8_t)(200*(1.f-t))));
            debris.setPosition({px,py});
            w.draw(debris);
        }
    }
}

void GameClient::drawHUD(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;

    float y = 10.f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!m_lobby.slots[i].active) continue;

        auto& ps = m_gameState.players[i];

        // Combined string: Name | K: 0 | W: 0
        std::string s = std::string(m_lobby.slots[i].name) +
            "  K:" + std::to_string(ps.kills) +
            "  W:" + std::to_string(m_roundWins[i]);

        sf::Color c = ps.alive ? skinColor(ps.skin) : sf::Color(100, 100, 100);
        if (!ps.alive) s += " [DEAD]";

        auto t = makeText(s, 16, c);
        t.setPosition({ 10.f, y });
        w.draw(t);

        y += 20.f;
    }
}

void GameClient::drawChat(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;
    float y=WIN_H-180.f;
    for(auto& line:m_chatHistory){
        auto t=makeText(line,16,sf::Color(200,220,200));
        t.setPosition({10.f,y}); w.draw(t);
        y+=18.f;
    }
    if(m_chatActive){
        sf::RectangleShape box({500.f,26.f});
        box.setPosition({10.f,WIN_H-30.f});
        box.setFillColor(sf::Color(30,30,30));
        box.setOutlineThickness(1.f); box.setOutlineColor(sf::Color::Cyan);
        w.draw(box);
        auto t=makeText("> "+m_chatInput+"_",16,sf::Color::Cyan);
        t.setPosition({14.f,WIN_H-28.f}); w.draw(t);
    }
}

void GameClient::drawInGame(sf::RenderWindow& w)
{
    bool localAlive = (m_pid < MAX_PLAYERS) && m_gameState.players[m_pid].alive;
    uint8_t target = localAlive ? m_pid : m_spectateTarget;

    if (m_playingKillCam && !m_killCamBuffer.empty())
    {
        // 1. CALCULATE PROGRESS
        float playbackTime = m_killCamTimer * PLAYBACK_SPEED;
        float progress = std::min(1.0f, playbackTime / 3.0f);
        size_t frameIdx = (size_t)(progress * (m_killCamBuffer.size() - 1));
        Snapshot& sn = m_killCamBuffer[frameIdx];

        // 2. CONFIGURE CAMERA (Center on winner)
        float camX = (m_killCamWinnerPid != -1) ? sn.x[m_killCamWinnerPid] : (float)MAP_W * 0.5f;
        float camY = (m_killCamWinnerPid != -1) ? sn.y[m_killCamWinnerPid] : (float)MAP_H * 0.5f;

        sf::View replayView;
        replayView.setCenter(sf::Vector2f(camX, camY));
        replayView.setSize(sf::Vector2f(WIN_W * 0.50f, WIN_H * 0.50f));
        w.setView(replayView); // Set the camera FIRST

        // 3. DRAW WORLD
        drawBackground(w);
        drawObstacles(w);

        // 4. DRAW RECORDED PROJECTILES
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (sn.bullets[i].active) {
                BulletState tempBs;
                tempBs.x = sn.bullets[i].x;
                tempBs.y = sn.bullets[i].y;
                tempBs.active = true;
                drawBullet(w, tempBs);
            }
        }

        // 5. DRAW RECORDED TANKS
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!sn.alive[i] || !m_lobby.slots[i].active) continue;
            PlayerState tempPs;
            tempPs.x = sn.x[i]; tempPs.y = sn.y[i]; tempPs.angle = sn.angle[i];
            tempPs.alive = sn.alive[i]; tempPs.hp = 3; tempPs.skin = m_lobby.slots[i].skin;
            drawTank(w, tempPs, (uint8_t)i);
        }

        drawBorder(w);
    }
    else
    {
        // 2. LIVE RENDERING
        if (target != 0xFF && target < MAX_PLAYERS && m_gameState.players[target].alive)
        {
            auto& ps = m_gameState.players[target];
            float camX = ps.x, camY = ps.y;
            const float ZOOM = 0.55f;
            const float halfW = WIN_W * 0.5f * ZOOM, halfH = WIN_H * 0.5f * ZOOM;
            camX = std::max(halfW, std::min((float)MAP_W - halfW, camX));
            camY = std::max(halfH, std::min((float)MAP_H - halfH, camY));

            sf::View gameView(sf::Vector2f(camX, camY), sf::Vector2f((float)WIN_W * ZOOM, (float)WIN_H * ZOOM));
            w.setView(gameView);

            drawBackground(w);
            drawObstacles(w);
            for (int i = 0; i < MAX_BARRELS; i++)    drawBarrel(w, m_gameState.barrels[i]);
            for (auto& p : m_gameState.powerups) drawPowerup(w, p);
            for (int i = 0; i < MAX_PLAYERS; i++)    drawTank(w, m_gameState.players[i], (uint8_t)i);
            for (auto& b : m_gameState.bullets)  drawBullet(w, b);
            drawExplosions(w);
            drawBorder(w);
        }
        else
        {
            const float MARGIN = 40.f;
            const float scaleToFit = std::min((WIN_W - MARGIN * 2.f) / MAP_W, (WIN_H - MARGIN * 2.f) / MAP_H);
            sf::View spectView(sf::Vector2f((float)MAP_W * 0.5f, (float)MAP_H * 0.5f), sf::Vector2f(WIN_W / scaleToFit, WIN_H / scaleToFit));
            w.setView(spectView);
            drawBackground(w);
            drawObstacles(w);
            for (int i = 0; i < MAX_BARRELS; i++)    drawBarrel(w, m_gameState.barrels[i]);
            for (auto& p : m_gameState.powerups) drawPowerup(w, p);
            for (int i = 0; i < MAX_PLAYERS; i++)    drawTank(w, m_gameState.players[i], (uint8_t)i);
            for (auto& b : m_gameState.bullets)  drawBullet(w, b);
            drawExplosions(w);
            drawBorder(w);
            w.setView(m_lbView);
            drawScreenBorder(w, MARGIN);
        }
    }

    // 3. UI OVERLAYS (SCREEN SPACE)
    w.setView(m_lbView);
    drawHUD(w);
    drawMinimap(w);
    drawChat(w);

    // RESTORE SPECTATOR UI
    if (!localAlive && m_fontLoaded && !m_playingKillCam)
    {
        const float MARGIN = 40.f;
        const float BANNER_H = 44.f;
        sf::RectangleShape banner({ WIN_W - MARGIN * 2.f, BANNER_H });
        banner.setPosition({ MARGIN, MARGIN });
        banner.setFillColor(sf::Color(0, 0, 0, 140));
        w.draw(banner);

        auto dead = makeText("YOU ARE DEAD", 22, sf::Color(220, 60, 60));
        dead.setStyle(sf::Text::Bold);
        auto db = dead.getLocalBounds();
        dead.setPosition({ WIN_W * 0.5f - db.size.x * 0.5f - (float)db.position.x, MARGIN + 4.f });
        w.draw(dead);

        std::string targetName = (m_spectateTarget == 0xFF) ? "ALL" : pidName(m_spectateTarget);
        auto spec = makeText("SPECTATING: " + targetName, 14, sf::Color(150, 160, 180));
        auto sb = spec.getLocalBounds();
        spec.setPosition({ WIN_W * 0.5f - sb.size.x * 0.5f - (float)sb.position.x, MARGIN + 26.f });
        w.draw(spec);

        const float btnY = 47.f, btnW = 30.f, btnH = 30.f;
        const float lX = (float)WIN_W * 0.5f - 165.f;
        const float rX = (float)WIN_W * 0.5f + 135.f;

        auto drawBtn = [&](float x, float y, const std::string& txt) {
            sf::RectangleShape b({ btnW, btnH });
            b.setPosition({ x, y });
            // Map physical mouse position to logical coords through the current view
            auto physMp = sf::Mouse::getPosition(w);
            auto logPt  = w.mapPixelToCoords(physMp);
            bool hov = (logPt.x >= x && logPt.x <= x + btnW && logPt.y >= y && logPt.y <= y + btnH);
            b.setFillColor(hov ? sf::Color(100, 100, 100) : sf::Color(50, 50, 50));
            b.setOutlineThickness(1.f);
            b.setOutlineColor(sf::Color::White);
            w.draw(b);
            auto t = makeText(txt, 18);
            auto tb = t.getLocalBounds();
            t.setPosition({ x + (btnW / 2.f) - tb.size.x * 0.5f - (float)tb.position.x,
                            y + (btnH / 2.f) - tb.size.y * 0.5f - (float)tb.position.y });
            w.draw(t);
            };
        drawBtn(lX, btnY, "<");
        drawBtn(rX, btnY, ">");
    }

    if (m_playingKillCam) {
        bool showText = ((int)(m_killCamTimer * 4) % 2 == 0) || (m_killCamTimer * PLAYBACK_SPEED >= 3.0f);
        if (showText) {
            auto replayTag = makeText("SLOW-MO REPLAY", 32, sf::Color::Red);
            replayTag.setStyle(sf::Text::Bold);
            auto rb = replayTag.getLocalBounds();
            replayTag.setPosition({ WIN_W / 2.f - rb.size.x / 2.f, 60.f });
            w.draw(replayTag);
        }
    }
}

void GameClient::drawMinimap(sf::RenderWindow& w)
{
    const float MM_SIZE = 180.f;
    const float MM_X = (float)WIN_W - MM_SIZE - 20.f;
    const float MM_Y = 20.f;

    // Calculate scale to fit the larger dimension
    float scale = MM_SIZE / (float)std::max(MAP_W, MAP_H);
    float mmW = (float)MAP_W * scale;
    float mmH = (float)MAP_H * scale;

    // 1. Background
    sf::RectangleShape bg({ mmW, mmH });
    bg.setPosition({ MM_X, MM_Y });
    bg.setFillColor(sf::Color(0, 0, 0, 150));
    bg.setOutlineThickness(1.5f);
    bg.setOutlineColor(sf::Color(150, 150, 150));
    w.draw(bg);

    // 2. Obstacles (Simplified)
    for (auto& o : m_obstacles) {
        sf::RectangleShape os({ o.w * scale, o.h * scale });
        os.setPosition({ MM_X + o.x * scale, MM_Y + o.y * scale });
        os.setFillColor(sf::Color(70, 70, 70));
        w.draw(os);
    }

    // 3. Players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!m_lobby.slots[i].active) continue;
        auto& ps = m_gameState.players[i];
        if (!ps.alive) continue;

        sf::CircleShape dot(2.5f);
        dot.setOrigin({ 2.5f, 2.5f });
        dot.setPosition({ MM_X + ps.x * scale, MM_Y + ps.y * scale });

        if (i == m_pid) {
            dot.setFillColor(sf::Color::Cyan); // Local player
            dot.setOutlineThickness(1.f);
            dot.setOutlineColor(sf::Color::White);
        }
        else {
            dot.setFillColor(skinColor(m_lobby.slots[i].skin));
        }
        w.draw(dot);
    }
}

void GameClient::drawRoundOver(sf::RenderWindow& w)
{
    drawInGame(w);  // show final state underneath (already handles offset)
    if(!m_fontLoaded) return;
    sf::RectangleShape overlay({(float)WIN_W,(float)WIN_H});
    overlay.setFillColor(sf::Color(0,0,0,140));
    w.draw(overlay);

    auto t=makeText("Round Over!  Next round starting...",32,sf::Color::Yellow);
    auto b=t.getLocalBounds();
    t.setPosition({WIN_W/2.f-b.size.x/2.f, WIN_H/2.f-60.f});
    w.draw(t);

    float y=WIN_H/2.f;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(!m_lobby.slots[i].active) continue;
        std::string s=std::string(m_lobby.slots[i].name)+" - "+std::to_string(m_roundWins[i])+" round wins";
        auto rt=makeText(s,22,skinColor(m_gameState.players[i].skin));
        rt.setPosition({WIN_W/2.f-180.f,y}); w.draw(rt);
        y+=30.f;
    }
}

void GameClient::drawMatchOver(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;

    auto title=makeText("MATCH OVER!",48,sf::Color::Yellow);
    auto tb=title.getLocalBounds();
    title.setPosition({WIN_W/2.f-tb.size.x/2.f,30.f});
    w.draw(title);

    std::string winner = "Winner: " + pidName(m_matchOver.winner);
    auto wt=makeText(winner,30,sf::Color::Cyan);
    auto wb=wt.getLocalBounds();
    wt.setPosition({WIN_W/2.f-wb.size.x/2.f,96.f});
    w.draw(wt);

    //  Per-player stats table 
    float y=150.f;
    auto sh=makeText("Player            Kills    XP Earned   Coins Earned",20,sf::Color(160,170,180));
    sh.setPosition({80.f,y}); w.draw(sh); y+=4.f;
    // Separator line
    sf::RectangleShape sep({860.f,1.f}); sep.setFillColor(sf::Color(60,70,90));
    sep.setPosition({80.f,y+20.f}); w.draw(sep); y+=26.f;

    for(int i=0;i<MAX_PLAYERS;i++){
        if(!m_lobby.slots[i].active) continue;

        bool isMe = (i == (int)m_pid);
        sf::Color rowCol = isMe ? sf::Color::Cyan : skinColor(m_lobby.slots[i].skin);

        // Highlight row for local player
        if(isMe){
            sf::RectangleShape hl({860.f,24.f});
            hl.setFillColor(sf::Color(0,60,80,120));
            hl.setPosition({80.f,y-2.f}); w.draw(hl);
        }

        std::string name = std::string(m_lobby.slots[i].name);
        while((int)name.size()<18) name+=" ";

        // kills
        char buf[128];
        snprintf(buf, sizeof(buf), "%-18s  %3d      +%-6u      +%u",
                 name.c_str(),
                 (int)m_matchOver.kills[i],
                 (unsigned)m_matchOver.xpGained[i],
                 (unsigned)m_matchOver.coinsGained[i]);

        auto rt=makeText(buf,20,rowCol);
        rt.setPosition({80.f,y}); w.draw(rt);
        y+=26.f;
    }

    //  My updated totals (from latest PROFILE_UPDATE) 
    y+=6.f;
    sf::RectangleShape sep2({860.f,1.f}); sep2.setFillColor(sf::Color(60,70,90));
    sep2.setPosition({80.f,y}); w.draw(sep2); y+=10.f;
    char totBuf[128];
    snprintf(totBuf,sizeof(totBuf),"Your totals:  XP %u  |  Coins %u  |  Lv.%u",
             (unsigned)m_profile.xp,(unsigned)m_profile.coins,(unsigned)m_profile.level);
    auto tot=makeText(totBuf,18,sf::Color::Yellow);
    tot.setPosition({80.f,y}); w.draw(tot); y+=30.f;

    //  Leaderboard 
    y+=8.f;
    auto lbh=makeText("Global Leaderboard  (Top 5 by Wins)",20,sf::Color(230,180,40));
    lbh.setPosition({80.f,y}); w.draw(lbh); y+=28.f;
    for(int i=0;i<5;i++){
        if(m_matchOver.lbName[i][0]=='\0') break;
        char lbBuf[64];
        snprintf(lbBuf,sizeof(lbBuf),"%d.  %-16s  Wins: %-4u  Lv.%u",
                 i+1, m_matchOver.lbName[i],
                 (unsigned)m_matchOver.lbWins[i],
                 (unsigned)m_matchOver.lbLevel[i]);
        auto lt=makeText(lbBuf,18,i==0?sf::Color(255,220,60):sf::Color::White);
        lt.setPosition({80.f,y}); w.draw(lt);
        y+=22.f;
    }

    //  Footer hint 
    bool talking = m_voice.isTalking();
    std::string endHint = "[Esc] Menu    [V] Voice";
    if(talking) endHint += "  [MIC ON]";
    auto hint=makeText(endHint,16, talking ? sf::Color(100,255,100) : sf::Color(100,110,130));
    hint.setPosition({WIN_W/2.f-90.f,WIN_H-32.f}); w.draw(hint);
}


// Pause Menu  (drawn on top of whatever the current phase is showing)

void GameClient::drawPauseMenu(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;

    // Dimmed full-screen overlay
    sf::RectangleShape overlay({(float)WIN_W,(float)WIN_H});
    overlay.setFillColor(sf::Color(0,0,0,170));
    w.draw(overlay);

    // Card — wider and taller to fit two sliders + two buttons comfortably
    const float CW=440.f, CH=420.f;
    const float CX=(WIN_W-CW)*0.5f, CY=(WIN_H-CH)*0.5f;
    sf::RectangleShape card({CW,CH});
    card.setPosition({CX,CY});
    card.setFillColor(sf::Color(16,20,32,250));
    card.setOutlineThickness(2.f);
    card.setOutlineColor(sf::Color(0,200,140));
    w.draw(card);

    // Accent bar at top of card
    sf::RectangleShape accentBar({CW,4.f});
    accentBar.setPosition({CX,CY});
    accentBar.setFillColor(sf::Color(0,200,140));
    w.draw(accentBar);

    // Title
    auto title = makeText("PAUSED",30,sf::Color(0,200,140));
    title.setStyle(sf::Text::Bold);
    auto tb = title.getLocalBounds();
    title.setPosition({CX+(CW-tb.size.x)*0.5f-tb.position.x, CY+16.f});
    w.draw(title);

    // Thin divider under title
    sf::RectangleShape div({CW-60.f,1.f});
    div.setFillColor(sf::Color(40,55,75));
    div.setPosition({CX+30.f, CY+58.f});
    w.draw(div);

    auto mp = w.mapPixelToCoords(sf::Mouse::getPosition(w), m_lbView);
    bool lmbDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    //  Generic slider helper 
    //  returns true if the value changed this frame
    auto drawSlider = [&](const std::string& label, float sY,
                          float& value, sf::Color fillCol) -> bool
    {
        auto lbl = makeText(label, 15, sf::Color(160,170,190));
        lbl.setPosition({CX+30.f, sY});
        w.draw(lbl);

        const float TX=CX+30.f, TY=sY+26.f, TW=CW-100.f, TH=6.f;

        // Track
        sf::RectangleShape track({TW,TH});
        track.setPosition({TX,TY}); track.setFillColor(sf::Color(35,45,65));
        w.draw(track);
        // Fill
        float frac = value/100.f;
        if(frac>0.f){
            sf::RectangleShape fill({TW*frac,TH});
            fill.setPosition({TX,TY}); fill.setFillColor(fillCol);
            w.draw(fill);
        }
        // Thumb
        float thumbX = TX + TW*frac;
        bool overThumb = std::abs(mp.x - thumbX) < 14.f &&
                         std::abs(mp.y - (TY+TH*0.5f)) < 14.f;
        sf::CircleShape thumb(overThumb ? 10.f : 8.f);
        thumb.setOrigin({thumb.getRadius(),thumb.getRadius()});
        thumb.setPosition({thumbX, TY+TH*0.5f});
        thumb.setFillColor(overThumb ? sf::Color(255,255,255) : fillCol);
        thumb.setOutlineThickness(1.5f);
        thumb.setOutlineColor(sf::Color(200,240,230,180));
        w.draw(thumb);
        // Percentage label
        char vbuf[8]; snprintf(vbuf,sizeof(vbuf),"%d%%",(int)value);
        auto vt=makeText(vbuf, 14, sf::Color(170,185,200));
        vt.setPosition({TX+TW+10.f, TY-4.f}); w.draw(vt);

        // Drag
        bool changed = false;
        if(lmbDown && mp.x>=TX-8 && mp.x<=TX+TW+8 &&
           mp.y>=TY-14 && mp.y<=TY+TH+14)
        {
            float newVal = std::max(0.f, std::min(100.f,
                (float)(mp.x-TX)/TW*100.f));
            if(newVal != value){ value=newVal; changed=true; }
        }
        return changed;
    };

    //  Music / SFX volume 
    if(drawSlider("Music & SFX Volume", CY+72.f, m_volume, sf::Color(0,200,140)))
    {
        if(m_audioOk) m_bgm.setVolume(m_volume*0.35f);
        if(m_sndShoot)   m_sndShoot->setVolume(m_volume);
        if(m_sndHit)     m_sndHit->setVolume(m_volume);
        if(m_sndDead)    m_sndDead->setVolume(m_volume);
        if(m_sndPowerup) m_sndPowerup->setVolume(m_volume);
    }

    //  Voice chat volume 
    if(drawSlider("Voice Chat Volume", CY+154.f, m_voiceVolume, sf::Color(80,180,255)))
    {
        // VoicePlayer doesn't expose a volume API directly, but we can scale
        // by storing the value and applying it when feeding audio to SFML Sound
        // For now the value is stored and applied in VoicePlayer::tick via global
        // We pass it through the VoiceChat manager
        m_voice.setVolume(m_voiceVolume);
    }

    // Divider above buttons
    sf::RectangleShape div2({CW-60.f,1.f});
    div2.setFillColor(sf::Color(40,55,75));
    div2.setPosition({CX+30.f, CY+238.f});
    w.draw(div2);

    //  Button helper (edge-triggered via static tracker) 
    auto drawBtn = [&](float y, const std::string& lbl,
                       sf::Color normCol, sf::Color hovCol,
                       bool& wasHov) -> bool
    {
        const float BX=CX+30.f, BW=CW-60.f, BH=44.f;
        bool h = (mp.x>=(int)BX && mp.x<=(int)(BX+BW) &&
                  mp.y>=(int)y  && mp.y<=(int)(y+BH));
        sf::RectangleShape btn({BW,BH});
        btn.setPosition({BX,y});
        btn.setFillColor(h ? hovCol : normCol);
        btn.setOutlineThickness(1.f);
        btn.setOutlineColor(sf::Color(50,65,85));
        w.draw(btn);
        auto lt = makeText(lbl, 18, h ? sf::Color::Black : sf::Color(200,210,230));
        auto lb = lt.getLocalBounds();
        lt.setPosition({BX+(BW-lb.size.x)*0.5f-lb.position.x,
                        y +(BH-lb.size.y)*0.5f-lb.position.y});
        w.draw(lt);
        bool clicked = h && lmbDown && !wasHov;
        wasHov = h && lmbDown;
        return clicked;
    };

    static bool resumeHov=false, leaveHov=false;

    if(drawBtn(CY+256.f, "Resume  (Esc)",
               sf::Color(24,36,54), sf::Color(0,180,120), resumeHov))
        m_pauseOpen = false;

    std::string leaveLbl = (m_phase==ClientPhase::LOBBY) ? "Leave Lobby" : "Leave Game";
    if(drawBtn(CY+314.f, leaveLbl,
               sf::Color(55,15,15), sf::Color(200,55,55), leaveHov))
    {
        PktDisconnect d; d.pid=m_pid;
        m_net.send(&d,sizeof(d));
        m_disconnectMsg = "You left the " +
            std::string(m_phase==ClientPhase::LOBBY ? "lobby." : "game.");
        m_phase = ClientPhase::DISCONNECTED;
        m_pauseOpen = false;
        if(m_audioOk) m_bgm.stop();
    }

    // Bottom hint
    auto esc=makeText("[Esc] to Resume", 13, sf::Color(55,68,88));
    auto eb=esc.getLocalBounds();
    esc.setPosition({CX+(CW-eb.size.x)*0.5f-eb.position.x, CY+CH-22.f});
    w.draw(esc);
}


// Disconnected screen

void GameClient::drawDisconnected(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;

    // Full dark background
    sf::RectangleShape bg({(float)WIN_W,(float)WIN_H});
    bg.setFillColor(sf::Color(8,10,16));
    w.draw(bg);

    // Broken-connection X icon
    const float CX=WIN_W*0.5f, CY=WIN_H*0.5f-70.f;
    for(int i=-1;i<=1;i+=2)
    {
        sf::RectangleShape arm({80.f,12.f});
        arm.setOrigin({40.f,6.f});
        arm.setPosition({CX,CY});
        arm.setRotation(sf::degrees(45.f*(float)i));
        arm.setFillColor(sf::Color(220,60,60));
        w.draw(arm);
    }

    // Title
    auto title=makeText("DISCONNECTED",32,sf::Color(220,60,60));
    title.setStyle(sf::Text::Bold);
    auto tb=title.getLocalBounds();
    title.setPosition({WIN_W*0.5f-tb.size.x*0.5f-tb.position.x, CY+52.f});
    w.draw(title);

    // Message
    if(!m_disconnectMsg.empty())
    {
        auto msg=makeText(m_disconnectMsg,18,sf::Color(150,160,180));
        auto mb=msg.getLocalBounds();
        msg.setPosition({WIN_W*0.5f-mb.size.x*0.5f-mb.position.x, CY+96.f});
        w.draw(msg);
    }

    // "Return to Menu" button
    const float BW=220.f, BH=44.f;
    const float BX=WIN_W*0.5f-BW*0.5f, BY=CY+145.f;
    auto mp = w.mapPixelToCoords(sf::Mouse::getPosition(w), m_lbView);
    bool hov = mp.x>=(int)BX && mp.x<=(int)(BX+BW) &&
               mp.y>=(int)BY && mp.y<=(int)(BY+BH);
    sf::RectangleShape btn({BW,BH});
    btn.setPosition({BX,BY});
    btn.setFillColor(hov ? sf::Color(0,160,100) : sf::Color(20,32,48));
    btn.setOutlineThickness(1.5f);
    btn.setOutlineColor(sf::Color(0,200,140));
    w.draw(btn);
    auto bl=makeText("Return to Menu",18,hov?sf::Color::Black:sf::Color(0,200,140));
    auto bb=bl.getLocalBounds();
    bl.setPosition({BX+(BW-bb.size.x)*0.5f-bb.position.x,
                    BY+(BH-bb.size.y)*0.5f-bb.position.y});
    w.draw(bl);
    if(hov && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
        w.close();

    // Hint
    auto hint=makeText("Press Esc or click Return to Menu",14,sf::Color(50,60,80));
    auto hb=hint.getLocalBounds();
    hint.setPosition({WIN_W*0.5f-hb.size.x*0.5f-hb.position.x, WIN_H-44.f});
    w.draw(hint);
}


// Background – drawn in WORLD space while the game view is active.
// Covers the full MAP_W × MAP_H area with a dirt tactical ground.

void GameClient::drawBackground(sf::RenderWindow& w)
{
    // Base dirt
    sf::RectangleShape dirt({(float)MAP_W,(float)MAP_H});
    dirt.setPosition({0.f,0.f});
    dirt.setFillColor(sf::Color(52,46,36));
    w.draw(dirt);

    // Tactical grid
    const float CELL = 128.f;
    sf::Color gridCol(60,54,42,90);
    for(float x=0.f; x<=(float)MAP_W; x+=CELL)
    {
        sf::RectangleShape ln({1.f,(float)MAP_H});
        ln.setFillColor(gridCol); ln.setPosition({x,0.f}); w.draw(ln);
    }
    for(float y=0.f; y<=(float)MAP_H; y+=CELL)
    {
        sf::RectangleShape ln({(float)MAP_W,1.f});
        ln.setFillColor(gridCol); ln.setPosition({0.f,y}); w.draw(ln);
    }

    // Procedural dirt patches
    auto lcg = [](unsigned& s) -> unsigned {
        s = s*1664525u+1013904223u; return s;
    };
    unsigned seed = 0xCAFEBABE;
    for(int i=0;i<200;i++)
    {
        float px = (float)(lcg(seed)%(unsigned)MAP_W);
        float py = (float)(lcg(seed)%(unsigned)MAP_H);
        float r  = 20.f + (float)(lcg(seed)%40);
        uint8_t v = 42 + (uint8_t)(lcg(seed)%18);
        sf::CircleShape patch(r);
        patch.setOrigin({r,r}); patch.setPosition({px,py});
        patch.setFillColor(sf::Color(v+8,v,v-6,75));
        w.draw(patch);
    }

    // Tyre tracks
    seed = 0xDEAD1234;
    for(int i=0;i<50;i++)
    {
        float tx  = 60.f + (float)(lcg(seed)%(unsigned)(MAP_W-120));
        float ty  = 60.f + (float)(lcg(seed)%(unsigned)(MAP_H-120));
        float len = 80.f + (float)(lcg(seed)%120);
        float ang = (float)(lcg(seed)%180);
        sf::Color tc(36,30,22,110);
        for(int side=-1;side<=1;side+=2)
        {
            sf::RectangleShape track({len,3.f});
            track.setFillColor(tc);
            track.setOrigin({len*0.5f,1.5f});
            track.setPosition({tx+(float)side*7.f,ty});
            track.setRotation(sf::degrees(ang));
            w.draw(track);
        }
    }
}


// Border – heavy armoured wall drawn IN WORLD SPACE around the map perimeter.
// Called while the game view is active, so coordinates are world coordinates.

void GameClient::drawBorder(sf::RenderWindow& w)
{
    const float BT = 40.f;   // border thickness in world units
    const float MW = (float)MAP_W;
    const float MH = (float)MAP_H;

    sf::Color baseCol (68, 64, 56);
    sf::Color hiCol   (105,100,90,200);
    sf::Color shadCol (30, 28, 22);
    sf::Color boltCol (46, 42, 36);

    // Draw one slab with highlight/shadow edges
    auto drawSlab = [&](float x, float y, float sw, float sh)
    {
        sf::RectangleShape slab({sw,sh});
        slab.setPosition({x,y}); slab.setFillColor(baseCol); w.draw(slab);
        sf::RectangleShape hi({sw,4.f});
        hi.setFillColor(hiCol); hi.setPosition({x,y}); w.draw(hi);
        sf::RectangleShape hi2({4.f,sh});
        hi2.setFillColor(hiCol); hi2.setPosition({x,y}); w.draw(hi2);
        sf::RectangleShape sh2({sw,4.f});
        sh2.setFillColor(shadCol); sh2.setPosition({x,y+sh-4.f}); w.draw(sh2);
        sf::RectangleShape sh3({4.f,sh});
        sh3.setFillColor(shadCol); sh3.setPosition({x+sw-4.f,y}); w.draw(sh3);
    };

    drawSlab(-BT,    -BT,    MW+BT*2, BT);    // Top
    drawSlab(-BT,    MH,     MW+BT*2, BT);    // Bottom
    drawSlab(-BT,    0.f,    BT,      MH);    // Left
    drawSlab(MW,     0.f,    BT,      MH);    // Right

    // Corner plates
    sf::Color cornerCol(50,46,40);
    auto drawCorner = [&](float x,float y)
    {
        sf::RectangleShape c({BT,BT});
        c.setPosition({x,y}); c.setFillColor(cornerCol);
        c.setOutlineThickness(2.f); c.setOutlineColor(shadCol); w.draw(c);
        sf::RectangleShape diag({BT*1.4f,4.f});
        diag.setOrigin({BT*0.7f,2.f}); diag.setPosition({x+BT*0.5f,y+BT*0.5f});
        diag.setRotation(sf::degrees(45.f)); diag.setFillColor(sf::Color(38,34,28,160));
        w.draw(diag);
    };
    drawCorner(-BT,  -BT);
    drawCorner(MW,   -BT);
    drawCorner(-BT,  MH);
    drawCorner(MW,   MH);

    // Bolt rivets
    auto drawBolt = [&](float x, float y)
    {
        sf::CircleShape bolt(5.f);
        bolt.setOrigin({5.f,5.f}); bolt.setPosition({x,y});
        bolt.setFillColor(boltCol);
        bolt.setOutlineThickness(1.5f); bolt.setOutlineColor(sf::Color(22,20,16));
        w.draw(bolt);
        sf::CircleShape shine(1.5f);
        shine.setOrigin({1.5f,1.5f}); shine.setPosition({x-1.5f,y-1.5f});
        shine.setFillColor(sf::Color(120,115,105,150)); w.draw(shine);
    };
    for(float x=100.f; x<MW; x+=120.f)
    {
        drawBolt(x, -BT*0.5f);
        drawBolt(x, MH+BT*0.5f);
    }
    for(float y=100.f; y<MH; y+=120.f)
    {
        drawBolt(-BT*0.5f, y);
        drawBolt(MW+BT*0.5f, y);
    }

    // "DANGER ZONE" text on top border
    if(m_fontLoaded)
    {
        auto warn=makeText("D A N G E R   Z O N E", 18, sf::Color(90,82,70,180));
        auto wb=warn.getLocalBounds();
        warn.setPosition({MW*0.5f-wb.size.x*0.5f-wb.position.x, -BT+8.f});
        w.draw(warn);
    }
}


// Screen-space border -- drawn in DEFAULT VIEW coords (pixels).
// Used in spectator/dead mode to frame the minimap with the same armoured
// aesthetic as the world-space border, but fitted to the window edges.
// margin = screen pixels of gap from window edge to playfield rect.

void GameClient::drawScreenBorder(sf::RenderWindow& w, float margin)
{
    const float BT = margin;          // use the margin itself as thickness
    const float IW = WIN_W - BT*2.f; // inner rect width
    const float IH = WIN_H - BT*2.f; // inner rect height

    sf::Color base  (68, 64, 56);
    sf::Color hi    (105,100,90,200);
    sf::Color shad  (30, 28, 22);
    sf::Color bolt  (46, 42, 36);

    // Helper: draw a slab rectangle
    auto slab = [&](float x, float y, float sw, float sh){
        sf::RectangleShape r({sw,sh});
        r.setPosition({x,y}); r.setFillColor(base); w.draw(r);
        sf::RectangleShape h({sw,3.f});
        h.setFillColor(hi); h.setPosition({x,y}); w.draw(h);
        sf::RectangleShape h2({3.f,sh});
        h2.setFillColor(hi); h2.setPosition({x,y}); w.draw(h2);
        sf::RectangleShape s2({sw,3.f});
        s2.setFillColor(shad); s2.setPosition({x,y+sh-3.f}); w.draw(s2);
        sf::RectangleShape s3({3.f,sh});
        s3.setFillColor(shad); s3.setPosition({x+sw-3.f,y}); w.draw(s3);
    };

    // Four sides
    slab(0.f,       0.f,       WIN_W,     BT);          // top
    slab(0.f,       WIN_H-BT,  WIN_W,     BT);          // bottom
    slab(0.f,       BT,        BT,        IH);          // left
    slab(WIN_W-BT,  BT,        BT,        IH);          // right

    // Corners
    sf::Color cornerCol(50,46,40);
    auto corner = [&](float x, float y){
        sf::RectangleShape c({BT,BT});
        c.setPosition({x,y}); c.setFillColor(cornerCol);
        c.setOutlineThickness(2.f); c.setOutlineColor(shad); w.draw(c);
        sf::RectangleShape d({BT*1.4f,3.f});
        d.setOrigin({BT*0.7f,1.5f}); d.setPosition({x+BT*0.5f,y+BT*0.5f});
        d.setRotation(sf::degrees(45.f));
        d.setFillColor(sf::Color(38,34,28,160)); w.draw(d);
    };
    corner(0.f,          0.f);
    corner(WIN_W-BT,     0.f);
    corner(0.f,          WIN_H-BT);
    corner(WIN_W-BT,     WIN_H-BT);

    // Bolt rivets
    auto boltFn = [&](float x, float y){
        sf::CircleShape b(4.f);
        b.setOrigin({4.f,4.f}); b.setPosition({x,y});
        b.setFillColor(bolt);
        b.setOutlineThickness(1.f); b.setOutlineColor(sf::Color(20,18,14));
        w.draw(b);
    };
    float bstep = 80.f;
    for(float x=BT+bstep*0.5f; x<WIN_W-BT; x+=bstep){
        boltFn(x, BT*0.5f);
        boltFn(x, WIN_H-BT*0.5f);
    }
    for(float y=BT+bstep*0.5f; y<WIN_H-BT; y+=bstep){
        boltFn(BT*0.5f, y);
        boltFn(WIN_W-BT*0.5f, y);
    }
}
