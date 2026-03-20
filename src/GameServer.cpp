#include "GameServer.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>

static constexpr float TICK_RATE      = 1.f/60.f;
static constexpr float STATE_RATE     = 1.f/20.f;  // broadcast 20 Hz
static constexpr float ROUND_DELAY    = 4.f;        // seconds before next round

static float nowSeconds()
{
    static auto start = std::chrono::steady_clock::now();
    auto dur = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<float>(dur).count();
}

GameServer::GameServer(uint16_t port)
{
    m_net.init(port);
    m_db.load();
}

void GameServer::run()
{
    std::cout << "[Server] Running. Waiting for players...\n";
    auto prev = std::chrono::steady_clock::now();
    float stateTimer = 0.f;

    while(true)
    {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;
        m_now = nowSeconds();

        m_net.poll(m_now);

        // Check timeouts
        auto timedOut = m_net.checkTimeouts(m_now);
        for(auto pid : timedOut)
        {
            std::cout << "[Server] Player " << (int)pid << " timed out\n";
            handleDisconnect(pid);
        }

        // Process packets
        Envelope e;
        while(m_net.pollEnvelope(e)) handlePacket(e);

        // Game update
        if(m_phase == ServerPhase::IN_GAME)
        {
            updateGame(dt);
            stateTimer += dt;
            if(stateTimer >= STATE_RATE){ broadcastGameState(); stateTimer=0.f; }
        }
        else if(m_phase == ServerPhase::ROUND_OVER)
        {
            m_roundTimer -= dt;
            if(m_roundTimer <= 0.f) resetRound();
        }
        else if(m_phase == ServerPhase::MATCH_OVER)
        {
            // Keep ticking so clients don't time out - return to lobby after delay
            m_roundTimer -= dt;
            if(m_roundTimer <= 0.f)
            {
                m_phase = ServerPhase::LOBBY;
                // Reset ready flags so players re-ready for next match
                for(int i=0;i<MAX_PLAYERS;i++) m_lobby[i].ready = false;
                m_roundWins.fill(0);
                broadcastLobbyState();
                std::cout<<"[Server] Returned to lobby.";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

//  Packet dispatch 
void GameServer::handlePacket(const Envelope& e)
{
    if(e.len < 1) return;
    PktType t = (PktType)e.buf[0];
    switch(t)
    {
        case PktType::CONNECT:       handleConnect(e); break;
        case PktType::PLAYER_READY:  if(e.len>=(int)sizeof(PktPlayerReady)){
            PktPlayerReady p; memcpy(&p,e.buf,sizeof(p)); handlePlayerReady(p,p.pid); } break;
        case PktType::INPUT:         if(e.len>=(int)sizeof(PktInput)){
            PktInput p; memcpy(&p,e.buf,sizeof(p)); handleInput(p); } break;
        case PktType::CHAT:          if(e.len>=(int)sizeof(PktChat)){
            PktChat p; memcpy(&p,e.buf,sizeof(p)); handleChat(p); } break;
        case PktType::BUY_SKIN:      if(e.len>=(int)sizeof(PktBuySkin)){
            PktBuySkin p; memcpy(&p,e.buf,sizeof(p)); handleBuySkin(p); } break;
        case PktType::DISCONNECT:    if(e.len>=(int)sizeof(PktDisconnect)){
            PktDisconnect p; memcpy(&p,e.buf,sizeof(p)); handleDisconnect(p.pid); } break;
        case PktType::PING:          { PktType pong=PktType::PONG_PKT; m_net.sendTo(e.buf[1],&pong,1); } break;
        default: break;
    }
}

void GameServer::handleConnect(const Envelope& e)
{
    if(e.len < (int)sizeof(PktConnect)) return;
    PktConnect p; memcpy(&p,e.buf,sizeof(p));
    p.name[15]='\0';

    if(m_phase != ServerPhase::LOBBY)
    {
        PktConnectAck ack; ack.denied=1;
        m_net.broadcast(&ack,sizeof(ack));
        return;
    }

    uint8_t pid = m_net.registerClient(e.from, p.name, m_now);
    if(pid==0xFF)
    {
        PktConnectAck ack; ack.denied=1;
        m_net.sendTo(pid,&ack,sizeof(ack));
        return;
    }

    // Init lobby slot
    m_lobby[pid].active=true;
    strncpy(m_lobby[pid].name, p.name, 15);
    m_lobby[pid].ready=false;

    // Load/create profile
    auto& rec = m_db.getOrCreate(p.name);

    PktConnectAck ack; ack.pid=pid; ack.denied=0;
    m_net.sendTo(pid,&ack,sizeof(ack));

    // Send profile
    sendProfileUpdate(pid);

    broadcastLobbyState();
    std::cout<<"[Lobby] "<<p.name<<" joined as pid "<<(int)pid<<"\n";
}

void GameServer::handlePlayerReady(const PktPlayerReady& p, uint8_t /*pid*/)
{
    if(p.pid>=MAX_PLAYERS || !m_lobby[p.pid].active) return;
    m_lobby[p.pid].ready  = p.ready;
    m_lobby[p.pid].skin   = p.skin;
    broadcastLobbyState();

    // Host auto-starts when all ready and >= MIN_PLAYERS
    if(allReady() && m_net.clientCount() >= MIN_PLAYERS)
        startGame();
}

void GameServer::handleInput(const PktInput& p)
{
    if(p.pid>=MAX_PLAYERS) return;
    // Accept only newer seq
    if(p.seq >= m_inputs[p.pid].seq)
        m_inputs[p.pid] = p;
}

void GameServer::handleChat(const PktChat& p)
{
    // Relay to all
    m_net.broadcast(&p, sizeof(p));
}

void GameServer::handleBuySkin(const PktBuySkin& p)
{
    if(p.pid>=MAX_PLAYERS || !m_lobby[p.pid].active) return;
    const char* name = m_lobby[p.pid].name;
    bool ok = m_db.buySkin(name, p.skinIdx);
    m_db.save();

    PktBuySkinAck ack;
    ack.success = ok ? 1 : 0;
    ack.skinIdx = p.skinIdx;
    ack.coinsLeft = (uint32_t)m_db.getOrCreate(name).coins;
    m_net.sendTo(p.pid, &ack, sizeof(ack));
    sendProfileUpdate(p.pid);
}

void GameServer::handleDisconnect(uint8_t pid)
{
    if(pid>=MAX_PLAYERS) return;
    m_lobby[pid].active=false;
    m_lobby[pid].ready=false;

    PktDisconnect d; d.pid=pid;
    m_net.broadcastExcept(pid,&d,sizeof(d));

    if(m_phase==ServerPhase::IN_GAME)
    {
        m_tanks[pid].takeDamage(999); // kill their tank
        checkRoundEnd();
    }
    broadcastLobbyState();
    std::cout<<"[Server] pid "<<(int)pid<<" disconnected\n";
}

//  Lobby 
void GameServer::broadcastLobbyState()
{
    PktLobbyState ls;
    ls.hostPid = m_hostPid;
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        ls.slots[i].active = m_lobby[i].active;
        strncpy(ls.slots[i].name, m_lobby[i].name, 15);
        ls.slots[i].ready  = m_lobby[i].ready;
        ls.slots[i].skin   = m_lobby[i].skin;
        if(m_lobby[i].active){
            auto& r = m_db.getOrCreate(m_lobby[i].name);
            ls.slots[i].level = r.level;
            ls.slots[i].wins  = r.totalWins;
        }
    }
    m_net.broadcast(&ls,sizeof(ls));
}

bool GameServer::allReady() const
{
    int cnt=0;
    for(int i=0;i<MAX_PLAYERS;i++) if(m_lobby[i].active){ if(!m_lobby[i].ready) return false; cnt++; }
    return cnt>=MIN_PLAYERS;
}

void GameServer::startGame()
{
    m_phase = ServerPhase::IN_GAME;
    m_mapSeed = (uint32_t)time(nullptr);
    generateMap(m_mapSeed);

    // Zero round wins
    m_roundWins.fill(0);

    PktGameStart gs; gs.mapSeed = m_mapSeed;
    m_net.broadcast(&gs,sizeof(gs));

    resetRound();
    std::cout<<"[Server] Game started! Seed="<<m_mapSeed<<"\n";
}

//  Game 
void GameServer::generateMap(uint32_t seed)
{
    m_obstacles.clear();
    srand(seed);
    // Fixed border walls are implicit; generate interior boxes
    float cellW = (float)MAP_W / OBS_COLS;
    float cellH = (float)MAP_H / OBS_ROWS;
    for(int r=1;r<OBS_ROWS-1;r++)
    for(int c=1;c<OBS_COLS-1;c++)
    {
        if(rand()%3 != 0) continue;
        float bw = 40.f + rand()%60;
        float bh = 40.f + rand()%60;
        float bx = c*cellW + (cellW-bw)/2.f;
        float by = r*cellH + (cellH-bh)/2.f;
        m_obstacles.push_back({bx,by,bw,bh});
    }
}

std::array<std::pair<float,float>, MAX_PLAYERS> GameServer::spawnPositions()
{
    return {{
        {80.f,  80.f},
        {MAP_W-80.f, MAP_H-80.f},
        {MAP_W-80.f, 80.f},
        {80.f,  MAP_H-80.f},
        {MAP_W/2.f, 80.f},
        {MAP_W/2.f, MAP_H-80.f}
    }};
}

void GameServer::resetRound()
{
    m_phase = ServerPhase::IN_GAME;
    m_bullets.clear();

    auto spawns = spawnPositions();
    int idx=0;
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        if(!m_lobby[i].active){ continue; }
        auto [sx,sy] = spawns[idx++];
        m_tanks[i] = Tank(sx,sy,(uint8_t)i,m_lobby[i].skin);
    }

    m_aliveCount=0;
    for(int i=0;i<MAX_PLAYERS;i++) if(m_lobby[i].active) m_aliveCount++;

    // Tell all clients to enter IN_GAME — same packet used for first start
    // Reuse same map seed so obstacles stay identical across rounds
    PktGameStart gs; gs.mapSeed = m_mapSeed;
    m_net.broadcast(&gs, sizeof(gs));
    std::cout << "[Server] Round started.";
}

void GameServer::updateGame(float dt)
{
    // Apply inputs
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        if(!m_lobby[i].active || !m_tanks[i].alive()) continue;
        auto& inp = m_inputs[i];
        m_tanks[i].update(dt, inp.forward, inp.back, inp.left, inp.right, m_obstacles);
        if(inp.fire)
        {
            if(m_tanks[i].tryShoot(m_now))
            {
                float bx,by,vx,vy;
                m_tanks[i].shootDir(bx,by,vx,vy);
                m_bullets.emplace_back((uint8_t)i,bx,by,vx,vy);

                PktBulletSpawn bs;
                bs.owner=i; bs.x=bx; bs.y=by; bs.vx=vx; bs.vy=vy;
                m_net.broadcast(&bs,sizeof(bs));
            }
        }
    }

    // Update bullets
    for(auto& b : m_bullets) b.update(dt, m_obstacles);

    checkBulletCollisions();

    // Remove dead bullets
    m_bullets.erase(std::remove_if(m_bullets.begin(),m_bullets.end(),
        [](const Bullet& b){ return !b.active(); }), m_bullets.end());

    checkRoundEnd();
}

void GameServer::checkBulletCollisions()
{
    for(auto& b : m_bullets)
    {
        if(!b.active()) continue;
        for(int i=0;i<MAX_PLAYERS;i++)
        {
            if(!m_lobby[i].active || !m_tanks[i].alive()) continue;
            if(b.owner()==(uint8_t)i) continue; // no self-hit
            float dx=b.x()-m_tanks[i].x(), dy=b.y()-m_tanks[i].y();
            if(dx*dx+dy*dy < (TANK_RADIUS+BULLET_RADIUS)*(TANK_RADIUS+BULLET_RADIUS))
            {
                m_tanks[i].takeDamage(1);
                b.kill();

                PktPlayerHit hit;
                hit.victim=i; hit.attacker=b.owner(); hit.hpLeft=m_tanks[i].hp();
                m_net.broadcast(&hit,sizeof(hit));

                if(!m_tanks[i].alive())
                {
                    m_tanks[b.owner()].addKill();
                    const char* killerName = m_lobby[b.owner()].name;
                    m_db.addKill(killerName);
                    m_db.save();

                    PktPlayerDead dead; dead.victim=i; dead.attacker=b.owner();
                    m_net.broadcast(&dead,sizeof(dead));
                    m_aliveCount--;
                }
                break;
            }
        }
    }
}

void GameServer::checkRoundEnd()
{
    if(m_phase != ServerPhase::IN_GAME) return;
    int alive=0; uint8_t lastPid=0xFF;
    for(int i=0;i<MAX_PLAYERS;i++)
        if(m_lobby[i].active && m_tanks[i].alive()){ alive++; lastPid=(uint8_t)i; }

    if(alive<=1)
        endRound(alive==1 ? lastPid : (uint8_t)0xFF);
}

void GameServer::endRound(uint8_t winner)
{
    m_phase = ServerPhase::ROUND_OVER;
    m_roundTimer = ROUND_DELAY;

    if(winner != 0xFF) m_roundWins[winner]++;

    PktRoundOver ro; ro.winner=winner;
    for(int i=0;i<MAX_PLAYERS;i++) ro.roundWins[i]=m_roundWins[i];
    m_net.broadcast(&ro,sizeof(ro));

    // Check match winner
    for(int i=0;i<MAX_PLAYERS;i++)
        if(m_roundWins[i]>=ROUNDS_TO_WIN){ endMatch((uint8_t)i); return; }
}

void GameServer::endMatch(uint8_t winner)
{
    m_phase = ServerPhase::MATCH_OVER;

    // Award XP/coins
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        if(!m_lobby[i].active) continue;
        const char* nm = m_lobby[i].name;
        int kills = m_tanks[i].kills();
        uint32_t xp    = kills*XP_PER_KILL + (i==winner ? XP_PER_WIN : 0);
        uint32_t coins = kills*COINS_PER_KILL + (i==winner ? COINS_PER_WIN : 0);
        m_db.addXP(nm,xp);
        m_db.addCoins(nm,coins);
        if(i==winner) m_db.addWin(nm);
        sendProfileUpdate((uint8_t)i);
    }
    m_db.save();

    PktMatchOver mo; mo.winner=winner;
    for(int i=0;i<MAX_PLAYERS;i++) mo.kills[i]=m_tanks[i].kills();

    // Fill leaderboard
    auto top = m_db.topByWins(5);
    for(int i=0;i<(int)top.size()&&i<5;i++){
        strncpy(mo.lbName[i],top[i].name.c_str(),15);
        mo.lbWins[i]  = top[i].totalWins;
        mo.lbLevel[i] = top[i].level;
    }
    m_net.broadcast(&mo,sizeof(mo));

    std::cout<<"[Server] Match over. Winner: pid "<<(int)winner<<"\n";

    // Stay in MATCH_OVER; run() loop will return to lobby after delay
    m_roundTimer = ROUND_DELAY * 3.f;
}

