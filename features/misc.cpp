#include "misc.hpp" // This now handles all the core includes in the right order


#include "../util/config.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include "timeddraw.hpp"
#include <regex>
#include <sstream>
#include <memory>
#include <string>
#include <numeric>  // For std::accumulate
#include <deque>   // <-- Make sure to include this at the top of your file

#include <optional> // Add this include at the top of misc.cpp
#include <regex>    // And this one too
#include <random>   // Make sure this is included for getRandomInt
#include "json.hpp"
using jsonek = nlohmann::json;

// Initialize static variables
std::vector<misc::DamageData> misc::damageList;
std::atomic<float> misc::g_currentSpeed2D = {0.0f};
std::atomic<bool> misc::g_is_universal_threat_enabled2 = false;

std::atomic<bool> misc::g_stopChatMonitorThread = false;
std::atomic<bool> misc::isChatMonitorEnabled = false;
std::atomic<bool> misc::isCrouchOnly = false;


// --- Auto Laser Dodge Globals ---

// Define the shared item ESP list and mutex
std::vector<misc::ItemESPData> misc::itemESPList;
std::mutex misc::itemESPListMutex;
std::mutex misc::itemESPFilterMutex; // Definition of the new mutex

// std::optional<misc::TriggerInfo> misc::g_triggerInfo;
// std::mutex misc::g_triggerInfoMutex;
struct DebugBox
{
    ImVec2 screenCorners[8];
    bool shouldDraw = false;
};

namespace misc 
{
    // The one and only definition of the variable
    std::vector<TrackedObject> g_universal_tracker; 
}

// A global vector to hold all boxes we want to draw this frame
inline std::vector<DebugBox> g_debugBoxesToDraw;
inline std::mutex g_debugBoxMutex;

std::vector<misc::DisplayedMessage> misc::g_displayedMessages;
std::mutex misc::g_displayedMessagesMutex;

namespace
{ // Anonymous namespace for internal linkage
    void chatMonitorWorker(MemoryManagement::moduleData client);
    void startChatMonitorThread(MemoryManagement::moduleData client);

    std::thread g_autoLaserThread;
    std::atomic<bool> g_stopAutoLaserThread = false;
    std::atomic<bool> isAutoLaserEnabled = false;

    struct LaserData
    {
        Vector3 position;
        float distance;
    };


    auto g_blacklist_refresh_time = std::chrono::steady_clock::now();
    std::thread g_universal_threat_thread;
    std::atomic<bool> g_stop_universal_threat_thread = false;
     std::atomic<bool> g_is_universal_threat_enabled = false;

    // The flag for your bhopWorker to read
    std::atomic<bool> g_isHighLaserThreat = false;

    // Forward declaration of the worker function
    void universalThreatDetectorWorker(MemoryManagement::moduleData client, LocalPlayer localPlayer);

    void handleSingleThreat(uintptr_t threatPawn, Vector3 myInitialPos, LocalPlayer localPlayer, float speed, std::string threatName);
    void handleTeleportingLaser(uintptr_t threatPawn, Vector3 myInitialPos, LocalPlayer localPlayer);

