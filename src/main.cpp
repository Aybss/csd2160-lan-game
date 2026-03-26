// main.cpp  -  GUI launcher for TankArena  (SFML 3 API)
// Flow:
//   [Username screen] -> [Create Game | Join Game]
//   Create Game: detach GameServer thread on localhost, then run GameClient
//   Join Game:   broadcast LAN scan, show server list, pick one, run GameClient
//
// No stdin prompts at all. Terminal window is fine (shows server logs).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SFML/Graphics.hpp>
#include <sodium.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "GameServer.h"
#include "GameClient.h"
#include "Network.h"
#include "Common.h"
#include "Persistence.h"


// Global font

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


// Layout constants & palette

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


static sf::View g_menuView;

static void updateMenuView(sf::RenderWindow& w)
{
    float winW = (float)w.getSize().x;
    float winH = (float)w.getSize().y;
    float logW = (float)MW, logH = (float)MH;
    float winAR = winW / winH, logAR = logW / logH;
    sf::FloatRect vp;
    if (winAR > logAR)
    {
        float scale = winH / logH;
        float vpW = (logW * scale) / winW;
        vp = sf::FloatRect(sf::Vector2f((1.f - vpW) * 0.5f, 0.f), sf::Vector2f(vpW, 1.f));
    }
    else
    {
        float scale = winW / logW;
        float vpH = (logH * scale) / winH;
        vp = sf::FloatRect(sf::Vector2f(0.f, (1.f - vpH) * 0.5f), sf::Vector2f(1.f, vpH));
    }
    g_menuView = sf::View(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(logW, logH)));
    g_menuView.setViewport(vp);
    w.setView(g_menuView);
}

// Draw helpers

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
    sf::Vector2f mp = w.mapPixelToCoords(sf::Mouse::getPosition(w));
    return mp.x >= x && mp.x <= x + bw &&
        mp.y >= y && mp.y <= y + bh;
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


// Screen 1 – Mode select

static AuthMode screenModeSelect(sf::RenderWindow& w)
{
    sf::Clock clk;
    bool lmbWasDown = true;

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { w.close(); return AuthMode::ANONYMOUS; }
            if (const auto* rs = ev->getIf<sf::Event::Resized>()) updateMenuView(w);
            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                if (kp->code == sf::Keyboard::Key::Escape) { w.close(); return AuthMode::ANONYMOUS; }
        }

        float ta     = clk.getElapsedTime().asSeconds() * 18.f;
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float cW=420.f, cX=(MW-cW)*0.5f, cY=185.f;
        float bW=cW-44.f, bH=70.f, bX=cX+22.f;
        float b1Y=cY+26.f, b2Y=b1Y+bH+8.f, b3Y=b2Y+bH+8.f;
        float panelH=b3Y+bH+22.f-cY;

        bool h1 = isHov(w, bX, b1Y, bW, bH);
        bool h2 = isHov(w, bX, b2Y, bW, bH);
        bool h3 = isHov(w, bX, b3Y, bW, bH);

        if (lmbFire)
        {
            if (h1) return AuthMode::LOGIN;
            if (h2) return AuthMode::REGISTER;
            if (h3) return AuthMode::ANONYMOUS;
        }

        w.clear(BG_DARK); updateMenuView(w); drawGrid(w);
        drawMiniTank(w, 70.f,      70.f,       {0,200,140,55},  ta,        0.45f);
        drawMiniTank(w, MW-70.f,   70.f,       {30,160,255,55}, -ta,       0.45f);
        drawMiniTank(w, 70.f,      MH-70.f,    {230,180,40,55},  ta*0.7f,  0.35f);
        drawMiniTank(w, MW-70.f,   MH-70.f,    {0,200,140,55},  -ta*0.8f,  0.35f);
        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            drawRect(w, cX, cY, cW, panelH, BG_PANEL, PANEL_BRD, 1.5f);
            auto sub = mkT("How do you want to play?", 14, TXT_DIM);
            centerX(sub, cX, cW); sub.setPosition({sub.getPosition().x, cY+8.f}); w.draw(sub);

            // Login
            drawRect(w, bX, b1Y, bW, bH, h1 ? sf::Color{0,175,115,255} : sf::Color{22,30,46,255}, PANEL_BRD, 1.5f);
            { auto l = mkT("Login", 22, h1 ? sf::Color::Black : ACCENT); l.setStyle(sf::Text::Bold);
              centerX(l, bX, bW); l.setPosition({l.getPosition().x, b1Y+8.f}); w.draw(l);
              auto s = mkT("Continue with your saved account", 13, h1 ? sf::Color{40,40,40,255} : TXT_DIM);
              centerX(s, bX, bW); s.setPosition({s.getPosition().x, b1Y+38.f}); w.draw(s); }

            // Register
            drawRect(w, bX, b2Y, bW, bH, h2 ? sf::Color{0,120,200,255} : sf::Color{22,30,46,255}, PANEL_BRD, 1.5f);
            { auto l = mkT("Register", 22, h2 ? sf::Color::Black : ACCENT2); l.setStyle(sf::Text::Bold);
              centerX(l, bX, bW); l.setPosition({l.getPosition().x, b2Y+8.f}); w.draw(l);
              auto s = mkT("Create a new account", 13, h2 ? sf::Color{40,40,40,255} : TXT_DIM);
              centerX(s, bX, bW); s.setPosition({s.getPosition().x, b2Y+38.f}); w.draw(s); }

            // Guest
            drawRect(w, bX, b3Y, bW, bH, h3 ? sf::Color{60,65,85,255} : sf::Color{18,22,34,255}, PANEL_BRD, 1.5f);
            { auto l = mkT("Play as Guest", 22, h3 ? sf::Color::White : TXT_DIM);
              centerX(l, bX, bW); l.setPosition({l.getPosition().x, b3Y+8.f}); w.draw(l);
              auto s = mkT("No account needed  \xc2\xb7  stats not saved", 13, TXT_DIM);
              centerX(s, bX, bW); s.setPosition({s.getPosition().x, b3Y+38.f}); w.draw(s); }
        }
        w.display();
    }
    return AuthMode::ANONYMOUS;
}

