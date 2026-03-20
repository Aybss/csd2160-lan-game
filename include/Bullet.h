#pragma once
#include "Common.h"
#include <vector>

class Bullet
{
public:
    Bullet() = default;
    Bullet(uint8_t owner, float x, float y, float vx, float vy);

    // Returns true if still alive
    bool update(float dt, const std::vector<Obstacle>& obs);

    float   x()      const { return m_x; }
    float   y()      const { return m_y; }
    float   vx()     const { return m_vx; }
    float   vy()     const { return m_vy; }
    uint8_t owner()  const { return m_owner; }
    bool    active() const { return m_active; }
    float   life()   const { return m_life; }

    void kill() { m_active = false; }

private:
    float   m_x = 0, m_y = 0;
    float   m_vx = 0, m_vy = 0;
    float   m_life   = BULLET_LIFETIME;
    uint8_t m_owner  = 0;
    bool    m_active = false;
};
