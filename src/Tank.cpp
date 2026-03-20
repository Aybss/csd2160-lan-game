#include "Tank.h"
#include <cmath>
#include <algorithm>

static constexpr float DEG2RAD    = 3.14159265f / 180.f;
static constexpr float SHOOT_CD   = 0.5f;
static constexpr float SHOOT_CD_RAPID = 0.15f;

Tank::Tank(float x, float y, uint8_t pid, uint8_t skin)
    : m_x(x), m_y(y), m_pid(pid), m_skin(skin)
{}

bool Tank::collides(float nx, float ny, const std::vector<Obstacle>& obs) const
{
    if(nx-TANK_RADIUS<0||ny-TANK_RADIUS<0||nx+TANK_RADIUS>MAP_W||ny+TANK_RADIUS>MAP_H)
        return true;
    for(auto& o:obs){
        float cx=std::max(o.x,std::min(nx,o.x+o.w));
        float cy=std::max(o.y,std::min(ny,o.y+o.h));
        float dx=nx-cx, dy=ny-cy;
        if(dx*dx+dy*dy < TANK_RADIUS*TANK_RADIUS) return true;
    }
    return false;
}

void Tank::update(float dt, bool fwd, bool back, bool left, bool right,
                  const std::vector<Obstacle>& obs)
{
    if(m_hp<=0) return;
    m_shootCooldown -= dt;

    if(left)  m_angle -= TANK_ROT_SPEED * dt;
    if(right) m_angle += TANK_ROT_SPEED * dt;

    // Speed buff: 1.8x
    float spd = ((m_buffMask & 0x01) ? TANK_SPEED * 1.8f : TANK_SPEED);
    float rad = m_angle * DEG2RAD;
    float dx=0, dy=0;
    if(fwd) { dx += cosf(rad)*spd*dt; dy += sinf(rad)*spd*dt; }
    if(back){ dx -= cosf(rad)*spd*dt; dy -= sinf(rad)*spd*dt; }

    if(!collides(m_x+dx,m_y+dy,obs))     { m_x+=dx; m_y+=dy; }
    else if(!collides(m_x+dx,m_y,obs))   { m_x+=dx; }
    else if(!collides(m_x,m_y+dy,obs))   { m_y+=dy; }
}

bool Tank::tryShoot(float /*now*/)
{
    if(m_hp<=0||m_shootCooldown>0.f) return false;
    m_shootCooldown = (m_buffMask & 0x02) ? SHOOT_CD_RAPID : SHOOT_CD;
    return true;
}

void Tank::applyPowerup(PowerupType t)
{
    int idx = (int)t;
    m_buffMask    |= (1 << idx);
    m_buffTimer[idx] = POWERUP_DURATION;
}

void Tank::tickBuffs(float dt)
{
    for(int i=0;i<3;i++){
        if(m_buffMask & (1<<i)){
            m_buffTimer[i] -= dt;
            if(m_buffTimer[i] <= 0.f){
                m_buffMask &= ~(1<<i);
                m_buffTimer[i] = 0.f;
            }
        }
    }
}

void Tank::takeDamage(int dmg)
{
    // Shield absorbs one hit
    if(m_buffMask & 0x04){
        m_buffMask &= ~0x04;
        m_buffTimer[2] = 0.f;
        return;  // blocked
    }
    m_hp -= dmg;
    if(m_hp < 0) m_hp = 0;
}

void Tank::shootDir(float& bx, float& by, float& vx, float& vy) const
{
    float rad = m_angle * DEG2RAD;
    bx = m_x + cosf(rad)*(TANK_RADIUS+BULLET_RADIUS+2.f);
    by = m_y + sinf(rad)*(TANK_RADIUS+BULLET_RADIUS+2.f);
    vx = cosf(rad)*BULLET_SPEED;
    vy = sinf(rad)*BULLET_SPEED;
}
