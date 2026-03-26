#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma comment(lib,"ws2_32.lib")
#include "Network.h"
#include <sodium.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>

//  WSA init 
static bool s_wsaInit = false;
static void ensureWsa()
{
    if (!s_wsaInit) {
        WSADATA w{}; WSAStartup(MAKEWORD(2,2),&w); s_wsaInit=true;
    }
}

// UdpSocket
UdpSocket::UdpSocket()
{
    ensureWsa();
    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == INVALID_SOCKET) throw std::runtime_error("socket() failed");
    u_long nb = 1;
    ioctlsocket(m_sock, FIONBIO, &nb);
    // Allow broadcast just in case
    int yes = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
}
UdpSocket::~UdpSocket() { if (m_sock != INVALID_SOCKET) closesocket(m_sock); }

bool UdpSocket::bind(uint16_t port)
{
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=INADDR_ANY;
    return ::bind(m_sock,(sockaddr*)&a,sizeof(a))==0;
}
bool UdpSocket::sendTo(const void* d,int len,const sockaddr_in& addr)
{
    return sendto(m_sock,(const char*)d,len,0,(const sockaddr*)&addr,sizeof(addr))!=SOCKET_ERROR;
}
int UdpSocket::recvFrom(void* buf,int maxLen,sockaddr_in& addr)
{
    int al=sizeof(addr);
    int r=recvfrom(m_sock,(char*)buf,maxLen,0,(sockaddr*)&addr,&al);
    if(r==SOCKET_ERROR){
        int e=WSAGetLastError();
        if(e==WSAEWOULDBLOCK) return 0;
        return -1;
    }
    return r;
}

// ServerNet
bool ServerNet::init(uint16_t port)
{
    if(!m_sock.bind(port)){ std::cerr<<"[Server] bind failed\n"; return false; }
    std::cout<<"[Server] UDP port "<<port<<"\n";
    return true;
}

void ServerNet::poll(float nowSec)
{
    uint8_t buf[2048];
    sockaddr_in from{};
    int r;
    while((r=m_sock.recvFrom(buf,sizeof(buf),from))>0)
    {
        Envelope e;
        e.len = r;
        memcpy(e.buf, buf, r);
        e.from = from;
        m_queue.push(e);

        // Update lastSeen by address match
        for(auto& c : m_clients)
            if(c.active && c.addr.sin_addr.s_addr==from.sin_addr.s_addr && c.addr.sin_port==from.sin_port)
                c.lastSeen = nowSec;

        // Also update by pid embedded in packet (more reliable on loopback)
        if(r >= 2)
        {
            uint8_t pid = buf[1];  // pid is always second byte in our packets
            if(pid < MAX_PLAYERS)
                for(auto& c : m_clients)
                    if(c.active && c.pid == pid)
                        c.lastSeen = nowSec;
        }
    }
}

uint8_t ServerNet::registerClient(const sockaddr_in& addr, const char* name, float nowSec)
{
    if(m_clients.size()>=MAX_PLAYERS) return 0xFF;
    // Check duplicate addr
    for(auto& c:m_clients)
        if(c.active && c.addr.sin_addr.s_addr==addr.sin_addr.s_addr && c.addr.sin_port==addr.sin_port)
            return c.pid;
    ClientRecord cr;
    cr.addr   = addr;
    cr.pid    = m_nextPid++;
    cr.active = true;
    strncpy(cr.name, name, 15);
    cr.lastSeen = nowSec;  // prevent immediate timeout
    m_clients.push_back(cr);
    return cr.pid;
}

bool ServerNet::hasClient(uint8_t pid) const
{
    for(auto& c:m_clients) if(c.active && c.pid==pid) return true;
    return false;
}
void ServerNet::sendTo(uint8_t pid,const void* data,int len)
{
    for(auto& c:m_clients) if(c.active && c.pid==pid){ m_sock.sendTo(data,len,c.addr); return; }
}
void ServerNet::broadcast(const void* data,int len)
{
    for(auto& c:m_clients) if(c.active) m_sock.sendTo(data,len,c.addr);
}
void ServerNet::broadcastExcept(uint8_t ex,const void* data,int len)
{
    for(auto& c:m_clients) if(c.active && c.pid!=ex) m_sock.sendTo(data,len,c.addr);
}
bool ServerNet::pollEnvelope(Envelope& out)
{
    if(m_queue.empty()) return false;
    out=m_queue.front(); m_queue.pop(); return true;
}
std::vector<uint8_t> ServerNet::checkTimeouts(float nowSec,float timeoutSec)
{
    std::vector<uint8_t> dead;
    for(auto& c:m_clients)
        if(c.active && (nowSec - c.lastSeen) > timeoutSec){ dead.push_back(c.pid); c.active=false; }
    return dead;
}
const ClientRecord* ServerNet::getClient(uint8_t pid) const
{
    for(auto& c:m_clients) if(c.active && c.pid==pid) return &c;
    return nullptr;
}
std::vector<uint8_t> ServerNet::activePids() const
{
    std::vector<uint8_t> v;
    for(auto& c:m_clients) if(c.active) v.push_back(c.pid);
    return v;
}

