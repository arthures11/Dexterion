#include "entry.hpp"
#include "misc.hpp"
#include <format>
#include <set>
#include <fstream>
#include <thread>

// Keep track of names we've already logged to avoid duplicates in the same session
std::set<std::string> loggedPlayerNames;

// Function to log player names to a file on the desktop
void logPlayerNameToFile(const std::string &playerName)
{
	if (loggedPlayerNames.find(playerName) != loggedPlayerNames.end())
	{
		return; // Skip empty names or already logged names
	}

	// Add to our set of logged names
	loggedPlayerNames.insert(playerName);

	// Open file in append mode
	std::ofstream logFile("C:\\Users\\arthur\\Desktop\\tex_nef.txt", std::ios::app);
	if (logFile.is_open())
	{
		// Write the player name with a timestamp
		logFile << playerName << std::endl;
		logFile.close();
	}
}

const char* getBoneName(int boneIndex) {
    switch (boneIndex) {
        case 0: return "head";
        case 2: return "chest";
        case 3: return "crotch";
        default: return "unknown"; // Fallback for any other value
    }
}

uintptr_t bestTargetPawn = 0;
float smallestDist = FLT_MAX; // Use a very large number, like float max

void mainLoop(bool state, MemoryManagement::moduleData client)
{
	// Classes
	CCSPlayerController CCSPlayerController(client.base);
	CBasePlayerController CBasePlayerController;
	C_CSPlayerPawn C_CSPlayerPawn(client.base);
	CGameSceneNode CGameSceneNode;
	LocalPlayer localPlayer(client.base);
	// C_C4					C_C4(client.base);

	// Shared variables (between features)
	view_matrix_t viewMatrix = MemMan.ReadMem<view_matrix_t>(client.base + offsets::clientDLL["dwViewMatrix"]);
	Vector3 baseViewAngles = MemMan.ReadMem<Vector3>(client.base + offsets::clientDLL["dwViewAngles"]);
	DWORD_PTR baseViewAnglesAddy = client.base + offsets::clientDLL["dwViewAngles"];
	uintptr_t entityList = MemMan.ReadMem<uintptr_t>(client.base + offsets::clientDLL["dwEntityList"]);

	// NOTE: Cheats that only need local player / visuals that don't relate to gameplay
	localPlayer.getPlayerPawn();

	CBasePlayerController.controller = localPlayer.getPlayerController();
	
	if (Shared::steamId != CBasePlayerController.getSteamId())
		Shared::steamId = CBasePlayerController.steamId;
	// Aimbot FOV circle
	if (aimConf.fovCircle)
	{
		if (!overlayESP::isMenuOpen())
		{
			if (!misc::isGameWindowActive())
				return;
		}

		ImVec2 p = ImGui::GetWindowPos();
		float screenMidX = GetSystemMetrics(SM_CXSCREEN) / 2.f;
		float screenMidY = GetSystemMetrics(SM_CYSCREEN) / 2.f;

		  float verticalOffset = 60.0f; 
    	float fovCenterY = screenMidY - verticalOffset;

		ImGui::GetBackgroundDrawList()->AddCircle({screenMidX, fovCenterY}, (aimConf.fov * 10), ImColor(1.f, 1.f, 1.f, 1.f), 0, 1.f);
	}

	 static bool zKeyWasPressed = false;
    if (GetAsyncKeyState('Z') & 0x8000) { // Check if 'Z' key is currently down
        if (!zKeyWasPressed) {
            // This is the first frame the key is pressed, so we execute the change
            zKeyWasPressed = true;
            
            // Cycle through the bone selection: 0 -> 2 -> 3 -> 0
            if (aimConf.boneSelect == 0) {
                aimConf.boneSelect = 2;
            } else if (aimConf.boneSelect == 2) {
                aimConf.boneSelect = 3;
            } else { // If it's 3 or any other value, reset to 0
                aimConf.boneSelect = 0;
            }
        }
    } else {
        // The key is not down, so reset our flag for the next press
        zKeyWasPressed = false;
    }


static bool upArrowWasPressed = false;
if (GetAsyncKeyState(VK_UP) & 0x8000) {
    if (!upArrowWasPressed) {
        upArrowWasPressed = true;
        
        // --- CORRECTED TOGGLE LOGIC ---
        // 1. Flip the state of our single boolean flag.
        misc::g_is_universal_threat_enabled2 = !misc::g_is_universal_threat_enabled2;
        
        // 2. Call the handler with the NEW state.
        misc::handleAutoLaser(misc::g_is_universal_threat_enabled2, client, localPlayer);
    }
} else {
    upArrowWasPressed = false;
}

static bool rightArrowWasPressed = false;

if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
    if (!rightArrowWasPressed) {
        rightArrowWasPressed = true;
        
        // --- CORRECTED TOGGLE LOGIC ---
        // 1. Flip the state of our single boolean flag.
        misc::isChatMonitorEnabled = !misc::isChatMonitorEnabled;
        
        // 2. Call the handler with the NEW state.
		misc::handleChatTrigger(misc::isChatMonitorEnabled, client);    }
} else {
    rightArrowWasPressed = false;
}