void GameServer::broadcastGameState()
{
    PktGameState gs; gs.seq = m_stateSeq++;
    for(int i=0;i<MAX_PLAYERS;i++)
    {
        gs.players[i].x     = m_tanks[i].x();
        gs.players[i].y     = m_tanks[i].y();
        gs.players[i].angle = m_tanks[i].angle();
        gs.players[i].hp    = (uint8_t)m_tanks[i].hp();
        gs.players[i].alive = m_tanks[i].alive() ? 1 : 0;
        gs.players[i].skin  = m_tanks[i].skin();
        gs.players[i].kills = m_tanks[i].kills();
    }
    int bi=0;
    for(auto& b:m_bullets)
    {
        if(bi>=(int)(MAX_PLAYERS*3)) break;
        gs.bullets[bi].active = b.active();
        gs.bullets[bi].owner  = b.owner();
        gs.bullets[bi].x=b.x(); gs.bullets[bi].y=b.y();
        gs.bullets[bi].vx=b.vx(); gs.bullets[bi].vy=b.vy();
        gs.bullets[bi].life=b.life();
        bi++;
    }
    m_net.broadcast(&gs,sizeof(gs));
}

void GameServer::sendProfileUpdate(uint8_t pid)
{
    if(!m_lobby[pid].active) return;
    auto& r = m_db.getOrCreate(m_lobby[pid].name);
    PktProfileUpdate pu;
    pu.xp=r.xp; pu.level=r.level; pu.coins=r.coins;
    pu.ownedSkins=r.ownedSkins; pu.totalWins=r.totalWins;
    m_net.sendTo(pid,&pu,sizeof(pu));
}
