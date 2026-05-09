# BreachTeam: Commando 🛸

> *"In the silence of the Void, the Corvette 'Acheron' stopped screaming. We were sent in to find out why. We found something that doesn't belong in this dimension."*

**BreachTeam: Commando** is a Linux-native, sci-fi "boomer shooter" and a technical homage to the **DOOM** engine. You play as a Commando dropped onto an abandoned space corvette that has become a breeding ground for a mysterious, reality-warping lifeform. 💀

Built with a **deterministic simulation scaffold**, this project ensures frame-perfect synchronization in peer-to-peer (P2P) environments through fixed-point math and a rigid input-to-tick pipeline. 🛠️

---

### 🌌 The Lore: The Acheron Incident

The **UEV Acheron** was a research vessel specializing in "Deep Fold" propulsion. Three weeks ago, it reappeared in local orbit—dark, silent, and bleeding strange radiation. 🛰️

As a member of the **BreachTeam**, your mission is simple:

* **Board** the derelict vessel. 🚪
* **Purge** the "Phage"—a hive-mind organism that mimics biological structures. 🔫
* **Survive** the ship's shifting geometry. 🌀

> [!CAUTION]
> The Phage doesn't just kill; it overwrites reality. This is why our technology must be **deterministic**—if your simulation drifts, the Acheron consumes you.

---

### 💻 Technical Architecture (The Engine)

The engine is built from the ground up to handle high-stakes P2P networking without the bloat of modern engines:

* **Deterministic Core:** Custom fixed-point math primitives and a fixed timestep ($60Hz$) to prevent simulation drift across architectures. 🧮
* **P2P Networking:**
* **Host Migration:** Automated candidate queueing ($k = \lceil \log_2(n) \rceil$) ensures the session continues even if the host disconnects. ⛓️
* **Input Pipeline:** Input-frame-to-tick mapping with host-authoritative conflict resolution and latest-frame overwrite.


* **Hybrid Rendering:**
* **CLI Raycaster:** A first-person ASCII raycaster for the ultimate "hacker-commando" aesthetic. 📟
* **SDL2 Frontend:** High-performance graphical output with DDA-based wall sampling. 🖼️



---

### 🏗️ Build & Configuration

#### 1. Requirements

Ensure your Linux environment has the necessary headers for graphics and networking:

```bash
sudo apt update
sudo apt install libsdl2-dev libsdl2-image-dev pkg-config libcurl4-openssl-dev

```

#### 2. Compilation

The build system uses **CMake** to auto-detect your local setup. If SDL2 is present, graphical mode is enabled automatically. ⚙️

```bash
# Initialize build directory with Release optimizations
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build using all available CPU threads
cmake --build build --parallel $(nproc)

# Run the validation suite (Protocol, Migration, and Input tests)
ctest --test-dir build --output-on-failure

```

---

### 🚀 Deployment & Execution

#### Launching the Game

The binary detects its environment. Launch it directly for the ASCII experience, or use the flag for graphics:

* **Terminal Mode:** `./build/breach_team` ⌨️
* **SDL2 Mode:** `./build/breach_team --sdl` 🎮

#### Multiplayer Roles

The game uses a **JSON-over-HTTP** bootstrap signaling client to coordinate peers:

* **Solo Play:** Local arena mode for testing weapon feel and movement.
* **Host Game:** Sets your machine as the initial authority (Migration Candidate 0).
* **Join Game:** Handshakes with the signaling server to sync with an existing host.

#### Docker (Headless/CI) 🐳

To test the networking stack in a clean environment:

```bash
docker build -f docker/Dockerfile -t breach-team:dev .
docker run -it --rm breach-team:dev

```

---

### 🎮 Controls & UI

| Input | Action |
| --- | --- |
| **W / S** | Move Forward / Backward |
| **A / D** | Rotate Left / Right (Strafe in SDL2) |
| **Space** | Discharge Pulse Weapon |
| **Q** | Emergency Evac (Quit) |

**Legend:**

* `@` : Commando Position
* `e` : Phage Entities 👾
* `#` : Titanium Bulkheads (Walls)

---

### 📂 Project Roadmap

* [x] **Milestone 1:** Deterministic fixed-point scaffold and P2P migration. ✅
* [x] **Milestone 2:** Dual-renderer (ASCII/SDL2) and DDA raycasting logic. ✅
* [ ] **Milestone 3:** Texture sampling for wall/sprite rendering. 🚧 *(In Progress)*
* [ ] **Milestone 4:** Complex AI "The Hivemind" implementation. 🧠

---

**Developed for Linux. No bloat. No mercy.**
*NVIM BTW :)* ⌨️🔥
