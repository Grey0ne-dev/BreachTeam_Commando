# Breach Team P2P Clone (Linux-first)

This repository contains the first milestone of a breach-team-inspired arena architecture:
- deterministic simulation scaffold (fixed-point math + fixed timestep),
- host migration candidate queue (`k = ceil(log2(n))`),
- deterministic packet protocol for P2P control/data messages,
- epoch-based host migration state manager,
- Linux-native CMake build and Docker packaging.

## Build locally (Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/breach_team
```

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
- `src`: core implementations and startup binary
- `tests`: queue behavior test

## Next milestone

- Add ENet-based peer transport and bootstrap signaling handshake using `breach_team::net::protocol`.
- Move simulation to a sector/wall map pipeline.
- Add state checkpoint + epoch handover for host migration.
