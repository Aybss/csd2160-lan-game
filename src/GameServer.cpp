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

        // LAN announce (every 1 second)
        m_announceTimer -= dt;
        if(m_announceTimer <= 0.f){ announcePresence(nullptr); m_announceTimer = 1.f; }

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
                std::cout<<"[Server] Returned to lobby.\n";
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
        case PktType::VOICE_DATA:    handleVoice(e); break;
        case PktType::SERVER_QUERY:  announcePresence(&e.from); break;
        case PktType::ADD_BOT:       if(e.len>=(int)sizeof(PktAddBot)){
            PktAddBot p; memcpy(&p,e.buf,sizeof(p)); handleAddBot(p); } break;
        default: break;
    }
}

void GameServer::handleVoice(const Envelope& e)
{
    if(e.len < (int)(sizeof(PktType)+sizeof(uint8_t)+sizeof(uint16_t))) return;
    // Relay voice packet to all OTHER clients as-is
    uint8_t senderPid = e.buf[1];
    m_net.broadcastExcept(senderPid, e.buf, e.len);
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
    m_lobby[pid].isBot=false;

    // First player to connect becomes host/admin
    if(m_hostPid==0xFF) m_hostPid=pid;

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

    // Count total active slots (real players + bots) for minimum player check
    int totalActive = 0;
    for(int i=0;i<MAX_PLAYERS;i++) if(m_lobby[i].active) totalActive++;
    if(allReady() && totalActive >= MIN_PLAYERS)
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
    bool wasHost = (pid == m_hostPid);
    m_lobby[pid].active=false;
    m_lobby[pid].ready=false;
    m_bots[pid].active=false;

    PktDisconnect d; d.pid=pid;
    m_net.broadcastExcept(pid,&d,sizeof(d));

    if(m_phase==ServerPhase::IN_GAME)
    {
        m_tanks[pid].takeDamage(999); // kill their tank
        checkRoundEnd();
    }
    if(wasHost) promoteNextHost();
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
        ls.slots[i].isBot  = m_lobby[i].isBot ? 1 : 0;
        if(m_lobby[i].active && !m_lobby[i].isBot){
            auto& r = m_db.getOrCreate(m_lobby[i].name);
            ls.slots[i].level = r.level;
            ls.slots[i].wins  = r.totalWins;
        }
    }
    m_net.broadcast(&ls,sizeof(ls));
}

bool GameServer::allReady() const
{
    int cnt = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_lobby[i].active) {
            // A bot is logically always ready, regardless of the stored flag
            if (!m_lobby[i].isBot && !m_lobby[i].ready) return false;
            cnt++;
        }
    }
    return cnt >= MIN_PLAYERS;
}

void GameServer::startGame()
{
    m_phase = ServerPhase::IN_GAME;
    m_mapSeed = (uint32_t)time(nullptr);
    generateMap(m_mapSeed);
    m_roundWins.fill(0);
    resetRound();
    std::cout<<"[Server] Game started! Seed="<<m_mapSeed<<"\n";
}