    int getRandomInt(int min, int max)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(min, max);
        return distrib(gen);
    }
    // In misc.cpp's anonymous namespace

    void CalculateAndStoreCollisionBox(uintptr_t entityPawn, const view_matrix_t &viewMatrix)
    {
        uintptr_t pCollision = MemMan.ReadMem<uintptr_t>(entityPawn + clientDLL::C_BaseEntity_["m_pCollision"]);
        if (!pCollision)
            return;

        Vector3 vecMins = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
        Vector3 vecMaxs = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);

        uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(entityPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
        if (!pGameSceneNode)
            return;

        Vector3 threatOrigin = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
        Vector3 threatRotation = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angRotation"]);

        Vector3 localCorners[8] = {
            {vecMins.x, vecMins.y, vecMins.z}, {vecMins.x, vecMaxs.y, vecMins.z}, {vecMaxs.x, vecMaxs.y, vecMins.z}, {vecMaxs.x, vecMins.y, vecMins.z}, {vecMins.x, vecMins.y, vecMaxs.z}, {vecMins.x, vecMaxs.y, vecMaxs.z}, {vecMaxs.x, vecMaxs.y, vecMaxs.z}, {vecMaxs.x, vecMins.y, vecMaxs.z}};

        Vector3 worldCorners[8];
        for (int i = 0; i < 8; ++i)
        {
            worldCorners[i] = misc::RotatePoint(localCorners[i], threatRotation) + threatOrigin;
        }

        misc::DebugBox box;
        for (int i = 0; i < 8; ++i)
        {
            Vector3 screenPos = worldCorners[i].worldToScreen(viewMatrix);
            if (screenPos.z < 0.01f)
            {
                box.shouldDraw = false;
                return; // Don't add the box if any part is behind us
            }
            box.screenCorners[i] = {screenPos.x, screenPos.y};
        }

        box.shouldDraw = true;

        // Add the calculated box to our global list, protected by a mutex
        std::lock_guard<std::mutex> lock(misc::g_debugBoxMutex);
        misc::g_debugBoxesToDraw.push_back(box);
    }

    // 	void DrawCollisionBox(uintptr_t entityPawn, const view_matrix_t& viewMatrix) {
    //     // 1. Get Collision Property pointer from the entity's pawn.
    //     // C_BaseEntity -> m_pCollision
    //     uintptr_t pCollision = MemMan.ReadMem<uintptr_t>(entityPawn + clientDLL::C_BaseEntity_["m_pCollision"]);
    //     if (!pCollision) return;

    //     // 2. Read Min/Max vectors from the Collision Property.
    //     // CCollisionProperty -> m_vecMins / m_vecMaxs
    //     Vector3 vecMins = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
    //     Vector3 vecMaxs = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);

    //     // 3. Get the entity's Scene Node for position and rotation.
    //     // C_BaseEntity -> m_pGameSceneNode
    //     uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(entityPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
    //     if (!pGameSceneNode) return;

    //     // CGameSceneNode -> m_vecAbsOrigin (get origin from scene node for accuracy)
    //     Vector3 threatOrigin = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
    //     // CGameSceneNode -> m_angRotation
    //     Vector3 threatRotation = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angRotation"]);

    //     // 4. Define the 8 local corners from mins and maxs
    //     Vector3 localCorners[8] = {
    //         {vecMins.x, vecMins.y, vecMins.z}, {vecMins.x, vecMaxs.y, vecMins.z},
    //         {vecMaxs.x, vecMaxs.y, vecMins.z}, {vecMaxs.x, vecMins.y, vecMins.z},
    //         {vecMins.x, vecMins.y, vecMaxs.z}, {vecMins.x, vecMaxs.y, vecMaxs.z},
    //         {vecMaxs.x, vecMaxs.y, vecMaxs.z}, {vecMaxs.x, vecMins.y, vecMaxs.z}
    //     };

    //     // 5. Transform them into world space
    //     Vector3 worldCorners[8];
    //     for (int i = 0; i < 8; ++i) {
    //         worldCorners[i] = misc::RotatePoint(localCorners[i], threatRotation) + threatOrigin;
    //     }

    //     // 6. Project all 8 corners to the screen
    //     ImVec2 screenCorners[8];
    //     bool allOnScreen = true;
    //     for (int i = 0; i < 8; ++i) {
    //         Vector3 screenPos = worldCorners[i].worldToScreen(viewMatrix);
    //         if (screenPos.z < 0.01f) {
    //             allOnScreen = false;
    //             break;
    //         }
    //         screenCorners[i] = {screenPos.x, screenPos.y};
    //     }

    //     if (!allOnScreen) return; // Don't draw if any part is behind us

    // 	Logger::error("DRAWING .. . . ");
    //     // 7. Draw the box edges
    //     ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    //     ImU32 boxColor = IM_COL32(255, 0, 255, 255); // Magenta

    //     drawList->AddLine(screenCorners[0], screenCorners[1], boxColor);
    //     drawList->AddLine(screenCorners[1], screenCorners[2], boxColor);
    //     drawList->AddLine(screenCorners[2], screenCorners[3], boxColor);
    //     drawList->AddLine(screenCorners[3], screenCorners[0], boxColor);
    //     drawList->AddLine(screenCorners[4], screenCorners[5], boxColor);
    //     drawList->AddLine(screenCorners[5], screenCorners[6], boxColor);
    //     drawList->AddLine(screenCorners[6], screenCorners[7], boxColor);
    //     drawList->AddLine(screenCorners[7], screenCorners[4], boxColor);
    //     drawList->AddLine(screenCorners[0], screenCorners[4], boxColor);
    //     drawList->AddLine(screenCorners[1], screenCorners[5], boxColor);
    //     drawList->AddLine(screenCorners[2], screenCorners[6], boxColor);
    //     drawList->AddLine(screenCorners[3], screenCorners[7], boxColor);
    // }

    std::string readStringFromPointer(uintptr_t baseAddress, uintptr_t offset, MemoryManagement &memManager)
    {
        if (!baseAddress)
            return "[null base]";
        uintptr_t stringAddress = memManager.ReadMem<uintptr_t>(baseAddress + offset);
        if (!stringAddress)
            return "[null string ptr]";
        char buffer[256]{};
        memManager.ReadRawMem(stringAddress, buffer, sizeof(buffer) - 1);
        return std::string(buffer);
    }

    // Gets the entity's targetname/classname
    std::string getEntityName(uintptr_t pawn, MemoryManagement &memManager)
    {
        if (!pawn)
            return "[null pawn]";
        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(pawn + 0x10); // CEntityInstance::m_pEntity
        if (!pEntityIdentity)
            return "[null identity]";
        return readStringFromPointer(pEntityIdentity, 0x18, memManager); // CEntityIdentity::m_name
    }

    int findIndexOfPawn(uintptr_t targetPawn, uintptr_t clientBase)
    {
        if (targetPawn == 0)
            return -1;
        for (int i = 66; i < 85999; i++)
        {
            C_CSPlayerPawn C_CSPlayerPawn(clientBase);
            C_CSPlayerPawn.value = i;
            if (C_CSPlayerPawn.getListEntry() && C_CSPlayerPawn.getPlayerPawn() == targetPawn)
            {
                return i; // Found it!
            }
        }
        return -1; // Not found
    }

    // New function to get a pawn by its index in the entity list
    uintptr_t getPawnByIndex(int index, uintptr_t dwEntityList, MemoryManagement &memManager)
    {
        if (index < 0 || index > 8192)
            return 0; // Safety check

        uintptr_t listEntry = memManager.ReadMem<uintptr_t>(dwEntityList + (8 * (index >> 9) + 16));
        if (!listEntry)
            return 0;

        uintptr_t pCSPlayerPawn = memManager.ReadMem<uintptr_t>(listEntry + (112 * (index & 0x1FF)));
        return pCSPlayerPawn;
    }

    void dumpEntityIdentity(uintptr_t pawn, MemoryManagement &memManager)
    {
        if (!pawn)
        {
            Logger::info("[IdentityDump] Pawn is null.");
            return;
        }
        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(pawn + 0x10); // CEntityInstance::m_pEntity
        if (!pEntityIdentity)
        {
            Logger::info("[IdentityDump] Pawn has no CEntityIdentity.");
            return;
        }

        size_t dumpSize = 128;
        std::vector<unsigned char> buffer(dumpSize);
        memManager.ReadRawMem(pEntityIdentity, buffer.data(), dumpSize);

        Logger::info(std::format("--- CEntityIdentity Dump for Pawn {:#x} at {:#x} ---", pawn, pEntityIdentity));

        for (size_t i = 0; i < dumpSize; i += 16)
        {
            std::stringstream ss_hex, ss_char;
            ss_hex << "0x" << std::hex << std::setw(2) << std::setfill('0') << i << ": ";

            for (size_t j = 0; j < 16; ++j)
            {
                if (i + j < dumpSize)
                {
                    ss_hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i + j]) << " ";

                    // Add character representation
                    char c = buffer[i + j];
                    ss_char << (isprint(c) ? c : '.');
                }
                else
                {
                    ss_hex << "   ";
                }
            }
            Logger::info(ss_hex.str() + "| " + ss_char.str());
        }
        Logger::info("--- End of Dump ---");
    }

    void dumpStringNeighborhood(uintptr_t pawn, MemoryManagement &memManager)
    {
        if (!pawn)
        {
            Logger::info("[StringDump] Pawn is null.");
            return;
        }
        // 1. Get pointer to CEntityIdentity
        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(pawn + 0x10);
        if (!pEntityIdentity)
        {
            Logger::info("[StringDump] Pawn has no CEntityIdentity.");
            return;
        }

        // 2. Get the pointer TO the string (from m_name at 0x18)
        uintptr_t pString = memManager.ReadMem<uintptr_t>(pEntityIdentity + 0x18);
        if (!pString)
        {
            Logger::info("[StringDump] CEntityIdentity has no name string pointer.");
            return;
        }

        // 3. Read a block of memory AROUND the string
        size_t dumpSize = 128;
        // We'll read from 32 bytes before the string to 96 bytes after
        uintptr_t startAddress = pString - 32;
        std::vector<unsigned char> buffer(dumpSize);
        memManager.ReadRawMem(startAddress, buffer.data(), dumpSize);

        Logger::info(std::format("--- String Neighborhood Dump for Pawn {:#x} (String at {:#x}) ---", pawn, pString));

        for (size_t i = 0; i < dumpSize; i += 16)
        {
            std::stringstream ss_hex, ss_char;
            // Calculate the actual memory address for this line
            uintptr_t currentLineAddress = startAddress + i;
            ss_hex << std::hex << std::setw(8) << std::setfill('0') << currentLineAddress << ": ";

            for (size_t j = 0; j < 16; ++j)
            {
                if (i + j < dumpSize)
                {
                    ss_hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i + j]) << " ";

                    char c = buffer[i + j];
                    // Replace null terminators with a special character to make them visible
                    ss_char << (c == '\0' ? '_' : (isprint(c) ? c : '.'));
                }
                else
                {
                    ss_hex << "   ";
                }
            }
            Logger::info(ss_hex.str() + "| " + ss_char.str());
        }
        Logger::info("--- End of Dump ---");
    }

    
    std::string getModelNameFromPawn(uintptr_t pawn, MemoryManagement &memManager)
    {
        // This function implements the logic found in the server-side CBaseModelEntity::GetModelName()

        // 1. C_BaseEntity -> m_CBodyComponent
        uintptr_t pBodyComponent = memManager.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_CBodyComponent"]);
        if (!pBodyComponent)
            return "[No BodyComponent]";

        // 2. CBodyComponent -> m_pSceneNode
        // This is the pointer to what should be the CSkeletonInstance for a model entity.
        uintptr_t pSceneNode = memManager.ReadMem<uintptr_t>(pBodyComponent + clientDLL::CBodyComponent_["m_pSceneNode"]);
        if (!pSceneNode)
            return "[No SceneNode]";

        // 3. CSkeletonInstance -> m_modelState (Embedded struct)
        // The pSceneNode *is* the CSkeletonInstance.
        uintptr_t pModelState = pSceneNode + clientDLL::CSkeletonInstance_["m_modelState"];

        // 4. CModelState -> m_ModelName (Pointer to string)
        uintptr_t pModelNameString = memManager.ReadMem<uintptr_t>(pModelState + clientDLL::CModelState_["m_ModelName"]);
        if (!pModelNameString)
            return "[No ModelName Ptr]";

        // 5. Read the actual string
        char modelNameBuffer[256]{};
        memManager.ReadRawMem(pModelNameString, modelNameBuffer, sizeof(modelNameBuffer) - 1);

        return std::string(modelNameBuffer);
    }

    // The Ultimate Diagnostic Tool
    void inspectEntity(uintptr_t pawn, MemoryManagement &memManager, int i)
    {
        // This static set ensures we only print this massive log ONCE per unique threat pawn address.
        // static std::set<uintptr_t> alreadyInspected;
        // if (alreadyInspected.count(pawn))
        // {
        //     return;
        // }
        // alreadyInspected.insert(pawn);

        Logger::info("======================================================================");
        Logger::info(std::format("ENTITY INSPECTOR: Full Data Dump for Pawn @ {:#x}", pawn));
        Logger::info("----------------------------------------------------------------------");

        if (!pawn)
        {
            Logger::error("  [ERROR] Pawn address is NULL.");
            Logger::info("======================================================================");
            return;
        }

        // --- CEntityIdentity ---
        const uintptr_t CEntityIdentity_m_nameStringableIndex = 20;

        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(pawn + 0x10); // CEntityInstance::m_pEntity
        if (pEntityIdentity)
        {
            std::string name = readStringFromPointer(pEntityIdentity, 0x18, memManager);
            std::string designerName = readStringFromPointer(pEntityIdentity, 0x20, memManager);
                        //std::string mod = getModelNameFromPawn(pEntityIdentity, memManager);
                        std::string modelName2 = getModelNameFromPawn(pawn, MemMan);

            int32_t stringIndex = memManager.ReadMem<int32_t>(pEntityIdentity + CEntityIdentity_m_nameStringableIndex);

            Logger::warn("[CEntityIdentity] I: " + std::to_string(i));
            Logger::info(std::format("  - m_name (targetname): '{}'", name));
            Logger::info(std::format("  - m_designerName: '{}'", designerName));
                        Logger::info("  - stringable_index: '{}'"+ std::to_string(stringIndex) );
                        Logger::info("  - model_name_entity: '{}'"+ utils::sanitizeString(modelName2) );

        }
        else
        {
            Logger::error("  [ERROR] CEntityIdentity pointer is NULL.");
        }
        Logger::info("----------------------------------------------------------------------");

        // --- C_BaseEntity ---
        Vector3 vecVelocity = memManager.ReadMem<Vector3>(pawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
        int32_t health = memManager.ReadMem<int32_t>(pawn + clientDLL::C_BaseEntity_["m_iHealth"]);
        int32_t teamNum = memManager.ReadMem<int32_t>(pawn + clientDLL::C_BaseEntity_["m_iTeamNum"]);
        int32_t hammerId = memManager.ReadMem<int32_t>(pawn + 0x558); // m_sUniqueHammerID

        Logger::warn("[C_BaseEntity]");
        Logger::info(std::format("  - m_iHealth: {}", health));
        Logger::info(std::format("  - m_iTeamNum: {}", teamNum));
        Logger::info(std::format("  - m_vecVelocity: X:{:.2f}, Y:{:.2f}, Z:{:.2f} (Speed: {:.2f})",
                                 vecVelocity.x, vecVelocity.y, vecVelocity.z, vecVelocity.Length()));
        Logger::info(std::format("  - m_sUniqueHammerID (at 0x558): {}", hammerId));
        Logger::info("----------------------------------------------------------------------");

        // --- CGameSceneNode ---
        uintptr_t pGameSceneNode = memManager.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
        if (pGameSceneNode)
        {
            Vector3 vecAbsOrigin = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            Vector3 vecOrigin = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecOrigin"]);
            Vector3 angRotation = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angRotation"]);
            Vector3 angAbsRotation = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angAbsRotation"]);
            float flScale = memManager.ReadMem<float>(pGameSceneNode + clientDLL::CGameSceneNode_["m_flScale"]);

            Logger::warn("[CGameSceneNode]");
            Logger::info(std::format("  - m_vecAbsOrigin: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecAbsOrigin.x, vecAbsOrigin.y, vecAbsOrigin.z));
            Logger::info(std::format("  - m_vecOrigin (Local): X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecOrigin.x, vecOrigin.y, vecOrigin.z));
            Logger::info(std::format("  - m_angRotation (Local): P:{:.2f}, Y:{:.2f}, R:{:.2f}", angRotation.x, angRotation.y, angRotation.z));
            Logger::info(std::format("  - m_angAbsRotation: P:{:.2f}, Y:{:.2f}, R:{:.2f}", angAbsRotation.x, angAbsRotation.y, angAbsRotation.z));
            Logger::info(std::format("  - m_flScale: {:.2f}", flScale));
        }
        else
        {
            Logger::error("  [ERROR] CGameSceneNode pointer is NULL.");
        }
        Logger::info("----------------------------------------------------------------------");

        // --- CCollisionProperty ---
        uintptr_t pCollision = memManager.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_pCollision"]);
        if (pCollision)
        {
            Vector3 vecMins = memManager.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
            Vector3 vecMaxs = memManager.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);
            float aabbHeight = vecMaxs.z - vecMins.z;

            Logger::warn("[CCollisionProperty]");
            Logger::info(std::format("  - m_vecMins: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecMins.x, vecMins.y, vecMins.z));
            Logger::info(std::format("  - m_vecMaxs: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecMaxs.x, vecMaxs.y, vecMaxs.z));
            Logger::info(std::format("  -> Calculated AABB Height: {:.2f}", aabbHeight));
        }
        else
        {
            Logger::error("  [ERROR] CCollisionProperty pointer is NULL.");
        }

        Logger::info("======================================================================");
        Logger::info("END OF INSPECTION. This thread will now idle.");
        Logger::info("======================================================================");

        // Stop the thread after the one-time dump.
      //  while (true)
      //  {
      //      std::this_thread::sleep_for(std::chrono::seconds(1));
      //  }
    }

    bool hasFirstDecimalDigitNine(float number)
    {
        number = std::fabs(number);         // ignore negative sign
        number -= static_cast<int>(number); // get fractional part
        int firstDigit = static_cast<int>(number * 10) % 10;
        return firstDigit == 9;
    }

    uintptr_t getPawnByHandle(uint32_t handle, uintptr_t clientBase, MemoryManagement &memManager)
    {
        if (handle == 0xFFFFFFFF)
            return 0;

        uintptr_t dwEntityList = clientBase + offsets::clientDLL["dwEntityList"];
        int entryIndex = handle & 0x7FFF;

        uintptr_t listEntry = memManager.ReadMem<uintptr_t>(dwEntityList + (8 * (entryIndex >> 9) + 16));
        if (!listEntry)
            return 0;

        // A proper implementation would also check the handle's serial number against the entity's.
        return memManager.ReadMem<uintptr_t>(listEntry + (112 * (entryIndex & 0x1FF)));
    }

    void inspectEntity(uintptr_t pawn, const std::string &label, MemoryManagement &memManager)
    {
        if (!pawn)
        {
            Logger::error(std::format("INSPECT FAILED for {}: Pawn address is NULL.", label));
            return;
        }

        Logger::info("----------------------------------------------------------------------");
        Logger::warn(std::format("INSPECTING ENTITY: {} (Pawn @ {:#x})", label, pawn));

        // --- CEntityIdentity ---
        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(pawn + 0x10);
        std::string name = getEntityName(pawn, memManager);
        std::string designerName = readStringFromPointer(pEntityIdentity, 0x20, memManager);
        Logger::info(std::format("  [Identity] Name: '{}', DesignerName: '{}'", name, designerName));

        // --- C_BaseEntity ---
        Vector3 vecVelocity = memManager.ReadMem<Vector3>(pawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
        int32_t health = memManager.ReadMem<int32_t>(pawn + clientDLL::C_BaseEntity_["m_iHealth"]);
        int32_t teamNum = memManager.ReadMem<int32_t>(pawn + clientDLL::C_BaseEntity_["m_iTeamNum"]);
        uint32_t ownerHandleVal = memManager.ReadMem<uint32_t>(pawn + clientDLL::C_BaseEntity_["m_hOwnerEntity"]);
        Logger::info(std::format("  [BaseEntity] Health: {}, Team: {}, Velocity: {:.1f}, OwnerHandle: {:#x}",
                                 health, teamNum, vecVelocity.Length(), ownerHandleVal));

        // --- CGameSceneNode ---
        uintptr_t pGameSceneNode = memManager.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
        if (pGameSceneNode)
        {
            Vector3 absOrigin = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            Vector3 localOrigin = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecOrigin"]);
            Vector3 absRotation = memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angAbsRotation"]);
            Logger::info(std::format("  [SceneNode] AbsOrigin: {:.1f}, {:.1f}, {:.1f}", absOrigin.x, absOrigin.y, absOrigin.z));
            Logger::info(std::format("  [SceneNode] LocalOrigin: {:.1f}, {:.1f}, {:.1f}", localOrigin.x, localOrigin.y, localOrigin.z));
            Logger::info(std::format("  [SceneNode] AbsRotation: P:{:.1f}, Y:{:.1f}, R:{:.1f}", absRotation.x, absRotation.y, absRotation.z));
        }
        else
        {
            Logger::error("  [SceneNode] POINTER IS NULL");
        }

        // --- CCollisionProperty ---
        uintptr_t pCollision = memManager.ReadMem<uintptr_t>(pawn + clientDLL::C_BaseEntity_["m_pCollision"]);
        if (pCollision)
        {
            Vector3 mins = memManager.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
            Vector3 maxs = memManager.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);
            Logger::info(std::format("  [Collision] AABB Height: {:.2f}", maxs.z - mins.z));
        }
        else
        {
            Logger::error("  [Collision] POINTER IS NULL");
        }
        Logger::info("----------------------------------------------------------------------");
    }
    // In misc.cpp

    // In misc.cpp - Replace your old function with this one



    

uintptr_t findAndInspectChild(uintptr_t parentPawn, const std::string& childNameSubstring, MemoryManagement& memManager) {
    
    // --- Offsets from your client_dll.json (use your real values) ---
    const uintptr_t C_BaseEntity_m_pGameSceneNode = clientDLL::C_BaseEntity_["m_pGameSceneNode"];
    const uintptr_t CGameSceneNode_m_pChild = clientDLL::CGameSceneNode_["m_pChild"];
    const uintptr_t CGameSceneNode_m_pNextSibling = clientDLL::CGameSceneNode_["m_pNextSibling"];
    const uintptr_t CGameSceneNode_m_pOwner = clientDLL::CGameSceneNode_["m_pOwner"];

    // --- Get the parent's scene node ---
    uintptr_t pParentSceneNode = memManager.ReadMem<uintptr_t>(parentPawn + C_BaseEntity_m_pGameSceneNode);
    if (!pParentSceneNode) {
        Logger::warn("Parent pawn has no scene node, cannot search for children.");
         return 0;
    }
    
    // --- Start walking the linked list of children ---
    uintptr_t pCurrentChildSceneNode = memManager.ReadMem<uintptr_t>(pParentSceneNode + CGameSceneNode_m_pChild);

    
    
    if (!pCurrentChildSceneNode) {
        Logger::info("Parent has no children.");
         return 0;
    }
    
    bool foundChild = false;
    while (pCurrentChildSceneNode) {
        // Get the child's pawn address from its scene node
        uintptr_t pChildPawn = memManager.ReadMem<uintptr_t>(pCurrentChildSceneNode + CGameSceneNode_m_pOwner);
        if (pChildPawn) {
            // Get the child's name
            //std::string childName = getEntityName(pChildPawn, memManager);

                               // std::string childName = getModelNameFromPawn(pChildPawn, MemMan);
                                
                //   Logger::warn("co jest? "+utils::sanitizeString(childName));
            
          //  childName = utils::toLower(childName);
           // Logger::warn("jakis tam child: "+childName);
            // Check if this is the child we're looking for
           // if (childName.find(childNameSubstring) != std::string::npos) {
               // foundChild = true;
               // Logger::warn(std::format("<<<<< FOUND MATCHING CHILD '{}' >>>>>", childName));
                
                // We found it! Perform a full inspection.
               // inspectEntity(pChildPawn, "Hurtbox Child", memManager);
                return pChildPawn;
              Logger::warn("<<<<<<<<<<<<<<<<<< END CHILD INSPECTION >>>>>>>>>>>>>>>>>>");
                break; // Stop searching once we've found it
          //  }
        }
        
        // Move to the next child in the list
        pCurrentChildSceneNode = memManager.ReadMem<uintptr_t>(pCurrentChildSceneNode + CGameSceneNode_m_pNextSibling);
    }
    
    if (!foundChild) {
        //   pCurrentChildSceneNode = memManager.ReadMem<uintptr_t>(pParentSceneNode + CGameSceneNode_m_pChild);
            Logger::warn("<<<<<<no hurt found >>>>>>>>>>>");

          // return memManager.ReadMem<uintptr_t>(pCurrentChildSceneNode + CGameSceneNode_m_pOwner);

         return 0;
    }
    return 0;
}


uintptr_t getPawnByHandle2(uint32_t handle, uintptr_t entityList, MemoryManagement& memManager) {
    if (handle == 0xFFFFFFFF) return 0;
    int entryIndex = handle & 0x7FFF;
    uintptr_t listEntry = memManager.ReadMem<uintptr_t>(entityList + 0x8 * ((entryIndex & 0x7FFF) >> 9) + 0x10);
    if (!listEntry) return 0;
    return memManager.ReadMem<uintptr_t>(listEntry + 0x70 * (entryIndex & 0x1FF));
}

const uintptr_t C_BaseEntity_m_sUniqueHammerID = 1512;

    // Final combined code: Your proven Hunter logic + our new verified Dodger logic.
    void universalThreatDetectorWorker(MemoryManagement::moduleData client, LocalPlayer localPlayer)
    {
        // --- YOUR ORIGINAL CONFIGURATION ---
        const auto OBSERVATION_PERIOD = std::chrono::milliseconds(17);
        const auto BLACKLIST_REFRESH_INTERVAL = std::chrono::seconds(10);
        const float MIN_MOVEMENT_DISTANCE_SQUARED = 2.0f * 2.0f;
        const float MIN_LASER_SPEED = 550.0f;

        // --- NEW FIXED MECHANICAL TIMINGS (Replaces JUMP_REACTION_TIME_SECONDS) ---
        const float PLAYER_JUMP_EXECUTION_TIME = 0.45f;
        const float PLAYER_CROUCH_EXECUTION_TIME = 0.25f;
        std::set<uintptr_t> alreadyInspected;
        // --- This variable is no longer needed but kept to not break your menu code ---
        float JUMP_REACTION_TIME_SECONDS = 0.0f;

        const Vector3 initialMyPos = localPlayer.getOrigin();

    //      uintptr_t pGameEntitySystem = MemMan.ReadMem<uintptr_t>(client.base + offsets::clientDLL["dwGameEntitySystem"]);
    // if (!pGameEntitySystem) {
    //     Logger::error("Could not get CGameEntitySystem!");
    //     return; // Cannot proceed
    // }

        while (!g_stop_universal_threat_thread)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(444));
            auto now = std::chrono::steady_clock::now();

            // if (now - g_blacklist_refresh_time > BLACKLIST_REFRESH_INTERVAL)
            // {
            //     misc::g_universal_tracker.clear();
            //     g_blacklist_refresh_time = now;
            // }

            localPlayer.getPlayerPawn();
            if (localPlayer.playerPawn == 0)
                continue;

            {
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                if (!misc::g_cleanupQueue.empty())
                {
                    for (uintptr_t pawnToClean : misc::g_cleanupQueue)
                    {
                        // Erase the cleaned object from the main tracker
                        std::erase_if(misc::g_universal_tracker, [&](const misc::TrackedObject &obj)
                                      { return obj.pawn_address == pawnToClean; });
                        // Logger::info(std::format("[Main Thread] Cleaned up object {:#x}.", pawnToClean));
                    }
                    misc::g_cleanupQueue.clear();
                }
            }

            // ==========================================================
            //  YOUR PROVEN HUNTER LOGIC (UNCHANGED)
            // ==========================================================
            std::vector<uintptr_t> current_entities_in_game;




        // Read the highest entity index currently in use. This is a crucial optimization.
       // int highestEntityIndex = MemMan.ReadMem<int>(pGameEntitySystem + offsets::clientDLL["dwGameEntitySystem_highestEntityIndex"]);

                           //Logger::warn("indexes how many:  "+std::to_string(highestEntityIndex));


            for (int i = 64; i <= 66666; i++)
            {
                C_CSPlayerPawn C_CSPlayerPawn(client.base);
                C_CSPlayerPawn.value = i;
                 if (!C_CSPlayerPawn.getListEntry() || !C_CSPlayerPawn.getPlayerPawn()){
                     continue;
                 }


                uintptr_t current_pawn = C_CSPlayerPawn.playerPawn;

                // std::string classname = getEntityName(current_pawn, MemMan);
                // if(classname.empty()){
                //     continue;
                // }
                // // Logger::warn(classname + ", index: "+ std::to_string(i));
                // // continue;

                 //std::string modelName3 = getModelName(current_pawn, MemMan);
                    //std::string modelName2 = getModelNameFromPawn(current_pawn, MemMan);
                  // Logger::warn("co jest, ID: "+std::to_string(i)+",name: "+utils::sanitizeString(modelName2));
                current_entities_in_game.push_back(current_pawn);
                auto it = std::find_if(misc::g_universal_tracker.begin(), misc::g_universal_tracker.end(),
                                       [current_pawn](const misc::TrackedObject &obj)
                                       { return obj.pawn_address == current_pawn; });

                CGameSceneNode sceneNode;
                sceneNode.value = C_CSPlayerPawn.getCGameSceneNode();
                Vector3 current_pos = sceneNode.getOrigin();

                if (it == misc::g_universal_tracker.end())
                {
                    misc::g_universal_tracker.push_back({current_pawn, current_pos, now, misc::ObjectState::Observing});

                }
                else
                {
                    // --- STATE MACHINE LOGIC ---
                    switch (it->state)
                    {
                    case misc::ObjectState::Observing:
                        if (now - it->first_seen_time > OBSERVATION_PERIOD)
                        {
                            it->state = misc::ObjectState::Monitoring; // Observation over, start monitoring
                        }
                        break;

                    case misc::ObjectState::Monitoring:
                    {
                        // Check if the object has moved significantly since we first saw it
                        float dist_moved_sq = (current_pos - it->last_position).LengthSqr();
                        if (dist_moved_sq > MIN_MOVEMENT_DISTANCE_SQUARED)
                        {
                            // Logger::info(std::format("[ThreatDetector] Object {:#x} is now moving. Promoting to Threat.", it->pawn_address));
                            it->state = misc::ObjectState::Threat; // It's moving! It's a potential threat.
                             //inspectEntity(current_pawn, MemMan, i);

                        }
                        break;
                    }

                    // If it's a threat, static, or dodged, we don't change its state here.
                    case misc::ObjectState::Threat:
                    case misc::ObjectState::Dodged:
                        break;
                    }
                    it->last_position = current_pos; // Always update last known position
                }
            }

            // Cleanup logic remains the same
            // Logger::warn(std::format("[Discovery] Frame finished. Found a total of {} entities.", current_entities_in_game.size()));

            misc::g_universal_tracker.erase(
                std::remove_if(misc::g_universal_tracker.begin(), misc::g_universal_tracker.end(),
                               [&](const misc::TrackedObject &obj)
                               {
                                   return std::find(current_entities_in_game.begin(), current_entities_in_game.end(), obj.pawn_address) == current_entities_in_game.end();
                               }),
                misc::g_universal_tracker.end());
            // =======================================================
            //  YOUR PROVEN THREAT PRIORITIZATION LOGIC (UNCHANGED)
            // ==========================================================

            misc::TrackedObject *primary_threat_ptr = nullptr;

            std::string threttingName = "";

            for (auto &obj : misc::g_universal_tracker)
            {


                float lowest_tti = 10.0f;

                // We only care about objects that are confirmed threats
                if (obj.state == misc::ObjectState::Handled)
                    continue;
                if (obj.state != misc::ObjectState::Threat)
                    continue;
                // uintptr_t gameSceneNode = MemMan.ReadMem<uintptr_t>(localPlayer.playerPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);

                // Vector3 myPosNow = MemMan.ReadMem<Vector3>(gameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);


                   std::string modelName = getModelNameFromPawn(obj.pawn_address, MemMan);

                    if (utils::toLower(modelName).find("/entities/") != std::string::npos)
                    {
                        
                    // std::string dsa = getModelNameFromPawn(obj.pawn_address, MemMan);

                    

                    // Vector3 threatVels = MemMan.ReadMem<Vector3>(obj.pawn_address + clientDLL::C_BaseEntity_["m_vecVelocity"]);
                    // float speeds = threatVels.Length();
                    // Logger::error(dsa + ", speed: "+std::to_string(speeds));
                    if (utils::toLower(modelName).find("vertical_laser3_model") != std::string::npos)
                    {
                      Vector3 threatVel3 = MemMan.ReadMem<Vector3>(obj.pawn_address + clientDLL::C_BaseEntity_["m_vecVelocity"]);
                         float speed3 = threatVel3.Length();
                         Logger::warn(" no speed: "+std::to_string(speed3) );
                        std::thread(handleSingleThreat, obj.pawn_address, initialMyPos, localPlayer, 969.988098f, threttingName).detach();

                    }
                    }
                    else{
                        continue;
                    }
                threttingName = utils::sanitizeString(modelName);
                Vector3 threatVel = MemMan.ReadMem<Vector3>(obj.pawn_address + clientDLL::C_BaseEntity_["m_vecVelocity"]);

                Vector3 myPosNow = localPlayer.getOrigin();
                float speed = threatVel.Length();

                uintptr_t currentPawn = obj.pawn_address;

                //   uintptr_t pEntityIdentity = MemMan.ReadMem<uintptr_t>(currentPawn + clientDLL::CEntityInstance_["m_pEntity"]); // Using JSON offset

                // if (pEntityIdentity) {
                //     // Read Target Name (m_name)
                //     std::string targetName = readStringFromPointer(pEntityIdentity, clientDLL::CEntityIdentity_["m_name"], MemMan); // Using JSON offset

                //     // Read Class Name (m_designerName) - This is what your getEntityName should be doing
                //     std::string className = readStringFromPointer(pEntityIdentity, clientDLL::CEntityIdentity_["m_designerName"], MemMan); // Using JSON offset

                //     // Read Model Name using our new helper function
                //     std::string modelName = getModelNameFromPawn(currentPawn, MemMan);
                //     std::string modSan = utils::sanitizeString(modelName);

                //     // Log all three for comparison
                //     Logger::info(std::format("Pawn {:#x} -> Class: '{}', TargetName: '{}', Model: '{}', Speed: '{}', tti:: '{}'",
                //         currentPawn,
                //         className,
                //         targetName,
                //         modSan,
                //         speed,
                //         (obj.last_position - myPosNow).Length() / speed
                //     ));
                // }

                // Logger::info("x: "+ std::to_string(threatVel.x) +", y: "+ std::to_string(threatVel.y)+", z: "+  std::to_string(threatVel.z));
                // Logger::error(std::format("[Filter Check] Pawn {:#x} | Speed: {:.2f} | Is moving away? {} | TTI: {:.2f}",
                //     obj.pawn_address,
                //     speed,
                //     (threatVel.Dot(obj.last_position - myPosNow) >= 0),
                //     (speed > 0.0f) ? (obj.last_position - myPosNow).Length() / speed : -1.0f
                // ));

                // Logger::info(text + ", speed: "+std::to_string(speed));

                if (speed != 0.0f)
                {
                    float time_to_impact = (obj.last_position - myPosNow).Length() / speed;
                    // Logger::warn("time to impact" + std::to_string(time_to_impact));
                    // Logger::warn("lowest_tti" + std::to_string(lowest_tti));
                    // Logger::warn("is higher than 0? " + std::to_string(time_to_impact > 0.0f));
                    if (time_to_impact < lowest_tti && time_to_impact > 0.0f)
                    {
                        lowest_tti = time_to_impact;
                        primary_threat_ptr = &obj;

                        // Get CEntityIdentity once to read both names from it

                        // --- NEIGHBORHOOD WATCH LOG ---
                        // uintptr_t pEntityIdentity = MemMan.ReadMem<uintptr_t>(obj.pawn_address + 0x10);
                        // std::string name = getEntityName(obj.pawn_address, MemMan);
                        // std::string designerName = readStringFromPointer(pEntityIdentity, 0x20, MemMan);
                        // Logger::info(std::format("  [Identity] Name: '{}', DesignerName: '{}'", name, designerName));

                        // if(!hasFirstDecimalDigitNine(speed)){
                        //     continue;
                        // }

                        if (speed < 899.0f)
                        {
                            continue;
                        }
                        // Vector3 threatDir = threatVel / speed;

                        // Check if it's moving away from us
                        if (threatVel.Dot(obj.last_position - myPosNow) >= -0.01f)
                        {
                            Logger::warn("moving away, ending the threat named: "+ threttingName);
                            continue;
                        }

                        if (speed > 5990.0f)
                        {
                            continue;
                        }

                        //    if (text.length() < 4) {
                        //      primary_threat_ptr = nullptr;
                        //         continue;
                        //     }
                        // if (text.length() > 44) {
                        //  primary_threat_ptr = nullptr;
                        //     continue;
                        // }

                        // Logger::info(text);

                        uintptr_t entity = MemMan.ReadMem<uintptr_t>(obj.pawn_address + 0x10);
                        uintptr_t designerNameAddy2 = MemMan.ReadMem<uintptr_t>(entity + 0x18);
                        char designerNameBuffer2[MAX_PATH]{};
                        MemMan.ReadRawMem(designerNameAddy2, designerNameBuffer2, MAX_PATH);

                        std::string name2 = std::string(designerNameBuffer2);

                        std::string text = utils::sanitizeString(name2);
                        threttingName = text;
                    }

                    else
                    {

                        //             std::string classname = getEntityName(obj.pawn_address, MemMan);

                        // Logger::info("--- Starting Viewmodel Inspector --- of "+ classname);

                        //                if (alreadyInspected.count(obj.pawn_address)) {
                        //     continue;
                        // }

                        // // --- Find our targets ---

                        //     alreadyInspected.insert(obj.pawn_address);

                        //     Logger::warn(std::format("<<<<<<<<<< FOUND A VIEWMODEL: '{}' >>>>>>>>>>", classname));

                        //     // Inspect the viewmodel itself
                        // inspectEntity(obj.pawn_address, "ViewModel", MemMan);

                        //     // Find and inspect its OWNER
                        //     uint32_t ownerHandle = MemMan.ReadMem<uint32_t>(obj.pawn_address + clientDLL::C_BaseEntity_["m_hOwnerEntity"]);

                        //     if (ownerHandle != 0xFFFFFFFF) {
                        //         uintptr_t ownerPawn = getPawnByHandle(ownerHandle, client.base, MemMan);
                        //         inspectEntity(ownerPawn, "OWNER", MemMan);
                        //     } else {
                        //         Logger::warn("  -> This ViewModel has no owner.");
                        //     }
                        //     Logger::warn("<<<<<<<<<<<<<<<<<<<<<<<<< END >>>>>>>>>>>>>>>>>>>>>>>>");

                        continue;
                    }

                }
                else
                {
                    // primary_threat_ptr = &obj;
                    // if (primary_threat_ptr != nullptr)
                    // {
                    //    uintptr_t threatPawn = primary_threat_ptr->pawn_address;

                    //    std::string modelName = getModelNameFromPawn(threatPawn, MemMan);

                    //    if (utils::toLower(modelName).find("laser") != std::string::npos)
                    //    {
                    //        if (utils::toLower(modelName).find("laser_wall_button_") != std::string::npos)
                    //        {
                    //            primary_threat_ptr = nullptr;
                    //            continue;
                    //        }
                    //        if (utils::toLower(modelName).find("laserwall_cubes_") != std::string::npos)
                    //        {
                    //            primary_threat_ptr = nullptr;
                    //            continue;
                    //        }
                    //        Logger::error(modelName);
                    //        primary_threat_ptr->state = misc::ObjectState::Handled;
                    //        std::thread(handleTeleportingLaser, threatPawn, initialMyPos, localPlayer).detach();
                    //    }
                    //    else if (utils::toLower(modelName).find("lazer") != std::string::npos)
                    //    {
                    //     Logger::error("contains lazer word: "+ modelName);
                    //     //    if (utils::toLower(modelName).find("gabranth_lazer_") != std::string::npos){
                    //     //    primary_threat_ptr = nullptr;
                    //     //    continue;
                    //     //    }

                    //        primary_threat_ptr->state = misc::ObjectState::Handled;
                    //        std::thread(handleTeleportingLaser, threatPawn, initialMyPos, localPlayer).detach();
                    //    }
                    //    else if (utils::toLower(modelName).find("s4_final_boss_return_doorlaser") != std::string::npos)
                    //    {
                    //        Logger::error(modelName);
                    //        primary_threat_ptr->state = misc::ObjectState::Handled;
                    //        std::thread(handleTeleportingLaser, threatPawn, initialMyPos, localPlayer).detach();
                    //    }
                    //    else{

                    //        primary_threat_ptr = nullptr;
                    //        continue;
                    //    }


                    // }
                      continue;
                }
                // ==========================================================
                //  NEW AND IMPROVED DODGER LOGIC (REPLACEMENT)
                // ==========================================================
                if (primary_threat_ptr != nullptr)
                {

                    // 2. Mark it as "Dodged" in our main tracker so we don't launch another handler for it.
                    // In this context, "Dodged" really means "Handled" or "Dispatched".

                    // 3. Launch a new, detached thread to handle this specific laser.
                    //    The thread will manage its own lifecycle and exit when done.

                    uintptr_t threatPawn = primary_threat_ptr->pawn_address;
                    uintptr_t entity = MemMan.ReadMem<uintptr_t>(threatPawn + 0x10);

                    Vector3 threatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);

                    float speed2 = threatVel.Length();

                    //   if (std::abs(speed2 - speedin) > 0.001f)
                    //      {
                    //         Logger::warn("A: "+std::to_string(speed2) + "B: "+std::to_string(speedin));
                    //      primary_threat_ptr = nullptr;
                    //     continue;
                    //       }
                    // Logger::warn(std::format("[Dispatcher] Final speed check for pawn {:#x}. Speed is: {:.2f}", threatPawn, speed2));

                    if (speed2 < 960.0f || speed2 > 4030.0f)
                    {
                        primary_threat_ptr = nullptr;
                        continue;
                    }



                    primary_threat_ptr->state = misc::ObjectState::Handled;

                    std::thread(handleSingleThreat, threatPawn, initialMyPos, localPlayer, speed2, threttingName).detach();
                    Logger::info("running laser handler... for... " + modelName);                }
            }

            // 	continue;

            //     bool action_taken = false;

            //     // --- Get Live Data ---
            //     uintptr_t threatPawn = primary_threat_ptr->pawn_address;
            //     uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            //     if (!pGameSceneNode) continue;

            //     Vector3 threatOrigin = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            //     Vector3 threatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
            //     float threatSpeed = threatVel.Length();

            //     if (threatSpeed < 1.0f) continue;
            //     Vector3 threatDirection = threatVel.Normalize();

            //     // --- Calculate True TTI using Verified Projection Math ---
            //     Vector3 vecToPlayer = myPos - threatOrigin;
            //     float forwardDistance = vecToPlayer.Dot(threatDirection);
            //     if (forwardDistance < 0) continue;
            //     float true_tti = forwardDistance / threatSpeed;

            // 	Logger::warn(std::to_string(true_tti));
            //     // --- Make Dodge Decision ---
            //     float predictedLaserHeight = threatOrigin.z - myPos.z;

            //     if (predictedLaserHeight < STANDING_FEET_HEIGHT) { // Low Laser
            //         if (true_tti <= PLAYER_JUMP_EXECUTION_TIME) {
            //             Logger::info(std::format("ACTION: JUMP (TTI={:.2f})", true_tti));
            //             mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
            //             std::this_thread::sleep_for(std::chrono::milliseconds(25));
            //             keybd_event(VK_CONTROL, 0, 0, 0);
            //             std::this_thread::sleep_for(std::chrono::milliseconds(200));
            //             keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
            //             action_taken = true;
            //         }
            //     } else if (predictedLaserHeight >= CROUCHING_EYE_HEIGHT) { // High Laser
            //         if (true_tti <= PLAYER_CROUCH_EXECUTION_TIME) {
            //             Logger::info(std::format("ACTION: CROUCH (TTI={:.2f})", true_tti));
            //             keybd_event(VK_CONTROL, 0, 0, 0);
            //             auto crouchStartTime = std::chrono::steady_clock::now();
            //             while (true) {
            //                 if (std::chrono::steady_clock::now() - crouchStartTime > std::chrono::seconds(5)) break;
            //                 Vector3 currentMyPos = localPlayer.getOrigin();
            //                 uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            //                 Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            //                 Vector3 currentThreatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
            //                 if (currentThreatVel.LengthSqr() < 1.0f) break;
            //                 if ((currentMyPos - currentThreatOrigin).Dot(currentThreatVel.Normalize()) > 0) break;
            //                 std::this_thread::sleep_for(std::chrono::milliseconds(5));
            //             }
            //             keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
            //             action_taken = true;
            //         }
            //     }

            //     if (action_taken) {
            //         uintptr_t dodged_pawn_address = primary_threat_ptr->pawn_address;
            //         g_universal_tracker.erase(std::remove_if(g_universal_tracker.begin(), g_universal_tracker.end(),
            //             [dodged_pawn_address](const TrackedObject& obj) {
            //                 return obj.pawn_address == dodged_pawn_address;
            //             }),
            //             g_universal_tracker.end());
            //         Logger::info(std::format("[ThreatDetector] Object {:#x} dodged and removed.", dodged_pawn_address));
            //     }
            // }
        }
    }

    void handleTeleportingLaser(uintptr_t threatPawn, Vector3 myInitialPos, LocalPlayer localPlayer)
    {

        const bool UchichaLvl2 = false;

         const bool UchichaLvl4 = false;

        float PLAYER_JUMP_EXECUTION_TIME = 0.38f + miscConf.latencyLasers + 0.03f;

        const float PLAYER_CROUCH_EXECUTION_TIME = 1.35f;
        const float CROUCHING_EYE_HEIGHT = 54.0f;
        const float STANDING_FEET_HEIGHT = 53.99999;

        constexpr int FL_ONGROUNDA = (1 << 0); // Player is standing on ground.

        auto threadStartTime = std::chrono::steady_clock::now();
        const auto threadTimeout = std::chrono::seconds(9); // Max lifetime for a handler thread
        std::deque<float> speed_samples;

        Vector3 last_pos = {};
        auto last_time = std::chrono::steady_clock::now();
        bool is_initialized = false;
        float averaged_speed = 0.0f;

        const float reference_z = myInitialPos.z;

                bool isLeftTilt = false;
                bool isRightTilt = false;

            uintptr_t pGameSceneNode2 = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            if (!pGameSceneNode2){
                Logger::error(" No pGameSceneNode");

                return;
            }
            const uintptr_t CGameSceneNode_m_angAbsRotation = clientDLL::CGameSceneNode_["m_angAbsRotation"];

            Vector3 angRotation = MemMan.ReadMem<Vector3>(pGameSceneNode2 + clientDLL::CGameSceneNode_["m_angRotation"]);

            //Vector3 liveRotation = MemMan.ReadMem<Vector3>(pGameSceneNode2 + CGameSceneNode_m_angAbsRotation);
            Logger::error("rotZ: " + std::to_string(angRotation.z));
            Logger::error("rotX: " + std::to_string(angRotation.x));
            //memManager.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angAbsRotation"])



            int first = 2.5;
            int second = -2.5;


            if(UchichaLvl4){
            if(angRotation.x > first){
                isLeftTilt = true;
            }
            else if( angRotation.x < second){
                isRightTilt = true;
            }
            }
            else{
            if(angRotation.z > first){
                isLeftTilt = true;
            }
            else if( angRotation.z < second){
                isRightTilt = true;
            }
            }


        bool isTiltedLaser = isLeftTilt || isRightTilt;

            std::string modelName_u = getModelNameFromPawn(threatPawn, MemMan);


            



        Logger::warn("starting tp laser dodge.");
        while (true)
        {

            auto now = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));

            if (std::chrono::steady_clock::now() - threadStartTime > threadTimeout)
            {
                Logger::error(std::format("Handler for {:#x} timed out.", threatPawn));
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }

            uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            if (!pGameSceneNode){
                Logger::error(" No pGameSceneNode");

                return;
            }
            Vector3 myPos = localPlayer.getOrigin();
            Vector3 threatOrigin = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            Vector3 effectiveVel;
            float threatSpeed;
            if(!UchichaLvl2 && !UchichaLvl4){



            // --- Initialize on first run ---
            if (!is_initialized)
            {
                last_pos = threatOrigin;
                last_time = now;
                is_initialized = true;
                continue; // Skip to next frame to get a delta
            }

            // --- Calculate time delta ---
            auto time_delta = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_time);
            if (time_delta.count() < 1e-6){
                continue;
            }

            // --- Determine Velocity ---

            // This is a stationary/teleporting laser. Calculate its velocity.
            effectiveVel = (threatOrigin - last_pos) / time_delta.count();

            // Update state for next frame
            last_pos = threatOrigin;
            last_time = now;

            threatSpeed = effectiveVel.Length();

            if (threatSpeed < 700.0f)
            {
                continue;
            }
            if (threatSpeed > 2999.0f)
            {
                continue;
            }
            speed_samples.push_back(threatSpeed);
            if(speed_samples.size()<3){
                continue;
            }
            float sum = std::accumulate(speed_samples.begin(), speed_samples.end(), 0.0f);
            averaged_speed = sum / speed_samples.size();


            threatSpeed = averaged_speed; //hard coded for now, we need to make it average

                        }
                        else{
                            if(UchichaLvl2){
                                threatSpeed  = 1250.0f;
                            }
                            else if(UchichaLvl4){
                                threatSpeed  = 1500.0f;
                            }
                        }

            Vector3 threatDirection;
            if(UchichaLvl2){
                threatDirection = Vector3{1500.0f, 0.0f, 0.0f}.Normalize(); 
            }
            else if(UchichaLvl4){

            if (modelName_u.find("inhugd_susano_laser_door2_17.vm") != std::string::npos) {
                threatDirection = Vector3{ 0.0f, 1.0f, 0.0f };
             }
             else{
                threatDirection = Vector3{ 0.0f, -1.0f, 0.0f };

             }
                
            }
            else{
                threatDirection = effectiveVel.Normalize();
            }
            //Logger::warn("threatDirection: " + std::to_string(threatDirection.x) +", " + std::to_string(threatDirection.y) +", "+std::to_string(threatDirection.z));
            // --- Use our verified TTI calculation with the determined velocity ---
            Vector3 vecToPlayer = myPos - threatOrigin;
            float forwardDistance = vecToPlayer.Dot(threatDirection);
            if (forwardDistance < 0)
                return; // Passed us

            float true_tti = forwardDistance / threatSpeed;

            // Logger::info("przeszlo: "+ std::to_string(true_tti));

            uintptr_t pCollision = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pCollision"]);
            if (!pCollision)
                continue; // Skip frame if no collision data

            Vector3 threatMins = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
            Vector3 threatMaxs = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);
            Vector3 threatRotation = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angRotation"]);

            // 2. Calculate the 8 corners of the laser's hitbox in the world
            Vector3 localCorners[8] = {
                {threatMins.x, threatMins.y, threatMins.z}, {threatMins.x, threatMaxs.y, threatMins.z}, {threatMaxs.x, threatMaxs.y, threatMins.z}, {threatMaxs.x, threatMins.y, threatMins.z}, {threatMins.x, threatMins.y, threatMaxs.z}, {threatMins.x, threatMins.y, threatMaxs.z}, {threatMaxs.x, threatMaxs.y, threatMaxs.z}, {threatMaxs.x, threatMins.y, threatMaxs.z}};
            Vector3 worldCorners[8];
            for (int i = 0; i < 8; ++i)
            {
                worldCorners[i] = misc::RotatePoint(localCorners[i], threatRotation) + threatOrigin;
            }

            // 3. Find the corner that is "most forward" along the path of travel
            float maxProjection = -FLT_MAX;
            Vector3 leadingEdgePoint = threatOrigin; // Default to origin
            for (const auto &corner : worldCorners)
            {
                // Project each corner onto the direction vector relative to the origin.
                float projection = (corner - threatOrigin).Dot(threatDirection);
                if (projection > maxProjection)
                {
                    maxProjection = projection;
                    // This corner is the one that forms the "tip" of the laser.
                }
            }

            leadingEdgePoint = threatOrigin + (threatDirection * maxProjection);

            // 4. Calculate the TTI from YOU to this new, accurate LEADING EDGE POINT
            Vector3 vecToPlayerFromEdge = myPos - leadingEdgePoint;
            float predictedLaserHeight = leadingEdgePoint.z - reference_z;

            if(!UchichaLvl2){
               if (isTiltedLaser)
                     {   // IS TILTED?
              
                bool isMovingMostlyOnY = std::abs(threatDirection.y) > std::abs(threatDirection.x);

                // 2. Get the full bounds of the AABB in world coordinates
                float laser_min_x = threatOrigin.x + threatMins.x;
                float laser_max_x = threatOrigin.x + threatMaxs.x;
                float laser_min_y = threatOrigin.y + threatMins.y;
                float laser_max_y = threatOrigin.y + threatMaxs.y;
                float laser_bottom_z = threatOrigin.z + threatMins.z;
                float laser_top_z = threatOrigin.z + threatMaxs.z;

                // 3. Calculate my percentage position along the laser's width
                float my_relevant_coord = isMovingMostlyOnY ? myPos.x : myPos.y;
                float laser_min_relevant_coord = isMovingMostlyOnY ? laser_min_x : laser_min_y;
                float laser_max_relevant_coord = isMovingMostlyOnY ? laser_max_x : laser_max_y;

                float width = laser_max_relevant_coord - laser_min_relevant_coord;
                float my_pos_along_width = my_relevant_coord - laser_min_relevant_coord;

                float percentage = 0.5f;
                if (width > 1.0f)
                {
                    percentage = my_pos_along_width / width;
                    percentage = (std::max)(0.0f, std::min(1.0f, percentage));
                }

                if (isRightTilt)
                {
                    percentage = 1.0f - percentage;
                    //predictedLaserHeight = 20.0f;
                     //  Logger::info("TILTRED RIGHT");
                }




                 float interpolated_height = laser_bottom_z + ((laser_top_z - laser_bottom_z) * percentage);
                 float laser_thickness = 4.0f; // Guess the thickness of the individual hurtboxes

                 float predicted_laser_bottom_at_my_pos = interpolated_height - (laser_thickness / 2.0f);

                 predictedLaserHeight = predicted_laser_bottom_at_my_pos - reference_z;
                
            }
            else
            {
                predictedLaserHeight = leadingEdgePoint.z - reference_z;
                // predictedLaserHeight = threatOrigin.z - myPos.z;
            }
                        }
            else{

             if (isRightTilt)
                {
                    //percentage = 1.0f - percentage;
                    predictedLaserHeight = 20.0f;
                     //  Logger::info("TILTRED RIGHT");
                }
                else if (isLeftTilt){
                    predictedLaserHeight = 60.0f;

                        // Logger::info("TILTRED LEFT");
                }
            }

            if(UchichaLvl4){
              if (modelName_u.find("crouch") != std::string::npos) {
                    predictedLaserHeight = 60.0f;
             }
             else{
            
             if (isRightTilt)
                {
                    //percentage = 1.0f - percentage;
                    predictedLaserHeight = 20.0f;
                     //  Logger::info("TILTRED RIGHT");
                }
                else if (isLeftTilt){
                    predictedLaserHeight = 60.0f;

                        // Logger::info("TILTRED LEFT");
                }
        }
            }



            bool action_taken = false;

            int flagss = localPlayer.getFlags();
            bool onGrounds = (flagss & FL_ONGROUNDA);

            if (predictedLaserHeight < -50.25f && onGrounds)
            {
                Logger::info("za nisko na groundsach: " + std::to_string(onGrounds));
                action_taken = true;
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }
            if (predictedLaserHeight > 111.0f && onGrounds)
            {
                Logger::info("za wysoko: " + std::to_string(onGrounds));
                action_taken = true;
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }


            if (predictedLaserHeight < 54.0f && predictedLaserHeight >= 0.0f)
            {
                if (true_tti <= PLAYER_JUMP_EXECUTION_TIME)
                {
                                 if (isRightTilt)
                {
                       Logger::info("TILTRED RIGHT");
                }
                else if(isLeftTilt){

                         Logger::info("TILTRED LEFT");
                }
                    Logger::info("TELEPROTING LASER HEIGHT: " + std::to_string(predictedLaserHeight) + "tti: " + std::to_string(true_tti));

                    
                     int flagss2 = localPlayer.getFlags();
                     bool onGrounds2 = (flagss & FL_ONGROUNDA);

                     while(!onGrounds2){
                        flagss2 = localPlayer.getFlags();
                        onGrounds2 = (flagss & FL_ONGROUNDA);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
                     }

                    Logger::info(std::format("HANDLER: JUMP on {:#x} (TTI={:.2f})", threatPawn, true_tti));
                    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                    if (predictedLaserHeight > 45.0f)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(3));
                        Logger::info("JumpCrouch once.");
                         mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                         std::this_thread::sleep_for(std::chrono::milliseconds(17));
                         keybd_event(VK_CONTROL, 0, 0, 0);
                         std::this_thread::sleep_for(std::chrono::milliseconds(550));
                    }

                    Vector3 last_pos_inner = MemMan.ReadMem<Vector3>(MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]) + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
                    auto last_time_inner = std::chrono::steady_clock::now();

                    while (true)
                    {

                        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // Check every 5ms
                        auto now_inner = std::chrono::steady_clock::now();

                        // Get current positions
                        Vector3 currentMyPos = localPlayer.getOrigin();
                        uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
                        if (!pCurrentGCS)
                            break;
                        Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);

                        // --- Calculate Effective Velocity inside the loop ---
                        auto time_delta_inner = std::chrono::duration_cast<std::chrono::duration<float>>(now_inner - last_time_inner);
                        if (time_delta_inner.count() < 1e-6){
                            continue;
                        }
                            

                        Vector3 effectiveVel_inner = (currentThreatOrigin - last_pos_inner) / time_delta_inner.count();

                        // Update state for next inner-loop iteration
                        last_pos_inner = currentThreatOrigin;
                        last_time_inner = now_inner;

                        if (effectiveVel_inner.LengthSqr() < 1.0f)
                        {
                            Logger::info("[CrouchHold] Threat stopped moving. Releasing.");
                            break;
                        }

                        // --- Use this new effective velocity to check if the laser has passed ---
                        Vector3 threatDirection_inner = effectiveVel_inner.Normalize();

                        // This is the check from our successful debug session.
                        // A positive projection means the laser has passed us.
                        float projection = (currentMyPos - currentThreatOrigin).Dot(threatDirection);

                        if (projection < -3.0f)
                        { // Use a small negative buffer to be safe
                            Logger::info(std::format("[CrouchHold] Threat passed (proj: {:.2f}). Releasing.", projection));
                            break;
                        }
                    }
                    
                    action_taken = true;
                }
            } // Inside handleTeleportingLaser, this replaces your existing crouch block.

            else if (predictedLaserHeight >= 54.0f && predictedLaserHeight < 100.0f)
            {
                if (true_tti <= PLAYER_CROUCH_EXECUTION_TIME)
                {
                                 if (isRightTilt)
                {
                       Logger::info("TILTRED RIGHT");
                }
                else if(isLeftTilt){

                         Logger::info("TILTRED LEFT");
                }
                            Logger::info("TELEPROTING LASER HEIGHT: " + std::to_string(predictedLaserHeight) + "tti: " + std::to_string(true_tti));

                    Logger::info(std::format("HANDLER [Teleport]: CROUCH on {:#x} (TTI={:.2f})", threatPawn, true_tti));

                    // --- CORRECTED DYNAMIC CROUCH LOGIC for Teleporting Lasers ---

                    keybd_event(VK_CONTROL, 0, 0, 0); // Press and HOLD crouch
                    auto crouchStartTime = std::chrono::steady_clock::now();

                    // We need state for the inner loop, similar to the main handler loop
                    Vector3 last_pos_inner = MemMan.ReadMem<Vector3>(MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]) + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
                    auto last_time_inner = std::chrono::steady_clock::now();

                    while (true)
                    {
                        // Safety timeout
                        if (std::chrono::steady_clock::now() - crouchStartTime > std::chrono::seconds(5))
                        {
                            Logger::error("[CrouchHold] Timed out for {:#x}", threatPawn);
                            break;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Check every 5ms
                        auto now_inner = std::chrono::steady_clock::now();

                        // Get current positions
                        Vector3 currentMyPos = localPlayer.getOrigin();
                        uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
                        if (!pCurrentGCS)
                            break;
                        Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);

                        // --- Calculate Effective Velocity inside the loop ---
                        auto time_delta_inner = std::chrono::duration_cast<std::chrono::duration<float>>(now_inner - last_time_inner);
                        if (time_delta_inner.count() < 1e-6)
                            continue;

                        Vector3 effectiveVel_inner = (currentThreatOrigin - last_pos_inner) / time_delta_inner.count();

                        // Update state for next inner-loop iteration
                        last_pos_inner = currentThreatOrigin;
                        last_time_inner = now_inner;


                        // --- Use this new effective velocity to check if the laser has passed ---
                        Vector3 threatDirection_inner = effectiveVel_inner.Normalize();

                        // This is the check from our successful debug session.
                        // A positive projection means the laser has passed us.
                        float projection = (currentMyPos - currentThreatOrigin).Dot(threatDirection);

                        if (projection < -10.0f)
                        { // Use a small negative buffer to be safe
                            Logger::info(std::format("[CrouchHold] Threat passed (proj: {:.2f}). Releasing.", projection));
                            break;
                        }
                    }

                     if (!misc::isCrouchOnly){
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); // Release crouch
                     }
                    
                    action_taken = true;
                }
            }

            if (action_taken)
            {
                {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                    misc::g_cleanupQueue.push_back(threatPawn);
                    return;
                }
            }
        }
    }

