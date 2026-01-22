# Dexterion: AI Agent Instructions

## Project Overview
Dexterion is a C++ CS2 (Counter-Strike 2) enhancement tool with aim assistance, ESP, laser dodging, and movement exploits. It's a Visual Studio 2022 DLL injection project targeting Windows.

## Critical Workflows
**Build**: Use Visual Studio 2022 (`v143` toolset) targeting x64 Release. The project outputs to `x64/Release/SteelSeries GG.exe`.

**Runtime**: Attach DLL injection or run the main executable after injecting into CS2 process. Hotkeys control features:
- `INSERT` - Toggle menu
- Mouse wheel + Space - Bunnyhop trigger
- Laser dodging runs in separate thread

## Key Architecture Patterns
- **Memory Management**: All game memory access via `util/MemMan.hpp` using template `ReadMem<T>()`. Pattern: `MemMan.ReadMem<Vector3>(address)`
- **Multithreading**: Laser threat detection runs in `g_universal_threat_thread` (features/misc.cpp). Use atomic flags and mutex locks for thread safety
- **JSON Offsets**: Game offsets stored in JSON under `util/`. Pattern: `clientDLL::C_BaseEntity_["m_vecVelocity"]`
- **Entity Access**: Use entity list iteration with `C_CSPlayerPawn` class. Pattern: `C_CSPlayerPawn.value = index; C_CSPlayerPawn.getPlayerPawn()`
- **UI**: ImGui-based overlay in `gui/overlay.cpp` for rendering. Menu state controlled in `gui/menu.cpp`

## Essential Directories & Files
- `features/`: Core cheat modules (`misc.cpp` for laser logic, `aim.cpp` for aimbot, `esp.cpp` for wallhacks)
- `util/`: Memory management, configs, and utilities
- `gui/`: ImGui-based UI components
- `imgui/`: ImGui framework (do not modify)

## Common Patterns
**Reading Game Data**:
```cpp
// Position
uintptr_t sceneNode = MemMan.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
Vector3 pos = MemMan.ReadMem<Vector3>(sceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);

// Velocity
Vector3 vel = MemMan.ReadMem<Vector3>(pawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
```

**Entity Iteration**:
```cpp
for (int i = 65; i <= 32768; i++) {
    C_CSPlayerPawn C_CSPlayerPawn(client.base);
    C_CSPlayerPawn.value = i;
    if (!C_CSPlayerPawn.getListEntry() || !C_CSPlayerPawn.getPlayerPawn()) continue;
    // Process entity
}
```

**Thread-Safe Tracking**:
```cpp
std::atomic<bool> g_stop_flag = false;
std::mutex g_data_mutex;
std::vector<TrackedObject> g_tracker;

// Protected access
std::lock_guard<std::mutex> lock(g_data_mutex);
// Modify g_tracker
```

## Integration Points
- **CS2 Process**: Injection target. Use `MemMan.hpp` to attach to `cs2.exe` process
- **Game Offsets**: Reverse-engineered, stored in JSON. Update offsets on game updates
- **Input Simulation**: Windows APIs (`keybd_event`, `mouse_event`) for automated dodging/aiming
- **Config System**: JSON-based with `jsonOps.cpp`. Configs in `x64/Release/dev-folder/config.json`

## Development Notes
- **Lasers**: Threat detection in `universalThreatDetectorWorker()`. Spawns handler threads for individual threats
- **Aimbot**: Uses bone mapping, velocity prediction. Aim targets from `boneArray` pointer
- **ESP**: World-to-screen projection for entity rendering. Real-time position tracking
- **Bunnyhop**: Movement exploit with `keybd_event` simulation
- **Hardcoded Paths**: Player name logging in `entry.cpp` writes to `C:\Users\arthur\Desktop\tex_nef.txt`

## Error Handling
Memory reads can fail. Always validate pointers:
```cpp
if (!pPointer) return; // Skip frame
if (!pCollision) continue; // Skip entity
```

## Build Configuration
- **Platform**: Windows x64 only
- **Runtime**: Multithreaded DLL
- **Dependencies**: Windows SDK 10.0, ImGui, nlohmann/json
- **Output**: `SteelSeries GG.exe` (x64/Release/)