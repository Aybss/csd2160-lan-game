// main.cpp  -  GUI launcher for TankArena  (SFML 3 API)
// Flow:
//   [Username screen] -> [Create Game | Join Game]
//   Create Game: detach GameServer thread on localhost, then run GameClient
//   Join Game:   broadcast LAN scan, show server list, pick one, run GameClient
//
// No stdin prompts at all. Terminal window is fine (shows server logs).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <SFML/Graphics.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "GameServer.h"
#include "GameClient.h"
#include "Network.h"
#include "Common.h"

// ============================================================
// Global font
// ============================================================
static sf::Font g_font;
static bool     g_fontLoaded = false;

static void loadFont()
{
    const char* paths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
    };
    for (auto& p : paths)
        if (g_font.openFromFile(p)) { g_fontLoaded = true; break; }
}

// ============================================================
// Layout constants & palette
// ============================================================
static constexpr int MW = 780;
static constexpr int MH = 580;

static const sf::Color BG_DARK   {12,  14,  20,  255};
static const sf::Color BG_PANEL  {20,  24,  36,  255};
static const sf::Color ACCENT    {0,   200, 140, 255};
static const sf::Color ACCENT2   {30,  160, 255, 255};
static const sf::Color TXT_DIM   {90,  100, 120, 255};
static const sf::Color TXT_MAIN  {200, 210, 225, 255};
static const sf::Color ERR_COL   {255,  80,  80, 255};
static const sf::Color PANEL_BRD {35,   45,  65, 255};
static const sf::Color GOLD_COL  {230, 180,  40, 255};

// ============================================================
// Draw helpers
// ============================================================
static sf::Text mkT(const std::string& s, unsigned sz,
                    sf::Color col = {200,210,225,255})
{
    sf::Text t(g_font, s, sz);
    t.setFillColor(col);
    return t;
}

static void centerX(sf::Text& t, float rx, float rw)
{
    auto b = t.getLocalBounds();
    t.setPosition({ rx + (rw - b.size.x)*0.5f - b.position.x,
                    t.getPosition().y });
}

static void drawRect(sf::RenderWindow& w,
                     float x, float y, float bw, float bh,
                     sf::Color fill,
                     sf::Color border = {0,0,0,0}, float thick = 0.f)
{
    sf::RectangleShape r({bw, bh});
    r.setPosition({x, y});
    r.setFillColor(fill);
    if (thick > 0.f) { r.setOutlineThickness(thick); r.setOutlineColor(border); }
    w.draw(r);
}

static bool isHov(const sf::RenderWindow& w,
                  float x, float y, float bw, float bh)
{
    auto mp = sf::Mouse::getPosition(w);
    return mp.x >= (int)x && mp.x <= (int)(x+bw) &&
           mp.y >= (int)y && mp.y <= (int)(y+bh);
}

static void drawButton(sf::RenderWindow& w,
                       float x, float y, float bw, float bh,
                       const std::string& label, bool hov,
                       sf::Color normFill = {28,38,55,255},
                       sf::Color hovFill  = {0,180,120,255})
{
    drawRect(w, x, y, bw, bh, hov ? hovFill : normFill, PANEL_BRD, 1.5f);
    auto t = mkT(label, 17, hov ? sf::Color::Black : TXT_MAIN);
    auto b = t.getLocalBounds();
    t.setPosition({ x + (bw - b.size.x)*0.5f - b.position.x,
                    y + (bh - b.size.y)*0.5f - b.position.y });
    w.draw(t);
}

static void drawGrid(sf::RenderWindow& w)
{
    for (int gx = 0; gx <= MW; gx += 40) {
        sf::RectangleShape l({1.f,(float)MH});
        l.setFillColor({25,30,45,255}); l.setPosition({(float)gx,0.f}); w.draw(l);
    }
    for (int gy = 0; gy <= MH; gy += 40) {
        sf::RectangleShape l({(float)MW,1.f});
        l.setFillColor({25,30,45,255}); l.setPosition({0.f,(float)gy}); w.draw(l);
    }
}

