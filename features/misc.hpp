#pragma once

#include "include.hpp"
#include "../gui/overlay.hpp"
#include "../util/config.hpp"
#include "../gui/menu.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <atomic>
#include <mutex>
#include <vector> // Include vector header
#include <optional>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

namespace misc
{

    // Structure to hold item ESP data
    struct ItemESPData
    {
        ImVec2 screenOrigin; // Renamed for clarity (was ImVec2 origin)
        Vector3 worldOrigin; // New: Store world origin for distance calculation
        std::string name;
    };


struct DisplayedMessage {
    std::string messageText;
    std::string normalizedText; // We need this back for identifying unique timers!
    std::chrono::seconds duration;
    std::chrono::steady_clock::time_point creationTime;
};

    extern std::vector<DisplayedMessage> g_displayedMessages;
    extern std::mutex g_displayedMessagesMutex;

    // MODIFIED: The handler now needs the client module info to perform pattern scans.
    void handleChatTrigger(bool enabled, MemoryManagement::moduleData client);
    void drawTriggerMessage();

    // --- End of feature-specific declarations ---


    // This function is the public interface to draw the messages


    // Storage for item ESP data and mutex for thread safety
	extern std::vector<ItemESPData> itemESPList;
	extern std::mutex itemESPListMutex;
	extern std::mutex itemESPFilterMutex; // New mutex for itemESPFilter

    inline namespace sharedData
    {
        inline int bhopInAir = (1 << 0);
    };

    void droppedItem(C_CSPlayerPawn C_CSPlayerPawn, CGameSceneNode CGameSceneNode, view_matrix_t viewMatrix);
    void droppedItemSeparateThread(MemoryManagement::moduleData client);
    void disableDroppedItemSeparateThread();
    bool isGameWindowActive();
    void bunnyHop(DWORD_PTR base, LocalPlayer localPlayer);
    Vector3 calculateAngle(const Vector3& localPosition, const Vector3& enemyPosition, const Vector3& viewAngles);
    float DistanceToRaySquared(const Vector3& p, const Vector3& ray_origin, const Vector3& ray_direction_normalized);
    extern std::atomic<bool> g_is_universal_threat_enabled2; // Use atomic for thread safety
    extern	std::atomic<bool> g_stopChatMonitorThread;
	extern std::atomic<bool> isChatMonitorEnabled;
    extern std::atomic<bool> isCrouchOnly;
    void AngleVectors(const Vector3& angles, Vector3* forward, Vector3* right, Vector3* up);
    Vector3 AngleToForwardVector(const Vector3& angles);
    // In misc.hpp
    struct DamageData
    {
        std::string playerName;
        int damage;
        int hits;
        uintptr_t playerHandle;

        DamageData(std::string name, int dmg, int hitCount, uintptr_t handle) : playerName(name), damage(dmg), hits(hitCount), playerHandle(handle) {}

        bool operator<(const DamageData &other) const
        {
            return damage > other.damage;
        }
    };

    // Storage for damage data
    extern std::vector<DamageData> damageList;
    extern int lastUpdateTime;
    extern std::atomic<float> g_currentSpeed2D;

    // Damage list functions
    void displayDamageList();                                                       // Display the damage list UI
    void addDamage(std::string name, int damage, int hits, uintptr_t playerHandle); // Add damage for a player
    void clearDamageList();                                                         // Clear the damage list (for round reset)
    void updatePlayerDamage(std::string name, int totalDamage, int totalHits, uintptr_t playerHandle);
    void updateDamageList(MemoryManagement::moduleData client);
    extern std::atomic<float> g_currentSpeed2D;

    void startBhopThread(DWORD_PTR base, LocalPlayer localPlayer); // Function to initialize and start the thread
    void stopBhopThread();                                         // Function to signal the thread to stop and join it
    
    void startItemESPThread(MemoryManagement::moduleData client); // Function to initialize and start the thread
    void handleAutoLaser(bool enabled, MemoryManagement::moduleData client, LocalPlayer localPlayer);
    Vector3 RotatePoint(Vector3 point, Vector3 angles);
    void stopItemESPThread();

    // Utility to get current timestamp in seconds
    inline int getCurrentTimestamp()
    {
        return static_cast<int>(std::time(nullptr));
    }

    void DrawAllDebugBoxes();

    struct DebugBox {
        ImVec2 screenCorners[8];
        bool shouldDraw = false;
    };

    // A global vector to hold all boxes we want to draw this frame
    inline std::vector<DebugBox> g_debugBoxesToDraw;
    inline std::mutex g_debugBoxMutex;

    struct CDamageRecord
    {
        // First validate the structure layout and offsets
        // Use minimal definition at first
        uintptr_t m_PlayerDamager;             // 0x00
        uintptr_t m_PlayerRecipient;           // 0x08
        uintptr_t m_PlayerControllerDamager;   // 0x10
        uintptr_t m_PlayerControllerRecipient; // 0x18
        // Skip to the fields we need
        char pad[0x30];                 // Adjust this padding based on actual memory layout
        char m_szPlayerDamagerName[64]; // Actual offset uncertain - verify!
        char m_szPlayerRecipientName[64];
        uint64_t m_DamagerXuid;
        uint64_t m_RecipientXuid;
        int32_t m_iBulletsDamage;
        int32_t m_iDamage; // This is what we need
    };

    // Define the NetworkedVector structure (simplified)
    struct NetworkedVector
    {
        // CS2 uses a different memory layout for vectors
        uintptr_t m_data;   // 0x00: Pointer to data array
        int32_t m_size;     // 0x08: Current number of elements
        int32_t m_capacity; // 0x0C: Allocated capacity
        int32_t m_growSize; // 0x10: Elements to add when growing
        // There may be additional fields
    };

}