// Shared draw helpers for the two-field auth sub-screens (Login / Register / Guest)
// Draws a single input field and returns the updated string (not used directly;
// logic is inlined below to keep each screen self-contained).

// Local-only password check (runs in a thread to keep UI live during Argon2id).
// Returns "" if credentials are valid (or if no local record — server will decide),
// or an error message string if the password is definitely wrong.
static std::string localCheckLogin(const std::string& name, const std::string& password)
{
    Persistence db;
    if (!db.load()) return ""; // no local db — let the server handle it
    auto* rec = db.find(name);
    if (!rec || rec->authKey.empty()) return "Not registered. Use Register.";
    if (rec->salt.empty()) return ""; // malformed record — let server handle

    uint8_t saltBin[crypto_pwhash_SALTBYTES]{};
    size_t decoded = 0;
    if (sodium_hex2bin(saltBin, sizeof(saltBin),
                       rec->salt.c_str(), rec->salt.size(),
                       nullptr, &decoded, nullptr) != 0 ||
        decoded != crypto_pwhash_SALTBYTES)
        return ""; // bad salt — let server handle

    uint8_t derived[32]{};
    if (crypto_pwhash(derived, 32,
                      password.c_str(), password.size(),
                      saltBin,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) != 0)
        return ""; // OOM — let server handle

    std::string derivedHex = Persistence::bytesToHex(derived, 32);
    sodium_memzero(derived, sizeof(derived));
    return (derivedHex == rec->authKey) ? "" : "Incorrect password.";
}

// Screen 2a – Login