static void drawMiniTank(sf::RenderWindow& w, float cx, float cy,
                          sf::Color col, float angleDeg, float alpha = 1.f)
{
    col.a = (uint8_t)(col.a * alpha);
    sf::Color dark = col;
    dark.r = (uint8_t)(dark.r * 0.45f);
    dark.g = (uint8_t)(dark.g * 0.45f);
    dark.b = (uint8_t)(dark.b * 0.45f);

    sf::RectangleShape body({52.f, 36.f});
    body.setOrigin({26.f,18.f}); body.setPosition({cx,cy});
    body.setRotation(sf::degrees(angleDeg)); body.setFillColor(col);
    w.draw(body);

    sf::CircleShape turret(11.f);
    turret.setOrigin({11.f,11.f}); turret.setPosition({cx,cy});
    turret.setFillColor(dark); w.draw(turret);

    sf::RectangleShape barrel({26.f,5.f});
    barrel.setOrigin({1.f,2.5f}); barrel.setPosition({cx,cy});
    barrel.setRotation(sf::degrees(angleDeg)); barrel.setFillColor(dark);
    w.draw(barrel);
}

static void drawTitleBar(sf::RenderWindow& w, float y = 38.f)
{
    if (!g_fontLoaded) return;
    auto t1 = mkT("TANK",  66, ACCENT);  t1.setStyle(sf::Text::Bold);
    auto t2 = mkT("ARENA", 66, ACCENT2); t2.setStyle(sf::Text::Bold);
    auto b1 = t1.getLocalBounds(), b2 = t2.getLocalBounds();
    float tw = b1.size.x + 12.f + b2.size.x;
    float tx = (MW - tw) * 0.5f;
    t1.setPosition({tx, y}); t2.setPosition({tx + b1.size.x + 12.f, y});
    w.draw(t1); w.draw(t2);
    sf::RectangleShape ln({tw, 2.f});
    ln.setFillColor(ACCENT); ln.setPosition({tx, y + 72.f}); w.draw(ln);
}

// ============================================================
// Screen 1 – Username entry
// ============================================================
static std::string screenUsername(sf::RenderWindow& w)
{
    std::string name;
    std::string errorMsg;
    sf::Clock   clk;
    // Start true so any button held before this screen opened doesn't fire immediately
    bool        lmbWasDown = true;

    while (w.isOpen())
    {
        // SFML 3 event loop
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { w.close(); return ""; }

            if (const auto* te = ev->getIf<sf::Event::TextEntered>())
            {
                uint32_t c = te->unicode;
                if (c == 8 && !name.empty()) { name.pop_back(); errorMsg.clear(); }
                else if (c >= 32 && c < 127 && (int)name.size() < 15)
                { name += (char)c; errorMsg.clear(); }
            }

            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
            {
                if (kp->code == sf::Keyboard::Key::Enter)
                {
                    if (!name.empty()) return name;
                    errorMsg = "Please enter a name first.";
                }
            }
        }

        float elapsed = clk.getElapsedTime().asSeconds();
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        // Layout
        float cW = 420.f, cX = (MW-cW)*0.5f, cY = 170.f;
        float ibX = cX+30.f, ibY = cY+62.f, ibW = cW-60.f, ibH = 46.f;
        float btnX = ibX, btnY = cY+138.f;
        bool hovBtn = isHov(w, btnX, btnY, ibW, 46.f);

        if (lmbFire && hovBtn)
        {
            if (!name.empty()) return name;
            errorMsg = "Please enter a name first.";
        }

        w.clear(BG_DARK);
        drawGrid(w);

        float ta = elapsed * 18.f;
        drawMiniTank(w, 70.f,      70.f,       {0,200,140,55},  ta,        0.45f);
        drawMiniTank(w, MW-70.f,   70.f,       {30,160,255,55}, -ta,       0.45f);
        drawMiniTank(w, 70.f,      MH-70.f,    {230,180,40,55},  ta*0.7f,  0.35f);
        drawMiniTank(w, MW-70.f,   MH-70.f,    {0,200,140,55},  -ta*0.8f,  0.35f);

        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            drawRect(w, cX, cY, cW, 220.f, BG_PANEL, PANEL_BRD, 1.5f);

            auto sub = mkT("Enter your call sign", 15, TXT_DIM);
            centerX(sub, cX, cW); sub.setPosition({sub.getPosition().x, cY+20.f});
            w.draw(sub);

            drawRect(w, ibX, ibY, ibW, ibH, {14,18,28,255}, ACCENT, 1.5f);
            auto nameT = mkT(name.empty() ? "Type name..." : name, 22,
                             name.empty() ? TXT_DIM : sf::Color::White);
            nameT.setPosition({ibX+12.f, ibY+9.f});
            w.draw(nameT);

            // Blinking cursor
            if (!name.empty() && (int)(elapsed*2.2f) % 2 == 0)
            {
                auto b = nameT.getLocalBounds();
                sf::RectangleShape cur({2.f, 22.f});
                cur.setFillColor(ACCENT);
                cur.setPosition({ibX+12.f + b.size.x + 3.f, ibY+11.f});
                w.draw(cur);
            }

            auto cc = mkT(std::to_string((int)name.size())+"/15", 12, TXT_DIM);
            cc.setPosition({ibX+ibW-34.f, ibY+ibH+4.f}); w.draw(cc);

            if (!errorMsg.empty())
            {
                auto et = mkT(errorMsg, 13, ERR_COL);
                et.setPosition({ibX, ibY+ibH+4.f}); w.draw(et);
            }

            drawButton(w, btnX, btnY, ibW, 46.f, "CONFIRM   Enter",
                       hovBtn, {24,34,50,255}, {0,200,140,255});

            auto hint = mkT("Press Enter or click Confirm to continue", 13, TXT_DIM);
            centerX(hint, cX, cW);
            hint.setPosition({hint.getPosition().x, cY+200.f}); w.draw(hint);
        }

        w.display();
    }
    return "";
}