#include <iomanip> // For std::hex, std::setw, std::setfill
#include <set>

    // Dumps a block of memory as Hex, Integers, and Floats
    void DumpMemory(const std::string &title, uintptr_t address, size_t size, MemoryManagement &memManager)
    {
        if (!address)
        {
            Logger::error(title + ": Invalid address (0)");
            return;
        }

        std::vector<unsigned char> buffer(size);
        memManager.ReadRawMem(address, buffer.data(), size);

        Logger::info("========== START MEMORY DUMP: " + title + " at address " + std::to_string(address) + " ==========");

        for (size_t i = 0; i < size; i += 16)
        {
            std::stringstream ss;
            // Offset
            ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << i << ": ";

            // Hex representation
            for (size_t j = 0; j < 16; ++j)
            {
                if (i + j < size)
                {
                    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i + j]) << " ";
                }
                else
                {
                    ss << "   ";
                }
            }
            ss << "| ";

            // Interpreted as 4-byte values (float and int)
            for (size_t j = 0; j < 16; j += 4)
            {
                if (i + j < size)
                {
                    float f_val;
                    int32_t i_val;
                    memcpy(&f_val, &buffer[i + j], sizeof(float));
                    memcpy(&i_val, &buffer[i + j], sizeof(int32_t));
                    ss << std::fixed << std::setprecision(2) << std::setw(8) << f_val << "f " << std::setw(8) << i_val << "i | ";
                }
            }
            Logger::info(ss.str());
        }

        Logger::info("========== END MEMORY DUMP: " + title + " ==========");
    }

    void recursiveChildDump(uintptr_t parentPawn, int depth, MemoryManagement &memManager)
    {
        if (depth > 5)
        { // Safety break to prevent infinite loops
            Logger::error(std::string(depth * 2, ' ') + "-> Recursion depth limit reached.");
            return;
        }

        // --- Define Offsets (use your real values) ---
        const uintptr_t OFF_P_GAMESCENENODE = clientDLL::C_BaseEntity_["m_pGameSceneNode"];
        const uintptr_t OFF_P_COLLISION = clientDLL::C_BaseEntity_["m_pCollision"];
        const uintptr_t OFF_P_ENTITY = 0x10;
        const uintptr_t OFF_P_CHILD = 0x40;
        const uintptr_t OFF_P_NEXTSIBLING = 0x48;
        const uintptr_t OFF_P_OWNER = 0x30;
        const uintptr_t OFF_VECABSORIGIN = clientDLL::CGameSceneNode_["m_vecAbsOrigin"];
        const uintptr_t OFF_NAME = 0x18;
        const uintptr_t OFF_VECMINS = clientDLL::CCollisionProperty_["m_vecMins"];
        const uintptr_t OFF_VECMAXS = clientDLL::CCollisionProperty_["m_vecMaxs"];
        // ---

        // 1. Get and Log Info for the Current Entity (the Parent for this level)
        std::string indent = std::string(depth * 4, ' ');
        uintptr_t pIdentity = memManager.ReadMem<uintptr_t>(parentPawn + OFF_P_ENTITY);
        std::string name = readStringFromPointer(pIdentity, OFF_NAME, memManager);

        uintptr_t pSceneNode = memManager.ReadMem<uintptr_t>(parentPawn + OFF_P_GAMESCENENODE);
        if (!pSceneNode)
        {
            Logger::warn(indent + std::format("[L{}] Pawn: {:#x}, Name: '{}' (NO SCENE NODE)", depth, parentPawn, name));
            return;
        }
        Vector3 origin = memManager.ReadMem<Vector3>(pSceneNode + OFF_VECABSORIGIN);

        uintptr_t pCollision = memManager.ReadMem<uintptr_t>(parentPawn + OFF_P_COLLISION);
        Vector3 mins = {0, 0, 0}, maxs = {0, 0, 0};
        if (pCollision)
        {
            mins = memManager.ReadMem<Vector3>(pCollision + OFF_VECMINS);
            maxs = memManager.ReadMem<Vector3>(pCollision + OFF_VECMAXS);
        }

        Logger::info(indent + std::format("[L{}] Pawn: {:#x}, Name: '{}'", depth, parentPawn, name));
        Logger::info(indent + std::format("  -> Origin: {:.2f}, {:.2f}, {:.2f}", origin.x, origin.y, origin.z));
        Logger::info(indent + std::format("  -> AABB Mins: {:.2f}, {:.2f}, {:.2f} | Maxs: {:.2f}, {:.2f}, {:.2f}",
                                          mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z));

        // 2. Find its children and recurse
        uintptr_t pChildSceneNode = memManager.ReadMem<uintptr_t>(pSceneNode + OFF_P_CHILD);
        if (!pChildSceneNode)
        {
            Logger::info(indent + "  (No children)");
            return;
        }

        while (pChildSceneNode)
        {
            uintptr_t pChildPawn = memManager.ReadMem<uintptr_t>(pChildSceneNode + OFF_P_OWNER);
            if (pChildPawn)
            {
                // RECURSIVE CALL: Go one level deeper for this child
                recursiveChildDump(pChildPawn, depth + 1, memManager);
            }
            // Move to the next sibling
            pChildSceneNode = memManager.ReadMem<uintptr_t>(pChildSceneNode + OFF_P_NEXTSIBLING);
        }
    }

    // std::string readStringFromPointer(uintptr_t baseAddress, uintptr_t offset, MemoryManagement& memManager) {
    //     if (!baseAddress) return "";

    //     // 1. Read the address where the string is located
    //     uintptr_t stringAddress = memManager.ReadMem<uintptr_t>(baseAddress + offset);
    //     if (!stringAddress) return "";

    //     // 2. Read the actual string data from that address
    //     char buffer[256]{};
    //     memManager.ReadRawMem(stringAddress, buffer, sizeof(buffer) - 1);

    //     return std::string(buffer);
    // }

    struct LaserConfig
    {
        std::vector<std::string> left_tilt_names;
        std::vector<std::string> right_tilt_names;
    };

    struct LaserProfile
    {
        std::vector<float> left_tilt_local_x;
        std::vector<float> right_tilt_local_x;
    };

    std::string getModelName(uintptr_t pawn, MemoryManagement &memManager)
    {
        // --- Offsets from your client_dll.json and verified understanding ---
        const uintptr_t C_BaseEntity_m_CBodyComponent = 0x38;
        const uintptr_t CBodyComponentSkeletonInstance_m_skeletonInstance = 0x50;
        const uintptr_t CSkeletonInstance_m_modelState = 0x170;
        const uintptr_t CModelState_m_ModelName_ptr = 0xA8; // This is a pointer inside the CUtlSymbolLarge

        // --- Walk the Pointer Chain ---
        uintptr_t pBodyComponent = memManager.ReadMem<uintptr_t>(pawn + C_BaseEntity_m_CBodyComponent);
        if (!pBodyComponent)
            return "[Error: No BodyComponent]";

        uintptr_t pSkeletonInstance = memManager.ReadMem<uintptr_t>(pBodyComponent + CBodyComponentSkeletonInstance_m_skeletonInstance);
        if (!pSkeletonInstance)
            return "[Error: No SkeletonInstance]";

        uintptr_t pModelState = pSkeletonInstance + CSkeletonInstance_m_modelState;

        // The m_ModelName field is a CUtlSymbolLarge. It's a struct, and the first member is the char*
        uintptr_t pModelNameString = memManager.ReadMem<uintptr_t>(pModelState + CModelState_m_ModelName_ptr);
        if (!pModelNameString)
            return "[Error: No ModelName Ptr]";

        // Now read the string itself
        char modelNameBuffer[256]{};
        memManager.ReadRawMem(pModelNameString, modelNameBuffer, sizeof(modelNameBuffer) - 1);

        return std::string(modelNameBuffer);
    }

    std::string getEntityLumpName(std::uintptr_t templateEntityPtr, MemoryManagement &memManager)
    {
        // --- Offset from your client_dll.json ---
        // CPointTemplate::m_iszSource2EntityLumpName -> 0x570 (1392)
        const std::uintptr_t CPointTemplate_m_iszSource2EntityLumpName = 0x570;

        // --- Read the pointer to the string ---
        // Like m_ModelName, this is a string_t, which holds a pointer to the actual string.
        std::uintptr_t pLumpNameString = memManager.ReadMem<std::uintptr_t>(templateEntityPtr + CPointTemplate_m_iszSource2EntityLumpName);
        if (!pLumpNameString)
            return "[Error: No LumpName Ptr]";

        // --- Read the string itself ---
        char lumpNameBuffer[256]{};
        memManager.ReadRawMem(pLumpNameString, lumpNameBuffer, sizeof(lumpNameBuffer) - 1);

        return std::string(lumpNameBuffer);
    }

    void inspectCollisionProperty(uintptr_t threatPawn, MemoryManagement &memManager)
    {
        static std::set<uintptr_t> alreadyInspected;
        if (alreadyInspected.count(threatPawn))
            return;
        alreadyInspected.insert(threatPawn);

        Logger::info("======================================================================");
        Logger::info(std::format("COLLISION PROPERTY DUMP for Pawn @ {:#x}", threatPawn));
        Logger::info("----------------------------------------------------------------------");

        uintptr_t pCollision = memManager.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pCollision"]);
        if (!pCollision)
        {
            Logger::error("  [ERROR] CCollisionProperty pointer is NULL.");
            Logger::info("======================================================================");
            return;
        }

        // --- Read ALL the properties from the schema ---
        Vector3 vecMins = memManager.ReadMem<Vector3>(pCollision + 0x40);        // 64
        Vector3 vecMaxs = memManager.ReadMem<Vector3>(pCollision + 0x4C);        // 76
        uint8_t usSolidFlags = memManager.ReadMem<uint8_t>(pCollision + 0x5A);   // 90
        uint8_t nSolidType = memManager.ReadMem<uint8_t>(pCollision + 0x5B);     // 91
        uint8_t triggerBloat = memManager.ReadMem<uint8_t>(pCollision + 0x5C);   // 92
        uint8_t nSurroundType = memManager.ReadMem<uint8_t>(pCollision + 0x5D);  // 93
        uint8_t collisionGroup = memManager.ReadMem<uint8_t>(pCollision + 0x5E); // 94
        uint8_t nEnablePhysics = memManager.ReadMem<uint8_t>(pCollision + 0x5F); // 95
        float flBoundingRadius = memManager.ReadMem<float>(pCollision + 0x60);   // 96

        Vector3 specifiedMins = memManager.ReadMem<Vector3>(pCollision + 0x64); // 100
        Vector3 specifiedMaxs = memManager.ReadMem<Vector3>(pCollision + 0x70); // 112

        // Get the entity's name for context
        uintptr_t pEntityIdentity = memManager.ReadMem<uintptr_t>(threatPawn + 0x10);
        std::string name = readStringFromPointer(pEntityIdentity, 0x18, memManager);

        // --- Log the data in a structured way ---
        Logger::warn(std::format("Inspecting for Name: '{}'", name));
        Logger::info("  [Main Bounding Box (AABB)]");
        Logger::info(std::format("    - m_vecMins: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecMins.x, vecMins.y, vecMins.z));
        Logger::info(std::format("    - m_vecMaxs: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", vecMaxs.x, vecMaxs.y, vecMaxs.z));
        Logger::info(std::format("    -> Calculated AABB Height: {:.2f}", vecMaxs.z - vecMins.z));
        Logger::info("");

        Logger::info("  [Physics Flags]");
        Logger::info(std::format("    - m_CollisionGroup: {}", (int)collisionGroup));
        Logger::info(std::format("    - m_nSolidType: {}", (int)nSolidType));
        Logger::info(std::format("    - m_usSolidFlags: {}", (int)usSolidFlags));
        Logger::info(std::format("    - m_nEnablePhysics: {}", (int)nEnablePhysics));
        Logger::info("");

        Logger::info("  [Other Bounding Shapes]");
        Logger::info(std::format("    - m_flBoundingRadius: {:.2f}", flBoundingRadius));
        Logger::info(std::format("    - m_nSurroundType: {}", (int)nSurroundType));
        Logger::info(std::format("    - m_vecSpecifiedSurroundingMins: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", specifiedMins.x, specifiedMins.y, specifiedMins.z));
        Logger::info(std::format("    - m_vecSpecifiedSurroundingMaxs: X:{:.2f}, Y:{:.2f}, Z:{:.2f}", specifiedMaxs.x, specifiedMaxs.y, specifiedMaxs.z));

        Logger::info("======================================================================");

        // Stop the thread
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void inspectModelPointerChain(uintptr_t pawn, MemoryManagement &memManager)
    {
        // --- Offsets from your client_dll.json ---
        const uintptr_t C_BaseEntity_m_CBodyComponent = 0x38;
        const uintptr_t CBodyComponentSkeletonInstance_m_skeletonInstance = 0x50;
        const uintptr_t CSkeletonInstance_m_modelState = 0x170;
        const uintptr_t CModelState_m_hModel = 0xA0;

        Logger::info("==========================================================");
        Logger::warn(std::format("Starting Pointer Chain Inspection for Pawn: {:#x}", pawn));
        Logger::info("==========================================================");

        // --- Step 1: CBodyComponent ---
        uintptr_t pBodyComponent = memManager.ReadMem<uintptr_t>(pawn + C_BaseEntity_m_CBodyComponent);
        if (!pBodyComponent)
        {
            Logger::error("Chain failed at Step 1: pBodyComponent is NULL.");
            return;
        }
        Logger::info(std::format("Step 1: pBodyComponent found at -> {:#x}", pBodyComponent));
        DumpMemory("Body Component Memory", pBodyComponent, 128, memManager);

        // --- Step 2: CSkeletonInstance ---
        uintptr_t pSkeletonInstance = memManager.ReadMem<uintptr_t>(pBodyComponent + CBodyComponentSkeletonInstance_m_skeletonInstance);
        if (!pSkeletonInstance)
        {
            Logger::error("Chain failed at Step 2: pSkeletonInstance is NULL.");
            return;
        }
        Logger::info(std::format("Step 2: pSkeletonInstance found at -> {:#x}", pSkeletonInstance));
        DumpMemory("Skeleton Instance Memory", pSkeletonInstance, 512, memManager); // Dump more here, as it contains ModelState

        // --- Step 3: CModelState ---
        // Remember, this is embedded. Its address is relative to pSkeletonInstance.
        uintptr_t pModelState = pSkeletonInstance + CSkeletonInstance_m_modelState;
        Logger::info(std::format("Step 3: pModelState should be at -> {:#x}", pModelState));
        // We already dumped this memory as part of the Skeleton Instance dump.

        // --- Step 4: CModel Handle (hModel) ---
        uintptr_t hModel = memManager.ReadMem<uintptr_t>(pModelState + CModelState_m_hModel);
        if (!hModel)
        {
            Logger::error("Chain failed at Step 4: hModel is NULL.");
            return;
        }
        Logger::info(std::format("Step 4: hModel found, points to -> {:#x}", hModel));
        DumpMemory("CModel Memory (Target of hModel)", hModel, 128, memManager);

        Logger::info("==========================================================");
        Logger::info("INSPECTION COMPLETE");
        Logger::info("==========================================================");
    }
    enum class PredictedAction
    {
        Unknown,
        Jump,
        JumpCrouch,
        Crouch
    };

    struct ThreatInfo
    {
        PredictedAction action = PredictedAction::Unknown;
        float tti = 100.0f; // Time To Impact
    };

    // The "Whiteboard" where all active handlers post their status.
    std::unordered_map<uintptr_t, ThreatInfo> g_activeThreats;
    std::mutex g_activeThreatsMutex;

    // The RAII helper class for automatic cleanup
    class ThreatLifecycleManager
    {
    public:
        // Constructor: Called when the object is created. Adds the threat to the map.
        ThreatLifecycleManager(uintptr_t pawn) : pawn_address(pawn)
        {
            std::lock_guard<std::mutex> lock(g_activeThreatsMutex);
            g_activeThreats[pawn_address] = ThreatInfo(); // Add to the whiteboard
        }

        // Destructor: Called automatically when the function exits. Removes the threat.
        ~ThreatLifecycleManager()
        {
            std::lock_guard<std::mutex> lock(g_activeThreatsMutex);
            g_activeThreats.erase(pawn_address); // Remove from the whiteboard
        }

    private:
        uintptr_t pawn_address;
    };

void handleAngleInspection(uintptr_t threatPawn, LocalPlayer localPlayer, MemoryManagement& memManager) {
    
    // --- Offsets from your client.dll.json ---
    const uintptr_t C_BaseEntity_m_pGameSceneNode = clientDLL::C_BaseEntity_["m_pGameSceneNode"];
    const uintptr_t CGameSceneNode_m_angAbsRotation = clientDLL::CGameSceneNode_["m_angAbsRotation"];
    
    // Get the threat's name once for logging
    std::string threatName = getEntityName(threatPawn, memManager);
    
    Logger::info(std::format("--- Starting Angle Inspector for: '{}' ({:#x}) ---", threatName, threatPawn));

    auto threadStartTime = std::chrono::steady_clock::now();
    const auto threadTimeout = std::chrono::seconds(15);

    // --- High-Frequency Logging Loop ---
    while (true) {
        // Run this check fairly frequently to see live updates
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 times per second

        // Check for timeout
        if (std::chrono::steady_clock::now() - threadStartTime > threadTimeout) {
            Logger::error(std::format("Angle Inspector for {} timed out.", threatName));
            return;
        }

        // --- Get live data ---
        uintptr_t pGameSceneNode = memManager.ReadMem<uintptr_t>(threatPawn + C_BaseEntity_m_pGameSceneNode);
        if (!pGameSceneNode) {
            Logger::warn(std::format("SceneNode for {} disappeared. Ending inspection.", threatName));
            return; // Threat is gone
        }

        // *** THIS IS THE CRITICAL READ ***
        Vector3 liveRotation = memManager.ReadMem<Vector3>(pGameSceneNode + CGameSceneNode_m_angAbsRotation);

        // Log the findings
        Logger::info(std::format("[{}] Angles -> Pitch: {:.2f}, Yaw: {:.2f}, Roll: {:.2f}", 
            threatName, liveRotation.x, liveRotation.y, liveRotation.z));
    }
}

    void handleSingleThreat(uintptr_t threatPawn, Vector3 myInitialPos, LocalPlayer localPlayer, float speed, std::string threatName)
    {


        {
            std::lock_guard<std::mutex> lock(g_activeThreatsMutex);
            g_activeThreats[threatPawn] = ThreatInfo();
        }
        ThreatLifecycleManager lifecycle(threatPawn);

    struct LaserConfig {
        std::vector<std::string> left_tilt_names;
        std::vector<std::string> right_tilt_names;
        std::vector<std::string> hardcoded_bottom_lasers;
        std::vector<std::string> hardcoded_up_lasers;
    };
        LaserConfig config;
        std::string currentMapProfile = "ze_ffxii_westersand_v8";

        //ze_mochi_island
        //ze_ffxii_westersand_v8

                //         std::thread(handleAngleInspection, 
                //             threatPawn, 
                //             localPlayer, 
                //             std::ref(MemMan) // Pass MemMan by reference
                // ).detach();

         try {
        std::ifstream configFile("laser_configs.json");
        if (configFile.is_open()) {
            jsonek data = jsonek::parse(configFile);
            if (data.contains(currentMapProfile)) {
                jsonek& profile = data[currentMapProfile];
                // Safely load all four lists
                if (profile.contains("left_tilt_names") && profile["left_tilt_names"].is_array()) 
                    config.left_tilt_names = profile["left_tilt_names"].get<std::vector<std::string>>();
                
                if (profile.contains("right_tilt_names") && profile["right_tilt_names"].is_array()) 
                    config.right_tilt_names = profile["right_tilt_names"].get<std::vector<std::string>>();

                if (profile.contains("hardcoded_bottom_lasers") && profile["hardcoded_bottom_lasers"].is_array()) 
                    config.hardcoded_bottom_lasers = profile["hardcoded_bottom_lasers"].get<std::vector<std::string>>();
                
                if (profile.contains("hardcoded_up_lasers") && profile["hardcoded_up_lasers"].is_array()) 
                    config.hardcoded_up_lasers = profile["hardcoded_up_lasers"].get<std::vector<std::string>>();
            }
        }
    } catch (const std::exception& e) {
        Logger::error(std::format("Error loading laser config: {}", e.what()));
    }


       // Logger::error(modelName + " : " + threatName);
        std::string modelName = getModelNameFromPawn(threatPawn, MemMan);

          bool isHardcodedJump = false;
    for(const auto& name : config.hardcoded_bottom_lasers) {
        if (modelName.find(name) != std::string::npos) {
            isHardcodedJump = true;
            break;
        }
    }

        bool isHardcodedCrouch = false;
    if (!isHardcodedJump) {
        for(const auto& name : config.hardcoded_up_lasers) {
            if (modelName.find(name) != std::string::npos) {
                isHardcodedCrouch = true;
                break;
            }
        }
    }

        bool isLeftTilt = false;
                bool isRightTilt = false;




    if (!isHardcodedJump && !isHardcodedCrouch) {

        for (const auto &model_substring : config.left_tilt_names)
        {
            if (modelName.find(model_substring) != std::string::npos)
            {
                isLeftTilt = true;
                Logger::info(std::format("Left Tilting"));

                break;
            }
        }
        if (!isLeftTilt)
        {
            for (const auto &model_substring : config.right_tilt_names)
            {
                if (modelName.find(model_substring) != std::string::npos)
                {
                    isRightTilt = true;
                    Logger::info(std::format("Right Tilting"));
                    break;
                }
            }
        }
    }
            uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);

            const uintptr_t CGameSceneNode_m_angAbsRotation = clientDLL::CGameSceneNode_["m_angAbsRotation"];


            Vector3 liveRotation = MemMan.ReadMem<Vector3>(pGameSceneNode + CGameSceneNode_m_angAbsRotation);

            if(liveRotation.z > 2){
                isLeftTilt = true;
            }
            else if( liveRotation.z < -2){
                isRightTilt = true;
            }

        bool isTiltedLaser = isLeftTilt || isRightTilt;
        bool isHardcodedLaser = isHardcodedJump || isHardcodedCrouch;

        // --- DODGE CONFIGURATION ---
        float PLAYER_JUMP_EXECUTION_TIME = 0.38f + miscConf.latencyLasers + 0.02f;
        const float PLAYER_CROUCH_EXECUTION_TIME = 1.35f;
        const float CROUCHING_EYE_HEIGHT = 54.0f;
        const float STANDING_FEET_HEIGHT = 53.99999;
        constexpr int FL_ONGROUNDA = (1 << 0); // Player is standing on ground.

        std::vector<std::string> g_left_tilt_names;
        std::vector<std::string> g_right_tilt_names;

        const float reference_z = myInitialPos.z;

        // for tilted testingg:
        const float PLAYER_FEET_Z = 0.0f;
        const float PLAYER_CROUCH_HEAD_Z = 54.0f;
        const float PLAYER_STAND_HEAD_Z = 72.0f;

        const float LOCAL_ORIGIN_TOLERANCE = 1.0f; // To account for float precision issues

        auto threadStartTime = std::chrono::steady_clock::now();
        const auto threadTimeout = std::chrono::seconds(15); // Max lifetime for a handler thread

        while (true)
        {

            // Safety timeout
            if (std::chrono::steady_clock::now() - threadStartTime > threadTimeout)
            {
                Logger::error(std::format("Handler for {:#x} timed out.", threatPawn));
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }

            // --- Get Live Data ---

            Vector3 threatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);
            Vector3 threatDirection = threatVel.Normalize();

            float threatSpeed = threatVel.Length();
            // if (std::abs(speed - threatSpeed) > 0.0001f)
            // {
            //    Logger::info("height bel ");
            //     //Logger::info(std::format("ABC"));
            //             std::lock_guard<std::mutex> lock(g_cleanupMutex);
            // g_cleanupQueue.push_back(threatPawn);
            // return;
            // }

            if (std::abs(speed - threatSpeed) > 0.001f)
            {
                // Logger::info("height bel ");
                 Logger::info(std::format("ABC"));
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }
            uintptr_t pGameSceneNode = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            // if (!pGameSceneNode){
            //     return; // Threat is gone
            // }



                

            Vector3 threatOrigin = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);

            Vector3 myPos = localPlayer.getOrigin();

            uintptr_t pCollision = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pCollision"]);
            // if (!pCollision){
            //     continue; // Skip frame if no collision data
            // }
                

            Vector3 threatMins = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMins"]);
            Vector3 threatMaxs = MemMan.ReadMem<Vector3>(pCollision + clientDLL::CCollisionProperty_["m_vecMaxs"]);
            Vector3 threatRotation = MemMan.ReadMem<Vector3>(pGameSceneNode + clientDLL::CGameSceneNode_["m_angRotation"]);

            // 2. Calculate the 8 corners of the laser's hitbox in the world
            Vector3 localCorners[8] = {
                {threatMins.x, threatMins.y, threatMins.z}, {threatMins.x, threatMaxs.y, threatMins.z}, {threatMaxs.x, threatMaxs.y, threatMins.z}, {threatMaxs.x, threatMins.y, threatMins.z}, {threatMins.x, threatMins.y, threatMaxs.z}, {threatMins.x, threatMins.y, threatMaxs.z}, {threatMaxs.x, threatMaxs.y, threatMaxs.z}, {threatMaxs.x, threatMins.y, threatMaxs.z}};
            Vector3 worldCorners2[8];
            for (int i = 0; i < 8; ++i)
            {
                worldCorners2[i] = misc::RotatePoint(localCorners[i], threatRotation) + threatOrigin;
            }
            // 3. Find the corner that is "most forward" along the path of travel
            float maxProjection = -FLT_MAX;
            Vector3 leadingEdgePoint = threatOrigin; // Default to origin
            for (const auto &corner : worldCorners2)
            {
                // Project each corner onto the direction vector relative to the origin.
                float projection = (corner - threatOrigin).Dot(threatDirection);
                if (projection > maxProjection)
                {
                    maxProjection = projection;
                    // This corner is the one that forms the "tip" of the laser.
                }
            }

            // The leading edge is the origin plus the offset to the tip.
            leadingEdgePoint = threatOrigin + (threatDirection * maxProjection);

            // 4. Calculate the TTI from YOU to this new, accurate LEADING EDGE POINT
            Vector3 vecToPlayerFromEdge = myPos - leadingEdgePoint;
            float forwardDistance = vecToPlayerFromEdge.Dot(threatDirection);
            if (forwardDistance < 0)
            {
                Logger::info("Leading edge has passed");
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return; // Leading edge has passed
            }

            float true_tti = forwardDistance / threatSpeed;

            float predictedLaserHeight;

        if (isHardcodedLaser) {
            if (isHardcodedJump) {
                predictedLaserHeight = 10.0f; 
                 Logger::info("Hardcoded JUMP override active for " + threatName);
            } else { // isHardcodedCrouch
                predictedLaserHeight = 60.0f; 
                 Logger::info("Hardcoded CROUCH override active for " + threatName);
            }
            }
            else{
                 if (isTiltedLaser)
                     {   // IS TILTED?
                // Logger::info("TILTRED "+std::to_string(aabb_height) );
                bool isMovingMostlyOnY = std::abs(threatDirection.y) > std::abs(threatDirection.x);

                // 2. Get the full bounds of the AABB in world coordinates
                float laser_min_x = threatOrigin.x + threatMins.x;
                float laser_max_x = threatOrigin.x + threatMaxs.x;
                float laser_min_y = threatOrigin.y + threatMins.y;
                float laser_max_y = threatOrigin.y + threatMaxs.y;
                float laser_bottom_z = threatOrigin.z + threatMins.z;
                float laser_top_z = threatOrigin.z + threatMaxs.z;

                // 3. Calculate my percentage position along the laser's width
                float my_relevant_coord = isMovingMostlyOnY ? myPos.x : myPos.y;
                float laser_min_relevant_coord = isMovingMostlyOnY ? laser_min_x : laser_min_y;
                float laser_max_relevant_coord = isMovingMostlyOnY ? laser_max_x : laser_max_y;

                float width = laser_max_relevant_coord - laser_min_relevant_coord;
                float my_pos_along_width = my_relevant_coord - laser_min_relevant_coord;

                float percentage = 0.5f;
                if (width > 1.0f)
                {
                    percentage = my_pos_along_width / width;
                    percentage = (std::max)(0.0f, std::min(1.0f, percentage));
                }

                if (isRightTilt)
                {
                    percentage = 1.0f - percentage;
                }

                float interpolated_height = laser_bottom_z + ((laser_top_z - laser_bottom_z) * percentage);
                float laser_thickness = 4.0f; // Guess the thickness of the individual hurtboxes

                float predicted_laser_bottom_at_my_pos = interpolated_height - (laser_thickness / 2.0f);

                predictedLaserHeight = predicted_laser_bottom_at_my_pos - reference_z;
            }
            else
            {
                predictedLaserHeight = leadingEdgePoint.z - reference_z;
                // predictedLaserHeight = threatOrigin.z - myPos.z;
            }
            }
        

            // FOR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE OR TESTING ABOVE

            bool action_taken = false;

            int flagss = localPlayer.getFlags();
            bool onGrounds = (flagss & FL_ONGROUNDA);

            if (predictedLaserHeight < -50.25f && onGrounds)
            {
                Logger::info("za nisko na groundsach: " + std::to_string(onGrounds));
                action_taken = true;
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }
            if (predictedLaserHeight > 105.0f && onGrounds)
            {
                Logger::info("za wysoko: ");
                action_taken = true;
                std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                misc::g_cleanupQueue.push_back(threatPawn);
                return;
            }

            PredictedAction myAction = PredictedAction::Unknown;
            if (predictedLaserHeight < CROUCHING_EYE_HEIGHT && predictedLaserHeight >= 0.0f)
            {
                myAction = (predictedLaserHeight > 37.0f) ? PredictedAction::JumpCrouch : PredictedAction::Jump;
            }
            else if (predictedLaserHeight >= CROUCHING_EYE_HEIGHT && predictedLaserHeight < 100.0f)
            {
                myAction = PredictedAction::Crouch;
            }
           // Logger::info(" Height: " + std::to_string(predictedLaserHeight) + "tti: " + std::to_string(true_tti));


            // 3. Continuously update my status on the whiteboard.
            {
                std::lock_guard<std::mutex> lock(g_activeThreatsMutex);
                if (g_activeThreats.count(threatPawn))
                { // Check if we are still supposed to be active
                    g_activeThreats[threatPawn].action = myAction;
                    g_activeThreats[threatPawn].tti = true_tti;
                }
                else
                {
                    Logger::info("Another thread might have cleaned us up. Exit gracefully.");
                    return;
                }
            }

            bool shouldExecute = false;
            if ((myAction == PredictedAction::Jump || myAction == PredictedAction::JumpCrouch) && true_tti <= PLAYER_JUMP_EXECUTION_TIME)
            {
                shouldExecute = true;
            }
            else if (myAction == PredictedAction::Crouch && true_tti <= PLAYER_CROUCH_EXECUTION_TIME)
            {
                shouldExecute = true;
            }

            // Logger::info(" X: "+ std::to_string(leadingEdgePoint.x - myPos.x));
            // Logger::info(" Y: "+ std::to_string(leadingEdgePoint.y - myPos.y));

            if (shouldExecute)
            {
                while (!onGrounds)
                {
                    flagss = localPlayer.getFlags();
                    onGrounds = (flagss & FL_ONGROUNDA);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
                }
                if (myAction == PredictedAction::Jump)
                {
                    Logger::info("Jump once.");
                    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                }
                else if (myAction == PredictedAction::JumpCrouch)
                {
                    Logger::info("JumpCrouch once.");
                    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(17));
                    keybd_event(VK_CONTROL, 0, 0, 0);
                   // std::this_thread::sleep_for(std::chrono::milliseconds(550));
                }
                else if (myAction == PredictedAction::Crouch)
                {
                    Logger::info("Crouch once.");
                    keybd_event(VK_CONTROL, 0, 0, 0);
                }
                Logger::info(" ACTION DONE, Height: " + std::to_string(predictedLaserHeight) + "tti: " + std::to_string(true_tti));

                if (myAction == PredictedAction::Jump)
                {
                    while (true)
                    {

                        Vector3 currentMyPos = localPlayer.getOrigin();
                        uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
                        if (!pCurrentGCS)
                        {
                            Logger::info("[pCurrentGCS issue.");

                            break;
                        }

                        Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
                        Vector3 currentThreatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);

                        if (currentThreatVel.LengthSqr() < 1.0f)
                        {
                            Logger::info("Threat stopped..");
                            break;
                        }

                        Vector3 threatDirection = threatVel.Normalize();
                        float projection = (currentMyPos - currentThreatOrigin).Dot(threatDirection);

                        // Logger::warn(std::to_string(projection));

                        if (projection < -1.0f)
                        { // Use a small negative buffer to be safe
                            Logger::info(std::format("[ThreatDetector] Threat has passed (projection: {:.2f}).", projection));
                            break; // Exit the loop to stand up
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }

                if (myAction == PredictedAction::Crouch || myAction == PredictedAction::JumpCrouch)
                {

                    while (true)
                    {

                        Vector3 currentMyPos = localPlayer.getOrigin();
                        uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
                        // if (!pCurrentGCS)
                        // {
                        //     return;
                        // }

                        Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
                        Vector3 currentThreatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);

                        if (currentThreatVel.LengthSqr() < 1.0f)
                        {
                             Logger::info("[CrouchHold] Threat stopped. Releasing.");
                            return;
                        }

                        Vector3 threatDirection = threatVel.Normalize();
                        float projection = (currentMyPos - currentThreatOrigin).Dot(threatDirection);

                        // Logger::warn(std::to_string(projection));

                        if (projection < -0.2f)
                        { // Use a small negative buffer to be safe
                            Logger::info(std::format("[ThreatDetector] Threat has passed (projection: {:.2f}).", projection));
                            break; // Exit the loop to stand up
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }

                    // NOW, BEFORE RELEASING, PEEK AT THE NEXT THREAT
                    bool shouldHoldCrouch = false;
                    uintptr_t nextThreatPawn = 0;
                    float nextThreatTTI = 100.0f;

                    { // Lock the mutex to safely inspect the whiteboard
                        std::lock_guard<std::mutex> lock(g_activeThreatsMutex);

                        // Find the next closest threat that is NOT me.
                        for (const auto &pair : g_activeThreats)
                        {
                            if (pair.first == threatPawn)
                                continue; // Skip myself
                            if (pair.second.tti < nextThreatTTI)
                            {
                                nextThreatTTI = pair.second.tti;
                                nextThreatPawn = pair.first;
                            }
                        }

                        if (nextThreatPawn != 0)
                        {
                            // We found a next threat. What is its required action?
                            PredictedAction nextAction = g_activeThreats[nextThreatPawn].action;
                            // If the next threat is close and requires a crouch, we should hold!
                            if ((nextAction == PredictedAction::Crouch || nextAction == PredictedAction::JumpCrouch) && nextThreatTTI < 2.2f)
                            { // 2.0s is a reasonable lookahead window
                                shouldHoldCrouch = true;
                                Logger::info("Next threat also needs crouch. HOLDING CTRL.");
                            }
                        }
                    } // Mutex is unlocked

                    if (!shouldHoldCrouch && !misc::isCrouchOnly)
                    {
                        Logger::info("Next threat does not need crouch. Releasing CTRL.");
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                    }
                }
            }
            else
            {
                continue;
            }

            // if (predictedLaserHeight <= STANDING_FEET_HEIGHT && predictedLaserHeight >= 0.0f) {
            //     if (true_tti <= PLAYER_JUMP_EXECUTION_TIME) {
            //             mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
            //             if(predictedLaserHeight > 41.0f){
            //                 Logger::info("HANDLER: jumpcrouch on and height: "+std::to_string(predictedLaserHeight)+" ,tti: "+ std::to_string(true_tti));

            //                 std::this_thread::sleep_for(std::chrono::milliseconds(3));
            //                 keybd_event(VK_CONTROL, 0, 0, 0);
            //                 std::this_thread::sleep_for(std::chrono::milliseconds(470));
            //                 keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
            //             }
            //             else{
            //                Logger::info("HANDLER: JUMP CLASSIC onand height: "+std::to_string(predictedLaserHeight)+" ,tti: "+ std::to_string(true_tti));

            //             }

            //             action_taken = true;
            //     }
            // } else if (predictedLaserHeight >= CROUCHING_EYE_HEIGHT && predictedLaserHeight < 100.0f) {
            //     if (true_tti <= PLAYER_CROUCH_EXECUTION_TIME) {
            //          Logger::info("HANDLER: CROUCH on and height: "+std::to_string(predictedLaserHeight) +" ,tti: "+ std::to_string(true_tti));

            //         // --- THIS IS THE CORRECTED CROUCH LOGIC ---
            //         keybd_event(VK_CONTROL, 0, 0, 0); // Press and HOLD crouch
            //         auto crouchStartTime = std::chrono::steady_clock::now();

            //         while (true) {
            //             if (std::chrono::steady_clock::now() - crouchStartTime > std::chrono::seconds(5)) {
            //                  action_taken = true;
            //                 Logger::error("[CrouchHold] Timed out for {:#x}", threatPawn);
            //                 break;
            //             }

            //             Vector3 currentMyPos = localPlayer.getOrigin();
            //             uintptr_t pCurrentGCS = MemMan.ReadMem<uintptr_t>(threatPawn + clientDLL::C_BaseEntity_["m_pGameSceneNode"]);
            //             if (!pCurrentGCS) {
            //             action_taken = true;
            //                  break;
            //             }

            //             Vector3 currentThreatOrigin = MemMan.ReadMem<Vector3>(pCurrentGCS + clientDLL::CGameSceneNode_["m_vecAbsOrigin"]);
            //             Vector3 currentThreatVel = MemMan.ReadMem<Vector3>(threatPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);

            //             if (currentThreatVel.LengthSqr() < 1.0f) {
            //                 //Logger::info("[CrouchHold] Threat stopped. Releasing.");
            //                 action_taken = true;
            //                  break;
            //             }

            //            Vector3 threatDirection = threatVel.Normalize();
            //                 float projection = (currentMyPos - currentThreatOrigin).Dot(threatDirection);

            // 				//Logger::warn(std::to_string(projection));

            //                 if (projection < -7.0f) { // Use a small negative buffer to be safe
            //                     Logger::info(std::format("[ThreatDetector] Threat has passed (projection: {:.2f}). Standing up.", projection));
            //                     break; // Exit the loop to stand up
            //                 }

            //             std::this_thread::sleep_for(std::chrono::microseconds(100));
            //         }

            //         keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); // Release crouch

            //             action_taken = true;
            //     }
            // }

            if (shouldExecute)
            {
                {
                    std::lock_guard<std::mutex> lock(misc::g_cleanupMutex);
                    misc::g_cleanupQueue.push_back(threatPawn);
                    return;
                }
            }
        }
    }

    std::atomic<bool> g_autoBhopEnabled = false;
    std::atomic<bool> g_stopBhopThread = false;
    std::atomic<bool> g_stopItemESPThread = false;
    std::atomic<bool> isItemESPEnabled = false;
    std::thread g_bhopThread;
    std::thread g_speedInfoThread;
    std::thread g_itemESPThread;

    struct TrackedMessage
    {
        std::string text;
        std::chrono::steady_clock::time_point lastSeen;
    };

    std::thread g_chatMonitorThread;

    constexpr int FL_ONGROUND = (1 << 0); // Player is standing on ground.

    // Worker function for the bunny hop thread (keep it internal)
    //     void bhopWorker(LocalPlayer localPlayer) {
    //         while (!g_stopBhopThread) { // Loop until explicitly told to stop

    // 			//if (onGround) {
    //                     mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
    // 					int randomValue = getRandomInt(14444, 16000);

    // 					//std::this_thread::sleep_for(std::chrono::microseconds(15626));
    // 					std::this_thread::sleep_for(std::chrono::microseconds(randomValue));
    // 					//std::this_thread::sleep_for(std::chrono::milliseconds(16));

    //                 }
    // 				//std::this_thread::sleep_for(std::chrono::microseconds(16625));

    //       //  }

    // bool LaunchFlaskServer(PROCESS_INFORMATION& pi) {
    //     STARTUPINFO si;
    //     ZeroMemory(&si, sizeof(STARTUPINFO));
    //     si.cb = sizeof(STARTUPINFO);
    //     // Hide the console window for the python process
    //     si.dwFlags = STARTF_USESHOWWINDOW;
    //     si.wShowWindow = SW_HIDE;

    //     char cmd[] = "python ocr_flask_server.py";

    //     if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    //         return false;
    //     }
    //     return true;
    // }

    std::optional<std::chrono::seconds> ParseTimerDuration(const std::string &text)
    {
        std::string lower_text = text;
        std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

        // Regex Explanation:
        // (^|\\s|\\W) : Start of string, whitespace, or non-word char. Prevents matching "12345s" from "abc12345s".
        // (\\d{1,3})   : Capture a number with 1 to 3 digits. This is our sanity check for the value.
        // \\s*         : Optional whitespace.
        // (min|m|sec|s): The unit keyword.
        // (?:...)?s?   : Optional parts for "minute", "second", and plural 's'.
        // (?!\\w)      : Must not be followed by another letter/number (e.g., to reject "sector").
        std::regex pattern("(^|\\s|\\W)(\\d{1,3})\\s*(?:min(?:ute)?s?|m|sec(?:ond)?s?|s)(?!\\w)");

        std::smatch match;
        if (std::regex_search(lower_text, match, pattern))
        {
            // match[2] is our captured number because of the (^|\\s|\\W) group at the start
            if (match.size() >= 3)
            {
                long long number = std::stoll(match[2].str());

                // --- Sanity Check Layer ---
                // A number over ~10 minutes (600s) is likely not a real short-term timer.
                // A number of 0 is also invalid. Adjust the upper limit if needed.
                if (number == 0 || number > 600)
                {
                    return std::nullopt; // Fails sanity check
                }

                std::string unit_part = match[0].str(); // Full matched text, e.g., " 2 min"

                if (unit_part.find("min") != std::string::npos || unit_part.find("m") != std::string::npos)
                {
                    return std::chrono::seconds(number * 60);
                }
                return std::chrono::seconds(number);
            }
        }

        // --- Hardened Fallback for context like "in 10" ---
        // (^|\\s)      : Must be preceded by start of string or whitespace.
        // in           : The literal word "in".
        // (\\d{1,3})   : Capture a number with 1 to 3 digits.
        // (?!\\d)      : Must not be followed by another digit.
        std::regex context_pattern("(^|\\s)(in|for) (\\d{1,3})(?!\\d)");
        if (std::regex_search(lower_text, match, context_pattern))
        {
            // match[3] is the number because we added a capture group for "in|for"
            if (match.size() >= 4)
            {
                long long number = std::stoll(match[3].str());
                if (number > 0 && number <= 120)
                { // Context timers are usually short
                    return std::chrono::seconds(number);
                }
            }
        }

        std::regex bare_number_pattern("(^|\\W)(\\d{1,2})($|\\W)");
        if (std::regex_search(lower_text, match, bare_number_pattern))
        {
            if (match.size() >= 3)
            {
                long long number = std::stoll(match[2].str());

                // We only accept this if the number is very small (e.g., under 60 seconds)
                // AND the original message is short. This avoids capturing numbers from long sentences.
                if (number > 0 && number < 60 && text.length() < 30)
                {
                    return std::chrono::seconds(number);
                }
            }
        }

        return std::nullopt; // No sane timer pattern found.
    }

    void stopChatMonitorThread()
    {
        if (g_chatMonitorThread.joinable())
        {
            misc::g_stopChatMonitorThread = true;
            // Clear the message list when the feature is turned off.
            std::lock_guard<std::mutex> lock(misc::g_displayedMessagesMutex);
            misc::g_displayedMessages.clear();
            g_chatMonitorThread.join();
        }
    }

    uintptr_t PatternScan(uintptr_t modBase, DWORD modSize, const char *pattern, const char *mask)
    {
        std::vector<char> buffer(modSize);
        if (!MemMan.ReadRawMem(modBase, buffer.data(), modSize))
        {
            Logger::error("[ChatTrigger] Failed to read module into buffer for pattern scan.");
            return 0;
        }

        size_t patternLen = strlen(mask);
        for (size_t i = 0; i < modSize - patternLen; ++i)
        {
            bool found = true;
            for (size_t j = 0; j < patternLen; ++j)
            {
                if (mask[j] != '?' && pattern[j] != buffer[i + j])
                {
                    found = false;
                    break;
                }
            }
            if (found)
            {
                return modBase + i;
            }
        }
        return 0;
    }

    std::string DecodeHtmlEntities(std::string text)
    {
        // Use a loop for multiple replacements in one string
        size_t pos = 0;
        while ((pos = text.find('&', pos)) != std::string::npos)
        {
            if (text.compare(pos, 4, ">") == 0)
                text.replace(pos, 4, ">");
            else if (text.compare(pos, 4, "<") == 0)
                text.replace(pos, 4, "<");
            else if (text.compare(pos, 5, "&") == 0)
                text.replace(pos, 5, "&");
            // Add more entities here if you find them (e.g., " for ")
            else
                pos++; // Not a recognized entity, skip it
        }
        return text;
    }

    bool ContainsNumber(const std::string &str)
    {
        return std::any_of(str.begin(), str.end(), ::isdigit);
    }

    // NEW: Helper to extract message from the raw HTML-like string
    std::string ExtractMessage(std::string text)
    { // Take by value to modify it
        if (text.empty())
        {
            return "";
        }

        // Step 1: Decode HTML entities using a standard, robust find-and-replace loop.
        const std::vector<std::pair<std::string, std::string>> entities = {
            {"<", "<"},
            {">", ">"},
            {"&", "&"}
            // Add more here in the future if needed
        };

        for (const auto &entity : entities)
        {
            size_t pos = 0;
            while ((pos = text.find(entity.first, pos)) != std::string::npos)
            {
                text.replace(pos, entity.first.length(), entity.second);
                pos += entity.second.length(); // Move past the replaced entity
            }
        }
        // After this step, your example becomes: "A player has picked up [ZM]<Gravity>"

        // Step 2: Strip ONLY valid HTML tags intelligently.
        std::string cleaned_text;
        cleaned_text.reserve(text.length());
        bool in_html_tag = false;

        for (size_t i = 0; i < text.length(); ++i)
        {
            if (text[i] == '<')
            {
                // A real HTML tag starts with a letter or a slash.
                // This prevents it from treating "<Gravity>" as a tag.
                if (i + 1 < text.length() && (isalpha(text[i + 1]) || text[i + 1] == '/'))
                {
                    in_html_tag = true;
                }
            }

            if (!in_html_tag)
            {
                cleaned_text += text[i];
            }

            if (text[i] == '>')
            {
                in_html_tag = false;
            }
        }

        // Step 3: Trim any leading/trailing whitespace.
        size_t first = cleaned_text.find_first_not_of(" \t\n\r");
        if (std::string::npos == first)
        {
            return "";
        }
        size_t last = cleaned_text.find_last_not_of(" \t\n\r");
        return cleaned_text.substr(first, (last - first + 1));
    }

    std::string NormalizeMessage(const std::string &msg)
    {
        std::string normalized = msg;
        normalized.erase(std::remove_if(normalized.begin(), normalized.end(), ::isdigit), normalized.end());
        // Also good to trim whitespace from the normalized version for consistency
        size_t first = normalized.find_first_not_of(" \t");
        if (std::string::npos == first)
            return "";
        size_t last = normalized.find_last_not_of(" \t");
        return normalized.substr(first, (last - first + 1));
    }

    // NEW: Thread starter function
    void startChatMonitorThread(MemoryManagement::moduleData client)
    {
        if (!g_chatMonitorThread.joinable())
        {
            misc::g_stopChatMonitorThread = false;
            g_chatMonitorThread = std::thread(chatMonitorWorker, client);
        }
    }

    void stopUniversalThreatThread()
    {
        if (g_universal_threat_thread.joinable())
        {
            g_stop_universal_threat_thread = true;
            g_isHighLaserThreat = false; // Ensure flag is reset
            g_universal_threat_thread.join();
           // misc::g_universal_tracker.clear();
        }
    }

    void startUniversalThreatThread(MemoryManagement::moduleData client, LocalPlayer localPlayer)
    {
        if (!g_universal_threat_thread.joinable())
        {
            g_stop_universal_threat_thread = false;
            g_universal_threat_thread = std::thread(universalThreatDetectorWorker, client, localPlayer);
        }
    }

    // NEW: The main worker function for reading chat messages from memory.
    void chatMonitorWorker(MemoryManagement::moduleData client)
    {
        Logger::info("[ChatTrigger] Starting chat monitor thread...");

        // --- Find the dynamic address for the chat messages ---
        // const char *chatPattern = "\x48\x83\xEC\x20\x48\x8B\x3D\x00\x00\x00\x00\x4C\x8B\xC9\x48\x85\xFF";
        // const char *chatMask = "xxxxxxx????xxxxxx";

        const char *chatPattern = "\x80\x38\x00\x0F\x85\x00\x00\x00\x00\x48\x8B\x0D\x00\x00\x00\x00\x48\x8D\x15\x00\x00\x00\x00\xE8\x00\x00\x00\x00";
        const char *chatMask = "xxxxx????xxx????xxx????x????";

        uintptr_t patternAddr = PatternScan(client.base, client.size, chatPattern, chatMask);

        if (!patternAddr)
        {
            Logger::error("[ChatTrigger] Could not find chat message pattern. Game may have updated.");
            return;
        }
        Logger::info(std::format("[ChatTrigger] Pattern found at client.dll + {:#x}", patternAddr - client.base));

        // const int32_t ripOffset = MemMan.ReadMem<int32_t>(patternAddr + 7);
        // const uintptr_t pStaticBase = patternAddr + 11 + ripOffset;
        // const std::vector<uintptr_t> offsets = {0x238, 0x138, 0x108, 0x0};



        //for:

// .text:0000000180AA3BD0                 push    rbx
// .text:0000000180AA3BD2                 sub     rsp, 20h
// .text:0000000180AA3BD6                 mov     rbx, rcx
// .text:0000000180AA3BD9                 call    sub_180A94090
// .text:0000000180AA3BDE                 mov     rcx, cs:qword_181D810E8
      //  const int32_t ripOffset = MemMan.ReadMem<int32_t>(patternAddr + 17);
      //  const uintptr_t pStaticBase = patternAddr + 21 + ripOffset;






//.text:00000001805A21C4                 cmp     byte ptr [rax], 0
//.text:00000001805A21C7                 jnz     loc_1805A2255
//.text:00000001805A21CD                 mov     rcx, cs:qword_181E8D900
        const int32_t ripOffset = MemMan.ReadMem<int32_t>(patternAddr + 12);
        const uintptr_t pStaticBase = patternAddr + 16 + ripOffset;

        const std::vector<uintptr_t> offsets = {0x250, 0x178, 0x118, 0x00};


        Logger::info(std::format("[ChatTrigger] Resolved static base pointer to: {:#x}", pStaticBase));

        std::string lastReadMessage = "";

        while (!misc::g_stopChatMonitorThread)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // --- Resolve pointer chain to get the message address ---
            uintptr_t pLastMessage = MemMan.ReadMem<uintptr_t>(pStaticBase);
            for (const auto &offset : offsets)
            {
                if (!pLastMessage)
                    break;
                if (offset == 0)
                    break; // Final pointer, don't read+add
                pLastMessage = MemMan.ReadMem<uintptr_t>(pLastMessage + offset);
            }

            if (!pLastMessage)
            {
                // This is normal before a map loads. Reset and wait.
                if (lastReadMessage != "")
                {
                    lastReadMessage = "";
                    Logger::info("[ChatTrigger] Pointer chain lost (map change?). Waiting to re-initialize...");
                }
                continue;
            }

            // --- Read and process the message ---
            char buffer[512] = {};
            MemMan.ReadRawMem(pLastMessage, buffer, sizeof(buffer) - 1);
            std::string currentMessage(buffer);

            if (currentMessage.empty() || currentMessage.length() < 5 || currentMessage == lastReadMessage)
            {
                continue; // No new message or message is too short to be useful
            }

            lastReadMessage = currentMessage; // Update to prevent re-processing

            // --- Key change: We only care about [CONSOLE] messages ---
            //Logger::info("asdasd");
            if (currentMessage.find("CONSOLE") == std::string::npos)
            {
                continue; // If it's not a console message, we ignore it completely.
            }

            // Now that we know it's a console message, clean it up for display.
            std::string extractedMsg = ExtractMessage(currentMessage);
            if (extractedMsg.empty())
            {
                continue;
            }

            // Remove the "[CONSOLE]: " prefix if it exists.
            // The cleaned string will be like "[CONSOLE] The message" or "CONSOLE: The message"
            std::string finalMsg = extractedMsg;
            if (auto pos = finalMsg.find("[CONSOLE]"); pos != std::string::npos)
            {
                // Find the actual message text after the tag
                size_t startPos = finalMsg.find_first_not_of(" \t:", pos + 9); // +9 for "[CONSOLE]"
                if (startPos != std::string::npos)
                {
                    finalMsg = finalMsg.substr(startPos);
                }
                else
                {
                    finalMsg = ""; // Nothing but whitespace after the tag
                }
            }
            else if (auto pos = finalMsg.find("CONSOLE:"); pos != std::string::npos)
            {
                // Handle the "CONSOLE: " variant
                size_t startPos = finalMsg.find_first_not_of(" \t", pos + 8); // +8 for "CONSOLE:"
                if (startPos != std::string::npos)
                {
                    finalMsg = finalMsg.substr(startPos);
                }
                else
                {
                    finalMsg = "";
                }
            }

            if (finalMsg.empty())
            {
                continue;
            }

            if (!ContainsNumber(finalMsg))
            {
                continue; // If no number is found, ignore this message.
            }

            auto durationOpt = ParseTimerDuration(finalMsg);

            // 2. If parsing fails, it's not a timer message we care about.
            if (!durationOpt.has_value())
            {
                continue;
            }
            auto duration = durationOpt.value();

            // At this point, `finalMsg` is the pure message, e.g., ">>The door will open in 15 seconds<<"
            std::string normalizedMsg = NormalizeMessage(finalMsg);

            // --- Check for duplicates and add to display list ---
            Logger::info(std::format("[ChatTrigger] New Timer: '{}' -> Duration: {}s", finalMsg, duration.count()));

            {
                std::lock_guard<std::mutex> lock(misc::g_displayedMessagesMutex);

                bool foundAndUpdated = false;
                // Search for an existing timer with the same normalized text
                for (auto &msg : misc::g_displayedMessages)
                {
                    if (msg.normalizedText == normalizedMsg)
                    {
                        // This is an update to an existing timer.
                        // Reset its values.
                        msg.messageText = finalMsg;
                        msg.duration = duration;
                        msg.creationTime = std::chrono::steady_clock::now();
                        foundAndUpdated = true;
                        break;
                    }
                }

                // If no existing timer was found, add this as a new one.
                if (!foundAndUpdated)
                {
                    if (misc::g_displayedMessages.size() >= 10)
                    { // Keep max limit
                        misc::g_displayedMessages.erase(misc::g_displayedMessages.begin());
                    }
                    misc::g_displayedMessages.push_back({finalMsg,
                                                         normalizedMsg, // Store the normalized text
                                                         duration,
                                                         std::chrono::steady_clock::now()});
                }
            }
        }
        Logger::info("[ChatTrigger] Chat monitor thread stopped.");
    }

    void DrawTextWithColoredNumbers(const std::string &text, const ImVec4 &numberColor)
    {
        // We will draw the text in segments: text, number, text, number...
        size_t start_pos = 0;
        size_t end_pos = 0;

        // Find the first digit
        end_pos = text.find_first_of("0123456789", start_pos);

        while (start_pos < text.length())
        {
            if (end_pos == std::string::npos)
            {
                // No more numbers, draw the rest of the string
                ImGui::TextUnformatted(text.c_str() + start_pos);
                break;
            }

            // 1. Draw the text part before the number
            if (end_pos > start_pos)
            {
                std::string text_part = text.substr(start_pos, end_pos - start_pos);
                ImGui::TextUnformatted(text_part.c_str());
                ImGui::SameLine(0, 0);
            }

            // 2. Find the end of the number sequence
            start_pos = end_pos;
            end_pos = text.find_first_not_of("0123456789", start_pos);

            if (end_pos == std::string::npos)
            {
                // Number is at the very end of the string
                end_pos = text.length();
            }

            // 3. Draw the number part in color
            std::string num_part = text.substr(start_pos, end_pos - start_pos);
            ImGui::TextColored(numberColor, "%s", num_part.c_str());

            if (end_pos < text.length())
            {
                ImGui::SameLine(0, 0);
            }

            // 4. Prepare for the next iteration
            start_pos = end_pos;
            end_pos = text.find_first_of("0123456789", start_pos);
        }
    }

    void speedInfoWorker(LocalPlayer localPlayer)
    {
        std::vector<float> highSpeeds;
        highSpeeds.reserve(50000);

        while (!g_stopBhopThread)
        {
            Vector3 playerVelocity = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);
            float speed2D = std::sqrt(playerVelocity.x * playerVelocity.x + playerVelocity.y * playerVelocity.y);
            misc::g_currentSpeed2D.store(speed2D);
            if (speed2D > 251.0f)
            {
                highSpeeds.push_back(speed2D);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(8333));
        }
        // misc::g_currentSpeed2D.store(0.0f);
        if (!highSpeeds.empty())
        {
            float sum = std::accumulate(highSpeeds.begin(), highSpeeds.end(), 0.0f);
            float averageSpeed = sum / highSpeeds.size();
            misc::g_currentSpeed2D.store(averageSpeed);
        }
        else
        {
            misc::g_currentSpeed2D.store(0.0f);
        }
    }

    void itemESPWorker(MemoryManagement::moduleData client)
    {

        while (!g_stopItemESPThread)
        {
            // std::string speedMessage = "is ON? " + std::to_string(g_stopItemESPThread) + "isItemESPEnabled: " + std::to_string(isItemESPEnabled);
            // Logger::info(speedMessage);
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // timeddraw::ProcessTimedDraws();
            C_CSPlayerPawn C_CSPlayerPawn(client.base);
            CGameSceneNode CGameSceneNode;
            view_matrix_t viewMatrix = MemMan.ReadMem<view_matrix_t>(client.base + offsets::clientDLL["dwViewMatrix"]);

            for (int i = 66; i < 12345; i++)
            {
                C_CSPlayerPawn.value = i;
                C_CSPlayerPawn.getListEntry();
                if (C_CSPlayerPawn.listEntry == 0)
                    continue;
                C_CSPlayerPawn.getPlayerPawn();
                if (C_CSPlayerPawn.playerPawn == 0)
                    continue;
                if (C_CSPlayerPawn.getOwner() != -1)
                    continue;

                uintptr_t entity = MemMan.ReadMem<uintptr_t>(C_CSPlayerPawn.playerPawn + 0x10);
                uintptr_t designerNameAddy2 = MemMan.ReadMem<uintptr_t>(entity + 0x18);

                // if (lowerCaseName2.empty()) {
                //	continue;
                // }


                uintptr_t designerNameAddy = MemMan.ReadMem<uintptr_t>(entity + 0x20);
                char designerNameBuffer[MAX_PATH]{};
                MemMan.ReadRawMem(designerNameAddy, designerNameBuffer, MAX_PATH);
                std::string name = std::string(designerNameBuffer);
                std::string lowerCaseName = utils::toLower(name);
                char designerNameBuffer2[MAX_PATH]{};
                MemMan.ReadRawMem(designerNameAddy2, designerNameBuffer2, MAX_PATH);

                std::string name2 = std::string(designerNameBuffer2);
                std::string lowerCaseName2 = utils::toLower(name2);

                if (!name2.empty())
                {
                    name = name + "_" + name2;
                }

                // bool isUsable = MemMan.ReadMem<bool>(C_CSPlayerPawn.playerPawn + 3372);
                // uintptr_t displayTextActualPtr = MemMan.ReadMem<uintptr_t>(C_CSPlayerPawn.playerPawn + 3376);
                // std::string displayText = "N/A";

                // if (displayTextActualPtr) {
                //	char displayTextBuffer[MAX_PATH]{};
                //	MemMan.ReadRawMem(displayTextActualPtr, displayTextBuffer, MAX_PATH);
                //	if (displayTextBuffer[0] != '\0') { // Check if string is not empty
                //		displayText = std::string(displayTextBuffer);
                //	}
                // }

                // if (isUsable || displayText != "N/A") {
                //	name = "weapon_" + name;
                //	lowerCaseName = "weapon_" + lowerCaseName;
                // }

                bool shouldProcess = false;
                {
                    std::lock_guard<std::mutex> lock(misc::itemESPFilterMutex); // Protect access to miscConf.itemESPFilter
                    for (const auto &filterStr : miscConf.itemESPFilter)
                    {
                        std::string lowerCaseFilterStr = utils::toLower(filterStr); // Convert filter string to lowercase
                        if (lowerCaseName.find(lowerCaseFilterStr) != std::string::npos)
                        {                         // If filter string IS found
                            shouldProcess = true; // Mark for processing (inclusion)

                            break;
                        }
                    }
                } // Lock is released here
                if (!shouldProcess)
                { // If NO filter string was found, then skip the item
                    continue;
                }

                if (strstr(name.c_str(), "weapon_"))
                {
                    name.erase(0, 7);
                }
                else
                {
                    continue;
                }

               // std::string xd = getModelName(C_CSPlayerPawn.playerPawn, MemMan);

                // if (isUsable || displayText != "N/A") {
                //	std::string buttonMessage = "Potential Usable Entity: DesignerName='" + name2 +
                //		"', DisplayText='" + displayText +
                //		"', IsUsable=" + (isUsable ? "true" : "false");
                //	Logger::info(buttonMessage);
                // }

                // int paintKit = MemMan.ReadMem<int>(entity + 5624);

                // if (paintKit > 0)
                //{
                //	continue;
                // }

                // int health = MemMan.ReadMem<int>(entity + 836);

                // std::string speedMessage = "add: " + std::to_string(entity) + ", " +name.c_str() + ", whast dat: " + lowerCaseName2;
                // Logger::info(speedMessage);

                CGameSceneNode.value = C_CSPlayerPawn.getCGameSceneNode();
                CGameSceneNode.getOrigin();
                CGameSceneNode.origin = CGameSceneNode.origin.worldToScreen(viewMatrix);

                //std::string ModelName = getModelName(C_CSPlayerPawn.playerPawn);

                if (CGameSceneNode.origin.z >= 0.01f)
                {
                    // Collect item data into the shared list
                    std::lock_guard<std::mutex> lock(misc::itemESPListMutex);
                    // Collect item data into the shared list
                    
                    misc::ItemESPData itemData = {ImVec2(CGameSceneNode.origin.x, CGameSceneNode.origin.y), CGameSceneNode.getOrigin(), name};
                    misc::itemESPList.push_back(itemData);
                }
            }
             std::this_thread::sleep_for(std::chrono::milliseconds(10)); 

            misc::itemESPList.clear();
        }
    }

    void bhopWorker(DWORD_PTR base, LocalPlayer localPlayer)
    {

        // int trig = 0;
        // uintptr_t ctrl = localPlayer.getPlayerController();
        // uint32_t lastTick = MemMan.ReadMem<uint32_t>( ctrl + clientDLL::CBasePlayerController_["m_nTickBase"] );

        // INPUT input;
        // input.type = INPUT_KEYBOARD;
        // input.ki.wVk = 0x36 ; // a lot of keys won't work, F keys work fine and F13-24 aren't used for anything
        // input.ki.dwExtraInfo = 0;
        // input.ki.time = 0;

        // 28 and 48 is the frame time

        // 6, 10 and 46
        int aaa = 0;

        uintptr_t globalVarsAddr = MemMan.ReadMem<uintptr_t>(base + offsets::clientDLL["dwGlobalVars"]);
        int lastTick = 0; // Keep track of the last tick we processed

        // mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
        while (!g_stopBhopThread)
        {
            int bbb = 0;
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            //  int currentTick = MemMan.ReadMem<int>(globalVarsAddr + 81);
            //  		//		std::string speedMessage = "tick rn: " + std::to_string(currentTick);
            //  	//	Logger::info(speedMessage);

            lastTick = MemMan.ReadMem<int>(globalVarsAddr + 81);

            //  int currentTick = MemMan.ReadMem<int>(globalVarsAddr + 81);
            //  		//		std::string speedMessage = "tick rn: " + std::to_string(currentTick);
            //  	//	Logger::info(speedMessage);

            // float frame_time = MemMan.ReadMem<float>(globalVarsAddr + 6);

            // 					constexpr float normalInterval = 1.0f / 64.0f;    // ≃0.015625
            // constexpr float lagThreshold  = 0.016f;           // anything above 20ms we count as lag

            // // scan offsets from 0 to, say, 128 bytes in 4-byte steps
            // for (int off = 0; off < 223; off ++) {
            // 	if(off == 64 || off == 60 ||off == 10 ||off == 7 ||off == 11 ||off == 47 ||off == 31 ||off == 6 ||off == 1 ||off == 25 ||off == 50 ||off == 54 ||off == 78 ||off == 73 ||off == 5 ||off == 9 ||off == 69 ||off == 46 ||off == 59 ||off == 62||off == 61||off == 30||off == 26||off == 79||off == 45||off == 33||off == 32||off == 51){
            // 		continue;
            // 	}
            //     float val = MemMan.ReadMem<float>(globalVarsAddr + off);
            //     if (val > lagThreshold && val < 0.070f) {
            //         // this field grows when the server is lagging
            // 		std::string speedMessage =  "Candidate lag-field at offset i:" + std::to_string(off) + " is  " + std::to_string(val) + "s";
            // 		Logger::info(speedMessage);
            //     }
            // }

            static int delta = 120;

            int flagss = localPlayer.getFlags();
            bool onGrounds = (flagss & FL_ONGROUND);
            // Vector3 playerVelocity = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);

            if (onGrounds)
            {
                int currentTicks = MemMan.ReadMem<int>(globalVarsAddr + 81);
                int one_or_zero = miscConf.trigg;
                float val = MemMan.ReadMem<float>(globalVarsAddr + 48);
                //	if(val > 0.016625){
                //		one_or_zero = 0;
                //	}
                while (lastTick + one_or_zero >= currentTicks)
                {
                    // Logger::info("loop, " + std::to_string(bbb));
                    bbb++;
                    currentTicks = MemMan.ReadMem<int>(globalVarsAddr + 81);
                    // std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                }
                std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
                // mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, delta, 0);
                std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
                delta = -delta;
                //	std::string speedMessage =  "Candidate lag-field at offset  " + std::to_string(val) + "s";
                aaa++;
                // Logger::info("num, " + std::to_string(aaa) + " OZ: " + std::to_string(one_or_zero));

                lastTick = currentTicks;
            }
            // std::this_thread::sleep_for(std::chrono::nanoseconds(1));
            continue;

            // if (currentTick > lastTick) { // Check if it's a new, valid tick
            //     int flags = localPlayer.getFlags();
            //     bool onGround = (flags & FL_ONGROUND);

            //     if (onGround) {
            //         // Send jump input ONCE per tick when on ground
            //         // Using -120 for mouse wheel down (standard jump)
            // 		std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
            //         mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
            //     }
            //     lastTick = currentTick; // Update the last processed tick
            // } else if (currentTick == 0 && lastTick != 0) {
            //      // Reset lastTick if the game resets tick count (e.g., map change)
            //      lastTick = 0;
            // }
            //         std::this_thread::sleep_for(std::chrono::microseconds(25));
            // 		continue;

            // int flags = localPlayer.getFlags();
            // bool onGround = (flags & FL_ONGROUND);

            // if (onGround) {
            // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
            // 	mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
            // 	Logger::info("ad");
            // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
            // 	continue;
            // }
            // std::this_thread::sleep_for(std::chrono::microseconds(16625));

            //  }

            // int flags = localPlayer.getFlags();
            // bool onGround = (flags & FL_ONGROUND);

            Vector3 playerVelocitys = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);

            if (playerVelocitys.z == 0.0f)
            {
                int currentTick = MemMan.ReadMem<int>(globalVarsAddr + 81);
                if (currentTick > lastTick)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
                    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                    lastTick = currentTick;
                }

                // std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
                //  Logger::warn("first jump?");
                //  std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
                //  mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                //  lastTime = std::chrono::high_resolution_clock::now();
                //  //mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);

                // while(onGround && elapsed < 31250){
                // //playerVelocity = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);
                // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleepForZero));
                // 	//lastTime = std::chrono::high_resolution_clock::now();
                // 	mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
                // 	Logger::info("jmp sent");
                //  flags = localPlayer.getFlags();
                // onGround = (flags & FL_ONGROUND);
                // now = std::chrono::high_resolution_clock::now();
                // elapsed = std::chrono::duration_cast<
                // 	std::chrono::microseconds
                // >(now - lastTime).count();

                // }

                // 	//lastTime = std::chrono::high_resolution_clock::now();
                // 	Logger::success("too much, sleepin..");
                // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
                // 	continue;
            }

            // uint32_t curTick = MemMan.ReadMem<uint32_t>( ctrl + clientDLL::CBasePlayerController_["m_nTickBase"] );

            // if (curTick != lastTick) {
            //  compute elapsed real time
            //  auto now      = std::chrono::high_resolution_clock::now();
            //  auto elapsed  = std::chrono::duration_cast<
            //                      std::chrono::microseconds
            //                  >(now - lastTime).count();

            // auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - lastTime).count();
            // Logger::info("… after {} μs", us);

            // if (playerVelocity.z  <= -145.0f || playerVelocity.z == 0.0f) {
            // 	std::this_thread::sleep_for(std::chrono::microseconds(15625));
            // 	mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
            // }

            // if (playerVelocity.z  <= miscConf.bhopJumpVelocityThreshold) {
            // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
            // 	mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
            // 	std::this_thread::sleep_for(std::chrono::microseconds(miscConf.bhopSleep));
            // 	continue;
            // 	//SendInput(1, &input, sizeof(INPUT));

            // 	//continue;
            // 	//std::string speedMessage = "jmp: " + std::to_string(miscConf.bhopJumpVelocityThreshold);
            // 	//trig=1000;
            // 	//Logger::info(speedMessage);
            // }

            // float speed2 = std::sqrt(playerVelocity.x * playerVelocity.x + playerVelocity.y * playerVelocity.y);
            // std::string speedMessage = "Speed: " + std::to_string(speed2);
            // Logger::info(speedMessage);

            // Vector3 playerVelocity = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);

            //    lastTick = curTick;
            //     lastTime = now;
            // }

            // if (flags != -1) {
            // mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
            // std::this_thread::sleep_for(std::chrono::microseconds(15626));
            // std::this_thread::sleep_for(std::chrono::milliseconds(16));

            //	}
        }
    }

}