static AuthInfo screenLoginForm(sf::RenderWindow& w, std::string& username,
                                const std::string& prefillError = "")
{
    std::string name     = username;
    std::string password;
    std::string errorMsg = prefillError;
    bool        nameFocus = true;
    sf::Clock   clk;
    bool        lmbWasDown = true;
    bool        checking   = false;
    std::future<std::string> checkFuture;

    auto launchCheck = [&]() {
        checking = true;
        std::string n = name, p = password;
        checkFuture = std::async(std::launch::async, localCheckLogin, n, p);
    };

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { w.close(); return {}; }
            if (const auto* rs = ev->getIf<sf::Event::Resized>()) updateMenuView(w);
            if (!checking)
            {
                if (const auto* te = ev->getIf<sf::Event::TextEntered>())
                {
                    uint32_t c = te->unicode;
                    if (nameFocus) {
                        if (c == 8 && !name.empty())     { name.pop_back(); errorMsg.clear(); }
                        else if (c >= 32 && c < 127 && (int)name.size() < 15) { name += (char)c; errorMsg.clear(); }
                    } else {
                        if (c == 8 && !password.empty()) { password.pop_back(); errorMsg.clear(); }
                        else if (c >= 32 && c < 127 && password.size() < 64) { password += (char)c; errorMsg.clear(); }
                    }
                }
                if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                {
                    if (kp->code == sf::Keyboard::Key::Tab) { nameFocus = !nameFocus; }
                    if (kp->code == sf::Keyboard::Key::Enter)
                    {
                        if (name.empty())          { errorMsg = "Enter your name.";     nameFocus = true; }
                        else if (password.empty()) { errorMsg = "Enter your password."; nameFocus = false; }
                        else                       { launchCheck(); }
                    }
                    if (kp->code == sf::Keyboard::Key::Escape) { username.clear(); return {}; }
                }
            }
        }

        // Poll async credential check
        if (checking && checkFuture.valid())
        {
            if (checkFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                std::string result = checkFuture.get();
                checking = false;
                if (!result.empty()) { errorMsg = result; }
                else { username = name; return AuthInfo{AuthMode::LOGIN, password}; }
            }
        }

        float elapsed = clk.getElapsedTime().asSeconds();
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float cW=420.f, cX=(MW-cW)*0.5f, cY=155.f;
        float ibX=cX+30.f, ibW=cW-60.f, fH=46.f;
        float nfY=cY+46.f, pwY=nfY+fH+20.f;
        float btnY=pwY+fH+14.f, btnH=46.f;
        float bkW=(ibW-10.f)*0.32f, loginW=ibW-bkW-10.f;
        float panelH=btnY+btnH+14.f-cY;

        bool hNf = isHov(w, ibX, nfY, ibW, fH);
        bool hPw = isHov(w, ibX, pwY, ibW, fH);
        bool hBk = isHov(w, ibX, btnY, bkW, btnH);
        bool hCf = isHov(w, ibX+bkW+10.f, btnY, loginW, btnH);
        bool canSubmit = !name.empty() && !password.empty();

        if (lmbFire && !checking)
        {
            if (hNf) { nameFocus = true;  errorMsg.clear(); }
            if (hPw) { nameFocus = false; errorMsg.clear(); }
            if (hBk) { username.clear(); return {}; }
            if (hCf && canSubmit) { launchCheck(); }
        }

        w.clear(BG_DARK); updateMenuView(w); drawGrid(w);
        float ta = elapsed * 18.f;
        drawMiniTank(w, 70.f,      70.f,       {0,200,140,55},  ta,        0.45f);
        drawMiniTank(w, MW-70.f,   70.f,       {30,160,255,55}, -ta,       0.45f);
        drawMiniTank(w, 70.f,      MH-70.f,    {230,180,40,55},  ta*0.7f,  0.35f);
        drawMiniTank(w, MW-70.f,   MH-70.f,    {0,200,140,55},  -ta*0.8f,  0.35f);
        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            drawRect(w, cX, cY, cW, panelH, BG_PANEL, PANEL_BRD, 1.5f);
            auto hdr = mkT("Login", 20, TXT_MAIN); hdr.setStyle(sf::Text::Bold);
            centerX(hdr, cX, cW); hdr.setPosition({hdr.getPosition().x, cY+12.f}); w.draw(hdr);

            // Name field
            auto nl = mkT("Name", 13, TXT_DIM); nl.setPosition({ibX, nfY-18.f}); w.draw(nl);
            drawRect(w, ibX, nfY, ibW, fH, {14,18,28,255}, nameFocus ? ACCENT : PANEL_BRD, nameFocus ? 2.f : 1.5f);
            auto nT = mkT(name.empty() ? "Your name..." : name, 22, name.empty() ? TXT_DIM : sf::Color::White);
            nT.setPosition({ibX+12.f, nfY+9.f}); w.draw(nT);
            if (nameFocus && !name.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = nT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(ACCENT);
                cur.setPosition({ibX+12.f+b.size.x+3.f, nfY+11.f}); w.draw(cur);
            }
            auto cc = mkT(std::to_string((int)name.size())+"/15", 12, TXT_DIM);
            cc.setPosition({ibX+ibW-34.f, nfY+fH+2.f}); w.draw(cc);

            // Password field
            bool pwSel = !nameFocus;
            auto pl = mkT("Password", 13, TXT_DIM); pl.setPosition({ibX, pwY-18.f}); w.draw(pl);
            drawRect(w, ibX, pwY, ibW, fH, {14,18,28,255}, pwSel ? ACCENT2 : PANEL_BRD, pwSel ? 2.f : 1.5f);
            std::string masked(password.size(), '*');
            auto pwT = mkT(password.empty() ? "Password..." : masked, 22, password.empty() ? TXT_DIM : sf::Color::White);
            pwT.setPosition({ibX+12.f, pwY+9.f}); w.draw(pwT);
            if (pwSel && !password.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = pwT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(ACCENT2);
                cur.setPosition({ibX+12.f+b.size.x+3.f, pwY+11.f}); w.draw(cur);
            }

            // Buttons
            drawButton(w, ibX, btnY, bkW, btnH, "Back", hBk && !checking, {22,30,46,255}, {60,70,90,255});
            if (checking) {
                // Spinner overlay on the login button while Argon2id runs
                drawRect(w, ibX+bkW+10.f, btnY, loginW, btnH, sf::Color{14,22,38,255}, ACCENT, 2.f);
                float dots = std::fmod(elapsed * 3.f, 4.f);
                std::string spin = "Verifying" + std::string((int)dots, '.');
                auto vt = mkT(spin, 18, ACCENT);
                centerX(vt, ibX+bkW+10.f, loginW); vt.setPosition({vt.getPosition().x, btnY+13.f}); w.draw(vt);
            } else {
                drawButton(w, ibX+bkW+10.f, btnY, loginW, btnH, "LOGIN   Enter", hCf && canSubmit,
                           canSubmit ? sf::Color{24,34,50,255} : sf::Color{14,18,28,255}, sf::Color{0,200,140,255});
            }
            auto hint = mkT("Tab to switch fields  \xc2\xb7  Esc = back", 12, TXT_DIM);
            centerX(hint, cX, cW); hint.setPosition({hint.getPosition().x, btnY+btnH+4.f}); w.draw(hint);

            if (!errorMsg.empty()) {
                auto et = mkT(errorMsg, 13, ERR_COL);
                et.setPosition({ibX, cY+panelH+6.f}); w.draw(et);
            }
        }
        w.display();
    }
    return {};
}

