# TankNet

Top-down multiplayer tank arena over UDP (LAN), built with C++17, SFML 3, and Winsock2.
Free-for-all: last tank standing wins rounds. First to 3 round wins takes the match.

---

## Prerequisites

| Tool | Install |
|---|---|
| Visual Studio 2022 | "Desktop development with C++" workload |
| CMake 3.20+ | `winget install Kitware.CMake` |
| vcpkg | See below |

---

## 1 – Install vcpkg (one time)

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

## 2 – Build

SFML is declared in `vcpkg.json` and downloaded automatically - no manual `vcpkg install` needed.

```powershell
cd TankNet
cmake --preset windows-x64-release
cmake --build build --preset release
```

Exe lands in `build/Release/TankNet.exe`. `server.cfg` and `assets/` are copied automatically.

---

## 4 – Running

### Same machine (testing)
```
cd build/Release
run.bat
```
Opens one server window and two client windows automatically.

### LAN (different machines)
**Server machine:**
```
TankNet.exe server
```

**Client machines** - edit `server.cfg`, set `SERVER_IP` to the server's LAN IP, then:
```
TankNet.exe client YourName
```
Or pass everything on command line:
```
TankNet.exe client YourName 192.168.1.42
```

---

## Controls

| Key | Action |
|---|---|
| W / S | Move forward / back |
| A / D | Rotate left / right |
| Space | Fire |
| R | Toggle ready (lobby) |
| O | Open skin shop (lobby) |
| Enter | Open / send chat |
| Esc | Close chat / back |

---

## Game Flow

1. Players connect - **Lobby** screen shows all players, their level and win count
2. Each player presses **R** to ready up
3. Once all players are ready (minimum 2), game starts automatically
4. **Round**: last tank standing wins the round. First to **3 round wins** wins the match
5. **Match Over** screen shows kills, XP/coins gained, and the **global leaderboard**
6. Server returns to lobby; player data persists in `players.json`

---

## Progression & Shop

- **XP**: +50 per kill, +100 for match win - levels up every 200 XP
- **Coins**: +20 per kill, +50 for match win - spend in shop
- **Skins**: 5 tank colours, unlocked in shop (press O in lobby)
  - Green: free (default)
  - Blue: 40 coins
  - Red: 80 coins
  - Gold: 120 coins
  - Purple: 200 coins
- All data saved to `players.json` next to the server exe

---

## Audio 

| File | When played | Credits
|---|---|
| `shoot.wav` | Tank fires | https://freesound.org/people/LittleRobotSoundFactory/sounds/270336/
| `hit.wav` | Tank takes damage | https://freesound.org/people/LittleRobotSoundFactory/sounds/270332/
| `dead.wav` | Tank destroyed | https://freesound.org/people/thehorriblejoke/sounds/259962/
| `powerup.wav` | Background music (loops) | https://freesound.org/people/Prof.Mudkip/sounds/422089/
| `bgm.ogg` | Background music (loops) | https://freesound.org/people/josefpres/sounds/655186/

---

## Network Architecture

- **UDP only** (Winsock2, non-blocking) - no TCP anywhere
- **Client-Server**: server owns all physics, collision, scoring, and persistence
- Clients send `PktInput` every frame; server broadcasts `PktGameState` at 20 Hz
- Packet types: CONNECT - CONNECT_ACK - LOBBY_STATE - GAME_START - INPUT / GAME_STATE / BULLET_SPAWN / PLAYER_HIT / PLAYER_DEAD - ROUND_OVER - MATCH_OVER
- **Disconnect handling**: timed-out clients (8s no packet) are removed gracefully; their tank is killed; other players continue unaffected
- **Out-of-order protection**: `PktGameState` and `PktInput` carry sequence numbers; stale packets are discarded

---

## Project Structure

```
TankNet/
├── CMakeLists.txt
├── CMakePresets.json
├── server.cfg
├── run.bat
├── README.md
├── assets/
│   └── README.txt        (place audio files here)
├── include/
│   ├── Common.h          packets, constants, enums
│   ├── Network.h         UdpSocket, ServerNet, ClientNet
│   ├── Tank.h            tank entity (server-side)
│   ├── Bullet.h          bullet entity (server-side)
│   ├── GameServer.h      authoritative server logic
│   ├── GameClient.h      client + all UI screens
│   └── Persistence.h     JSON player data
│   └── VoiceChat.h       Voice Chat (opus)
└── src/
    ├── main.cpp
    ├── Network.cpp
    ├── Tank.cpp
    ├── Bullet.cpp
    ├── GameServer.cpp
    ├── GameClient.cpp
    └── Persistence.cpp
    └── VoiceChat.cpp

```

---

## Design Report Notes (Assignment_5_Design_Report)

### Game Design
Top-down free-for-all tank arena. 2–6 players on LAN. First to 3 round wins wins the match.
Obstacles randomly generated each match from a shared seed (deterministic across all clients).

### Block Diagram
```
[Client A]──INPUT-->|
[Client B]──INPUT-->|---[Server: physics/collision/scoring/persistence]-->STATE-->[All Clients]
[Client C]──INPUT-->|
```

### Synchronization Strategy
- Server is authoritative for all state
- Clients dead-reckon locally between 20 Hz state broadcasts
- Bullet spawns, hits, deaths sent as discrete reliable-style events (broadcast immediately)
- Map generated deterministically from shared seed - no need to sync obstacle state

### Durable Communication
- Non-blocking UDP with 30-second timeout detection
- Disconnected players removed cleanly; game continues
- Sequence numbers prevent out-of-order state application
