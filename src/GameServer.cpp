#include "GameServer.h"
#include <sodium.h>
#include <fstream>
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
    if (sodium_init() < 0)
        std::cerr << "[Server] WARNING: sodium_init() failed\n";
    loadOrGenServerKeypair();
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

        // Expire stale pending auths older than 30 seconds
        m_pendingAuth.erase(
            std::remove_if(m_pendingAuth.begin(), m_pendingAuth.end(),
                           [this](const PendingAuth &pa)
                           { return (m_now - pa.timestamp) > 30.f; }),
            m_pendingAuth.end());

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
        case PktType::KEY_EXCHANGE: handleKeyExchange(e); break;
        case PktType::ENCRYPTED:
        {
            // Find the matching PendingAuth by source address and decrypt
            for (auto it = m_pendingAuth.begin(); it != m_pendingAuth.end(); ++it)
            {
                if (it->addr.sin_addr.s_addr == e.from.sin_addr.s_addr &&
                    it->addr.sin_port == e.from.sin_port)
                {
                    std::vector<uint8_t> dec;
                    if (decryptFromPending(*it, e, dec) && !dec.empty())
                    {
                        if ((PktType)dec[0] == PktType::AUTH_RESPONSE)
                        {
                            PendingAuth pa = *it; // copy before potential erase
                            handleAuthResponse(e, pa, dec);
                        }
                    }
                    break;
                }
            }
            break;
        }
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
        case PktType::KICK_BOT:
            if (e.len >= (int)sizeof(PktKickBot)) {
                PktKickBot p; memcpy(&p, e.buf, sizeof(p));
                handleKickBot(p);
            } break;
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

// ---- Server keypair (long-term Curve25519, stored in server.cfg) ----

void GameServer::loadOrGenServerKeypair()
{
    // Try to read existing keypair from server.cfg (lines: "pk=<hex64>" and "sk=<hex64>")
    std::ifstream f("server.cfg");
    std::string pkHex, skHex;
    if (f.is_open())
    {
        std::string line;
        while (std::getline(f, line))
        {
            if (line.substr(0, 3) == "pk=")
                pkHex = line.substr(3);
            if (line.substr(0, 3) == "sk=")
                skHex = line.substr(3);
        }
    }

    bool loaded = false;
    if (pkHex.size() == 64 && skHex.size() == 64)
    {
        if (Persistence::hexToBytes(pkHex, m_serverPk, 32) &&
            Persistence::hexToBytes(skHex, m_serverSk, 32))
        {
            loaded = true;
            std::cout << "[Server] Loaded server keypair from server.cfg\n";
        }
    }

    if (!loaded)
    {
        // Generate fresh keypair
        crypto_kx_keypair(m_serverPk, m_serverSk);
        // Persist to server.cfg (append / create)
        std::ofstream out("server.cfg", std::ios::app);
        out << "pk=" << Persistence::bytesToHex(m_serverPk, 32) << "\n";
        out << "sk=" << Persistence::bytesToHex(m_serverSk, 32) << "\n";
        std::cout << "[Server] Generated new server keypair and saved to server.cfg\n";
    }
}

// ---- Auth helpers ----

void GameServer::sendEncryptedToPending(PendingAuth &pa, const void *plaintext, int len)
{
    EncryptedEnvelopeHdr hdr;
    memset(hdr.nonce, 0, 24);
    memcpy(hdr.nonce, &pa.nonceTx, sizeof(pa.nonceTx));
    pa.nonceTx++;

    std::vector<uint8_t> buf(sizeof(hdr) + len + 16);
    memcpy(buf.data(), &hdr, sizeof(hdr));
    crypto_secretbox_easy(
        buf.data() + sizeof(hdr),
        (const uint8_t *)plaintext, (unsigned long long)len,
        hdr.nonce,
        pa.sessionTxKey);
    m_net.sendToAddr(pa.addr, buf.data(), (int)buf.size());
}

