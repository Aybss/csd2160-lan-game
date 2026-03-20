#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma comment(lib,"ws2_32.lib")
#include "Network.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

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
