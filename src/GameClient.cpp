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

// ── Skin base colours (used as fallback and for HUD tints) ──────────────────
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
        // ── Body texture ──────────────────────────────────────────────────
        m_skinBodyRT[s].resize({TW,TH});
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

        // ── Turret texture ────────────────────────────────────────────────
        m_skinTurretRT[s].resize({BW,BH});
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
    for(int r=1;r<OBS_ROWS-1;r++)
    for(int c=1;c<OBS_COLS-1;c++)
    {
        if(rand()%3 != 0) continue;
        float bw=40.f+rand()%60, bh=40.f+rand()%60;
        float bx=c*cellW+(cellW-bw)/2.f, by=r*cellH+(cellH-bh)/2.f;
        m_obstacles.push_back({bx,by,bw,bh});
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Main loop
// ════════════════════════════════════════════════════════════════════════════
void GameClient::run()
{
    sf::RenderWindow window(
        sf::VideoMode({(unsigned)WIN_W,(unsigned)WIN_H}),
        "TankNet - " + m_username
    );
    window.setFramerateLimit(60);
    sf::Clock clock;

    // Send initial connect
    PktConnect conn; strncpy(conn.name, m_username.c_str(), 15);
    m_net.send(&conn, sizeof(conn));

    while(window.isOpen())
    {
        // Events
        while(const auto ev = window.pollEvent())
        {
            if(ev->is<sf::Event::Closed>()) window.close();

            if(m_phase==ClientPhase::LOBBY || m_phase==ClientPhase::IN_GAME)
            {
                if(const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                {
                    if(kp->code == sf::Keyboard::Key::Enter && m_phase==ClientPhase::LOBBY)
                    {
                        if(m_chatActive) {
                            if(!m_chatInput.empty()){
                                PktChat c; c.pid=m_pid;
                                strncpy(c.msg,m_chatInput.c_str(),MAX_CHAT_MSG-1);
                                m_net.send(&c,sizeof(c));
                                m_chatInput.clear();
                            }
                            m_chatActive=false;
                        } else m_chatActive=true;
                    }
                    if(kp->code == sf::Keyboard::Key::Escape && m_chatActive)
                    { m_chatActive=false; m_chatInput.clear(); }
                    if(kp->code == sf::Keyboard::Key::Backspace && m_chatActive && !m_chatInput.empty())
                        m_chatInput.pop_back();
                }
                if(m_chatActive)
                    if(const auto* te = ev->getIf<sf::Event::TextEntered>())
                    {
                        char c = (char)te->unicode;
                        if(c>=32 && c<127 && m_chatInput.size()<MAX_CHAT_MSG-1)
                            m_chatInput += c;
                    }
            }
        }

        m_dt = clock.restart().asSeconds();
        m_dt = std::min(m_dt, 0.05f);

        m_net.poll();
        processPackets();

        // Voice chat runs in all phases (lobby, in-game, end screen)
        m_voice.setTalking(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::V));
        m_voice.tick();

        // Retry connect
        if(m_phase==ClientPhase::CONNECTING)
        {
            static float retryTimer=0.f;
            retryTimer+=m_dt;
            if(retryTimer>1.5f){
                PktConnect conn2; strncpy(conn2.name,m_username.c_str(),15);
                m_net.send(&conn2,sizeof(conn2));
                retryTimer=0.f;
            }
        }

        window.clear(sf::Color(15,15,25));
        switch(m_phase)
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

    PktDisconnect d; d.pid=m_pid;
    m_net.send(&d,sizeof(d));
}

// ════════════════════════════════════════════════════════════════════════════
// Packet processing
// ════════════════════════════════════════════════════════════════════════════
void GameClient::processPackets()
{
    Envelope e;
    while(m_net.pollEnvelope(e))
    {
        if(e.len<1) continue;
        PktType t=(PktType)e.buf[0];
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
                    // Server sent lobby state = we are back in lobby
                    if(m_phase==ClientPhase::MATCH_OVER || m_phase==ClientPhase::ROUND_OVER)
                    {
                        m_phase  = ClientPhase::LOBBY;
                        m_ready  = false;  // reset ready flag for next match
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
                if(e.len>=(int)sizeof(PktRoundOver)){
                    PktRoundOver ro; memcpy(&ro,e.buf,sizeof(ro));
                    for(int i=0;i<MAX_PLAYERS;i++) m_roundWins[i]=ro.roundWins[i];
                    m_phase=ClientPhase::ROUND_OVER;
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
                    std::string line = "*** " + pidName(d.pid) + " disconnected ***";
                    m_chatHistory.push_back(line);
                    if((int)m_chatHistory.size()>MAX_CHAT_HIST) m_chatHistory.pop_front();
                } break;

            default: break;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Input
// ════════════════════════════════════════════════════════════════════════════
void GameClient::sendInput(sf::RenderWindow& /*w*/)
{
    if(m_chatActive) return;
    PktInput inp;
    inp.pid     = m_pid;
    inp.seq     = m_inputSeq++;
    inp.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) ? 1:0;
    inp.back    = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) ? 1:0;
    inp.left    = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) ? 1:0;
    inp.right   = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) ? 1:0;
    inp.fire    = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space) ? 1:0;
    m_net.send(&inp,sizeof(inp));

    m_stateAge  += m_dt;
    m_gameTime  += m_dt;
    // Tick explosion particles
    for(auto& e : m_explosions) e.timer -= m_dt;
    m_explosions.erase(
        std::remove_if(m_explosions.begin(), m_explosions.end(),
            [](const Explosion& e){ return e.timer <= 0.f; }),
        m_explosions.end());
}

