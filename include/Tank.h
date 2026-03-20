#pragma once
#include "Common.h"
#include <vector>

struct Obstacle { float x, y, w, h; };

class Tank
{
public:
    Tank() = default;
    Tank(float x, float y, uint8_t pid, uint8_t skin);

    // Server-side update; returns true if still alive
    void update(float dt, bool fwd, bool back, bool left, bool right,
                const std::vector<Obstacle>& obs);

    bool tryShoot(float now);   // returns true if fired, enforces cooldown

    float   x()     const { return m_x; }
    float   y()     const { return m_y; }
    float   angle() const { return m_angle; }
    int     hp()    const { return m_hp; }
    bool    alive() const { return m_hp > 0; }
    uint8_t pid()   const { return m_pid; }
    uint8_t skin()  const { return m_skin; }
    int16_t kills() const { return m_kills; }

    void takeDamage(int dmg = 1)  { m_hp -= dmg; if (m_hp < 0) m_hp = 0; }
    void addKill()                { m_kills++; }
    void setSkin(uint8_t s)       { m_skin = s; }

    // Bullet spawn position & velocity
    void shootDir(float& bx, float& by, float& vx, float& vy) const;

private:
    float   m_x = 0, m_y = 0;
    float   m_angle = 0;   // degrees, 0 = right
    int     m_hp    = TANK_MAX_HP;
    uint8_t m_pid   = 0;
    uint8_t m_skin  = 0;
    int16_t m_kills = 0;
    float   m_shootCooldown = 0.f;

    bool collides(float nx, float ny, const std::vector<Obstacle>& obs) const;
};
