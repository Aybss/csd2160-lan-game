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

//  Skin colours 
static const sf::Color SKIN_COLORS[SKIN_COUNT] = {
    sf::Color(100,200,100),   // 0 default green
    sf::Color(100,150,220),   // 1 blue
    sf::Color(220,80,80),     // 2 red
    sf::Color(220,180,50),    // 3 gold
    sf::Color(180,80,220),    // 4 purple
};
static const std::string SKIN_NAMES[SKIN_COUNT] = {
    "Green (free)", "Blue (40c)", "Red (80c)", "Gold (120c)", "Purple (200c)"
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
    for (auto& f : fonts)
        if (m_font.openFromFile(f)) { m_fontLoaded = true; break; }

    // Sounds – graceful fallback if files missing
    // SFML 3: SoundBuffer uses openFromFile; Sound requires buffer in constructor
    if (m_sbShoot.loadFromFile("assets/shoot.wav"))    m_sndShoot.emplace(m_sbShoot);
    if (m_sbHit.loadFromFile("assets/hit.wav"))        m_sndHit.emplace(m_sbHit);
    if (m_sbDead.loadFromFile("assets/dead.wav"))      m_sndDead.emplace(m_sbDead);
    if (m_sbPowerup.loadFromFile("assets/powerup.wav")) m_sndPowerup.emplace(m_sbPowerup);
    if (m_bgm.openFromFile("assets/bgm.ogg"))
    {
        m_bgm.setLooping(true);   // SFML 3: setLoop -> setLooping
        m_bgm.setVolume(35.f);
        m_bgm.play();
        m_audioOk = true;
    }
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
    switch (t) {
    case PowerupType::SPEED:     return sf::Color(0, 220, 255);
    case PowerupType::RAPIDFIRE: return sf::Color(255, 100, 0);
    case PowerupType::SHIELD:    return sf::Color(100, 255, 100);
    default:                     return sf::Color::White;
    }
}

std::string GameClient::powerupName(PowerupType t) const
{
    switch (t) {
    case PowerupType::SPEED:     return "Speed Boost";
    case PowerupType::RAPIDFIRE: return "Rapid Fire";
    case PowerupType::SHIELD:    return "Shield";
    default:                     return "Unknown";
    }
}

std::string GameClient::pidName(uint8_t pid) const
{
    if (pid < MAX_PLAYERS && m_lobby.slots[pid].active)
        return std::string(m_lobby.slots[pid].name);
    return "P" + std::to_string((int)pid);
}

void GameClient::generateObstacles(uint32_t seed)
{
    m_obstacles.clear();
    srand(seed);
    float cellW = (float)MAP_W / OBS_COLS;
    float cellH = (float)MAP_H / OBS_ROWS;
    for (int r = 1;r < OBS_ROWS - 1;r++)
        for (int c = 1;c < OBS_COLS - 1;c++)
        {
            if (rand() % 3 != 0) continue;
            float bw = 40.f + rand() % 60, bh = 40.f + rand() % 60;
            float bx = c * cellW + (cellW - bw) / 2.f, by = r * cellH + (cellH - bh) / 2.f;
            m_obstacles.push_back({ bx,by,bw,bh });
        }
}