void misc::handleChatTrigger(bool enabled, MemoryManagement::moduleData client)
{
    if (enabled)
    {
        misc::isChatMonitorEnabled = true;
        Logger::info("Trigger Monitor enabled.");
        startChatMonitorThread(client);
    }
    else if (!enabled)
    {
        misc::isChatMonitorEnabled = false;
        Logger::info("Trigger Monitor disabled.");
        stopChatMonitorThread();
    }
}

ImVec4 Lerp(const ImVec4 &a, const ImVec4 &b, float t)
{
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}
#define M_PI 3.14159265358979323846
#define DEG2RAD(a) (((a) * M_PI) / 180.0f)

Vector3 misc::AngleToForwardVector(const Vector3 &angles)
{
    float pitch = DEG2RAD(angles.x);
    float yaw = DEG2RAD(angles.y);

    float cos_pitch = cos(pitch);
    float sin_pitch = sin(pitch);
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    return Vector3(cos_pitch * cos_yaw, cos_pitch * sin_yaw, -sin_pitch);
}

void misc::AngleVectors(const Vector3 &angles, Vector3 *forward, Vector3 *right, Vector3 *up)
{
    float sp, sy, sr, cp, cy, cr;

    float pitch = DEG2RAD(angles.x);
    float yaw = DEG2RAD(angles.y);
    float roll = DEG2RAD(angles.z);

    sp = sin(pitch);
    sy = sin(yaw);
    sr = sin(roll);
    cp = cos(pitch);
    cy = cos(yaw);
    cr = cos(roll);

    if (forward)
    {
        forward->x = cp * cy;
        forward->y = cp * sy;
        forward->z = -sp;
    }

    if (right)
    {
        right->x = -1 * sr * sp * cy + -1 * cr * -sy;
        right->y = -1 * sr * sp * sy + -1 * cr * cy;
        right->z = -1 * sr * cp;
    }

    if (up)
    {
        up->x = cr * sp * cy + -sr * -sy;
        up->y = cr * sp * sy + -sr * cy;
        up->z = cr * cp;
    }
}