// ════════════════════════════════════════════════════════════════════════════
// Draw helpers
// ════════════════════════════════════════════════════════════════════════════
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

    if(m_chatActive){
        // Reset held state so keys dont fire the moment chat closes
        rHeld = true;
        sHeld = true;
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

    // O = open shop
    bool sDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::O);
    if(sDown && !sHeld) m_phase=ClientPhase::SHOP;
    sHeld=sDown;
    (void)w;
}

void GameClient::drawLobby(sf::RenderWindow& w)
{
    if(!m_fontLoaded) return;

    // Title
    // ── 3D camo title ─────────────────────────────────────────────────────
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
        auto sub = makeText("- Lobby", 20, sf::Color(160,200,140));
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
            (sl.ready ? "  [READY]" : "");
        auto t = makeText(line, 22, i==(int)m_pid ? sf::Color::Cyan : sf::Color::White);
        t.setPosition({220.f, y+16.f});
        w.draw(t);
    }

    // Controls hint
    std::string lobbyHint = "[R] Ready  [O] Shop  [Enter] Chat  [V] Voice";
    if(m_voice.isTalking()) lobbyHint += "  [MIC ON]";
    auto hint = makeText(lobbyHint, 18, m_voice.isTalking() ? sf::Color(100,255,100) : sf::Color(160,160,160));
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
    // Local LCG - deterministic per obstacle without corrupting global rand state
    auto lcg = [](unsigned& s) -> unsigned {
        s = s * 1664525u + 1013904223u;
        return s;
    };

    for(auto& o:m_obstacles)
    {
        float cx = o.x + o.w*0.5f;
        float cy = o.y + o.h*0.5f;
        float sz = std::max(o.w, o.h);

        if(sz > 80.f)
        {
            // ── Boulder ───────────────────────────────────────────────────
            // Base dark grey fill
            sf::RectangleShape base({o.w,o.h});
            base.setPosition({o.x,o.y});
            base.setFillColor(sf::Color(70,70,72));
            base.setOutlineThickness(2.f);
            base.setOutlineColor(sf::Color(40,40,42));
            w.draw(base);

            // Irregular blob clusters to fake roundness
            { unsigned s=(unsigned)(o.x*7+o.y*13);
            for(int i=0;i<5;i++){
                float blobR = o.w*0.25f + (float)(lcg(s)%(int)(o.w*0.15f+1));
                sf::CircleShape blob(blobR);
                blob.setOrigin({blobR,blobR});
                blob.setPosition({
                    o.x + (float)(lcg(s)%(int)(o.w+1)),
                    o.y + (float)(lcg(s)%(int)(o.h+1))});
                blob.setFillColor(sf::Color(75+lcg(s)%20, 74+lcg(s)%18, 76+lcg(s)%20));
                w.draw(blob);
            }

            // Crack lines
            for(int i=0;i<3;i++){
                sf::RectangleShape crack({o.w*0.3f+(float)(lcg(s)%(int)(o.w*0.2f+1)), 2.f});
                crack.setFillColor(sf::Color(35,33,35,180));
                crack.setRotation(sf::degrees((float)(lcg(s)%180)));
                crack.setPosition({cx - o.w*0.1f, cy - o.h*0.1f});
                w.draw(crack);
            }}

            // Highlight top-left
            sf::RectangleShape hl({o.w*0.6f,4.f});
            hl.setFillColor(sf::Color(120,118,122,100));
            hl.setPosition({o.x+6.f,o.y+6.f});
            w.draw(hl);

            // Moss patches (dark green smudges)
            { unsigned ms=(unsigned)(o.x*3+o.y*17);
            for(int i=0;i<3;i++){
                sf::CircleShape moss(6.f+(float)(lcg(ms)%8));
                moss.setFillColor(sf::Color(45,72,38,140));
                moss.setPosition({
                    o.x+(float)(lcg(ms)%(int)(o.w+1)),
                    o.y+(float)(lcg(ms)%(int)(o.h+1))});
                w.draw(moss);
            }}
        }
        else if(sz > 50.f)
        {
            // ── Stone wall ────────────────────────────────────────────────
            sf::RectangleShape wall({o.w,o.h});
            wall.setPosition({o.x,o.y});
            wall.setFillColor(sf::Color(100,92,80));
            wall.setOutlineThickness(2.f);
            wall.setOutlineColor(sf::Color(60,55,48));
            w.draw(wall);

            // Brick rows
            int brickH = 14;
            bool offset = false;
            for(float by=o.y; by<o.y+o.h; by+=brickH){
                float brickW = 28.f;
                float startX = offset ? o.x-brickW*0.5f : o.x;
                for(float bx=startX; bx<o.x+o.w; bx+=brickW){
                    sf::RectangleShape brick({brickW-2.f,(float)brickH-2.f});
                    brick.setPosition({bx+1.f,by+1.f});
                    { unsigned bs2=(unsigned)(bx*3+by*7);
                    brick.setFillColor(sf::Color(
                        95+lcg(bs2)%20, 86+lcg(bs2)%18, 74+lcg(bs2)%16)); }
                    brick.setOutlineThickness(1.f);
                    brick.setOutlineColor(sf::Color(55,50,44));
                    w.draw(brick);
                }
                offset = !offset;
            }

            // Top highlight
            sf::RectangleShape hl({o.w,3.f});
            hl.setFillColor(sf::Color(150,140,125,80));
            hl.setPosition({o.x,o.y});
            w.draw(hl);
        }
        else
        {
            // ── Tree / bush ───────────────────────────────────────────────
            // Brown trunk
            float trunkW = o.w*0.25f;
            float trunkH = o.h*0.4f;
            sf::RectangleShape trunk({trunkW,trunkH});
            trunk.setOrigin({trunkW*0.5f,0.f});
            trunk.setPosition({cx, o.y+o.h-trunkH});
            trunk.setFillColor(sf::Color(90,60,30));
            w.draw(trunk);

            // Layered foliage circles (dark green)
            { unsigned ts=(unsigned)(o.x*11+o.y*7);
            float leafR = o.w*0.5f;
            for(int layer=0;layer<3;layer++){
                float lr = leafR*(1.f-layer*0.15f);
                sf::CircleShape leaf(lr);
                leaf.setOrigin({lr,lr});
                leaf.setPosition({
                    cx+(float)(lcg(ts)%8)-4.f,
                    o.y+o.h*0.45f-(float)layer*6.f});
                leaf.setFillColor(sf::Color(
                    30+lcg(ts)%20,
                    80+lcg(ts)%40,
                    25+lcg(ts)%20));
                w.draw(leaf);
            }}

            // Highlight dot on top foliage
            sf::CircleShape shine(4.f);
            shine.setFillColor(sf::Color(120,200,80,100));
            shine.setPosition({cx-6.f, o.y+4.f});
            w.draw(shine);
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
        // ── Textured body ─────────────────────────────────────────────────
        sf::Sprite body(m_skinBodyTex[skin]);
        auto tb = m_skinBodyTex[skin].getSize();
        body.setOrigin({tb.x/2.f, tb.y/2.f});
        body.setPosition({ps.x, ps.y});
        body.setRotation(sf::degrees(ps.angle));
        // Scale to match TANK_RADIUS
        float scaleX = (TANK_RADIUS*2.f) / (float)tb.x;
        float scaleY = (TANK_RADIUS*1.6f) / (float)tb.y;
        body.setScale({scaleX, scaleY});
        w.draw(body);

        // ── Textured barrel ───────────────────────────────────────────────
        sf::Sprite barrel(m_skinTurretTex[skin]);
        auto tt = m_skinTurretTex[skin].getSize();
        barrel.setOrigin({0.f, tt.y/2.f});
        barrel.setPosition({ps.x, ps.y});
        barrel.setRotation(sf::degrees(ps.angle));
        float bscale = (TANK_RADIUS*0.45f) / (float)tt.y;
        barrel.setScale({bscale, bscale});
        w.draw(barrel);
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
    if(!m_fontLoaded) return;
    // Kill scoreboard top-left
    float y=10.f;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(!m_lobby.slots[i].active) continue;
        auto& ps = m_gameState.players[i];
        std::string s = std::string(m_lobby.slots[i].name) + "  K:" + std::to_string(ps.kills);
        sf::Color c = ps.alive ? skinColor(ps.skin) : sf::Color(100,100,100);
        if(!ps.alive) s += " [DEAD]";
        auto t = makeText(s,18,c);
        t.setPosition({10.f,y}); w.draw(t);
        y+=22.f;
    }
    // Round wins top-right
    y=10.f;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(!m_lobby.slots[i].active) continue;
        std::string s = std::string(m_lobby.slots[i].name)+": "+std::to_string(m_roundWins[i])+" wins";
        auto t=makeText(s,16,sf::Color::White);
        auto b=t.getLocalBounds();
        t.setPosition({WIN_W-b.size.x-10.f,y}); w.draw(t);
        y+=20.f;
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
    drawObstacles(w);
    for(int i=0;i<MAX_BARRELS;i++)    drawBarrel(w,m_gameState.barrels[i]);
    for(auto& p:m_gameState.powerups) drawPowerup(w,p);
    for(int i=0;i<MAX_PLAYERS;i++)    drawTank(w,m_gameState.players[i],(uint8_t)i);
    for(auto& b:m_gameState.bullets)  drawBullet(w,b);
    drawExplosions(w);
    drawHUD(w);
    drawChat(w);

    // Controls reminder (small, bottom)
    if(m_fontLoaded){
        bool talking = m_voice.isTalking();
        std::string hint = "WASD=Move  Space=Fire  Enter=Chat  V=Voice";
        sf::Color hcol = talking ? sf::Color(100,255,100) : sf::Color(80,80,80);
        std::string voiceStatus = talking ? "  [MIC ON]" : "";
        auto h=makeText(hint + voiceStatus, 14, hcol);
        h.setPosition({10.f,WIN_H-18.f}); w.draw(h);
    }
}