// Main loop
void GameClient::run()
{
    sf::RenderWindow window(
        sf::VideoMode({ (unsigned)WIN_W,(unsigned)WIN_H }),
        "TankNet - " + m_username
    );
    window.setFramerateLimit(60);
    sf::Clock clock;

    // Send initial connect
    PktConnect conn; strncpy(conn.name, m_username.c_str(), 15);
    m_net.send(&conn, sizeof(conn));

    while (window.isOpen())
    {
        // Events
        while (const auto ev = window.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) window.close();

            if (m_phase == ClientPhase::LOBBY || m_phase == ClientPhase::IN_GAME)
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
                        }
                        else m_chatActive = true;
                    }
                    if (kp->code == sf::Keyboard::Key::Escape && m_chatActive)
                    {
                        m_chatActive = false; m_chatInput.clear();
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
        }

        m_dt = clock.restart().asSeconds();
        m_dt = std::min(m_dt, 0.05f);

        m_net.poll();
        processPackets();

        // Push-to-talk voice
        bool talking = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::V);
        m_voice.setTalking(talking);
        m_voice.tick();

        // Retry connect
        if (m_phase == ClientPhase::CONNECTING)
        {
            static float retryTimer = 0.f;
            retryTimer += m_dt;
            if (retryTimer > 1.5f) {
                PktConnect conn2; strncpy(conn2.name, m_username.c_str(), 15);
                m_net.send(&conn2, sizeof(conn2));
                retryTimer = 0.f;
            }
        }

        window.clear(sf::Color(15, 15, 25));
        switch (m_phase)
        {
        case ClientPhase::CONNECTING:  drawConnecting(window);  break;
        case ClientPhase::LOBBY:       updateLobby(window); drawLobby(window); break;
        case ClientPhase::SHOP:        updateShop(window);  drawShop(window);  break;
        case ClientPhase::IN_GAME:     sendInput(window); drawInGame(window);  break;
        case ClientPhase::ROUND_OVER:  drawRoundOver(window); break;
        case ClientPhase::MATCH_OVER:  drawMatchOver(window);  break;
        }
        window.display();
    }

    PktDisconnect d; d.pid = m_pid;
    m_net.send(&d, sizeof(d));
}


// Packet processing