// Screen 2b – Register

static AuthInfo screenRegisterForm(sf::RenderWindow& w, std::string& username,
                                   const std::string& prefillError = "")
{
    std::string name     = username;
    std::string password;
    std::string confirm;
    std::string errorMsg = prefillError;
    int         focus    = 0; // 0=name, 1=password, 2=confirm
    sf::Clock   clk;
    bool        lmbWasDown = true;

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { w.close(); return {}; }
            if (const auto* rs = ev->getIf<sf::Event::Resized>()) updateMenuView(w);
            if (const auto* te = ev->getIf<sf::Event::TextEntered>())
            {
                uint32_t c = te->unicode;
                std::string* tgt = (focus==0) ? &name : (focus==1) ? &password : &confirm;
                size_t maxLen    = (focus==0) ? 15 : 64;
                if (c == 8 && !tgt->empty())                              { tgt->pop_back(); errorMsg.clear(); }
                else if (c >= 32 && c < 127 && (int)tgt->size() < (int)maxLen) { *tgt += (char)c; errorMsg.clear(); }
            }
            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
            {
                if (kp->code == sf::Keyboard::Key::Tab) { focus = (focus+1)%3; }
                if (kp->code == sf::Keyboard::Key::Enter)
                {
                    if (name.empty())          { errorMsg = "Enter a name.";              focus = 0; }
                    else if (password.empty()) { errorMsg = "Enter a password.";          focus = 1; }
                    else if (confirm.empty())  { errorMsg = "Confirm your password.";     focus = 2; }
                    else if (password != confirm) { errorMsg = "Passwords don't match.";  focus = 2; confirm.clear(); }
                    else { username = name; return AuthInfo{AuthMode::REGISTER, password}; }
                }
                if (kp->code == sf::Keyboard::Key::Escape) { username.clear(); return {}; }
            }
        }

        float elapsed = clk.getElapsedTime().asSeconds();
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float cW=420.f, cX=(MW-cW)*0.5f, cY=130.f;
        float ibX=cX+30.f, ibW=cW-60.f, fH=46.f;
        float nfY=cY+46.f, pwY=nfY+fH+20.f, cfY2=pwY+fH+20.f;
        float btnY=cfY2+fH+14.f, btnH=46.f;
        float bkW=(ibW-10.f)*0.32f, regW=ibW-bkW-10.f;
        float panelH=btnY+btnH+14.f-cY;

        bool hNf  = isHov(w, ibX, nfY, ibW, fH);
        bool hPw  = isHov(w, ibX, pwY, ibW, fH);
        bool hCf2 = isHov(w, ibX, cfY2, ibW, fH);
        bool hBk  = isHov(w, ibX, btnY, bkW, btnH);
        bool hReg = isHov(w, ibX+bkW+10.f, btnY, regW, btnH);
        bool canSubmit = !name.empty() && !password.empty() && !confirm.empty();

        if (lmbFire)
        {
            if (hNf)  { focus = 0; errorMsg.clear(); }
            if (hPw)  { focus = 1; errorMsg.clear(); }
            if (hCf2) { focus = 2; errorMsg.clear(); }
            if (hBk)  { username.clear(); return {}; }
            if (hReg && canSubmit)
            {
                if (password != confirm) { errorMsg = "Passwords don't match."; focus = 2; confirm.clear(); }
                else { username = name; return AuthInfo{AuthMode::REGISTER, password}; }
            }
        }

        w.clear(BG_DARK); updateMenuView(w); drawGrid(w);
        float ta = elapsed * 18.f;
        drawMiniTank(w, 70.f,      70.f,       {0,200,140,55},  ta,        0.45f);
        drawMiniTank(w, MW-70.f,   70.f,       {30,160,255,55}, -ta,       0.45f);
        drawMiniTank(w, 70.f,      MH-70.f,    {230,180,40,55},  ta*0.7f,  0.35f);
        drawMiniTank(w, MW-70.f,   MH-70.f,    {0,200,140,55},  -ta*0.8f,  0.35f);
        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            drawRect(w, cX, cY, cW, panelH, BG_PANEL, PANEL_BRD, 1.5f);
            auto hdr = mkT("Create Account", 20, TXT_MAIN); hdr.setStyle(sf::Text::Bold);
            centerX(hdr, cX, cW); hdr.setPosition({hdr.getPosition().x, cY+12.f}); w.draw(hdr);

            // Name
            auto nl = mkT("Name", 13, TXT_DIM); nl.setPosition({ibX, nfY-18.f}); w.draw(nl);
            drawRect(w, ibX, nfY, ibW, fH, {14,18,28,255}, focus==0 ? ACCENT : PANEL_BRD, focus==0 ? 2.f : 1.5f);
            auto nT = mkT(name.empty() ? "Choose a name..." : name, 22, name.empty() ? TXT_DIM : sf::Color::White);
            nT.setPosition({ibX+12.f, nfY+9.f}); w.draw(nT);
            if (focus==0 && !name.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = nT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(ACCENT);
                cur.setPosition({ibX+12.f+b.size.x+3.f, nfY+11.f}); w.draw(cur);
            }
            auto cc = mkT(std::to_string((int)name.size())+"/15", 12, TXT_DIM);
            cc.setPosition({ibX+ibW-34.f, nfY+fH+2.f}); w.draw(cc);

            // Password
            auto pl = mkT("Password", 13, TXT_DIM); pl.setPosition({ibX, pwY-18.f}); w.draw(pl);
            drawRect(w, ibX, pwY, ibW, fH, {14,18,28,255}, focus==1 ? ACCENT2 : PANEL_BRD, focus==1 ? 2.f : 1.5f);
            std::string mp(password.size(), '*');
            auto pwT = mkT(password.empty() ? "Password..." : mp, 22, password.empty() ? TXT_DIM : sf::Color::White);
            pwT.setPosition({ibX+12.f, pwY+9.f}); w.draw(pwT);
            if (focus==1 && !password.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = pwT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(ACCENT2);
                cur.setPosition({ibX+12.f+b.size.x+3.f, pwY+11.f}); w.draw(cur);
            }

            // Confirm
            sf::Color cfAccent = (password == confirm && !confirm.empty()) ? ACCENT : ACCENT2;
            auto cl = mkT("Confirm Password", 13, TXT_DIM); cl.setPosition({ibX, cfY2-18.f}); w.draw(cl);
            drawRect(w, ibX, cfY2, ibW, fH, {14,18,28,255}, focus==2 ? cfAccent : PANEL_BRD, focus==2 ? 2.f : 1.5f);
            std::string mc(confirm.size(), '*');
            auto cfT = mkT(confirm.empty() ? "Repeat password..." : mc, 22, confirm.empty() ? TXT_DIM : sf::Color::White);
            cfT.setPosition({ibX+12.f, cfY2+9.f}); w.draw(cfT);
            if (focus==2 && !confirm.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = cfT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(cfAccent);
                cur.setPosition({ibX+12.f+b.size.x+3.f, cfY2+11.f}); w.draw(cur);
            }

            // Buttons
            drawButton(w, ibX, btnY, bkW, btnH, "Back", hBk, {22,30,46,255}, {60,70,90,255});
            drawButton(w, ibX+bkW+10.f, btnY, regW, btnH, "REGISTER   Enter", hReg && canSubmit,
                       canSubmit ? sf::Color{24,34,50,255} : sf::Color{14,18,28,255}, sf::Color{0,140,220,255});
            auto hint = mkT("Tab to cycle fields  \xc2\xb7  Esc = back", 12, TXT_DIM);
            centerX(hint, cX, cW); hint.setPosition({hint.getPosition().x, btnY+btnH+4.f}); w.draw(hint);

            if (!errorMsg.empty()) {
                auto et = mkT(errorMsg, 13, ERR_COL);
                et.setPosition({ibX, cY+panelH+6.f}); w.draw(et);
            }
        }
        w.display();
    }
    return {};
}

