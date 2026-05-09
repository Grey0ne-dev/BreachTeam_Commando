# Breach Team P2P Clone (Linux-first)

This repository contains the first milestone of a breach-team-inspired arena architecture:
- deterministic simulation scaffold (fixed-point math + fixed timestep),
- host migration candidate queue (`k = ceil(log2(n))`),
- deterministic packet protocol for P2P control/data messages,
- epoch-based host migration state manager,
- ENet-ready transport scaffold and one-shot bootstrap signaling client contract,
- input-frame-to-tick deterministic pipeline wiring with host-authoritative conflict resolution,
- Linux-native CMake build and Docker packaging.

## Build locally (Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/breach_team
```

### Optional SDL2 renderer

If `sdl2` is installed (`pkg-config --modversion sdl2`), CMake auto-enables SDL2 mode.

Run SDL2 client:

```bash
./build/breach_team --sdl
```

SDL2 mode now shows the same startup menu:
- `Solo Play`
- `Host Game`
- `Join Game`

## Playable mode

`./build/breach_team` now starts a terminal-playable first-person ASCII raycaster.

Startup menu modes:
- `Solo Play`
- `Host Game` (starts local session with host label)
- `Join Game` (starts local session with join label)

Controls:
- `W S`: move forward/backward
- `A D`: rotate left/right
- `Space`: shoot
- `Q`: quit

Legend:
- 3D view uses distance-shaded wall glyphs and sprite enemies (`M`/`w`)
- minimap matrix (top-left): `@` player, `e` enemies, `#` walls
- weapon sprite is rendered at bottom-center with a center crosshair

## Using textures (SDL2 path)

Current SDL2 renderer uses flat colors. To use textures:
1. Add a `assets/textures/` folder (e.g. `wall.png`, `floor.png`, `enemy.png`, `gun.png`).
2. Install SDL image loader:
   ```bash
   sudo apt install libsdl2-image-dev
   ```
3. In CMake, link SDL2_image and define a compile flag for texture mode.
4. In `src/sdl_game.cpp`, load textures once at startup (`IMG_Load` + `SDL_CreateTextureFromSurface`).
5. Replace per-column flat wall draw with texture sampling using wall hit coordinate from DDA.
6. Render enemy/gun as textured sprites instead of solid rectangles.

If you want, next step I can wire full wall + sprite texture sampling directly into your SDL raycaster.

## Build/run in Docker

```bash
docker build -f docker/Dockerfile -t breach-team:dev .
docker run --rm breach-team:dev
```

## Current structure

- `include/breach_team/core`: fixed-point and vector/matrix primitives
- `include/breach_team/game`: deterministic game session API
- `include/breach_team/net`: host-candidate queue API
- `include/breach_team/net/protocol.hpp`: packet formats + binary serialization
- `include/breach_team/net/migration_manager.hpp`: host migration state machine
- `include/breach_team/net/transport.hpp`: transport abstraction
- `include/breach_team/net/enet_transport.hpp`: ENet integration scaffold
- `include/breach_team/net/bootstrap_client.hpp`: one-shot signaling contact API
- `include/breach_team/net/input_pipeline.hpp`: input packet to deterministic tick mapping
- `src`: core implementations and startup binary
- `tests`: queue behavior test

## Next milestone

- Replace transport scaffold internals with real ENet socket/event integration.
- Implement live bootstrap HTTP/WebSocket signaling backend integration (current client uses `curl` with line payload format).
- Move simulation to a sector/wall map pipeline.
- Add state checkpoint + epoch handover for host migration.