void GameClient::processPackets()
{
    Envelope e;
    while (m_net.pollEnvelope(e))
    {
        if (e.len < 1) continue;
        PktType t = (PktType)e.buf[0];
        switch (t)
        {
        case PktType::CONNECT_ACK:
            if (e.len >= (int)sizeof(PktConnectAck)) {
                PktConnectAck a; memcpy(&a, e.buf, sizeof(a));
                if (!a.denied) {
                    m_pid = a.pid; m_net.m_pid = a.pid; m_net.m_connected = true;
                    m_phase = ClientPhase::LOBBY;
                    std::cout << "[Client] Connected as pid " << (int)m_pid << "\n";
                    // Init voice chat now that we know our pid
                    if (!m_voiceInit) {
                        m_voice.init(m_pid, [this](const void* d, int l) { m_net.send(d, l); });
                        m_voiceInit = true;
                    }
                }
            } break;

        case PktType::LOBBY_STATE:
            if (e.len >= (int)sizeof(PktLobbyState)) {
                memcpy(&m_lobby, e.buf, sizeof(m_lobby));
                // Server sent lobby state = we are back in lobby
                if (m_phase == ClientPhase::MATCH_OVER || m_phase == ClientPhase::ROUND_OVER)
                {
                    m_phase = ClientPhase::LOBBY;
                    m_ready = false;  // reset ready flag for next match
                    memset(m_roundWins, 0, sizeof(m_roundWins));
                }
            }
            break;

        case PktType::GAME_START:
            if (e.len >= (int)sizeof(PktGameStart)) {
                PktGameStart gs; memcpy(&gs, e.buf, sizeof(gs));
                m_mapSeed = gs.mapSeed;
                generateObstacles(m_mapSeed);
                m_phase = ClientPhase::IN_GAME;
                m_lastSeq = 0;
                m_gameState = PktGameState{};  // cleared; server sends immediate state
                if (m_audioOk) m_bgm.play();
            } break;

        case PktType::GAME_STATE:
            if (e.len >= (int)sizeof(PktGameState)) {
                PktGameState gs; memcpy(&gs, e.buf, sizeof(gs));
                if (gs.seq >= m_lastSeq) {
                    m_prevState = m_gameState;
                    m_gameState = gs;
                    m_lastSeq = gs.seq;
                    m_stateAge = 0.f;
                }
            } break;

        case PktType::BULLET_SPAWN:
            if (m_audioOk && m_sndShoot) m_sndShoot->play();
            break;

        case PktType::PLAYER_HIT:
            if (m_audioOk && m_sndHit)   m_sndHit->play();
            break;

        case PktType::PLAYER_DEAD:
            if (m_audioOk && m_sndDead)  m_sndDead->play();
            break;

        case PktType::ROUND_OVER:
            if (e.len >= (int)sizeof(PktRoundOver)) {
                PktRoundOver ro; memcpy(&ro, e.buf, sizeof(ro));
                for (int i = 0;i < MAX_PLAYERS;i++) m_roundWins[i] = ro.roundWins[i];
                m_phase = ClientPhase::ROUND_OVER;
            } break;

        case PktType::MATCH_OVER:
            if (e.len >= (int)sizeof(PktMatchOver)) {
                memcpy(&m_matchOver, e.buf, sizeof(m_matchOver));
                m_phase = ClientPhase::MATCH_OVER;
                if (m_audioOk) m_bgm.stop();
            } break;

        case PktType::POWERUP_COLLECT:
            if (e.len >= (int)sizeof(PktPowerupCollect)) {
                PktPowerupCollect pc; memcpy(&pc, e.buf, sizeof(pc));
                if (m_sndPowerup) m_sndPowerup->play();
                // Add chat notification
                std::string note = "*** " + pidName(pc.pid) + " picked up " + powerupName(pc.ptype) + "! ***";
                m_chatHistory.push_back(note);
                if ((int)m_chatHistory.size() > MAX_CHAT_HIST) m_chatHistory.pop_front();
            } break;

        case PktType::VOICE_DATA:
            if (e.len >= (int)(sizeof(PktType) + sizeof(uint8_t) + sizeof(uint16_t))) {
                uint8_t  vpid = e.buf[1];
                uint16_t vlen = 0; memcpy(&vlen, e.buf + 2, 2);
                if (vlen > 0 && vlen <= VOICE_MAX_BYTES && e.len >= (int)(4 + vlen))
                    m_voice.feedIncoming(vpid, e.buf + 4, vlen);
            } break;

        case PktType::CHAT:
            if (e.len >= (int)sizeof(PktChat)) {
                PktChat c; memcpy(&c, e.buf, sizeof(c));
                c.msg[MAX_CHAT_MSG - 1] = '\0';
                std::string line = "[" + pidName(c.pid) + "] " + c.msg;
                m_chatHistory.push_back(line);
                if ((int)m_chatHistory.size() > MAX_CHAT_HIST) m_chatHistory.pop_front();
            } break;

        case PktType::PROFILE_UPDATE:
            if (e.len >= (int)sizeof(PktProfileUpdate))
                memcpy(&m_profile, e.buf, sizeof(m_profile));
            break;

        case PktType::BUY_SKIN_ACK:
            if (e.len >= (int)sizeof(PktBuySkinAck)) {
                PktBuySkinAck ack; memcpy(&ack, e.buf, sizeof(ack));
                if (ack.success) {
                    // Auto-equip the just-bought skin
                    m_selSkin = ack.skinIdx;
                    PktPlayerReady pr; pr.pid = m_pid; pr.ready = m_ready ? 1 : 0; pr.skin = m_selSkin;
                    m_net.send(&pr, sizeof(pr));
                }
            }
            break;

        case PktType::DISCONNECT:
            if (e.len >= (int)sizeof(PktDisconnect)) {
                PktDisconnect d; memcpy(&d, e.buf, sizeof(d));
                std::string line = "*** " + pidName(d.pid) + " disconnected ***";
                m_chatHistory.push_back(line);
                if ((int)m_chatHistory.size() > MAX_CHAT_HIST) m_chatHistory.pop_front();
            } break;

        default: break;
        }
    }
}


// Input

void GameClient::sendInput(sf::RenderWindow& /*w*/)
{
    if (m_chatActive) return;
    PktInput inp;
    inp.pid = m_pid;
    inp.seq = m_inputSeq++;
    inp.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) ? 1 : 0;
    inp.back = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) ? 1 : 0;
    inp.left = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) ? 1 : 0;
    inp.right = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) ? 1 : 0;
    inp.fire = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space) ? 1 : 0;
    m_net.send(&inp, sizeof(inp));

    m_stateAge += m_dt;
}


// Draw helpers

void GameClient::drawConnecting(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;
    auto t = makeText("Connecting to server...", 28);
    t.setPosition({ WIN_W / 2.f - 160.f, WIN_H / 2.f });
    w.draw(t);
}