// ============================================================
// Screen 2 – Create or Join
// ============================================================
enum class MenuChoice { NONE, CREATE, JOIN };

static MenuChoice screenMainMenu(sf::RenderWindow& w, const std::string& username)
{
    sf::Clock clk;
    // Start as true so we don't register a carry-over click from the previous screen
    bool lmbWasDown = true;

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent())
            if (ev->is<sf::Event::Closed>()) { w.close(); return MenuChoice::NONE; }

        float ta = clk.getElapsedTime().asSeconds() * 22.f;
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float cW=420.f, cX=(MW-cW)*0.5f, cY=238.f;
        float bW=cW-44.f;
        float b1X=cX+22.f, b1Y=cY+22.f, bH=74.f;
        float b2X=cX+22.f, b2Y=cY+114.f;

        bool hC = isHov(w, b1X, b1Y, bW, bH);
        bool hJ = isHov(w, b2X, b2Y, bW, bH);

        if (lmbFire)
        {
            if (hC) return MenuChoice::CREATE;
            if (hJ) return MenuChoice::JOIN;
        }

        w.clear(BG_DARK);
        drawGrid(w);
        drawMiniTank(w, 110.f,    MH/2.f, {0,200,140,50},  ta,  0.45f);
        drawMiniTank(w, MW-110.f, MH/2.f, {30,160,255,50}, -ta, 0.45f);
        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            auto wt = mkT("Welcome back, " + username + "!", 20, TXT_MAIN);
            centerX(wt, 0.f, (float)MW);
            wt.setPosition({wt.getPosition().x, 140.f}); w.draw(wt);

            auto st = mkT("Choose how to play", 15, TXT_DIM);
            centerX(st, 0.f, (float)MW);
            st.setPosition({st.getPosition().x, 168.f}); w.draw(st);

            drawRect(w, cX, cY, cW, 220.f, BG_PANEL, PANEL_BRD, 1.5f);

            // Create button
            drawRect(w, b1X, b1Y, bW, bH,
                     hC ? sf::Color{0,175,115,255} : sf::Color{22,30,46,255},
                     PANEL_BRD, 1.5f);
            {
                auto l = mkT("Create Game", 22, hC ? sf::Color::Black : ACCENT);
                l.setStyle(sf::Text::Bold);
                centerX(l, b1X, bW); l.setPosition({l.getPosition().x, b1Y+8.f}); w.draw(l);
                auto s = mkT("Start a new server + join as host", 13,
                              hC ? sf::Color{30,30,30,255} : TXT_DIM);
                centerX(s, b1X, bW); s.setPosition({s.getPosition().x, b1Y+38.f}); w.draw(s);
            }

            // Join button
            drawRect(w, b2X, b2Y, bW, bH,
                     hJ ? sf::Color{0,120,200,255} : sf::Color{22,30,46,255},
                     PANEL_BRD, 1.5f);
            {
                auto l = mkT("Join Game", 22, hJ ? sf::Color::Black : ACCENT2);
                l.setStyle(sf::Text::Bold);
                centerX(l, b2X, bW); l.setPosition({l.getPosition().x, b2Y+8.f}); w.draw(l);
                auto s = mkT("Browse & join games on your network", 13,
                              hJ ? sf::Color{30,30,30,255} : TXT_DIM);
                centerX(s, b2X, bW); s.setPosition({s.getPosition().x, b2Y+38.f}); w.draw(s);
            }
        }

        w.display();
    }
    return MenuChoice::NONE;
}