void GameClient::drawRoundOver(sf::RenderWindow& w)
{
    drawInGame(w);  // show final state underneath
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
    wt.setPosition({WIN_W/2.f-120.f,100.f});
    w.draw(wt);

    // Stats
    float y=160.f;
    auto sh=makeText("Player          Kills   XP+   Coins+",22,sf::Color(180,180,180));
    sh.setPosition({100.f,y}); w.draw(sh); y+=30.f;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(!m_lobby.slots[i].active) continue;
        std::string row = std::string(m_lobby.slots[i].name);
        while((int)row.size()<16) row+=" ";
        row += std::to_string(m_matchOver.kills[i]) + "       " +
               std::to_string(m_matchOver.xpGained[i]) + "    " +
               std::to_string(m_matchOver.coinsGained[i]);
        auto rt=makeText(row,20,skinColor(m_gameState.players[i].skin));
        rt.setPosition({100.f,y}); w.draw(rt);
        y+=26.f;
    }

    // Leaderboard
    y+=20.f;
    auto lbh=makeText("Global Leaderboard (Top 5 by Wins)",22,sf::Color::Yellow);
    lbh.setPosition({100.f,y}); w.draw(lbh); y+=30.f;
    for(int i=0;i<5;i++){
        if(m_matchOver.lbName[i][0]=='\0') break;
        std::string row = std::to_string(i+1)+". "+m_matchOver.lbName[i];
        while((int)row.size()<20) row+=" ";
        row += "Wins:"+std::to_string(m_matchOver.lbWins[i])+"  Lv."+std::to_string(m_matchOver.lbLevel[i]);
        auto lt=makeText(row,20,sf::Color::White);
        lt.setPosition({100.f,y}); w.draw(lt);
        y+=24.f;
    }
    std::string endHint = "[Esc] Quit  [V] Voice";
    if(m_voice.isTalking()) endHint += "  [MIC ON]";
    auto hint=makeText(endHint,18, m_voice.isTalking() ? sf::Color(100,255,100) : sf::Color(120,120,120));
    hint.setPosition({WIN_W/2.f-80.f,WIN_H-40.f}); w.draw(hint);
}