void GameClient::updateLobby(sf::RenderWindow& w)
{
    static bool rHeld = false;
    static bool sHeld = false;

    if (m_chatActive) {
        // Reset held state so keys dont fire the moment chat closes
        rHeld = true;
        sHeld = true;
        (void)w; return;
    }

    // R = toggle ready
    bool rDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R);
    if (rDown && !rHeld) {
        m_ready = !m_ready;
        PktPlayerReady pr; pr.pid = m_pid; pr.ready = m_ready ? 1 : 0; pr.skin = m_selSkin;
        m_net.send(&pr, sizeof(pr));
    }
    rHeld = rDown;

    // O = open shop
    bool sDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::O);
    if (sDown && !sHeld) m_phase = ClientPhase::SHOP;
    sHeld = sDown;
    (void)w;
}

void GameClient::drawLobby(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;

    // Title
    auto title = makeText("TANK NET  -  Lobby", 36, sf::Color::Yellow);
    title.setPosition({ WIN_W / 2.f - 180.f, 20.f });
    w.draw(title);

    // Player list
    for (int i = 0;i < MAX_PLAYERS;i++)
    {
        auto& sl = m_lobby.slots[i];
        if (!sl.active) continue;
        float y = 100.f + i * 70.f;

        sf::RectangleShape bg({ 700.f,60.f });
        bg.setPosition({ 160.f,y });
        bg.setFillColor(sl.ready ? sf::Color(30, 80, 30) : sf::Color(40, 40, 60));
        w.draw(bg);

        // Skin swatch
        sf::CircleShape dot(18.f);
        dot.setFillColor(skinColor(sl.skin));
        dot.setPosition({ 175.f, y + 12.f });
        w.draw(dot);

        std::string line = std::string(sl.name) +
            "  Lv." + std::to_string(sl.level) +
            "  Wins:" + std::to_string(sl.wins) +
            (sl.ready ? "  [READY]" : "");
        auto t = makeText(line, 22, i == (int)m_pid ? sf::Color::Cyan : sf::Color::White);
        t.setPosition({ 220.f, y + 16.f });
        w.draw(t);
    }

    // Controls hint
    auto hint = makeText("[R] Toggle Ready   [O] Shop   [V] Toggle Voice Chat   [Enter] Chat", 18, sf::Color(160, 160, 160));
    hint.setPosition({ 160.f, WIN_H - 60.f });
    w.draw(hint);

    // Profile bar
    std::string prof = "Coins: " + std::to_string(m_profile.coins) +
        "  XP: " + std::to_string(m_profile.xp) +
        "  Lv." + std::to_string(m_profile.level);
    auto pt = makeText(prof, 20, sf::Color::Yellow);
    pt.setPosition({ 20.f, WIN_H - 30.f });
    w.draw(pt);

    drawChat(w);
}

void GameClient::updateShop(sf::RenderWindow& w)
{
    static bool upH = false, dnH = false, bH = false, escH = false;
    bool up = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W);      // W/S instead of arrows
    bool dn = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S);
    bool buy = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Enter);
    bool esc = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape);

    if (up && !upH) m_shopCursor = std::max(0, m_shopCursor - 1);
    if (dn && !dnH) m_shopCursor = std::min(SKIN_COUNT - 1, m_shopCursor + 1);
    if (buy && !bH)
    {
        bool owned = (m_profile.ownedSkins >> m_shopCursor) & 1;
        if (owned)
        {
            // Equip: set local selection and send ready packet with new skin
            m_selSkin = (uint8_t)m_shopCursor;
            PktPlayerReady pr; pr.pid = m_pid; pr.ready = m_ready ? 1 : 0; pr.skin = m_selSkin;
            m_net.send(&pr, sizeof(pr));
        }
        else
        {
            // Buy
            PktBuySkin bs; bs.pid = m_pid; bs.skinIdx = (uint8_t)m_shopCursor;
            m_net.send(&bs, sizeof(bs));
        }
    }
    if (esc && !escH) m_phase = ClientPhase::LOBBY;
    upH = up; dnH = dn; bH = buy; escH = esc;
    (void)w;
}