Vector3 misc::calculateAngle(const Vector3 &localPosition, const Vector3 &enemyPosition, const Vector3 &viewAngles)
{
    Vector3 delta = localPosition - enemyPosition;
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);

    Vector3 angles;
    angles.x = (atan2(delta.z, hyp) * 180.0f / M_PI) - viewAngles.x;
    angles.y = (atan2(delta.y, delta.x) * 180.0f / M_PI) - viewAngles.y;
    angles.z = 0.0f;

    // Normalize the angles
    while (angles.y > 180.0f)
        angles.y -= 360.0f;
    while (angles.y < -180.0f)
        angles.y += 360.0f;

    return angles;
}

// MODIFIED: Drawing logic updated to the new "message : timer" format.
void misc::drawTriggerMessage()
{
    std::lock_guard<std::mutex> lock(misc::g_displayedMessagesMutex);

    if (g_displayedMessages.empty())
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    const ImVec4 redColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

    // Remove any timers that have expired. Use a standard erase-remove idiom.
    g_displayedMessages.erase(
        std::remove_if(g_displayedMessages.begin(), g_displayedMessages.end(),
                       [&](const DisplayedMessage &msg)
                       {
                           return (now - msg.creationTime) >= msg.duration;
                       }),
        g_displayedMessages.end());

    if (g_displayedMessages.empty())
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 90), ImGuiCond_Always);
    ImGui::Begin("Trigger Info", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    float sine = (sin(ImGui::GetTime() * 5.0f) + 1.0f) * 0.5f; // Oscillates between 0.0 and 1.0
    ImVec4 dynamicColor;

    // Loop through ALL active timers and draw them.
    for (const auto &msg : g_displayedMessages)
    {
        auto elapsed = now - msg.creationTime;
        auto remaining = msg.duration - elapsed;
        auto remaining_seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
        if (remaining_seconds <= 14)
        {
            // FIXED: Using our own Lerp function
            dynamicColor = Lerp(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 1.0f, 0.0f, 1.0f), sine);
        }
        else
        { // Otherwise, use a static bright color (e.g., white)
            dynamicColor = redColor;
        }

        DrawTextWithColoredNumbers(msg.messageText, redColor);
        ImGui::SameLine();
        ImGui::Text(" : ");
        ImGui::SameLine();

        ImGui::TextColored(dynamicColor, "%llds", remaining_seconds + 1);
    }

    ImGui::End();
}

