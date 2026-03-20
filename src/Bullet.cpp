#include "Bullet.h"
#include "Tank.h"
#include <cmath>

Bullet::Bullet(uint8_t owner, float x, float y, float vx, float vy)
    : m_x(x), m_y(y), m_vx(vx), m_vy(vy), m_owner(owner), m_active(true)
{}

bool Bullet::update(float dt, const std::vector<Obstacle>& obs)
{
    if(!m_active) return false;
    m_life -= dt;
    if(m_life<=0.f){ m_active=false; return false; }

    m_x += m_vx * dt;
    m_y += m_vy * dt;

    // Wall bounce
    if(m_x-BULLET_RADIUS<0)    { m_x=BULLET_RADIUS;      m_vx=fabsf(m_vx); }
    if(m_x+BULLET_RADIUS>MAP_W) { m_x=MAP_W-BULLET_RADIUS; m_vx=-fabsf(m_vx); }
    if(m_y-BULLET_RADIUS<0)    { m_y=BULLET_RADIUS;      m_vy=fabsf(m_vy); }
    if(m_y+BULLET_RADIUS>MAP_H) { m_y=MAP_H-BULLET_RADIUS; m_vy=-fabsf(m_vy); }

    // Obstacle collision – kill bullet
    for(auto& o:obs)
    {
        float cx=std::max(o.x,std::min(m_x,o.x+o.w));
        float cy=std::max(o.y,std::min(m_y,o.y+o.h));
        float dx=m_x-cx, dy=m_y-cy;
        if(dx*dx+dy*dy < BULLET_RADIUS*BULLET_RADIUS){ m_active=false; return false; }
    }
    return true;
}