// Screen 2c – Guest

static AuthInfo screenGuestForm(sf::RenderWindow& w, std::string& username,
                                const std::string& prefillError = "")
{
    std::string name     = username;
    std::string errorMsg = prefillError;
    sf::Clock   clk;
    bool        lmbWasDown = true;

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) { w.close(); return {}; }
            if (const auto* rs = ev->getIf<sf::Event::Resized>()) updateMenuView(w);
            if (const auto* te = ev->getIf<sf::Event::TextEntered>())
            {
                uint32_t c = te->unicode;
                if (c == 8 && !name.empty())     { name.pop_back(); errorMsg.clear(); }
                else if (c >= 32 && c < 127 && (int)name.size() < 15) { name += (char)c; errorMsg.clear(); }
            }
            if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
            {
                if (kp->code == sf::Keyboard::Key::Enter)
                {
                    if (name.empty()) { errorMsg = "Enter a name first."; }
                    else { username = name; return AuthInfo{AuthMode::ANONYMOUS, ""}; }
                }
                if (kp->code == sf::Keyboard::Key::Escape) { username.clear(); return {}; }
            }
        }

        float elapsed = clk.getElapsedTime().asSeconds();
        bool lmbNow  = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        bool lmbFire = lmbNow && !lmbWasDown;
        lmbWasDown   = lmbNow;

        float cW=420.f, cX=(MW-cW)*0.5f, cY=180.f;
        float ibX=cX+30.f, ibW=cW-60.f, fH=46.f;
        float nfY=cY+46.f, warnY=nfY+fH+8.f;
        float btnY=warnY+22.f+10.f, btnH=46.f;
        float bkW=(ibW-10.f)*0.32f, playW=ibW-bkW-10.f;
        float panelH=btnY+btnH+14.f-cY;

        bool hNf  = isHov(w, ibX, nfY, ibW, fH);
        bool hBk  = isHov(w, ibX, btnY, bkW, btnH);
        bool hPlay = isHov(w, ibX+bkW+10.f, btnY, playW, btnH);
        bool canSubmit = !name.empty();

        if (lmbFire)
        {
            if (hNf) errorMsg.clear();
            if (hBk) { username.clear(); return {}; }
            if (hPlay && canSubmit) { username = name; return AuthInfo{AuthMode::ANONYMOUS, ""}; }
        }

        w.clear(BG_DARK); updateMenuView(w); drawGrid(w);
        float ta = elapsed * 18.f;
        drawMiniTank(w, 70.f,      70.f,       {0,200,140,55},  ta,        0.45f);
        drawMiniTank(w, MW-70.f,   70.f,       {30,160,255,55}, -ta,       0.45f);
        drawMiniTank(w, 70.f,      MH-70.f,    {230,180,40,55},  ta*0.7f,  0.35f);
        drawMiniTank(w, MW-70.f,   MH-70.f,    {0,200,140,55},  -ta*0.8f,  0.35f);
        drawTitleBar(w, 38.f);

        if (g_fontLoaded)
        {
            drawRect(w, cX, cY, cW, panelH, BG_PANEL, PANEL_BRD, 1.5f);
            auto hdr = mkT("Play as Guest", 20, TXT_MAIN); hdr.setStyle(sf::Text::Bold);
            centerX(hdr, cX, cW); hdr.setPosition({hdr.getPosition().x, cY+12.f}); w.draw(hdr);

            // Name field
            auto nl = mkT("Name", 13, TXT_DIM); nl.setPosition({ibX, nfY-18.f}); w.draw(nl);
            drawRect(w, ibX, nfY, ibW, fH, {14,18,28,255}, ACCENT, 2.f);
            auto nT = mkT(name.empty() ? "Enter a call sign..." : name, 22, name.empty() ? TXT_DIM : sf::Color::White);
            nT.setPosition({ibX+12.f, nfY+9.f}); w.draw(nT);
            if (!name.empty() && (int)(elapsed*2.2f)%2==0) {
                auto b = nT.getLocalBounds(); sf::RectangleShape cur({2.f,22.f}); cur.setFillColor(ACCENT);
                cur.setPosition({ibX+12.f+b.size.x+3.f, nfY+11.f}); w.draw(cur);
            }
            auto cc = mkT(std::to_string((int)name.size())+"/15", 12, TXT_DIM);
            cc.setPosition({ibX+ibW-34.f, nfY+fH+2.f}); w.draw(cc);

            // Warning line
            auto warn = mkT("Stats won't be saved for guest sessions.", 13, sf::Color{230,180,40,200});
            centerX(warn, cX, cW); warn.setPosition({warn.getPosition().x, warnY+2.f}); w.draw(warn);

            // Buttons
            drawButton(w, ibX, btnY, bkW, btnH, "Back", hBk, {22,30,46,255}, {60,70,90,255});
            drawButton(w, ibX+bkW+10.f, btnY, playW, btnH, "PLAY   Enter", hPlay && canSubmit,
                       canSubmit ? sf::Color{24,34,50,255} : sf::Color{14,18,28,255}, sf::Color{0,200,140,255});
            auto hint = mkT("Esc = back", 12, TXT_DIM);
            centerX(hint, cX, cW); hint.setPosition({hint.getPosition().x, btnY+btnH+4.f}); w.draw(hint);

            if (!errorMsg.empty()) {
                auto et = mkT(errorMsg, 13, ERR_COL);
                et.setPosition({ibX, cY+panelH+6.f}); w.draw(et);
            }
        }
        w.display();
    }
    return {};
}