void GameClient::drawShop(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;
    auto title = makeText("SHOP - Tank Skins", 34, sf::Color::Yellow);
    title.setPosition({ WIN_W / 2.f - 160.f,30.f });
    w.draw(title);

    auto coins = makeText("Coins: " + std::to_string(m_profile.coins), 22, sf::Color(255, 215, 0));
    coins.setPosition({ 30.f,30.f });
    w.draw(coins);

    for (int i = 0;i < SKIN_COUNT;i++)
    {
        float y = 120.f + i * 80.f;
        bool owned = (m_profile.ownedSkins >> i) & 1;
        bool selected = i == m_shopCursor;

        sf::RectangleShape bg({ 600.f,65.f });
        bg.setPosition({ 200.f,y });
        bg.setFillColor(selected ? sf::Color(60, 60, 90) : sf::Color(30, 30, 50));
        if (selected) { bg.setOutlineThickness(2.f); bg.setOutlineColor(sf::Color::Cyan); }
        w.draw(bg);

        sf::CircleShape dot(22.f);
        dot.setFillColor(skinColor((uint8_t)i));
        dot.setPosition({ 215.f,y + 10.f });
        w.draw(dot);

        std::string label = SKIN_NAMES[i] + (owned ? "  [OWNED]" : "");
        auto t = makeText(label, 22, owned ? sf::Color::Green : sf::Color::White);
        t.setPosition({ 270.f,y + 18.f });
        w.draw(t);
    }
    auto hint = makeText("[W/S] Select   [Enter] Buy/Equip   [Esc] Back", 18, sf::Color(160, 160, 160));
    hint.setPosition({ 200.f,WIN_H - 50.f });
    w.draw(hint);
}

void GameClient::drawObstacles(sf::RenderWindow& w)
{
    for (auto& o : m_obstacles)
    {
        sf::RectangleShape r({ o.w,o.h });
        r.setPosition({ o.x,o.y });
        r.setFillColor(sf::Color(80, 60, 40));
        r.setOutlineThickness(2.f);
        r.setOutlineColor(sf::Color(60, 40, 20));
        w.draw(r);
    }
}

void GameClient::drawTank(sf::RenderWindow& w, const PlayerState& ps, uint8_t pid)
{
    if (!ps.alive) return;
    if (pid < MAX_PLAYERS && !m_lobby.slots[pid].active) return;
    sf::Color col = skinColor(ps.skin);

    // Body
    sf::RectangleShape body({ TANK_RADIUS * 2.f, TANK_RADIUS * 1.6f });
    body.setOrigin({ TANK_RADIUS, TANK_RADIUS * 0.8f });
    body.setPosition({ ps.x, ps.y });
    body.setRotation(sf::degrees(ps.angle));
    body.setFillColor(col);
    w.draw(body);

    // Barrel
    sf::RectangleShape barrel({ TANK_RADIUS * 1.4f, TANK_RADIUS * 0.4f });
    barrel.setOrigin({ 0.f, TANK_RADIUS * 0.2f });
    barrel.setPosition({ ps.x, ps.y });
    barrel.setRotation(sf::degrees(ps.angle));
    barrel.setFillColor(sf::Color(col.r / 2, col.g / 2, col.b / 2));
    w.draw(barrel);

    // HP pips
    for (int h = 0;h < ps.hp;h++) {
        sf::CircleShape pip(4.f);
        pip.setFillColor(sf::Color::Red);
        pip.setPosition({ ps.x - (TANK_MAX_HP * 10.f) / 2.f + h * 10.f, ps.y - TANK_RADIUS - 12.f });
        w.draw(pip);
    }

    // Buff icons below tank
    uint8_t buffs = m_gameState.buffs[pid];
    float bx = ps.x - 18.f;
    for (int b = 0;b < 3;b++) {
        if (buffs & (1 << b)) {
            sf::CircleShape bi(5.f);
            bi.setOrigin({ 5.f,5.f });
            bi.setPosition({ bx, ps.y + TANK_RADIUS + 8.f });
            bi.setFillColor(powerupColor((PowerupType)b));
            w.draw(bi);
            bx += 14.f;
        }
    }

    // Name tag
    if (m_fontLoaded) {
        auto tag = makeText(pidName(pid), 14, sf::Color::White);
        tag.setPosition({ ps.x - 24.f, ps.y - TANK_RADIUS - 26.f });
        w.draw(tag);
    }
}