// ============================================================
// Screen 3 – LAN Server Browser
// ============================================================
static std::pair<std::string,uint16_t>
screenBrowser(sf::RenderWindow& w, const std::string& /*username*/)
{
    std::vector<DiscoveredServer> servers;
    std::atomic<bool>  scanning{true};
    std::string        statusMsg = "Scanning network...";
    sf::Clock          uiClk;
    int                sel      = -1;
    float              scrollY  = 0.f;
    // Start as true so we don't register a carry-over click from the previous screen
    bool               lmbWasDown = true;

    static constexpr float LIST_Y = 188.f;
    static constexpr float LIST_H = 292.f;
    static constexpr float ROW_H  = 78.f;
    static constexpr float LIST_X = 18.f;
    static constexpr float LIST_W = (float)MW - 36.f;

    auto launchScan = [&]()
    {
        std::thread([&]()
        {
            auto res = scanLAN(NET_PORT, 1200);
            servers  = res;
            scanning.store(false);
            statusMsg = res.empty() ? "No games found on LAN." : "";
        }).detach();
    };
    launchScan();

    while (w.isOpen())
    {
        // SFML 3 event loop
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) return {"",0};

            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
            {
                if (kp->code == sf::Keyboard::Key::Escape) return {"",0};
                if (kp->code == sf::Keyboard::Key::F5 && !scanning.load())
                {
                    servers.clear(); sel=-1; scrollY=0.f;
                    scanning.store(true); statusMsg="Scanning network...";
                    launchScan();
                }
            }

            if (const auto* mw = ev->getIf<sf::Event::MouseWheelScrolled>())
            {
                scrollY -= mw->delta * 22.f;
                if (scrollY < 0.f) scrollY = 0.f;
            }
        }

        float ta     = uiClk.getElapsedTime().asSeconds() * 20.f;
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float btnY = MH-58.f;
        bool hBack    = isHov(w, 18.f,      btnY, 130.f, 40.f);
        bool hRefresh = isHov(w, 160.f,     btnY, 160.f, 40.f);
        bool canJoin  = sel>=0 && sel<(int)servers.size() && !servers[sel].inGame;
        bool hJoin    = isHov(w, MW-170.f,  btnY, 152.f, 40.f) && canJoin;

        if (lmbFire)
        {
            if (hBack) return {"",0};

            if (hRefresh && !scanning.load())
            {
                servers.clear(); sel=-1; scrollY=0.f;
                scanning.store(true); statusMsg="Scanning network...";
                launchScan();
            }

            if (hJoin && canJoin)
                return {servers[sel].ip, servers[sel].port};

            // Row selection
            auto mp = sf::Mouse::getPosition(w);
            if (mp.x >= (int)LIST_X && mp.x <= (int)(LIST_X+LIST_W) &&
                mp.y >= (int)LIST_Y  && mp.y <= (int)(LIST_Y+LIST_H))
            {
                int row = (int)((mp.y - LIST_Y + scrollY) / ROW_H);
                if (row >= 0 && row < (int)servers.size()) sel = row;
            }
        }

        //  Draw 
        w.clear(BG_DARK);
        drawGrid(w);

        if (g_fontLoaded)
        {
            auto title = mkT("LAN GAME BROWSER", 26, ACCENT);
            title.setStyle(sf::Text::Bold);
            title.setPosition({22.f, 20.f}); w.draw(title);

            bool sc = scanning.load();
            if (sc)
            {
                float spin = uiClk.getElapsedTime().asSeconds();
                const char* dots[] = {"   ", ".  ", ".. ", "..."};
                auto st = mkT("Scanning" + std::string(dots[(int)(spin*3)%4]), 15, ACCENT2);
                st.setPosition({22.f, 58.f}); w.draw(st);
            }
            else
            {
                std::string sm = statusMsg.empty()
                    ? std::to_string((int)servers.size()) + " game(s) found   (F5 = refresh)"
                    : statusMsg;
                auto st = mkT(sm, 15, TXT_DIM);
                st.setPosition({22.f, 58.f}); w.draw(st);
            }

            // Column headers
            drawRect(w, LIST_X, 90.f, LIST_W, 26.f, {18,24,38,255}, PANEL_BRD, 1.f);
            {
                auto h1=mkT("HOST IP",   12,TXT_DIM); h1.setPosition({28.f,  94.f}); w.draw(h1);
                auto h2=mkT("PLAYERS",  12,TXT_DIM); h2.setPosition({230.f, 94.f}); w.draw(h2);
                auto h3=mkT("IN LOBBY", 12,TXT_DIM); h3.setPosition({340.f, 94.f}); w.draw(h3);
                auto h4=mkT("STATUS",   12,TXT_DIM); h4.setPosition({640.f, 94.f}); w.draw(h4);
            }

            // List panel
            drawRect(w, LIST_X, LIST_Y, LIST_W, LIST_H, {13,17,27,255}, PANEL_BRD, 1.f);

            for (int i = 0; i < (int)servers.size(); i++)
            {
                float ry = LIST_Y + i*ROW_H - scrollY;
                if (ry+ROW_H < LIST_Y || ry > LIST_Y+LIST_H) continue;

                auto& sv    = servers[i];
                bool isSel  = (i == sel);
                bool rowHov = isHov(w, LIST_X+2.f, ry, LIST_W-4.f, ROW_H-2.f);

                sf::Color rowBg = isSel  ? sf::Color{0,50,40,200}
                                : rowHov ? sf::Color{20,28,44,200}
                                         : sf::Color{13,17,27,200};
                drawRect(w, LIST_X+2.f, ry+1.f, LIST_W-4.f, ROW_H-3.f,
                         rowBg, isSel ? ACCENT : PANEL_BRD, isSel ? 1.5f : 0.8f);

                // IP + port
                auto ipT = mkT(sv.ip, 16, isSel ? ACCENT : TXT_MAIN);
                ipT.setPosition({30.f, ry+10.f}); w.draw(ipT);
                auto portT = mkT(":"+std::to_string(sv.port), 12, TXT_DIM);
                portT.setPosition({30.f, ry+32.f}); w.draw(portT);

                // Player count
                std::string ps = std::to_string((int)sv.playerCount)+"/"
                               + std::to_string((int)sv.maxPlayers);
                sf::Color pc = sv.playerCount >= sv.maxPlayers ? ERR_COL
                             : sv.playerCount > 0              ? GOLD_COL : TXT_DIM;
                auto pcT = mkT(ps, 18, pc); pcT.setStyle(sf::Text::Bold);
                pcT.setPosition({232.f, ry+18.f}); w.draw(pcT);

                // Player names
                std::string names;
                for (int n = 0; n < (int)sv.playerCount && n < MAX_PLAYERS; n++)
                    if (sv.playerNames[n][0])
                    {
                        if (!names.empty()) names += ", ";
                        names += sv.playerNames[n];
                    }
                if (!names.empty())
                {
                    auto nt = mkT(names, 13, TXT_DIM);
                    nt.setPosition({342.f, ry+22.f}); w.draw(nt);
                }

                // Status badge
                sf::Color bc = sv.inGame ? ERR_COL
                             : sv.playerCount < sv.maxPlayers ? ACCENT
                             : sf::Color{130,130,130,255};
                std::string bt = sv.inGame ? "IN GAME" : "OPEN";
                drawRect(w, 640.f, ry+20.f, 82.f, 24.f, {18,26,42,255}, bc, 1.f);
                auto bdT = mkT(bt, 12, bc);
                centerX(bdT, 640.f, 82.f);
                bdT.setPosition({bdT.getPosition().x, ry+24.f}); w.draw(bdT);

                // Deco mini-tank
                drawMiniTank(w, LIST_X+LIST_W-48.f, ry+ROW_H*0.5f,
                             isSel ? ACCENT : sf::Color{38,48,66,255},
                             ta*(i%2==0 ? 1.f : -1.f), 0.65f);
            }

            // Empty-state illustration
            if (!sc && servers.empty())
            {
                drawMiniTank(w, MW*0.5f, LIST_Y+LIST_H*0.38f, {28,38,58,255}, ta, 0.6f);
                auto et = mkT("No servers found on this network", 19, TXT_DIM);
                centerX(et, 0.f, (float)MW);
                et.setPosition({et.getPosition().x, LIST_Y+LIST_H*0.6f}); w.draw(et);
                auto et2 = mkT("Have someone start a server, then press F5 to refresh",
                               13, sf::Color{46,56,76,255});
                centerX(et2, 0.f, (float)MW);
                et2.setPosition({et2.getPosition().x, LIST_Y+LIST_H*0.72f}); w.draw(et2);
            }

            // Bottom bar
            drawRect(w, 0.f, MH-68.f, (float)MW, 68.f, {10,14,24,255}, PANEL_BRD, 1.f);
            drawButton(w, 18.f,    btnY, 130.f, 40.f, "Back",       hBack);
            drawButton(w, 160.f,   btnY, 160.f, 40.f, "Refresh F5",
                       !sc && hRefresh, {22,32,48,255}, {0,160,200,255});
            drawButton(w, MW-170.f, btnY, 152.f, 40.f, "Join",
                       hJoin,
                       canJoin ? sf::Color{22,32,48,255} : sf::Color{16,20,30,255},
                       canJoin ? sf::Color{0,200,140,255} : sf::Color{40,50,60,255});
        }

        w.display();
    }

    return {"",0};
}