bool GameServer::decryptFromPending(const PendingAuth &pa, const Envelope &e, std::vector<uint8_t> &out)
{
    constexpr int HDR = (int)sizeof(EncryptedEnvelopeHdr);
    if (e.len <= HDR + 16)
        return false;

    const uint8_t *nonce = e.buf + 1; // skip PktType byte; EncryptedEnvelopeHdr.nonce starts at offset 1
    const uint8_t *ct = e.buf + HDR;
    int ctLen = e.len - HDR;

    out.resize((size_t)(ctLen - 16));
    return crypto_secretbox_open_easy(
               out.data(),
               ct, (unsigned long long)ctLen,
               nonce,
               pa.sessionRxKey // server rx = messages FROM the client
               ) == 0;
}

// ---- Key exchange handler ----

void GameServer::handleKeyExchange(const Envelope &e)
{
    if (e.len < (int)sizeof(PktKeyExchange))
        return;
    PktKeyExchange p;
    memcpy(&p, e.buf, sizeof(p));
    p.name[15] = '\0';

    // Early rejection: not in lobby or lobby full
    if (m_phase != ServerPhase::LOBBY || m_net.clientCount() >= MAX_PLAYERS)
    {
        // Send plaintext denial that the client can interpret before session is secured
        PktConnectAck ack;
        ack.denied = 1;
        m_net.sendToAddr(e.from, &ack, sizeof(ack));
        return;
    }

    // Expire any stale pending auths from the same address (retransmit case)
    m_pendingAuth.erase(
        std::remove_if(m_pendingAuth.begin(), m_pendingAuth.end(),
                       [&](const PendingAuth &x)
                       {
                           return x.addr.sin_addr.s_addr == e.from.sin_addr.s_addr && x.addr.sin_port == e.from.sin_port;
                       }),
        m_pendingAuth.end());

    // Derive session keys via crypto_kx (server side)
    uint8_t rxKey[32], txKey[32]; // rx = incoming from client, tx = outgoing to client
    if (crypto_kx_server_session_keys(rxKey, txKey, m_serverPk, m_serverSk, p.clientPk) != 0)
    {
        std::cerr << "[Server] crypto_kx failed for " << p.name << " (invalid client pk?)\n";
        return;
    }

    // Retrieve stored salt for LOGIN mode (so we can include it in the challenge)
    char saltHex[33]{};
    if (p.mode == AuthMode::LOGIN)
    {
        auto *rec = m_db.find(p.name);
        if (rec && !rec->salt.empty())
            strncpy(saltHex, rec->salt.c_str(), 32);
        // If not found, salt stays empty — rejection happens later in handleAuthResponse
    }

    // Store PendingAuth
    PendingAuth pa;
    pa.addr = e.from;
    strncpy(pa.name, p.name, 15);
    pa.mode = p.mode;
    memcpy(pa.sessionRxKey, rxKey, 32);
    memcpy(pa.sessionTxKey, txKey, 32);
    pa.nonceTx = 0;
    pa.timestamp = m_now;
    randombytes_buf(pa.challengeNonce, 32);
    m_pendingAuth.push_back(pa);

    // Send PktKeyExchangeAck (plaintext — server public key is not secret)
    PktKeyExchangeAck ack;
    memcpy(ack.serverPk, m_serverPk, 32);
    m_net.sendToAddr(e.from, &ack, sizeof(ack));

    // Send encrypted PktAuthChallenge
    PktAuthChallenge chal;
    memcpy(chal.challengeNonce, pa.challengeNonce, 32);
    strncpy(chal.saltHex, saltHex, 32);
    sendEncryptedToPending(m_pendingAuth.back(), &chal, sizeof(chal));
}

// ---- Shared lobby-slot helper ----

uint8_t GameServer::registerPlayerInLobby(const sockaddr_in &addr, const char *name,
                                           bool isAnonymous, float nowSec)
{
    uint8_t pid = m_net.registerClient(addr, name, nowSec);
    if (pid == 0xFF)
        return 0xFF;

    m_lobby[pid].active      = true;
    m_lobby[pid].isAnonymous = isAnonymous;
    m_lobby[pid].isBot       = false;
    m_lobby[pid].ready       = false;
    strncpy(m_lobby[pid].name, name, 15);
    m_lobby[pid].name[15]    = '\0';
    if (m_hostPid == 0xFF)
        m_hostPid = pid;

    return pid;
}

