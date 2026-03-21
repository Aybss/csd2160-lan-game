# TankNet

Top-down multiplayer tank arena over UDP (LAN), built with C++17, SFML 3, and Winsock2.
Free-for-all: last tank standing wins rounds. First to 3 round wins takes the match.

---

## Prerequisites

| Tool | Install |
|---|---|
| Visual Studio 2022 | "Desktop development with C++" workload |
| CMake 3.20+ | `winget install Kitware.CMake` |
| vcpkg | Required for **Opus** (Voice) and **SFML 3** dependencies |

---

## 1 - Setup
```
run setup.bat
```

## 2 – Running

### Same machine (testing)
Open at least 2 terminal windows. One can run the server, the others can join.
Otherwise, can start the game with just 1 player, and add bots in the lobby.

## Controls

| Key | Action |
|---|---|
| **W / S** | Move forward / back |
| **A / D** | Rotate left / right |
| **Space** | Fire |
| **R** | Toggle ready (lobby) |
| **B** | Open skin shop (lobby) |
| **P** | Add Bot (Admin/Host only) |
| **Enter** | Open / send chat |
| **Esc** | Close chat / back |
| **V** | Toggle voice chat |

---

## Game Flow

1. **Entry**: Enter username, then join an existing game via the browser or create a new one.
2. **Lobby**: View all players, their levels, and win counts.
3. **Admin Tools**: The Host can press **P** to add bots or click the **X** next to a name to kick players/bots.
4. **Ready Up**: Each player presses **R** to toggle ready status.
5. **Start**: Once all players are ready (minimum 2), the game starts automatically.
6. **Match**: Last tank standing wins the round. First to **3 round wins** takes the match.
7. **Persistence**: Post-match stats show XP/Coins gained; data persists in `players.json`.

---

## Progression & Shop

- **XP**: +50 per kill, +100 for match win. Level up every 200 XP.
- **Coins**: +20 per kill, +50 for match win. Spend in the shop.
- **Skins**: 6 tank colors unlocked in shop (Press **B** in lobby):
  - Army Green: Free (Default)
  - Camo: 40 coins
  - Desert: 80 coins
  - Arctic: 120 coins
  - Stealth: 160 coins
  - Gold: 200 coins

---

## Audio

| File | When played | Credits |
|---|---|---|
| `shoot.wav` | Tank fires | [Link](https://freesound.org/people/LittleRobotSoundFactory/sounds/270336/) |
| `hit.wav` | Tank takes damage | [Link](https://freesound.org/people/LittleRobotSoundFactory/sounds/270332/) |
| `dead.wav` | Tank destroyed | [Link](https://freesound.org/people/thehorriblejoke/sounds/259962/) |
| `powerup.wav` | Powerup collected | [Link](https://freesound.org/people/Prof.Mudkip/sounds/422089/) |
| `bgm.ogg` | Background music | [Link](https://freesound.org/people/josefpres/sounds/655186/) |

---

## Network Architecture

- **UDP Only**: Built on Winsock2 with no-blocking sockets.
- **Authoritative Server**: Server handles physics, collisions, scoring, and A* pathfinding.
- **State Sync**: Server broadcasts game state at 20 Hz; clients use sequence numbers to discard stale packets.
- **Resiliency**: 30-second timeout detection removes disconnected clients gracefully.

## Project Structure

```
TankNet/
├ CMakeLists.txt
├ CMakePresets.json
├ server.cfg
├ run.bat
├ README.md
├ assets/
│   └ README.txt        (place audio files here)
├ include/
│   ├ Common.h          packets, constants, enums
│   ├ Network.h         UdpSocket, ServerNet, ClientNet
│   ├ Tank.h            tank entity (server-side)
│   ├ Bullet.h          bullet entity (server-side)
│   ├ GameServer.h      authoritative server logic
│   ├ GameClient.h      client + all UI screens
│   └ Persistence.h     JSON player data
│   └ VoiceChat.h       Voice Chat (opus)
└ src/
    ├ main.cpp
    ├ Network.cpp
    ├ Tank.cpp
    ├ Bullet.cpp
    ├ GameServer.cpp
    ├ GameClient.cpp
    └ Persistence.cpp
    └ VoiceChat.cpp

```

---

## Design Report Notes (Assignment_5_Design_Report)

### Game Design
Top-down free-for-all tank arena. 2–6 players on LAN. First to 3 round wins wins the match.
Obstacles randomly generated each match from a shared seed (deterministic across all clients).

### Block Diagram
```
[Client A]INPUT-->|
[Client B]INPUT-->|---[Server: physics/collision/scoring/persistence]-->STATE-->[All Clients]
[Client C]INPUT-->|
```