// Screen 2 – Create or Join

enum class MenuChoice { NONE, CREATE, JOIN };

static MenuChoice screenMainMenu(sf::RenderWindow& w, const std::string& username)
{
    sf::Clock clk;
    // Start as true so we don't register a carry-over click from the previous screen
    bool lmbWasDown = true;

    while (w.isOpen())
    {
        while (const auto ev = w.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) { w.close(); return MenuChoice::NONE; }

            if (const auto* rs = ev->getIf<sf::Event::Resized>())
                updateMenuView(w);
        }

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
        updateMenuView(w);
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


// Screen 3 – LAN Server Browser

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
            
            if (const auto* rs = ev->getIf<sf::Event::Resized>())
                updateMenuView(w);

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
            auto mp = w.mapPixelToCoords(sf::Mouse::getPosition(w));
            if (mp.x >= LIST_X && mp.x <= LIST_X+LIST_W &&
                mp.y >= LIST_Y  && mp.y <= LIST_Y+LIST_H)
            {
                int row = (int)((mp.y - LIST_Y + scrollY) / ROW_H);
                if (row >= 0 && row < (int)servers.size()) sel = row;
            }
        }

        //  Draw 
        w.clear(BG_DARK);
        updateMenuView(w);
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


// main
//int main(int argc, char* argv[]){
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
    loadFont();
    if (sodium_init() < 0) return 1;   // libsodium must init before any crypto

    // Create the window ONCE at the start
    sf::RenderWindow window(
        sf::VideoMode({ (unsigned)MW, (unsigned)MH }),
        "TankArena",
        sf::Style::Default);          // Default = Titlebar | Resize | Close
    window.setFramerateLimit(60);
    window.setMinimumSize(sf::Vector2u((unsigned)MW / 2, (unsigned)MH / 2));

    std::string username;
    std::string loginErrMsg;
    AuthMode    lastMode = AuthMode::LOGIN;
    bool        retrying = false;

    while (window.isOpen())
    {
        if (!retrying)
            lastMode = screenModeSelect(window);
        retrying = false;
        if (!window.isOpen()) break;

        AuthInfo auth;
        if (lastMode == AuthMode::LOGIN)
            auth = screenLoginForm(window, username, loginErrMsg);
        else if (lastMode == AuthMode::REGISTER)
            auth = screenRegisterForm(window, username, loginErrMsg);
        else
            auth = screenGuestForm(window, username, loginErrMsg);
        loginErrMsg.clear();

        if (username.empty() || !window.isOpen()) continue;  // Back → mode select
        if ((int)username.size() > 15) username = username.substr(0, 15);

        MenuChoice choice = screenMainMenu(window, username);

        if (!window.isOpen() || choice == MenuChoice::NONE) break;

        std::string serverIp = "127.0.0.1";
        uint16_t serverPort = NET_PORT;

        if (choice == MenuChoice::JOIN)
        {
            auto [ip, port] = screenBrowser(window, username);
            if (!window.isOpen()) break;
            if (ip.empty()) continue;
            serverIp = ip;
            serverPort = port;
        }
        else // CREATE
        {
            static bool s_serverStarted = false;
            if (!s_serverStarted)
            {
                std::thread([]() {
                    try { GameServer srv(NET_PORT); srv.run(); }
                    catch (...) { /* handle error */ }
                    }).detach();
                s_serverStarted = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            }
        }

        // Run GameClient using the SAME window
        try {
            GameClient cli(serverIp, serverPort, username, auth);
            cli.run(window); // Pass the window reference here
            if (cli.getAuthFailed()) { loginErrMsg = cli.getAuthError(); retrying = true; }
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
    return 0;
}