// ClientNet
bool ClientNet::init(const std::string& ip, uint16_t port)
{
    if(!m_sock.bind(0)){ std::cerr<<"[Client] local bind failed\n"; return false; }
    m_server.sin_family=AF_INET;
    m_server.sin_port=htons(port);
    inet_pton(AF_INET,ip.c_str(),&m_server.sin_addr);
    return true;
}
void ClientNet::poll()
{
    uint8_t buf[2048]; sockaddr_in from{};
    int r;
    while((r=m_sock.recvFrom(buf,sizeof(buf),from))>0)
    {
        Envelope e; e.len=r; memcpy(e.buf,buf,r); e.from=from;
        m_queue.push(e);
    }
}
void ClientNet::send(const void* data,int len){ m_sock.sendTo(data,len,m_server); }
bool ClientNet::pollEnvelope(Envelope& out)
{
    if(m_queue.empty()) return false;
    out=m_queue.front(); m_queue.pop(); return true;
}

//  LAN discovery 
// Discovery protocol:
//   1. Scanner binds to NET_PORT+1 (dedicated discovery port).
//   2. Scanner broadcasts PktServerQuery to NET_PORT (game port).
//   3. Server receives query, unicasts PktServerAnnounce to scanner (NET_PORT+1).
//   4. Server also periodically broadcasts PktServerAnnounce to NET_PORT+1.
//   Both paths land on the scanner socket.
std::vector<DiscoveredServer> scanLAN(uint16_t port, int waitMs)
{
    ensureWsa();
    std::vector<DiscoveredServer> results;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock == INVALID_SOCKET) return results;

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);

    // Bind to dedicated discovery port (NET_PORT+1).
    // The server unicasts replies here, and also broadcasts to this port periodically.
    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(port + 1);
    local.sin_addr.s_addr = INADDR_ANY;
    if(::bind(sock, (sockaddr*)&local, sizeof(local)) != 0)
    {
        // Fallback to ephemeral if port already in use
        local.sin_port = 0;
        ::bind(sock, (sockaddr*)&local, sizeof(local));
    }

    // Broadcast PktServerQuery to the game port so servers respond immediately
    PktServerQuery q;
    sockaddr_in bcast{};
    bcast.sin_family      = AF_INET;
    bcast.sin_port        = htons(port);          // game port
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(sock, (const char*)&q, sizeof(q), 0, (sockaddr*)&bcast, sizeof(bcast));

    auto start    = std::chrono::steady_clock::now();
    auto deadline = std::chrono::milliseconds(waitMs);
    uint8_t buf[2048];

    while(std::chrono::steady_clock::now() - start < deadline)
    {
        sockaddr_in from{}; int fl = sizeof(from);
        int r = recvfrom(sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if(r >= (int)sizeof(PktServerAnnounce) &&
           buf[0] == (uint8_t)PktType::SERVER_ANNOUNCE)
        {
            PktServerAnnounce ann;
            memcpy(&ann, buf, sizeof(ann));

            DiscoveredServer ds;
            char ipbuf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
            ds.ip          = ipbuf;
            ds.port        = ann.port;
            ds.playerCount = ann.playerCount;
            ds.maxPlayers  = ann.maxPlayers;
            ds.inGame      = ann.inGame != 0;
            memcpy(ds.playerNames, ann.playerNames, sizeof(ds.playerNames));

            bool dup = false;
            for(auto& ex : results)
                if(ex.ip == ds.ip && ex.port == ds.port) { dup = true; break; }
            if(!dup) results.push_back(ds);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    closesocket(sock);
    return results;
}

// ---- ServerNet crypto helpers ----

void ServerNet::sendToAddr(const sockaddr_in &addr, const void *data, int len)
{
    m_sock.sendTo(data, len, addr);
}

void ServerNet::setClientKeys(uint8_t pid, const uint8_t rxKey[32], const uint8_t txKey[32])
{
    for (auto &c : m_clients)
    {
        if (c.active && c.pid == pid)
        {
            memcpy(c.sessionRxKey, rxKey, 32);
            memcpy(c.sessionTxKey, txKey, 32);
            c.keyExchangeDone = true;
            c.nonceTx = 0;
            return;
        }
    }
}

void ServerNet::sendEncrypted(uint8_t pid, const void *plaintext, int len)
{
    ClientRecord *cr = nullptr;
    for (auto &c : m_clients)
        if (c.active && c.pid == pid)
        {
            cr = &c;
            break;
        }
    if (!cr || !cr->keyExchangeDone)
        return;

    // Build 24-byte nonce from 8-byte little-endian counter (remaining bytes zero)
    EncryptedEnvelopeHdr hdr;
    memset(hdr.nonce, 0, 24);
    memcpy(hdr.nonce, &cr->nonceTx, sizeof(cr->nonceTx));
    cr->nonceTx++;

    // ciphertext = plaintext + 16-byte MAC (crypto_secretbox_MACBYTES)
    std::vector<uint8_t> buf(sizeof(hdr) + len + 16);
    memcpy(buf.data(), &hdr, sizeof(hdr));
    crypto_secretbox_easy(
        buf.data() + sizeof(hdr),
        (const uint8_t *)plaintext, (unsigned long long)len,
        hdr.nonce,
        cr->sessionTxKey);
    m_sock.sendTo(buf.data(), (int)buf.size(), cr->addr);
}

bool ServerNet::decryptFrom(uint8_t pid, const Envelope &e, std::vector<uint8_t> &out)
{
    constexpr int HDR = (int)sizeof(EncryptedEnvelopeHdr);
    if (e.len <= HDR + 16)
        return false;

    const ClientRecord *cr = getClient(pid);
    if (!cr || !cr->keyExchangeDone)
        return false;

    const uint8_t *nonce = e.buf + 1; // skip PktType byte (1), nonce starts at offset 1
    const uint8_t *ct = e.buf + HDR;
    int ctLen = e.len - HDR;

    out.resize((size_t)(ctLen - 16));
    return crypto_secretbox_open_easy(
               out.data(),
               ct, (unsigned long long)ctLen,
               nonce,
               cr->sessionRxKey) == 0;
}

// ---- ClientNet crypto helpers ----

void ClientNet::setSessionKeys(const uint8_t rxKey[32], const uint8_t txKey[32])
{
    memcpy(m_sessionRxKey, rxKey, 32);
    memcpy(m_sessionTxKey, txKey, 32);
    m_keyExchangeDone = true;
    m_nonceTx = 0;
}

void ClientNet::sendEncrypted(const void *plaintext, int len)
{
    if (!m_keyExchangeDone)
        return;

    EncryptedEnvelopeHdr hdr;
    memset(hdr.nonce, 0, 24);
    memcpy(hdr.nonce, &m_nonceTx, sizeof(m_nonceTx));
    m_nonceTx++;

    std::vector<uint8_t> buf(sizeof(hdr) + len + 16);
    memcpy(buf.data(), &hdr, sizeof(hdr));
    crypto_secretbox_easy(
        buf.data() + sizeof(hdr),
        (const uint8_t *)plaintext, (unsigned long long)len,
        hdr.nonce,
        m_sessionTxKey);
    m_sock.sendTo(buf.data(), (int)buf.size(), m_server);
}

bool ClientNet::decryptPacket(const Envelope &e, std::vector<uint8_t> &out)
{
    constexpr int HDR = (int)sizeof(EncryptedEnvelopeHdr);
    if (e.len <= HDR + 16 || !m_keyExchangeDone)
        return false;

    const uint8_t *nonce = e.buf + 1;
    const uint8_t *ct = e.buf + HDR;
    int ctLen = e.len - HDR;

    out.resize((size_t)(ctLen - 16));
    return crypto_secretbox_open_easy(
               out.data(),
               ct, (unsigned long long)ctLen,
               nonce,
               m_sessionRxKey) == 0;
}
