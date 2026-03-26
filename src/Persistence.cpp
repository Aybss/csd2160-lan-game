#include "Persistence.h"
#include "Common.h"
#include <sodium.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

// Minimal hand-rolled JSON (no external lib needed)
// Format per player:
// {"name":"x","xp":0,"level":1,"coins":100,"ownedSkins":1,"totalWins":0,"totalKills":0,"authKey":"","salt":""}

static std::string jsonStr(const std::string& key, const std::string& val)
{ return "\"" + key + "\":\"" + val + "\""; }
static std::string jsonNum(const std::string& key, uint32_t val)
{ return "\"" + key + "\":" + std::to_string(val); }

static std::string serialize(const PlayerRecord& r)
{
    return "{" + jsonStr("name",r.name) + "," +
           jsonNum("xp",r.xp) + "," +
           jsonNum("level",r.level) + "," +
           jsonNum("coins",r.coins) + "," +
           jsonNum("ownedSkins",r.ownedSkins) + "," +
           jsonNum("totalWins",r.totalWins) + "," +
           jsonNum("totalKills",r.totalKills) + "," +
           jsonStr("authKey",r.authKey) + "," +
           jsonStr("salt",r.salt) + "}";
}

static std::string extractStr(const std::string& json, const std::string& key)
{
    auto pos = json.find("\""+key+"\":\"");
    if(pos==std::string::npos) return "";
    pos += key.size()+4;
    auto end = json.find('"',pos);
    return json.substr(pos, end-pos);
}
static uint32_t extractNum(const std::string& json, const std::string& key)
{
    auto pos = json.find("\""+key+"\":");
    if(pos==std::string::npos) return 0;
    pos += key.size()+3;
    return (uint32_t)std::stoul(json.substr(pos));
}

bool Persistence::load(const std::string& path)
{
    m_path = path;
    std::ifstream f(path);
    if(!f.is_open()){ std::cout<<"[DB] No file found, starting fresh.\n"; return false; }
    std::string line;
    while(std::getline(f,line))
    {
        if(line.empty()||line[0]!= '{') continue;
        PlayerRecord r;
        r.name       = extractStr(line,"name");
        r.xp         = extractNum(line,"xp");
        r.level      = (uint16_t)extractNum(line,"level");
        r.coins      = extractNum(line,"coins");
        r.ownedSkins = (uint8_t)extractNum(line,"ownedSkins");
        r.totalWins  = (uint16_t)extractNum(line,"totalWins");
        r.totalKills = (uint16_t)extractNum(line,"totalKills");
        r.authKey    = extractStr(line,"authKey");
        r.salt       = extractStr(line,"salt");
        if(!r.name.empty()) m_records.push_back(r);
    }
    std::cout<<"[DB] Loaded "<<m_records.size()<<" players.\n";
    return true;
}
bool Persistence::save(const std::string& path)
{
    std::string p = path.empty() ? m_path : path;
    std::ofstream f(p);
    if(!f.is_open()){ std::cerr<<"[DB] Cannot save to "<<p<<"\n"; return false; }
    for(auto& r:m_records) f << serialize(r) << "\n";
    return true;
}

PlayerRecord* Persistence::find(const std::string& name)
{
    for(auto& r:m_records) if(r.name==name) return &r;
    return nullptr;
}
PlayerRecord& Persistence::getOrCreate(const std::string& name)
{
    auto* p = find(name);
    if(p) return *p;
    PlayerRecord r; r.name=name;
    m_records.push_back(r);
    return m_records.back();
}
void Persistence::recomputeLevel(PlayerRecord& r)
{
    r.level = (uint16_t)(1 + r.xp / XP_PER_LEVEL);
}
void Persistence::addXP(const std::string& name, uint32_t amount)
{
    auto& r = getOrCreate(name); r.xp+=amount; recomputeLevel(r);
}
void Persistence::addCoins(const std::string& name, uint32_t amount)
{
    auto& r = getOrCreate(name); r.coins+=amount;
}
bool Persistence::buySkin(const std::string& name, uint8_t skinIdx)
{
    if(skinIdx>=SKIN_COUNT) return false;
    auto& r = getOrCreate(name);
    if(r.coins < SKIN_PRICES[skinIdx]) return false;
    if(r.ownedSkins & (1<<skinIdx)) return false; // already owned
    r.coins -= SKIN_PRICES[skinIdx];
    r.ownedSkins |= (1<<skinIdx);
    return true;
}
void Persistence::addWin(const std::string& name){ auto& r=getOrCreate(name); r.totalWins++; }
void Persistence::addKill(const std::string& name){ auto& r=getOrCreate(name); r.totalKills++; }

std::vector<PlayerRecord> Persistence::topByWins(int n) const
{
    auto copy = m_records;
    std::sort(copy.begin(),copy.end(),[](const PlayerRecord& a,const PlayerRecord& b){
        return a.totalWins>b.totalWins;
    });
    if((int)copy.size()>n) copy.resize(n);
    return copy;
}

// ---- Crypto helpers ----

std::string Persistence::generateSaltHex()
{
    uint8_t salt[crypto_pwhash_SALTBYTES]; // 16 bytes
    randombytes_buf(salt, sizeof(salt));
    char hex[crypto_pwhash_SALTBYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof(hex), salt, sizeof(salt));
    return std::string(hex);
}

std::string Persistence::bytesToHex(const uint8_t *data, size_t len)
{
    std::string hex(len * 2 + 1, '\0');
    sodium_bin2hex(&hex[0], hex.size(), data, len);
    hex.resize(len * 2);
    return hex;
}

bool Persistence::hexToBytes(const std::string &hex, uint8_t *out, size_t len)
{
    size_t decoded = 0;
    return sodium_hex2bin(out, len, hex.c_str(), hex.size(),
                          nullptr, &decoded, nullptr) == 0 &&
           decoded == len;
}

bool Persistence::verifyHmac(const std::string &storedAuthKeyHex,
                             const uint8_t challengeNonce[32],
                             const uint8_t receivedHmac[32])
{
    uint8_t key[32];
    if (!hexToBytes(storedAuthKeyHex, key, 32))
        return false;

    uint8_t expected[crypto_auth_hmacsha256_BYTES]; // 32 bytes
    crypto_auth_hmacsha256(expected, challengeNonce, 32, key);

    // Constant-time comparison to prevent timing attacks
    return crypto_verify_32(expected, receivedHmac) == 0;
}