// ============================================================
// main
// ============================================================
int main(int /*argc*/, char* /*argv*/[])
{
    loadFont();

    // Ask username once — persists across reconnects
    std::string username;
    {
        sf::RenderWindow window(
            sf::VideoMode({(unsigned)MW, (unsigned)MH}),
            "TankArena",
            sf::Style::Titlebar | sf::Style::Close);
        window.setFramerateLimit(60);
        username = screenUsername(window);
        if (username.empty() || !window.isOpen()) return 0;
        if ((int)username.size() > 15) username = username.substr(0, 15);
    }

    // After GameClient exits (leave/disconnect) we come back here
    while (true)
    {
        sf::RenderWindow window(
            sf::VideoMode({(unsigned)MW, (unsigned)MH}),
            "TankArena",
            sf::Style::Titlebar | sf::Style::Close);
        window.setFramerateLimit(60);

        MenuChoice choice = screenMainMenu(window, username);
        if (!window.isOpen() || choice == MenuChoice::NONE) return 0;

        std::string  serverIp   = "127.0.0.1";
        uint16_t     serverPort = NET_PORT;

        if (choice == MenuChoice::JOIN)
        {
            auto [ip, port] = screenBrowser(window, username);
            if (!window.isOpen()) return 0;
            if (ip.empty()) continue;   // user backed out - re-show menu
            serverIp   = ip;
            serverPort = port;
            window.close();
        }
        else  // CREATE
        {
            window.close();

            // Only spawn the server thread once per process lifetime
            static bool s_serverStarted = false;
            if (!s_serverStarted)
            {
                std::thread([]()
                {
                    try { GameServer srv(NET_PORT); srv.run(); }
                    catch (const std::exception& ex)
                    { std::cerr << "[Server] Fatal: " << ex.what() << "\n"; }
                }).detach();
                s_serverStarted = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            }
            serverIp   = "127.0.0.1";
            serverPort = NET_PORT;
            std::cout << "[Launcher] Server started, joining as client...\n";
        }

        // Run game — when player leaves or gets disconnected, run() returns
        try
        {
            std::cout << "[Launcher] Connecting to "
                      << serverIp << ":" << serverPort
                      << " as " << username << "\n";
            GameClient cli(serverIp, serverPort, username);
            cli.run();
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[Client] Fatal: " << ex.what() << "\n";
            return 1;
        }

        // Small pause so the old window fully closes before re-opening menu
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        // Loop - re-open menu window
    }
}