// The corrected function definition

Vector3 misc::RotatePoint(Vector3 point, Vector3 angles)
{
    float radX = angles.x * (M_PI / 180.0f);
    float radY = angles.y * (M_PI / 180.0f);
    float radZ = angles.z * (M_PI / 180.0f);

    float sX = sin(radX), cX = cos(radX);
    float sY = sin(radY), cY = cos(radY);
    float sZ = sin(radZ), cZ = cos(radZ);

    Vector3 temp;

    // ZXY rotation order used by Source 2
    temp.x = point.x * cZ - point.y * sZ;
    temp.y = point.x * sZ + point.y * cZ;
    point.x = temp.x;
    point.y = temp.y;

    temp.x = point.x * cY + point.z * sY;
    temp.z = -point.x * sY + point.z * cY;
    point.x = temp.x;
    point.z = temp.z;

    temp.y = point.y * cX - point.z * sX;
    temp.z = point.y * sX + point.z * cX;
    point.y = temp.y;
    point.z = temp.z;

    return point;
}

bool misc::isGameWindowActive()
{
    HWND hwnd = GetForegroundWindow();
    if (hwnd)
    {
        char windowTitle[256];
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        return std::string(windowTitle).find("Counter-Strike 2") != std::string::npos;
    }
    return false;
}