//  Game 
void GameServer::generateMap(uint32_t seed)
{
    m_obstacles.clear();
    srand(seed);
    float cellW = (float)MAP_W / OBS_COLS;
    float cellH = (float)MAP_H / OBS_ROWS;

    for(int r=1;r<OBS_ROWS-1;r++)
    for(int c=1;c<OBS_COLS-1;c++)
    {
        // Keep spawn corners clear (first and last 2 cells in each corner)
        if(r<=1 && c<=1) continue;
        if(r<=1 && c>=OBS_COLS-2) continue;
        if(r>=OBS_ROWS-2 && c<=1) continue;
        if(r>=OBS_ROWS-2 && c>=OBS_COLS-2) continue;

        // Decide what to put here — bias heavily toward small (trees/bushes)
        int roll = rand()%10;
        float bw, bh;
        if(roll < 2)          // 20% large boulder
        { bw=80.f+rand()%70; bh=80.f+rand()%70; }
        else if(roll < 4)     // 20% medium wall
        { bw=50.f+rand()%50; bh=50.f+rand()%50; }
        else if(roll < 9)     // 50% small tree/bush (was previously ~33%)
        { bw=20.f+rand()%25; bh=20.f+rand()%25; }
        else continue;        // 10% empty cell

        float bx = c*cellW + (cellW-bw)*0.5f + (rand()%20-10);
        float by = r*cellH + (cellH-bh)*0.5f + (rand()%20-10);
        bx = std::max(10.f, std::min((float)MAP_W-bw-10.f, bx));
        by = std::max(10.f, std::min((float)MAP_H-bh-10.f, by));
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

    spawnPowerups();
    spawnBarrels();

    // Tell all clients to enter IN_GAME
    PktGameStart gs; gs.mapSeed = m_mapSeed;
    m_net.broadcast(&gs, sizeof(gs));

    // Immediately broadcast state so clients have real tank positions
    // without waiting up to 50ms for the next scheduled broadcast
    broadcastGameState();

    std::cout << "[Server] Round started.\n";
}

void GameServer::spawnPowerups()
{
    // Spread powerups around the map in fixed zones, random types
    static const float PX[MAX_POWERUPS] = {
        MAP_W * 0.25f, MAP_W * 0.75f, MAP_W * 0.25f, MAP_W * 0.75f
    };
    static const float PY[MAX_POWERUPS] = {
        MAP_H * 0.25f, MAP_H * 0.25f, MAP_H * 0.75f, MAP_H * 0.75f
    };
    for(int i=0;i<MAX_POWERUPS;i++){
        m_powerups[i].active = 1;
        m_powerups[i].ptype  = (PowerupType)(rand() % 3);
        m_powerups[i].x      = PX[i] + (rand()%80 - 40);
        m_powerups[i].y      = PY[i] + (rand()%80 - 40);
        m_powerupRespawn[i]  = 0.f;
    }
}

void GameServer::checkPowerupCollisions()
{
    for(int pi=0;pi<MAX_POWERUPS;pi++){
        if(!m_powerups[pi].active) continue;
        for(int ti=0;ti<MAX_PLAYERS;ti++){
            if(!m_lobby[ti].active || !m_tanks[ti].alive()) continue;
            float dx = m_tanks[ti].x() - m_powerups[pi].x;
            float dy = m_tanks[ti].y() - m_powerups[pi].y;
            float r  = TANK_RADIUS + POWERUP_RADIUS;
            if(dx*dx+dy*dy < r*r){
                // Collect
                m_tanks[ti].applyPowerup(m_powerups[pi].ptype);
                m_powerups[pi].active    = 0;
                m_powerupRespawn[pi]     = 10.f;  // respawn in 10s

                PktPowerupCollect pc;
                pc.pid   = (uint8_t)ti;
                pc.idx   = (uint8_t)pi;
                pc.ptype = m_powerups[pi].ptype;
                m_net.broadcast(&pc, sizeof(pc));
                break;
            }
        }
    }
}

void GameServer::updateGame(float dt)
{
    // Run bot AI to fill m_inputs for bot slots
    updateBots(dt);

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

    // Tick buffs on all tanks
    for(int i=0;i<MAX_PLAYERS;i++)
        if(m_lobby[i].active && m_tanks[i].alive()) m_tanks[i].tickBuffs(dt);

    // Update bullets
    for(auto& b : m_bullets) b.update(dt, m_obstacles);

    checkBulletCollisions();
    checkPowerupCollisions();
    checkBarrelCollisions();

    // Powerup respawn timers
    for(int i=0;i<MAX_POWERUPS;i++){
        if(!m_powerups[i].active){
            m_powerupRespawn[i] -= dt;
            if(m_powerupRespawn[i] <= 0.f){
                m_powerups[i].active = 1;
                m_powerupRespawn[i]  = 0.f;
            }
        }
    }

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
    for(int i=0;i<MAX_PLAYERS;i++){
        mo.kills[i] = m_tanks[i].kills();
        if(m_lobby[i].active){
            int kills = m_tanks[i].kills();
            mo.xpGained[i]    = (uint16_t)(kills*XP_PER_KILL    + (i==(int)winner ? XP_PER_WIN    : 0));
            mo.coinsGained[i] = (uint16_t)(kills*COINS_PER_KILL + (i==(int)winner ? COINS_PER_WIN : 0));
        }
    }

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
    m_roundTimer = ROUND_DELAY * 1.1f;
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
    // Powerups
    for(int i=0;i<MAX_POWERUPS;i++) gs.powerups[i] = m_powerups[i];
    // Buff masks
    for(int i=0;i<MAX_PLAYERS;i++)  gs.buffs[i]    = m_tanks[i].buffMask();
    // Barrels
    for(int i=0;i<MAX_BARRELS;i++)  gs.barrels[i]  = m_barrels[i];

    m_net.broadcast(&gs,sizeof(gs));
}

void GameServer::spawnBarrels()
{
    // Place barrels in open areas around the map - not too close to spawns
    static const float BX[MAX_BARRELS] = {
        MAP_W*0.5f,  MAP_W*0.3f,  MAP_W*0.7f,
        MAP_W*0.5f,  MAP_W*0.2f,  MAP_W*0.8f
    };
    static const float BY[MAX_BARRELS] = {
        MAP_H*0.5f,  MAP_H*0.5f,  MAP_H*0.5f,
        MAP_H*0.3f,  MAP_H*0.7f,  MAP_H*0.7f
    };
    for(int i=0;i<MAX_BARRELS;i++){
        m_barrels[i].active = 1;
        // Add small random offset so they don't always sit in same exact spot
        m_barrels[i].x = BX[i] + (rand()%60-30);
        m_barrels[i].y = BY[i] + (rand()%60-30);
    }
}

void GameServer::checkBarrelCollisions()
{
    for(int bi=0;bi<MAX_BARRELS;bi++){
        if(!m_barrels[bi].active) continue;
        for(auto& b : m_bullets){
            if(!b.active()) continue;
            float dx = b.x()-m_barrels[bi].x;
            float dy = b.y()-m_barrels[bi].y;
            float r  = BULLET_RADIUS + BARREL_RADIUS;
            if(dx*dx+dy*dy < r*r){
                b.kill();
                explodeBarrel(bi);
                break;
            }
        }
    }
}

void GameServer::explodeBarrel(int idx)
{
    if(!m_barrels[idx].active) return;
    m_barrels[idx].active = 0;

    float ex = m_barrels[idx].x;
    float ey = m_barrels[idx].y;

    // Broadcast explosion event
    PktBarrelExplode pkt;
    pkt.idx = (uint8_t)idx;
    pkt.x   = ex;
    pkt.y   = ey;
    m_net.broadcast(&pkt, sizeof(pkt));

    // Damage all tanks within explosion radius
    for(int ti=0;ti<MAX_PLAYERS;ti++){
        if(!m_lobby[ti].active || !m_tanks[ti].alive()) continue;
        float dx = m_tanks[ti].x()-ex;
        float dy = m_tanks[ti].y()-ey;
        if(dx*dx+dy*dy < BARREL_EXPLODE_R*BARREL_EXPLODE_R){
            m_tanks[ti].takeDamage(2);  // barrels hit hard
            PktPlayerHit hit;
            hit.victim=ti; hit.attacker=0xFF; hit.hpLeft=m_tanks[ti].hp();
            m_net.broadcast(&hit,sizeof(hit));
            if(!m_tanks[ti].alive()){
                m_aliveCount--;
                PktPlayerDead dead; dead.victim=ti; dead.attacker=0xFF;
                m_net.broadcast(&dead,sizeof(dead));
            }
        }
    }

    // Chain reaction: check if other barrels are within explosion radius
    for(int bi=0;bi<MAX_BARRELS;bi++){
        if(bi==idx || !m_barrels[bi].active) continue;
        float dx = m_barrels[bi].x-ex;
        float dy = m_barrels[bi].y-ey;
        if(dx*dx+dy*dy < BARREL_EXPLODE_R*BARREL_EXPLODE_R)
            explodeBarrel(bi);  // chain!
    }

    checkRoundEnd();
    std::cout<<"[Server] Barrel "<<idx<<" exploded at ("<<ex<<","<<ey<<")\n";
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

void GameServer::announcePresence(const sockaddr_in* replyTo)
{
    PktServerAnnounce ann;
    ann.port   = NET_PORT;
    ann.inGame = (m_phase != ServerPhase::LOBBY) ? 1 : 0;
    uint8_t cnt = 0;
    for(int i=0;i<MAX_PLAYERS;i++){
        if(m_lobby[i].active){
            strncpy(ann.playerNames[cnt], m_lobby[i].name, 15);
            ann.playerNames[cnt][15] = '\0';
            cnt++;
        }
    }
    ann.playerCount = cnt;
    ann.maxPlayers  = MAX_PLAYERS;

    // Lazy-create a socket bound to NET_PORT+1 (dedicated discovery port).
    // Scanners also bind to NET_PORT+1 so they receive these broadcasts.
    static SOCKET bsock = INVALID_SOCKET;
    if(bsock == INVALID_SOCKET){
        bsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int yes = 1;
        setsockopt(bsock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
        setsockopt(bsock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        u_long nb = 1;
        ioctlsocket(bsock, FIONBIO, &nb);
        sockaddr_in local{};
        local.sin_family      = AF_INET;
        local.sin_port        = htons(NET_PORT + 1);
        local.sin_addr.s_addr = INADDR_ANY;
        ::bind(bsock, (sockaddr*)&local, sizeof(local));
    }

    if(replyTo)
    {
        // Direct unicast reply back to whoever queried us, on their source port
        sendto(bsock, (const char*)&ann, sizeof(ann), 0,
               (const sockaddr*)replyTo, sizeof(*replyTo));
    }
    else
    {
        // Periodic broadcast on NET_PORT+1 – scanners bind to this port
        sockaddr_in dest{};
        dest.sin_family      = AF_INET;
        dest.sin_port        = htons(NET_PORT + 1);
        dest.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(bsock, (const char*)&ann, sizeof(ann), 0,
               (sockaddr*)&dest, sizeof(dest));
        // Also send to loopback so local scanner (Create+Join-self) works
        dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(bsock, (const char*)&ann, sizeof(ann), 0,
               (sockaddr*)&dest, sizeof(dest));
    }
}

//  Host promotion 
void GameServer::promoteNextHost()
{
    m_hostPid = 0xFF;
    // Find the active non-bot player with the lowest pid
    for(int i=0;i<MAX_PLAYERS;i++){
        if(m_lobby[i].active && !m_lobby[i].isBot){
            m_hostPid = (uint8_t)i;
            std::cout<<"[Server] New host: pid "<<i<<" ("<<m_lobby[i].name<<")\n";
            return;
        }
    }
    std::cout<<"[Server] No players left; no host.\n";
}

//  Add bot 
void GameServer::handleAddBot(const PktAddBot& p)
{
    // Only the current host/admin may add bots, and only in lobby
    if(p.requestPid != m_hostPid) return;
    if(m_phase != ServerPhase::LOBBY) return;

    // Find a free slot
    int slot = -1;
    for(int i=0;i<MAX_PLAYERS;i++)
        if(!m_lobby[i].active){ slot=i; break; }
    if(slot<0) return;  // full

    // Register a fake "client" with no real address — bots don't need network I/O
    // We give them a sentinel address so the network layer won't try to send to them
    sockaddr_in fakeAddr{};
    fakeAddr.sin_family = AF_INET;
    fakeAddr.sin_port   = 0;
    fakeAddr.sin_addr.s_addr = 0;

    // Directly fill the lobby slot (bypass network registration)
    uint8_t pid = (uint8_t)slot;
    char botName[16];
    snprintf(botName, sizeof(botName), "Bot-%d", slot);

    m_lobby[pid].active = true;
    strncpy(m_lobby[pid].name, botName, 15);
    m_lobby[pid].ready  = true;   // bots are always ready
    m_lobby[pid].skin   = (uint8_t)(slot % SKIN_COUNT);
    m_lobby[pid].isBot  = true;

    m_bots[pid].active     = true;
    m_bots[pid].thinkTimer = 0.f;
    m_bots[pid].targetPid  = 0xFF;
    m_bots[pid].aimAngle   = 0.f;

    std::cout<<"[Server] Bot added as pid "<<(int)pid<<" ("<<botName<<")\n";
    broadcastLobbyState();
}

//  Bot AI 
// Simple seek-and-shoot FSM: find nearest living enemy, turn toward them, shoot.
void GameServer::updateBots(float dt)
{
    if (m_phase != ServerPhase::IN_GAME) return;

    // Helper to check collision since Tank::collides is private
    auto checkWall = [&](float nx, float ny) {
        if (nx - TANK_RADIUS<0 || ny - TANK_RADIUS<0 || nx + TANK_RADIUS>MAP_W || ny + TANK_RADIUS>MAP_H)
            return true;
        for (auto& o : m_obstacles) {
            float cx = std::max(o.x, std::min(nx, o.x + o.w));
            float cy = std::max(o.y, std::min(ny, o.y + o.h));
            float dx = nx - cx, dy = ny - cy;
            if (dx * dx + dy * dy < TANK_RADIUS * TANK_RADIUS) return true;
        }
        return false;
        };

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!m_bots[i].active || !m_tanks[i].alive()) continue;

        auto& bot = m_bots[i];
        auto& tank = m_tanks[i];

        // --- Target Selection Logic ---
        bot.thinkTimer -= dt;
        if (bot.thinkTimer <= 0.f || bot.targetPid == 0xFF || !m_tanks[bot.targetPid].alive()) {
            bot.thinkTimer = 0.5f + (rand() % 5) * 0.1f;
            float bestDist = 1e9f;
            bot.targetPid = 0xFF;
            for (int j = 0; j < MAX_PLAYERS; j++) {
                if (j == i || !m_lobby[j].active || !m_tanks[j].alive()) continue;
                float d = std::pow(m_tanks[j].x() - tank.x(), 2) + std::pow(m_tanks[j].y() - tank.y(), 2);
                if (d < bestDist) { bestDist = d; bot.targetPid = (uint8_t)j; }
            }
        }

        // --- Steering & Movement ---
        m_inputs[i] = PktInput{}; // Reset
        m_inputs[i].pid = (uint8_t)i;
        if (bot.targetPid == 0xFF) continue;

        float rad = tank.angle() * (3.14159265f / 180.f); // Define rad
        float dx = m_tanks[bot.targetPid].x() - tank.x();
        float dy = m_tanks[bot.targetPid].y() - tank.y();
        float dist = sqrtf(dx * dx + dy * dy);
        float desiredAngle = atan2f(dy, dx) * 180.f / 3.14159265f;
        float angleDiff = desiredAngle - tank.angle();

        while (angleDiff > 180.f) angleDiff -= 360.f;
        while (angleDiff < -180.f) angleDiff += 360.f;

        // Turn toward target
        if (angleDiff > 10.f) m_inputs[i].right = 1;
        else if (angleDiff < -10.f) m_inputs[i].left = 1;

        // --- Whisker Obstacle Avoidance ---
        float lookDist = 70.f;
        float fx = tank.x() + cosf(rad) * lookDist;
        float fy = tank.y() + sinf(rad) * lookDist;

        if (checkWall(fx, fy)) {
            m_inputs[i].forward = 0;
            m_inputs[i].back = 1; // Back away from wall
            // Veer away
            if (angleDiff > 0) m_inputs[i].left = 1; else m_inputs[i].right = 1;
        }
        else {
            if (dist > 150.f) m_inputs[i].forward = 1;
            else if (dist < 100.f) m_inputs[i].back = 1;
        }

        if (fabsf(angleDiff) < 25.f && dist < 500.f) m_inputs[i].fire = 1;
    }
}