// ---- Auth response handler ----

void GameServer::handleAuthResponse(const Envelope &e, PendingAuth pa,
                                    const std::vector<uint8_t> &decrypted)
{
    if (decrypted.size() < sizeof(PktAuthResponse))
        return;
    PktAuthResponse resp;
    memcpy(&resp, decrypted.data(), sizeof(resp));
    resp.name[15] = '\0';

    // Remove this pending auth from the vector now (pa is already a copy)
    m_pendingAuth.erase(
        std::remove_if(m_pendingAuth.begin(), m_pendingAuth.end(),
                       [&pa](const PendingAuth &x)
                       {
                           return x.addr.sin_addr.s_addr == pa.addr.sin_addr.s_addr && x.addr.sin_port == pa.addr.sin_port;
                       }),
        m_pendingAuth.end());

    // Helper to send encrypted result using the pending auth tx key
    auto sendResult = [&](uint8_t result, uint8_t pid, const char *reason)
    {
        PktAuthResult ar;
        ar.result = result;
        ar.pid = pid;
        strncpy(ar.reason, reason, 31);
        sendEncryptedToPending(pa, &ar, sizeof(ar));
    };

    // Re-check lobby state in case something changed during key exchange
    if (m_phase != ServerPhase::LOBBY)
    {
        sendResult(3, 0xFF, "Game in progress");
        return;
    }

    if (resp.mode == AuthMode::ANONYMOUS)
    {
        // Reject if the chosen name is already active in the lobby
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (m_lobby[i].active && strncmp(m_lobby[i].name, resp.name, 15) == 0)
            {
                sendResult(5, 0xFF, "Name already in use. Choose another.");
                return;
            }
        }
        uint8_t pid = registerPlayerInLobby(e.from, resp.name, true, m_now);
        if (pid == 0xFF) { sendResult(4, 0xFF, "Lobby full"); return; }

        m_net.setClientKeys(pid, pa.sessionRxKey, pa.sessionTxKey);
        sendResult(0, pid, "");
        // No profile for anonymous — skip sendProfileUpdate
        broadcastLobbyState();
        std::cout << "[Lobby] " << resp.name << " joined anonymously as pid " << (int)pid << "\n";
    }
    else if (resp.mode == AuthMode::REGISTER)
    {
        auto *existing = m_db.find(resp.name);
        if (existing && !existing->authKey.empty())
        {
            sendResult(2, 0xFF, "Name already registered");
            return;
        }

        // Store auth key and salt before registering so the record exists
        PlayerRecord &r = m_db.getOrCreate(resp.name);
        r.authKey = Persistence::bytesToHex(resp.authData, 32);
        r.salt    = std::string(resp.saltHex, strnlen(resp.saltHex, 32));
        m_db.save();

        uint8_t pid = registerPlayerInLobby(e.from, resp.name, false, m_now);
        if (pid == 0xFF) { sendResult(4, 0xFF, "Lobby full"); return; }

        m_net.setClientKeys(pid, pa.sessionRxKey, pa.sessionTxKey);
        sendResult(0, pid, "");
        sendProfileUpdate(pid);
        broadcastLobbyState();
        std::cout << "[Lobby] " << resp.name << " registered and joined as pid " << (int)pid << "\n";
    }
    else // AuthMode::LOGIN
    {
        auto *rec = m_db.find(resp.name);
        if (!rec || rec->authKey.empty())
        {
            sendResult(1, 0xFF, "Not registered. Use Register.");
            return;
        }

        // Verify: HMAC-SHA256(stored_authKey, challengeNonce) must equal resp.authData
        if (!Persistence::verifyHmac(rec->authKey, pa.challengeNonce, resp.authData))
        {
            sendResult(1, 0xFF, "Incorrect password");
            return;
        }

        uint8_t pid = registerPlayerInLobby(e.from, resp.name, false, m_now);
        if (pid == 0xFF) { sendResult(4, 0xFF, "Lobby full"); return; }

        m_net.setClientKeys(pid, pa.sessionRxKey, pa.sessionTxKey);
        sendResult(0, pid, "");
        sendProfileUpdate(pid);
        broadcastLobbyState();
        std::cout << "[Lobby] " << resp.name << " authenticated and joined as pid " << (int)pid << "\n";
    }
}