void GameClient::drawBullet(sf::RenderWindow& w, const BulletState& bs)
{
    if (!bs.active) return;
    sf::CircleShape c(BULLET_RADIUS);
    c.setOrigin({ BULLET_RADIUS,BULLET_RADIUS });
    c.setPosition({ bs.x,bs.y });
    c.setFillColor(sf::Color::Yellow);
    w.draw(c);
}

void GameClient::drawPowerup(sf::RenderWindow& w, const PowerupState& ps)
{
    if (!ps.active) return;
    sf::Color col = powerupColor(ps.ptype);

    // Outer glow ring
    sf::CircleShape ring(POWERUP_RADIUS + 4.f);
    ring.setOrigin({ POWERUP_RADIUS + 4.f, POWERUP_RADIUS + 4.f });
    ring.setPosition({ ps.x, ps.y });
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineThickness(3.f);
    ring.setOutlineColor(sf::Color(col.r, col.g, col.b, 160));
    w.draw(ring);

    // Inner circle
    sf::CircleShape c(POWERUP_RADIUS);
    c.setOrigin({ POWERUP_RADIUS, POWERUP_RADIUS });
    c.setPosition({ ps.x, ps.y });
    c.setFillColor(sf::Color(col.r, col.g, col.b, 200));
    w.draw(c);

    // Letter label
    if (m_fontLoaded) {
        std::string label;
        switch (ps.ptype) {
        case PowerupType::SPEED:     label = "S"; break;
        case PowerupType::RAPIDFIRE: label = "R"; break;
        case PowerupType::SHIELD:    label = "P"; break;
        }
        sf::Text t(m_font, label, 14);
        t.setFillColor(sf::Color::White);
        auto b = t.getLocalBounds();
        t.setOrigin({ b.size.x / 2.f, b.size.y / 2.f });
        t.setPosition({ ps.x, ps.y - 4.f });
        w.draw(t);
    }
}

void GameClient::drawHUD(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;
    // Kill scoreboard top-left
    float y = 10.f;
    for (int i = 0;i < MAX_PLAYERS;i++) {
        if (!m_lobby.slots[i].active) continue;
        auto& ps = m_gameState.players[i];
        std::string s = std::string(m_lobby.slots[i].name) + "  K:" + std::to_string(ps.kills);
        sf::Color c = ps.alive ? skinColor(ps.skin) : sf::Color(100, 100, 100);
        if (!ps.alive) s += " [DEAD]";
        auto t = makeText(s, 18, c);
        t.setPosition({ 10.f,y }); w.draw(t);
        y += 22.f;
    }
    // Round wins top-right
    y = 10.f;
    for (int i = 0;i < MAX_PLAYERS;i++) {
        if (!m_lobby.slots[i].active) continue;
        std::string s = std::string(m_lobby.slots[i].name) + ": " + std::to_string(m_roundWins[i]) + " wins";
        auto t = makeText(s, 16, sf::Color::White);
        auto b = t.getLocalBounds();
        t.setPosition({ WIN_W - b.size.x - 10.f,y }); w.draw(t);
        y += 20.f;
    }
}

void GameClient::drawChat(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;
    float y = WIN_H - 180.f;
    for (auto& line : m_chatHistory) {
        auto t = makeText(line, 16, sf::Color(200, 220, 200));
        t.setPosition({ 10.f,y }); w.draw(t);
        y += 18.f;
    }
    if (m_chatActive) {
        sf::RectangleShape box({ 500.f,26.f });
        box.setPosition({ 10.f,WIN_H - 30.f });
        box.setFillColor(sf::Color(30, 30, 30));
        box.setOutlineThickness(1.f); box.setOutlineColor(sf::Color::Cyan);
        w.draw(box);
        auto t = makeText("> " + m_chatInput + "_", 16, sf::Color::Cyan);
        t.setPosition({ 14.f,WIN_H - 28.f }); w.draw(t);
    }
}

