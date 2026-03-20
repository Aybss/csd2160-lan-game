#include <iostream>
#include <fstream>
#include <string>
#include "GameServer.h"
#include "GameClient.h"
#include "Common.h"

static void loadConfig(std::string& ip, uint16_t& port)
{
    ip   = "127.0.0.1";
    port = NET_PORT;
    std::ifstream f("server.cfg");
    if(!f.is_open()) return;
    std::string line;
    while(std::getline(f,line)){
        if(line.empty()||line[0]=='#') continue;
        auto eq=line.find('=');
        if(eq==std::string::npos) continue;
        std::string k=line.substr(0,eq), v=line.substr(eq+1);
        if(k=="SERVER_IP") ip=v;
        else if(k=="PORT") port=(uint16_t)std::stoi(v);
    }
}

int main(int argc, char* argv[])
{
    std::string ip; uint16_t port;
    loadConfig(ip, port);

    std::string mode, username;

    if(argc >= 2) mode = argv[1];
    if(argc >= 3) username = argv[2];
    if(argc >= 4) ip = argv[3];

    if(mode.empty())
    {
        std::cout << "Run as [s]erver or [c]lient? ";
        char c; std::cin>>c;
        mode = (c=='s'||c=='S') ? "server" : "client";
        if(mode=="client"){
            std::cout << "Username: "; std::cin>>username;
            std::cout << "Server IP [" << ip << "]: ";
            std::string inp; std::cin.ignore();
            std::getline(std::cin,inp);
            if(!inp.empty()) ip=inp;
        }
    }

    if(username.empty()) username = "Player";
    // Clamp to 15 chars
    if(username.size()>15) username=username.substr(0,15);

    try {
        if(mode=="server"){
            std::cout<<"[MODE] Server on port "<<port<<"\n";
            GameServer srv(port);
            srv.run();
        } else {
            std::cout<<"[MODE] Client -> "<<ip<<":"<<port<<" as "<<username<<"\n";
            GameClient cli(ip,port,username);
            cli.run();
        }
    } catch(const std::exception& ex){
        std::cerr<<"Fatal: "<<ex.what()<<"\n";
        return 1;
    }
    return 0;
}