static bool downArrowWasPressed = false;

if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
    if (!downArrowWasPressed) {
        downArrowWasPressed = true;
        
		if(misc::g_is_universal_threat_enabled2){
        misc::isCrouchOnly = !misc::isCrouchOnly;
		}

        

		   }
} else {
    downArrowWasPressed = false;
}



    //misc::DrawAllDebugBoxes(); 

	// Recoil control

	// Bomb Timer
	// if (miscConf.bombTimer) bomb::timer(C_C4);
	// Bunny Hop
	if (miscConf.bhopEnabled)
		misc::bunnyHop(client.base, localPlayer);

	if (localPlayer.getPlayerPawn() != 0)
	{														// Only draw if local player is valid
		float currentSpeed = misc::g_currentSpeed2D.load(); // Read the shared speed
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
		ImGui::Begin("Speed Display", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Text("Speed: %.2f", currentSpeed);
		 if (misc::g_is_universal_threat_enabled2 && misc::isCrouchOnly) {
        ImGui::Text("AutoLaser: ENABLED + crouch only");
    } 
	else if(misc::g_is_universal_threat_enabled2 && !misc::isCrouchOnly){
		ImGui::Text("AutoLaser: ENABLED");
	}
	else if (misc::g_is_universal_threat_enabled2 == false) {
        ImGui::Text("AutoLaser: ");
    }
		ImGui::Text("Bone: %s", getBoneName(aimConf.boneSelect));
		ImGui::End();
	}

	// Tigger
	if (aimConf.trigger){
		aim::triggerBot(localPlayer, client.base);
	}

		//Logger::info("About to call "); // Add this line
		misc::drawTriggerMessage();

	// std::unordered_map<std::string, std::pair<int, uintptr_t>> playerDamageMap;
	std::unordered_map<std::string, std::tuple<int, int, uintptr_t>> playerDamageMap;

	static int lastDamageUpdateTime = 0;
	int currentTime = misc::getCurrentTimestamp();
	std::vector<std::string> spectators{};

	 logPlayerNameToFile("-----------------------------------------------------------------------");
	for (int i = 1; i <= 64; i++)
	{
		// Player controller
		CCSPlayerController.id = i;
		CCSPlayerController.getListEntry();
		if (!CCSPlayerController.listEntry)
			continue;
		CCSPlayerController.getController();
		if (CCSPlayerController.value == 0)
			continue;
		CCSPlayerController.getPawnName();

		// Player pawn
		C_CSPlayerPawn.value = CCSPlayerController.getC_CSPlayerPawn();
		C_CSPlayerPawn.getListEntry();
		C_CSPlayerPawn.getPlayerPawn();
		C_CSPlayerPawn.getPawnHealth();

		if (currentTime - lastDamageUpdateTime >= 10)
		{
			uintptr_t actionServices = MemMan.ReadMem<uintptr_t>(
				CCSPlayerController.value +
				clientDLL::clientDLLOffsets["CCSPlayerController"]["fields"]["m_pActionTrackingServices"]);

			if (!actionServices)
				continue;

			uint32_t totalDamage = MemMan.ReadMem<uint32_t>(
				actionServices +
				clientDLL::clientDLLOffsets["CCSPlayerController_ActionTrackingServices"]["fields"]["m_unTotalRoundDamageDealt"]);

			uintptr_t bulletServices = MemMan.ReadMem<uintptr_t>(
				C_CSPlayerPawn.playerPawn +
				clientDLL::clientDLLOffsets["C_CSPlayerPawn"]["fields"]["m_pBulletServices"]);

			if (!bulletServices)
				continue;

			int totalHits = MemMan.ReadMem<int>(
				bulletServices +
				clientDLL::clientDLLOffsets["CCSPlayer_BulletServices"]["fields"]["m_totalHitsOnServer"]);

			// uintptr_t damageServices = MemMan.ReadMem<uintptr_t>(
			// 	CCSPlayerController.value + clientDLL::clientDLLOffsets["CCSPlayerController"]["fields"]["m_pDamageServices"]
			// );

			// uintptr_t damageListPtr = MemMan.ReadMem<uintptr_t>(damageServices + 72); // ptr to vector

			// // Now read vector size
			// int damageListSize = MemMan.ReadMem<int>(damageListPtr + sizeof(uintptr_t));  // m_nSize @ +8

			// const int recordSize = 112;

			// for (int i = 0; i < damageListSize; ++i)
			// {
			// 	uintptr_t entryPtr = MemMan.ReadMem<uintptr_t>(damageListPtr) + i * recordSize;

			// 	int numHits = MemMan.ReadMem<int>(entryPtr + 100);   // m_iNumHits
			// 	int damage   = MemMan.ReadMem<int>(entryPtr + 92);   // m_iDamage

			// 	uintptr_t damagerHandle = MemMan.ReadMem<uintptr_t>(entryPtr + 48); // m_hPlayerControllerDamager

			// 	std::string speedMessage = "numHits: "+std::to_string(numHits) +", damage: "+ std::to_string(damage);
			// 	Logger::info(speedMessage);
			// }

			// for (int i = 0; i < 333; i++)
			// {
			// 	int totalHits = MemMan.ReadMem<int>(
			// 		bulletServices + i);
			// 	std::string speedMessage = "i: "+std::to_string(i) +", hits: "+ std::to_string(totalHits);
			// 	Logger::info(speedMessage);
			// }

			// uint32_t totalHits = 0;
			// totalHits = MemMan.ReadMem<uint32_t>(
			// 	bulletServices +
			// 		clientDLL::clientDLLOffsets["CCSPlayer_BulletServices"]["fields"]["m_totalHitsOnServer"]);

			// 	&totalHits, sizeof(totalHits);

			// uintptr_t m_pDamageServices = MemMan.ReadMem<uintptr_t>(
			// 	CCSPlayerController.value +
			// 	clientDLL::clientDLLOffsets["CCSPlayerController"]["fields"]["m_pDamageServices"]);

			// if (!actionServices)
			// 	continue;

			// uintptr_t m_DamageList = MemMan.ReadMem<uintptr_t>(
			// 	m_pDamageServices +
			// 	clientDLL::clientDLLOffsets["CCSPlayerController_DamageServices"]["fields"]["m_DamageList"]);

			// int hits = MemMan.ReadMem<int>(
			// 	m_DamageList +
			// 	clientDLL::clientDLLOffsets["CDamageRecord"]["fields"]["m_iDamage"]);

			if (totalDamage > 0)
			{
				playerDamageMap[CCSPlayerController.pawnName] = {totalDamage, totalHits, CCSPlayerController.value};
			}
		}

		// Spectator List
		if (miscConf.spectator && CCSPlayerController.isSpectating(true))
			spectators.push_back(CCSPlayerController.pawnName);

		// if (localPlayer.getTeam() == CCSPlayerController.getPawnTeam() && !miscConf.deathmatchMode) continue;
		if (localPlayer.getTeam() == CCSPlayerController.getPawnTeam())
			continue;
		// if (strcmp(CCSPlayerController.pawnName.c_str(), "Unknown") == 0
		// //|| strcmp(CCSPlayerController.pawnName.c_str(), "weapon_type_grenade") == 0
		// //|| strcmp(CCSPlayerController.pawnName.c_str(), "m_nElementCount") == 0
		// ) {
		// 	continue;
		// }

		// Checks
		if (aim::lockedPlayer == C_CSPlayerPawn.playerPawn && (C_CSPlayerPawn.pawnHealth <= 0 || (aimConf.checkSpotted && !C_CSPlayerPawn.getEntitySpotted())))
			aim::lockedPlayer = 0;
		if ((C_CSPlayerPawn.pawnHealth <= 0 || C_CSPlayerPawn.pawnHealth > 100000) || strcmp(CCSPlayerController.pawnName.c_str(), "DemoRecorder") == 0 || strcmp(CCSPlayerController.pawnName.c_str(), "Unknown") == 0)
			continue;

		// Game scene node
		CGameSceneNode.value = C_CSPlayerPawn.getCGameSceneNode();

		 logPlayerNameToFile(CCSPlayerController.pawnName);

		// ESP
		if (espConf.state)
		{
			// Logger::info(std::format("ESPs ({})", i));
			if (C_CSPlayerPawn.getPlayerPawn() == localPlayer.getPlayerPawn())
				continue;
			//esp::sharedData::weaponID = C_CSPlayerPawn.getWeaponID();
			//esp::sharedData::weaponName = C_CSPlayerPawn.getWeaponName();
			esp::sharedData::localOrigin = localPlayer.getOrigin();
			esp::sharedData::entityOrigin = C_CSPlayerPawn.getOrigin();
			esp::sharedData::distance = (int)(utils::getDistance(esp::sharedData::localOrigin, esp::sharedData::entityOrigin)) / 100;

			C_CSPlayerPawn.getOrigin();
			CGameSceneNode.getBoneArray();

			if (espConf.checkSpotted)
			{
				if (SharedFunctions::spottedCheck(C_CSPlayerPawn, localPlayer))
				{
					esp::boundingBox(C_CSPlayerPawn.origin, viewMatrix, CCSPlayerController.pawnName, C_CSPlayerPawn.pawnHealth, CGameSceneNode.boneArray, true);
				}
			}
			else
			{
				esp::boundingBox(C_CSPlayerPawn.origin, viewMatrix, CCSPlayerController.pawnName, C_CSPlayerPawn.pawnHealth, CGameSceneNode.boneArray, SharedFunctions::spottedCheck(C_CSPlayerPawn, localPlayer));
			}
		}

		// C4 ESP
		// if (espConf.c4State) {
		//	if (!overlayESP::isMenuOpen()) {
		//		if (!misc::isGameWindowActive()) return;
		//	}
		//	CGameSceneNode.value = C_C4.getCGameSceneNode();
		//	CGameSceneNode.getOrigin();
		//
		//	esp::drawC4(CGameSceneNode.origin, viewMatrix, localPlayer, C_C4);
		//}

		// Aim
		if (aimConf.isHotAim)
		{
			if (GetAsyncKeyState(aimConf.hotKeyMap[aimConf.hotKey[aimConf.hotSelectAim]]))
			{
				if (aimConf.state)
				{
					if (aimConf.rcs){
						aim::recoilControl(localPlayer, true);
					}
					// if (!spectators.empty()) {
					// 	continue;
					// }
					// if (C_CSPlayerPawn.getPlayerPawn() == localPlayer.getPlayerPawn()){
					// 	continue;
					// }


					// Player lock
					if (aimConf.playerLock)
					{
						// Check if current enemy is the preferred target
						uintptr_t preferredPlayerPawn = doPreferred(C_CSPlayerPawn, CGameSceneNode, localPlayer, aim::lockedPlayer, viewMatrix, aimConf.aimModeMap[aimConf.aimModes[aimConf.aimMode]], client).playerPawn;
						if(preferredPlayerPawn==0){
							continue;
						}
						// If this enemy isn't the preferred target, skip to the next enemy in loop
						if (preferredPlayerPawn != C_CSPlayerPawn.playerPawn)
						{
							continue;
						}

						// Update the locked player for future iterations
						aim::lockedPlayer = preferredPlayerPawn;

						// // Debug text to show which enemy is being targeted
						// float screenCenterX = (float)GetSystemMetrics(SM_CXSCREEN) / 2;
						// ImGui::GetBackgroundDrawList()->AddText(
						// 	{ screenCenterX - 100, 30 }, ImColor(1.0f, 1.0f, 0.0f),
						// 	("Target: " + CCSPlayerController.pawnName).c_str()
						// );
					}

					CGameSceneNode.value = C_CSPlayerPawn.getCGameSceneNode();
					CGameSceneNode.getBoneArray();

					localPlayer.getCameraPos();
					localPlayer.getEyePos();
					localPlayer.getViewAngles();
					if (aimConf.checkSpotted)
					{
						if (SharedFunctions::spottedCheck(C_CSPlayerPawn, localPlayer))
						{
							aim::aimBot(localPlayer, baseViewAngles, C_CSPlayerPawn.playerPawn, CGameSceneNode.boneArray, client);
						}
					}
					else
					{
						//Logger::warn(CCSPlayerController.pawnName);
						aim::aimBot(localPlayer, baseViewAngles, C_CSPlayerPawn.playerPawn, CGameSceneNode.boneArray, client);
					}
				}
			}
		}
	}

	if (miscConf.spectator)
	{
		if (!overlayESP::isMenuOpen())
		{
			if (!misc::isGameWindowActive())
				return;
		}

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowPos({0.f, 200.f}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize({100.f, 250.f}, ImGuiCond_FirstUseEver);
		if (spectators.size() > 0)
		{
			if (ImGui::Begin("Spectators + ", nullptr, flags))
			{
				for (int i = 0; i < spectators.size(); i++)
				{
					std::string name = spectators[i];

					ImGui::TextColored(utils::float3ToImColor(miscConf.spectatorColours, miscConf.spectatorColours[3]).Value, name.c_str());
				}

				ImGui::End();
			}
		}
	}

	misc::displayDamageList();
	if (currentTime - lastDamageUpdateTime >= 10)
	{
		lastDamageUpdateTime = currentTime;

		static bool wasInGame = false;
		bool isInGame = SharedFunctions::inGame(client.base);

		// If we just started a new game/round, clear the damage list
		if (isInGame && !wasInGame)
		{
			misc::clearDamageList();
		}
		wasInGame = isInGame;

		// Update our damage list
		misc::damageList.clear();
		// for (const auto &[name, damageInfo] : playerDamageMap)
		// {
		// 	misc::damageList.push_back(misc::DamageData(name, damageInfo.first, damageInfo.second));
		// }

		for (const auto &[name, damageInfo] : playerDamageMap)
		{
			int totalDamage = std::get<0>(damageInfo);
			int totalHits = std::get<1>(damageInfo);
			uintptr_t handle = std::get<2>(damageInfo);

			misc::damageList.push_back(misc::DamageData(name, totalDamage, totalHits, handle));
		}

		// Sort by damage
		std::sort(misc::damageList.begin(), misc::damageList.end());
	}

	// Dropped Item
	// if (miscConf.itemESP) misc::droppedItem(C_CSPlayerPawn, CGameSceneNode, viewMatrix);

	if (miscConf.itemESP)
	{
		misc::droppedItemSeparateThread(client);

		
		std::lock_guard<std::mutex> lock(misc::itemESPListMutex);
		for (const misc::ItemESPData &item : misc::itemESPList)
		{
			// Use the drawing logic from misc::itemESPWorker
			ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str());
			// Assuming getTextOffsets is accessible or defined elsewhere (like overlay.hpp or utilFunctions.hpp)
			// If not, you might need to include the necessary header or define it here.
			// Based on the original code, it seems to be in overlay.hpp or a related file.
			// Let's assume it's accessible or included.
			// auto [horizontalOffset, verticalOffset] = getTextOffsets(textSize.x, textSize.y, 2.f); // Assuming getTextOffsets is available

			// Replicating the offset calculation manually if getTextOffsets is not easily accessible
			float horizontalOffset = textSize.x / 2.f; // Simple center alignment
			float verticalOffset = textSize.y / 2.f;   // Simple vertical alignment

			ImFont *gunText = imGuiMenu::espNameText; // Assuming imGuiMenu::espNameText is accessible

			
			ImColor itemColor = ImColor(255, 0, 255, 255); // half orange, half blue

		

			// Calculate distance to local player for font size scaling
			float distance = utils::getDistance(localPlayer.getOrigin(), item.worldOrigin) / 100.0f;
			if(distance < 18.0f){
			 itemColor = ImColor(255, 0, 0, 255);
			}
				
			float scaledFontSize = miscConf.itemESPFontSize - utils::fixFontSize(distance);
			if (scaledFontSize < 1.0f) scaledFontSize = 2.0f; // Ensure minimum font size

			ImGui::GetBackgroundDrawList()->AddText(gunText, scaledFontSize, {item.screenOrigin.x - horizontalOffset, item.screenOrigin.y - verticalOffset}, itemColor, item.name.c_str());
		}
		// Clear the list after drawing
	}
	else
	{
		misc::disableDroppedItemSeparateThread();
	}
}