void GameClient::drawInGame(sf::RenderWindow& w)
{
    drawObstacles(w);
    for (auto& p : m_gameState.powerups) drawPowerup(w, p);
    for (int i = 0;i < MAX_PLAYERS;i++)    drawTank(w, m_gameState.players[i], (uint8_t)i);
    for (auto& b : m_gameState.bullets)  drawBullet(w, b);
    drawHUD(w);
    drawChat(w);

    // Controls reminder (small, bottom)
    if (m_fontLoaded) {
        bool talking = m_voice.isTalking();
        std::string hint = "WASD=Move  Space=Fire  Enter=Chat  V=Voice";
        sf::Color hcol = talking ? sf::Color(100, 255, 100) : sf::Color(80, 80, 80);
        std::string voiceStatus = talking ? "  [MIC ON]" : "";
        auto h = makeText(hint + voiceStatus, 14, hcol);
        h.setPosition({ 10.f,WIN_H - 18.f }); w.draw(h);
    }
}

void GameClient::drawRoundOver(sf::RenderWindow& w)
{
    drawInGame(w);  // show final state underneath
    if (!m_fontLoaded) return;
    sf::RectangleShape overlay({ (float)WIN_W,(float)WIN_H });
    overlay.setFillColor(sf::Color(0, 0, 0, 140));
    w.draw(overlay);

    auto t = makeText("Round Over!  Next round starting...", 32, sf::Color::Yellow);
    auto b = t.getLocalBounds();
    t.setPosition({ WIN_W / 2.f - b.size.x / 2.f, WIN_H / 2.f - 60.f });
    w.draw(t);

    float y = WIN_H / 2.f;
    for (int i = 0;i < MAX_PLAYERS;i++) {
        if (!m_lobby.slots[i].active) continue;
        std::string s = std::string(m_lobby.slots[i].name) + " - " + std::to_string(m_roundWins[i]) + " round wins";
        auto rt = makeText(s, 22, skinColor(m_gameState.players[i].skin));
        rt.setPosition({ WIN_W / 2.f - 180.f,y }); w.draw(rt);
        y += 30.f;
    }
}

void GameClient::drawMatchOver(sf::RenderWindow& w)
{
    if (!m_fontLoaded) return;

    auto title = makeText("MATCH OVER!", 48, sf::Color::Yellow);
    auto tb = title.getLocalBounds();
    title.setPosition({ WIN_W / 2.f - tb.size.x / 2.f,30.f });
    w.draw(title);

    std::string winner = "Winner: " + pidName(m_matchOver.winner);
    auto wt = makeText(winner, 30, sf::Color::Cyan);
    wt.setPosition({ WIN_W / 2.f - 120.f,100.f });
    w.draw(wt);

    // Stats
    float y = 160.f;
    auto sh = makeText("Player          Kills   XP+   Coins+", 22, sf::Color(180, 180, 180));
    sh.setPosition({ 100.f,y }); w.draw(sh); y += 30.f;
    for (int i = 0;i < MAX_PLAYERS;i++) {
        if (!m_lobby.slots[i].active) continue;
        std::string row = std::string(m_lobby.slots[i].name);
        while ((int)row.size() < 16) row += " ";
        row += std::to_string(m_matchOver.kills[i]) + "       " +
            std::to_string(m_matchOver.xpGained[i]) + "    " +
            std::to_string(m_matchOver.coinsGained[i]);
        auto rt = makeText(row, 20, skinColor(m_gameState.players[i].skin));
        rt.setPosition({ 100.f,y }); w.draw(rt);
        y += 26.f;
    }

    // Leaderboard
    y += 20.f;
    auto lbh = makeText("Global Leaderboard (Top 5 by Wins)", 22, sf::Color::Yellow);
    lbh.setPosition({ 100.f,y }); w.draw(lbh); y += 30.f;
    for (int i = 0;i < 5;i++) {
        if (m_matchOver.lbName[i][0] == '\0') break;
        std::string row = std::to_string(i + 1) + ". " + m_matchOver.lbName[i];
        while ((int)row.size() < 20) row += " ";
        row += "Wins:" + std::to_string(m_matchOver.lbWins[i]) + "  Lv." + std::to_string(m_matchOver.lbLevel[i]);
        auto lt = makeText(row, 20, sf::Color::White);
        lt.setPosition({ 100.f,y }); w.draw(lt);
        y += 24.f;
    }
    auto hint = makeText("[Esc] Quit", 18, sf::Color(120, 120, 120));
    hint.setPosition({ WIN_W / 2.f - 40.f,WIN_H - 40.f }); w.draw(hint);
}