#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct PlayerRecord
{
    std::string name;
    uint32_t    xp         = 0;
    uint16_t    level      = 1;
    uint32_t    coins      = 100;   // start with some coins
    uint8_t     ownedSkins = 0x01;  // bitmask; bit0 = skin0 (default, always owned)
    uint16_t    totalWins  = 0;
    uint16_t    totalKills = 0;
};

// Loads/saves players.json next to the server exe
class Persistence
{
public:
    bool load(const std::string& path = "players.json");
    bool save(const std::string& path = "players.json");

    // Returns existing or creates new record
    PlayerRecord& getOrCreate(const std::string& name);

    void addXP   (const std::string& name, uint32_t amount);
    void addCoins(const std::string& name, uint32_t amount);
    bool buySkin (const std::string& name, uint8_t skinIdx); // returns false if can't afford
    void addWin  (const std::string& name);
    void addKill (const std::string& name);

    // Top N by totalWins
    std::vector<PlayerRecord> topByWins(int n = 5) const;

    const std::vector<PlayerRecord>& all() const { return m_records; }

private:
    std::vector<PlayerRecord> m_records;
    std::string               m_path;

    PlayerRecord* find(const std::string& name);
    void          recomputeLevel(PlayerRecord& r);
};