//	 {"Closest to Player",0},
// {"Closest to Crosshair",1},
// {"Furthest from crosshair",2
// },{"No preference",3} };
C_CSPlayerPawn doPreferred(C_CSPlayerPawn C_CSPlayerPawn_, CGameSceneNode CGameSceneNode, LocalPlayer localPlayer, uintptr_t preferredTarget, view_matrix_t viewMatrix, int mode, MemoryManagement::moduleData client)
{
	C_CSPlayerPawn target(client.base);
	if (preferredTarget == 0){
		return C_CSPlayerPawn_;
	}

	target.playerPawn = preferredTarget;

	if (target.getPawnHealth() <= 0 || target.getPawnHealth() > 100000){
		return C_CSPlayerPawn_;
	}

	Vector3 newVelocity = MemMan.ReadMem<Vector3>(C_CSPlayerPawn_.playerPawn + clientDLL::C_BaseEntity_["m_vecVelocity"]);

    // NEW: Check if the target is falling too fast.
    // if (newVelocity.z > 340.0f)
    // {
    //     return 0;
    // }


	switch (mode)
	{
	case 0:
	{
		if (utils::getDistance(localPlayer.getOrigin(), target.getOrigin()) >
			utils::getDistance(localPlayer.getOrigin(), C_CSPlayerPawn_.getOrigin()))
			return C_CSPlayerPawn_;
		else
			return target;
	}
	case 1:
	{
		// C_CSPlayerPawn_ (Next player in entity list)
		Vector3 newOrigin = C_CSPlayerPawn_.getOrigin();
		Vector3 newOriginalPosToScreen = newOrigin.worldToScreen(viewMatrix);

		Vector3 newHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);

		Vector3 newHeadPosToScreen = newHeadPos.worldToScreen(viewMatrix);

		// preferredAimbot (Last potential target)
		CGameSceneNode.value = target.getCGameSceneNode();

		Vector3 oldOrigin = target.getOrigin();
		Vector3 oldOriginalPosToScreen = oldOrigin.worldToScreen(viewMatrix);

		Vector3 oldHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);

		Vector3 oldHeadPosToScreen = oldHeadPos.worldToScreen(viewMatrix);

		if (utils::getDistance({oldHeadPosToScreen.x, oldHeadPosToScreen.y}, {(float)GetSystemMetrics(SM_CXSCREEN) / 2, (float)GetSystemMetrics(SM_CYSCREEN) / 2}) >
			utils::getDistance({newHeadPosToScreen.x, newHeadPosToScreen.y}, {(float)GetSystemMetrics(SM_CXSCREEN) / 2, (float)GetSystemMetrics(SM_CYSCREEN) / 2}))
			return C_CSPlayerPawn_;
		else
			return target;
	}
	case 2:
	{
		// C_CSPlayerPawn_ (Next player in entity list)
		Vector3 newOrigin = C_CSPlayerPawn_.getOrigin();
		Vector3 newOriginalPosToScreen = newOrigin.worldToScreen(viewMatrix);

		Vector3 newHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);

		Vector3 newHeadPosToScreen = newHeadPos.worldToScreen(viewMatrix);

		// preferredAimbot (Last potential target)
		CGameSceneNode.value = target.getCGameSceneNode();

		Vector3 oldOrigin = target.getOrigin();
		Vector3 oldOriginalPosToScreen = oldOrigin.worldToScreen(viewMatrix);

		Vector3 oldHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);

		Vector3 oldHeadPosToScreen = oldHeadPos.worldToScreen(viewMatrix);

		if (utils::getDistance({oldHeadPosToScreen.x, oldHeadPosToScreen.y}, {(float)GetSystemMetrics(SM_CXSCREEN) / 2, (float)GetSystemMetrics(SM_CYSCREEN) / 2}) >
			utils::getDistance({newHeadPosToScreen.x, newHeadPosToScreen.y}, {(float)GetSystemMetrics(SM_CXSCREEN) / 2, (float)GetSystemMetrics(SM_CYSCREEN) / 2}))
			return target;
		else
			return C_CSPlayerPawn_;
	}
	case 3:
	{
		return target;
	}
	case 4:
	{
		// First check if the targets are within FOV
		Vector3 newOrigin = C_CSPlayerPawn_.getOrigin();
		Vector3 newHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);
		Vector3 newHeadPosToScreen = newHeadPos.worldToScreen(viewMatrix);

		// Get screen center coordinates
		float screenCenterX = (float)GetSystemMetrics(SM_CXSCREEN) / 2;
		float screenCenterY = ((float)GetSystemMetrics(SM_CYSCREEN) / 2) - 50.0f;

		float newDistToCenter = utils::getDistance(
			{newHeadPosToScreen.x, newHeadPosToScreen.y},
			{screenCenterX, screenCenterY});

		// Check if target is within FOV and on screen
		bool newValidScreen = (newHeadPosToScreen.z >= 0.01f &&
							   newHeadPosToScreen.x >= 0 && newHeadPosToScreen.x <= GetSystemMetrics(SM_CXSCREEN) &&
							   newHeadPosToScreen.y >= 0 && newHeadPosToScreen.y <= GetSystemMetrics(SM_CYSCREEN));

		bool newInFOV = newValidScreen && (newDistToCenter <= (aimConf.fov * 10));

		// Check if preferred target is within FOV
		CGameSceneNode.value = target.getCGameSceneNode();
		Vector3 oldHeadPos = MemMan.ReadMem<Vector3>(CGameSceneNode.getBoneArray() + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);
		Vector3 oldHeadPosToScreen = oldHeadPos.worldToScreen(viewMatrix);

		float oldDistToCenter = utils::getDistance(
			{oldHeadPosToScreen.x, oldHeadPosToScreen.y},
			{screenCenterX, screenCenterY});

		// Check if old target is within FOV and on screen
		bool oldValidScreen = (oldHeadPosToScreen.z >= 0.01f &&
							   oldHeadPosToScreen.x >= 0 && oldHeadPosToScreen.x <= GetSystemMetrics(SM_CXSCREEN) &&
							   oldHeadPosToScreen.y >= 0 && oldHeadPosToScreen.y <= GetSystemMetrics(SM_CYSCREEN));

		bool oldInFOV = oldValidScreen && (oldDistToCenter <= (aimConf.fov * 10));


	// 	  bool isNewTargetInFront = newHeadPosToScreen.z >= 0.01f;
    // bool isOldTargetInFront = oldHeadPosToScreen.z >= 0.01f;

    // // --- Now, make a decision based on who is in front ---
    // if (isNewTargetInFront && !isOldTargetInFront) {
    //     return C_CSPlayerPawn_; // New target is in front, old is behind. Obvious choice.
    // }
    // if (!isNewTargetInFront && isOldTargetInFront) {
    //     return target; // Old target is in front, new is behind. Stick with old.
    // }
    // if (!isNewTargetInFront && !isOldTargetInFront) {
    //     return target; // Both are behind, stick with the old target to prevent weird switching.
    // }
		// Calculate distances for use after FOV check

		// TARGETING LOGIC:
		// 1. FOV has absolute priority - if one player is in FOV and the other isn't, pick the one in FOV
		if (newInFOV && !oldInFOV)
		{
			return C_CSPlayerPawn_;
		}
		else if (oldInFOV && !newInFOV)
		{
			return target;
		}
		 else if (newInFOV && oldInFOV)
    {
        float distToNew = utils::getDistance(localPlayer.getOrigin(), C_CSPlayerPawn_.getOrigin());
	    float distToOld = utils::getDistance(localPlayer.getOrigin(), target.getOrigin());
        if (distToNew < distToOld) {
            return C_CSPlayerPawn_;
        } else {
            return target;
        }
    }
    // 4. (The FIX) If BOTH are OUTSIDE the FOV, do NOT switch. Stick with the old target.
    //    This prevents the aimbot from picking a new, random, out-of-FOV target.
    else // This handles the (!newInFOV && !oldInFOV) case
    {
        return 0; 
    }
	}
	default:
		return C_CSPlayerPawn_;
	}
}