static bool isBhopToggledActive = false;

static bool qKeyWasPressed = false;
static std::chrono::steady_clock::time_point qPressStartTime;
static const std::chrono::milliseconds holdDurationThreshold(17);

void misc::startBhopThread(DWORD_PTR base, LocalPlayer localPlayer)
{
    if (!g_bhopThread.joinable())
    {                                                              // Check if thread is not already running
        g_stopBhopThread = false;                                  // Reset the stop flag
        g_bhopThread = std::thread(bhopWorker, base, localPlayer); // Create and start the thread
                                                                   // Optional: Log thread start
                                                                   // Logger::info("[Misc] BunnyHop thread started.");
    }
    if (!g_speedInfoThread.joinable())
    {                                                                  // Check if thread is not already running
        g_stopBhopThread = false;                                      // Reset the stop flag
        g_speedInfoThread = std::thread(speedInfoWorker, localPlayer); // Create and start the thread
                                                                       // Optional: Log thread start
                                                                       // Logger::info("[Misc] BunnyHop thread started.");
    }
}

void misc::startItemESPThread(MemoryManagement::moduleData client)
{
    if (!g_itemESPThread.joinable())
    {
        g_stopItemESPThread = false;
        g_itemESPThread = std::thread(itemESPWorker, client);
    }
}

