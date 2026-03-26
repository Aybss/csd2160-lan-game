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

    // Auth fields (empty = unregistered; LOGIN requires both to be non-empty)
    std::string authKey; // hex-encoded 32-byte Argon2id(password, salt) output
    std::string salt;    // hex-encoded 16-byte Argon2id salt
};

// Loads/saves players.json next to the server exe
class Persistence
{
public:
    bool load(const std::string& path = "players.json");
    bool save(const std::string& path = "players.json");

    // Returns existing or creates new record
    PlayerRecord& getOrCreate(const std::string& name);

    // Public find (needed by GameServer for salt lookup during key exchange)
    PlayerRecord *find(const std::string& name);

    void addXP   (const std::string& name, uint32_t amount);
    void addCoins(const std::string& name, uint32_t amount);
    bool buySkin (const std::string& name, uint8_t skinIdx); // returns false if can't afford
    void addWin  (const std::string& name);
    void addKill (const std::string& name);

    // Top N by totalWins
    std::vector<PlayerRecord> topByWins(int n = 5) const;

    const std::vector<PlayerRecord>& all() const { return m_records; }

    // ---- Crypto helpers (require sodium_init() to have been called) ----

    // Generate a fresh hex-encoded 16-byte random Argon2id salt
    static std::string generateSaltHex();

    // Convert raw bytes to lowercase hex string
    static std::string bytesToHex(const uint8_t *data, size_t len);

    // Decode hex string into raw bytes (out must be at least len bytes)
    static bool hexToBytes(const std::string &hex, uint8_t *out, size_t len);

    // Verify HMAC-SHA256(storedAuthKeyHex decoded, challengeNonce) == receivedHmac
    static bool verifyHmac(const std::string &storedAuthKeyHex,
                           const uint8_t challengeNonce[32],
                           const uint8_t receivedHmac[32]);

private:
    std::vector<PlayerRecord> m_records;
    std::string               m_path;

    void          recomputeLevel(PlayerRecord& r);
};