void GameServer::handleConnect(const Envelope& e)
{
    if (e.len < (int)sizeof(PktConnect))
        return;

    PktConnect p;
    memcpy(&p, e.buf, sizeof(p));
    p.name[15] = '\0';

    PktConnectAck ack;
    if (m_phase != ServerPhase::LOBBY)
    {
        ack.denied = 1;
        m_net.sendToAddr(e.from, &ack, sizeof(ack));
        return;
    }

    uint8_t pid = registerPlayerInLobby(e.from, p.name, /*isAnonymous=*/true, m_now);
    if (pid == 0xFF)
    {
        ack.denied = 1;
        m_net.sendToAddr(e.from, &ack, sizeof(ack));
        return;
    }

    ack.pid    = pid;
    ack.denied = 0;
    m_net.sendTo(pid, &ack, sizeof(ack));
    broadcastLobbyState();
    std::cout << "[Lobby] " << p.name << " connected (legacy CONNECT) as pid " << (int)pid << "\n";
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
        ls.slots[i].isAnonymous = m_lobby[i].isAnonymous ? 1 : 0;
        if(m_lobby[i].active && !m_lobby[i].isBot && !m_lobby[i].isAnonymous){
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

    // Award XP/coins — skip anonymous players (their stats are not persisted)
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!m_lobby[i].active)
            continue;
        if (m_lobby[i].isAnonymous)
        {
            sendProfileUpdate((uint8_t)i);
            continue;
        }
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

// kick player/bot
void GameServer::handleKickBot(const PktKickBot& p) {
    if (p.requestPid != m_hostPid || p.botPid >= MAX_PLAYERS) return;
    if (!m_lobby[p.botPid].active) return;

    // 1. If it's a human, send them the Disconnect packet first
    if (!m_lobby[p.botPid].isBot) {
        PktDisconnect d;
        d.pid = p.botPid;
        m_net.sendTo(p.botPid, &d, sizeof(d)); // This triggers the client's screen
    }

    // 2. Clean up server state
    m_lobby[p.botPid].active = false;
    m_bots[p.botPid].active = false;
    m_tanks[p.botPid].takeDamage(999); // Ensure they are removed from the game world

    std::cout << "[Server] " << (m_lobby[p.botPid].isBot ? "Bot " : "Player ")
        << (int)p.botPid << " was kicked.\n";

    broadcastLobbyState();
}

//  Bot AI 
void GameServer::updateBots(float dt)
{
    if (m_phase != ServerPhase::IN_GAME) return;

    // 1. Define Navigation Grid (60x45 for a 2400x1800 map)
    const int NAV_COLS = 60;
    const int NAV_ROWS = 45;
    bool blocked[NAV_ROWS][NAV_COLS] = { false };
    float cw = (float)MAP_W / NAV_COLS;
    float ch = (float)MAP_H / NAV_ROWS;

    // 2. Obstacle Inflation
    const float margin = TANK_RADIUS + 5.f;
    for (auto& o : m_obstacles) {
        int c1 = std::clamp((int)((o.x - margin) / cw), 0, NAV_COLS - 1);
        int r1 = std::clamp((int)((o.y - margin) / ch), 0, NAV_ROWS - 1);
        int c2 = std::clamp((int)((o.x + o.w + margin) / cw), 0, NAV_COLS - 1);
        int r2 = std::clamp((int)((o.y + o.h + margin) / ch), 0, NAV_ROWS - 1);
        for (int r = r1; r <= r2; r++) {
            for (int c = c1; c <= c2; c++) blocked[r][c] = true;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!m_lobby[i].active || !m_lobby[i].isBot || !m_tanks[i].alive()) continue;

        auto& tank = m_tanks[i];
        auto& inp = m_inputs[i];
        inp = PktInput{};
        inp.pid = (uint8_t)i;
        inp.seq = m_stateSeq;

        // 3. TARGETING: Find nearest alive Player OR Bot
        float bestDistSq = 1e9f;
        int targetPid = -1;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            if (i == j || !m_lobby[j].active || !m_tanks[j].alive()) continue;
            float d2 = std::pow(m_tanks[j].x() - tank.x(), 2) + std::pow(m_tanks[j].y() - tank.y(), 2);
            if (d2 < bestDistSq) { bestDistSq = d2; targetPid = j; }
        }

        if (targetPid == -1) continue;
        auto& target = m_tanks[targetPid];

        // 4. A* PATHFINDING
        int startC = std::clamp((int)(tank.x() / cw), 0, NAV_COLS - 1);
        int startR = std::clamp((int)(tank.y() / ch), 0, NAV_ROWS - 1);
        int endC = std::clamp((int)(target.x() / cw), 0, NAV_COLS - 1);
        int endR = std::clamp((int)(target.y() / ch), 0, NAV_ROWS - 1);

        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
        std::vector<std::vector<float>> gScore(NAV_ROWS, std::vector<float>(NAV_COLS, 1e9f));
        std::vector<std::vector<std::pair<int, int>>> parent(NAV_ROWS, std::vector<std::pair<int, int>>(NAV_COLS, { -1, -1 }));

        gScore[startR][startC] = 0;
        openSet.push({ startR, startC, 0, (float)(abs(endR - startR) + abs(endC - startC)) });

        bool found = false;
        while (!openSet.empty()) {
            AStarNode curr = openSet.top(); openSet.pop();
            if (curr.r == endR && curr.c == endC) { found = true; break; }

            int dr[] = { -1, 1, 0, 0, -1, -1, 1, 1 }, dc[] = { 0, 0, -1, 1, -1, 1, -1, 1 }; // 8-way movement
            for (int k = 0; k < 8; k++) {
                int nr = curr.r + dr[k], nc = curr.c + dc[k];
                if (nr >= 0 && nr < NAV_ROWS && nc >= 0 && nc < NAV_COLS && !blocked[nr][nc]) {
                    float moveCost = (k < 4) ? 1.0f : 1.414f; // Diagonal cost
                    float tentG = gScore[curr.r][curr.c] + moveCost;
                    if (tentG < gScore[nr][nc]) {
                        parent[nr][nc] = { curr.r, curr.c };
                        gScore[nr][nc] = tentG;
                        openSet.push({ nr, nc, tentG, (float)(abs(endR - nr) + abs(endC - nc)) });
                    }
                }
            }
        }

        // 5. STEERING
        float tx = target.x(), ty = target.y();
        if (found && (startR != endR || startC != endC)) {
            std::pair<int, int> step = { endR, endC };
            while (parent[step.first][step.second].first != startR || parent[step.first][step.second].second != startC) {
                step = parent[step.first][step.second];
                if (step.first == -1) break;
            }
            tx = (step.second + 0.5f) * cw;
            ty = (step.first + 0.5f) * ch;
        }

        float angleToNode = std::atan2(ty - tank.y(), tx - tank.x()) * (180.f / 3.14159f);
        float diff = angleToNode - tank.angle();
        while (diff > 180) diff -= 360; while (diff < -180) diff += 360;

        if (std::abs(diff) > 10.f) { if (diff > 0) inp.right = 1; else inp.left = 1; }
        if (std::abs(diff) < 45.f) inp.forward = 1;

        // 6. LINE-OF-SIGHT (LOS) Shooting
        float distToTarget = std::sqrt(bestDistSq);
        if (distToTarget < 600.f && std::abs(diff) < 20.f) {
            bool clearShot = true;
            for (float d = 20.f; d < distToTarget; d += 20.f) {
                float rx = tank.x() + std::cos(tank.angle() * 3.14159f / 180.f) * d;
                float ry = tank.y() + std::sin(tank.angle() * 3.14159f / 180.f) * d;
                for (auto& o : m_obstacles) {
                    if (rx >= o.x && rx <= o.x + o.w && ry >= o.y && ry <= o.y + o.h) { clearShot = false; break; }
                }
                if (!clearShot) break;
            }
            if (clearShot) inp.fire = 1;
        }
    }
}