// void misc::handleAutoLaser(bool enabled, MemoryManagement::moduleData client, LocalPlayer localPlayer) {
//     if (enabled && !g_is_universal_threat_enabled) {
//         g_is_universal_threat_enabled = true;
// 		misc::g_is_universal_threat_enabled2 = true;
//         Logger::info("[ThreatDetector] Feature Enabled.");
//         startUniversalThreatThread(client, localPlayer);
//     } else if (!enabled && g_is_universal_threat_enabled) {
//         g_is_universal_threat_enabled = false;
// 		misc::g_is_universal_threat_enabled2 = false;
//         Logger::info("[ThreatDetector] Feature Disabled.");
//         stopUniversalThreatThread();
//     }
// }

void misc::handleAutoLaser(bool enabled, MemoryManagement::moduleData client, LocalPlayer localPlayer)
{
    // We can use a static bool inside the function to track the running state
    static bool isFeatureRunning = false;

    if (enabled && !isFeatureRunning)
    {
        // We want to turn it ON
        isFeatureRunning = true;
        Logger::info("[ThreatDetector] Feature Enabled.");
        startUniversalThreatThread(client, localPlayer); // Make sure this function exists to start your worker
    }
    else if (!enabled && isFeatureRunning)
    {
        // We want to turn it OFF
        isFeatureRunning = false;
        Logger::info("[ThreatDetector] Feature Disabled.");
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        stopUniversalThreatThread(); // Make sure this function exists to stop your worker
    }
}

float misc::DistanceToRaySquared(const Vector3 &p, const Vector3 &ray_origin, const Vector3 &ray_direction_normalized)
{
    Vector3 p_to_origin = p - ray_origin;
    float t = p_to_origin.Dot(ray_direction_normalized);

    // If t < 0, the object is "behind" the ray's starting point, so the closest
    // point on the line is the origin itself.
    if (t < 0.0f)
    {
        return p_to_origin.LengthSqr();
    }

    // Project the point onto the ray to find the closest point.
    Vector3 closest_point_on_ray = ray_origin + ray_direction_normalized * t;
    return (p - closest_point_on_ray).LengthSqr();
}

void misc::stopItemESPThread()
{

    if (g_itemESPThread.joinable())
    {
        g_stopItemESPThread = true;
        try
        {

            g_itemESPThread.join(); // Wait for the thread to finish execution
                                    // Optional: Log thread stop
                                    // Logger::info("[Misc] BunnyHop thread stopped and joined.");
        }
        catch (const std::system_error &e)
        {
            // Handle potential errors during join (e.g., if thread wasn't joinable)
        }
    }
    isItemESPEnabled = false;
}

void misc::stopBhopThread()
{
    if (g_bhopThread.joinable())
    {
        g_stopBhopThread = true; // Signal the thread to stop its loop
        try
        {
            g_bhopThread.join(); // Wait for the thread to finish execution
                                 // Optional: Log thread stop
                                 // Logger::info("[Misc] BunnyHop thread stopped and joined.");
        }
        catch (const std::system_error &e)
        {
            // Handle potential errors during join (e.g., if thread wasn't joinable)
        }
    }
    if (g_speedInfoThread.joinable())
    {
        g_stopBhopThread = true; // Signal the thread to stop its loop
        try
        {
            g_speedInfoThread.join(); // Wait for the thread to finish execution
                                      // Optional: Log thread stop
                                      // Logger::info("[Misc] BunnyHop thread stopped and joined.");
        }
        catch (const std::system_error &e)
        {
            // Handle potential errors during join (e.g., if thread wasn't joinable)
        }
    }
    g_autoBhopEnabled = false; // Ensure state is off on exit
}

static std::chrono::steady_clock::time_point lastToggleTime = std::chrono::steady_clock::now(); // Time of the last successful toggle
static const std::chrono::milliseconds toggleCooldown(360);                                     // 50ms cooldown duration

void misc::bunnyHop(DWORD_PTR base, LocalPlayer localPlayer)
{
    if (!isGameWindowActive())
        return;

    bool isQPressedNow = (GetAsyncKeyState('Q') & 0x8000) != 0;
    if (!isQPressedNow)
        return;

    auto now = std::chrono::steady_clock::now();
    if (now - lastToggleTime < toggleCooldown)
    {
        return;
    }
    else
    {

        lastToggleTime = now;
    }

    // Cooldown passed, toggle
    g_autoBhopEnabled = !g_autoBhopEnabled;
    if (g_autoBhopEnabled)
    {
        startBhopThread(base, localPlayer);
    }
    else
    {
        stopBhopThread();
    }
}

void misc::droppedItem(C_CSPlayerPawn C_CSPlayerPawn, CGameSceneNode CGameSceneNode, view_matrix_t viewMatrix)
{
    // startItemESPThread(C_CSPlayerPawn, CGameSceneNode, viewMatrix);

    if (!overlayESP::isMenuOpen())
    {
        if (!misc::isGameWindowActive())
            return;
    }

    // std::filesystem::path desktopPath = std::filesystem::path(getenv("USERPROFILE")) / "Desktop";
    // std::filesystem::path logFilePath = desktopPath / "LOG1.txt";
    // std::ofstream logFile(logFilePath, std::ios_base::app); // Open in append mode

    // if (!logFile.is_open()) {
    // 	// Optionally, handle the error e.g., log to console that file couldn't be opened
    // 	 Logger::error("Failed to open LOG1.txt for writing.");
    // 	return;
    // }

    for (int i = 65; i < 1856; i++)
    {
        // std::this_thread::sleep_for(std::chrono::microseconds(10));
        //  Entity
        C_CSPlayerPawn.value = i;
        C_CSPlayerPawn.getListEntry();
        if (C_CSPlayerPawn.listEntry == 0)
            continue;
        C_CSPlayerPawn.getPlayerPawn();
        if (C_CSPlayerPawn.playerPawn == 0)
            continue;
        if (C_CSPlayerPawn.getOwner() != -1)
            continue;

        // Entity name
        uintptr_t entity = MemMan.ReadMem<uintptr_t>(C_CSPlayerPawn.playerPawn + 0x10);
        uintptr_t designerNameAddy = MemMan.ReadMem<uintptr_t>(entity + 0x20);

        char designerNameBuffer[MAX_PATH]{};
        MemMan.ReadRawMem(designerNameAddy, designerNameBuffer, MAX_PATH);

        std::string name = std::string(designerNameBuffer);
        // std::string speedMessage = "id: " + std::to_string(i) + ", name:" +name;
        // Logger::info(speedMessage);

        // auto now = std::chrono::system_clock::now();
        // auto in_time_t = std::chrono::system_clock::to_time_t(now);

        // Format time as string
        // std::tm buf{};
        // localtime_s(&buf, &in_time_t); // Use localtime_s for safety
        // std::ostringstream oss;
        // oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
        // std::string timestamp = oss.str();

        // std::string logMessage = timestamp + " - id: " + std::to_string(i) + ", name:" + name;
        // logFile << logMessage << std::endl;

        if (strstr(name.c_str(), "weapon_"))
        {
            name.erase(0, 7);
        }

        // else if (strstr(name.c_str(), "_projectile")) name.erase(name.length() - 11, 11);
        // else if (strstr(name.c_str(), "baseanimgraph")) name = "defuse kit";
        else
        {
            continue;
        }

        if (name.find("te") == std::string::npos)
        {
            continue;
        }

        // Origin position of entity
        CGameSceneNode.value = C_CSPlayerPawn.getCGameSceneNode();
        CGameSceneNode.getOrigin();
        CGameSceneNode.origin = CGameSceneNode.origin.worldToScreen(viewMatrix);

        // Drawing

        if (CGameSceneNode.origin.z >= 0.01f)
        {
            ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            auto [horizontalOffset, verticalOffset] = getTextOffsets(textSize.x, textSize.y, 2.f);

            ImFont *gunText = {};
            if (std::filesystem::exists(DexterionSystem::weaponIconsTTF))
            {
                gunText = imGuiMenu::weaponIcons;
                name = gunIcon(name);
            }
            else
                gunText = imGuiMenu::espNameText;

            ImColor itemColor = ImColor(espConf.attributeColours[0], espConf.attributeColours[1], espConf.attributeColours[2]);
            itemColor = ImColor(255, 0, 0, 255); // RED color for "elite" items

            ImGui::GetBackgroundDrawList()->AddText(gunText, 12, {CGameSceneNode.origin.x - horizontalOffset, CGameSceneNode.origin.y - verticalOffset}, itemColor, name.c_str());
        }
    }
}

void misc::DrawAllDebugBoxes()
{
    std::lock_guard<std::mutex> lock(misc::g_debugBoxMutex);

    if (misc::g_debugBoxesToDraw.empty())
    {
        return;
    }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    ImU32 boxColor = IM_COL32(255, 0, 255, 255); // Magenta

    for (const auto &box : misc::g_debugBoxesToDraw)
    {
        if (!box.shouldDraw)
            continue;

        const ImVec2 *screenCorners = box.screenCorners;

        // This logic is identical to your old drawing logic
        drawList->AddLine(screenCorners[0], screenCorners[1], boxColor);
        drawList->AddLine(screenCorners[1], screenCorners[2], boxColor);
        // ... (add all 12 lines) ...
        drawList->AddLine(screenCorners[2], screenCorners[6], boxColor);
        drawList->AddLine(screenCorners[3], screenCorners[7], boxColor);
    }
}

void misc::droppedItemSeparateThread(MemoryManagement::moduleData client)
{
    if (!isItemESPEnabled)
    {
        isItemESPEnabled = true;
        startItemESPThread(client);
    }
}

void misc::disableDroppedItemSeparateThread()
{
    if (isItemESPEnabled)
    {
        isItemESPEnabled = false;
        misc::itemESPList.clear();
        stopItemESPThread();
    }

    //}
}

// Add damage for a player
void misc::addDamage(std::string name, int damage, int hits, uintptr_t playerHandle)
{
    bool found = false;

    for (auto &entry : damageList)
    {
        if (entry.playerHandle == playerHandle)
        {
            entry.damage += damage;
            entry.hits += hits;
            found = true;
            break;
        }
    }

    if (!found)
    {
        damageList.push_back(DamageData(name, damage, hits, playerHandle));
    }

    std::sort(damageList.begin(), damageList.end());
}

void misc::updatePlayerDamage(std::string name, int totalDamage, int totalHits, uintptr_t playerHandle)
{
    bool found = false;

    for (auto &entry : damageList)
    {
        if (entry.playerHandle == playerHandle)
        {
            entry.damage = totalDamage;
            entry.hits = totalHits;
            found = true;
            break;
        }
    }

    if (!found)
    {
        damageList.push_back(DamageData(name, totalDamage, totalHits, playerHandle));
    }

    std::sort(damageList.begin(), damageList.end());
}

// Clear the damage list (for round reset)
void misc::clearDamageList()
{
    damageList.clear();
}

// Display the damage list UI
void misc::displayDamageList()
{
    // Skip if disabled or game window not active
    if (!miscConf.damageList)
        return;
    if (!overlayESP::isMenuOpen())
    {
        if (!misc::isGameWindowActive())
            return;
    }

    // Always show the window (like spectator list), even if empty
    // This allows users to see it during gameplay

    // Set window position and size
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    ImGui::SetNextWindowPos({(float)GetSystemMetrics(SM_CXSCREEN) - 200.f, 200.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({180.f, 180.f}, ImGuiCond_FirstUseEver);

    // Create window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
    if (ImGui::Begin("Damage List", nullptr, flags))
    {
        // Header
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Damage This Round");
        ImGui::Separator();

        if (damageList.empty())
        {
            // Show a message when no damage to display
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No damage recorded yet");
        }
        else
        {
            // Display each player and their damage
            int count = 0;
            for (const auto &player : damageList)
            {
                // Limit to top 5 players
                // if (count >= 5)
                // 	break;

                // // Display player name
                // ImGui::Text("%s:", player.playerName.c_str());
                // ImGui::SameLine();

                // ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d", player.damage);

                // count++;

                if (count >= 5)
                    break;

                // ImGui::Text("%s:", player.playerName.c_str());
                ImGui::Text("%s:", player.playerName.substr(0, 12).c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d|%d", player.damage, player.hits);

                count++;
            }
        }

        // Add clear button when in menu
        if (overlayESP::isMenuOpen())
        {
            ImGui::Separator();
            if (ImGui::Button("Clear List"))
            {
                clearDamageList();
            }
        }

        ImGui::End();
    }
    ImGui::PopStyleVar();
}
