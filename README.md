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

# Features

| Category | Item | Purpose |
|---|---|---|
| Networking | Winsock2 (UDP) |Raw UDP socket transport for all game packets; TCP is strictly prohibited. |
|   | Non-blocking Sockets | Uses FIONBIO to poll server/client without stalling the game loop. |
|   | UDP Broadcast | LAN discovery where the server announces presence and clients scan for hosts. |
|   | Custom Protocol | Packed structs (#pragma pack) over UDP with a type byte as the first field. |
|   | Sequence Numbers | Input and game-state packets carry seq to reject old/out-of-order data. |
|   | Keepalive / Timeout | Client pings every 5s; server drops silent clients after 90s for graceful disconnects. |
| Rendering | SFML 3 Engine | Window creation, event loop, and draw calls for a polished multiplayer experience. |
|   | sf::View (Letterbox) | Scales logical canvas to any window size with black bars via makeLetterboxView. |
|   | Follow-Cam | Game world view centered on the player tank at 0.55× zoom for immersive play. |
|   | Spectator View | Full-map fit-to-screen view when dead; includes armored screen-space border. |
|   | Procedural Skins | Off-screen sf::RenderTexture generation for tracks, rivets, and camo patterns at startup. |
| Game Logic | Tank Physics | Angular rotation and forward/back movement with AABB obstacle collision. |
|   | AABB Collision | Detection for Tank-Obstacle, Bullet-Obstacle, Bullet-Tank, and Barrel-Bullet. |
|   | Powerup System | Speed (1.8×), Rapid Fire (0.15s CD), and Shield (absorbs one hit) with timed buffs. |
|   | Explosive Barrels | Chain-reaction AoE damage within a specific radius for tactical play.
|   | Round/Match System | First to 3 round wins takes the match; includes clear end-of-game flow. |
| Map Gen | Seeded srand | Same seed on server/client guarantees identical obstacle layouts across all screens. |
|   | Symmetric Placement | Obstacles mirrored across all four quadrants for a balanced competitive layout. |
|   | Classification | Size thresholds distinguish trees, walls, and boulders for unique rendering styles. |
| AI | Seek-and-Shoot FSM | Re-targets nearest human every ~0.5s; handles turning, advancing, ad firing. |
|   | Angle Steering|   | atan2 calculation for heading; handles left/right inputs from angle differences. |
|   | Edge Avoidance|   | Forces a turn+forward command when the bot detects map boundaries. |
Persistence | Hand-rolled JSON | "Flat-file player records including XP, currency, level, and owned skins. |
|   | XP/Coin Awards | Kill and win rewards computed on server and written to players.json |
|   | Leaderboard | Top-5 by total wins sorted on server and sent in PktMatchOver. |
Audio | SFML Audio| Integration of BGM and SFX with volume control. |
|   | Opus Voice Chat | 24kbps compression with discontinuous transmission and jitter buffer. |
| UI | LAN Browser | Broadcasts PktServerQuery and collects PktServerAnnounce replies. |
|   | Kill-Cam / Replay | Circular buffer of 180 snapshots; plays back at 0.5× speed on round end. |
|   | Spectator Cycling | Dead players cycle through living targets via keyboard or < > buttons. |
Scene Management | Persistent Window | Single sf::RenderWindow shared across all screens to prevent flicker. |
|   | Phase State Machine | ClientPhase enum drives which draw/update functions run each frame. |
