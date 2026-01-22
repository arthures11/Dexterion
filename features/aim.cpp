#include "aim.hpp"

#include "random"
#include "windows.h"
#include <iostream>

#include "../util/config.hpp"

#include <iostream>

using namespace std;

static std::chrono::time_point<std::chrono::steady_clock> lastMoveTime;

void aim::aimBot(LocalPlayer localPlayer, Vector3 baseViewAngles, uintptr_t enemyPlayer, uintptr_t boneArray, MemoryManagement::moduleData client) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastMoveTime < std::chrono::milliseconds(2)) return;
    
	Vector3 aimPos;
	Vector3 newAngle;
	Vector3 angle;

	if (aimConf.playerLock){
		if (lockedPlayer != 0 && lockedPlayer != enemyPlayer) return;
	}

	if (enemyPlayer == localPlayer.getPlayerPawn()) {
		lockedPlayer = 0;
		return;
	}

	aimPos = MemMan.ReadMem<Vector3>(boneArray + aimConf.boneMap[aimConf.bones[aimConf.boneSelect]] * 32);
	
	// Get player velocity for movement compensation
	Vector3 playerVelocity = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_vecVelocity"]);

	
	// Get enemy velocity for prediction
	Vector3 enemyVelocity = MemMan.ReadMem<Vector3>(enemyPlayer + clientDLL::C_BaseEntity_["m_vecVelocity"]);
	
	//Logger::info(std::to_string(enemyVelocity.z));

	// Calculate prediction time factor - time to hit the target
	float predictionTime = 0.030f; // 50ms prediction (adjust based on testing)
	
	// Predict enemy position based on their velocity
	aimPos.x += enemyVelocity.x * predictionTime;
	aimPos.y += enemyVelocity.y * predictionTime;
	aimPos.z += enemyVelocity.z * predictionTime;
	
	// If player is moving, apply movement compensation
	if (playerVelocity.x != 0.0f || playerVelocity.y != 0.0f) {
		// Calculate aim compensation based on player movement
		float moveCompensationFactor = 0.1f;
		
		// Adjust aim position based on movement direction and speed
		aimPos.x -= playerVelocity.x * moveCompensationFactor;
		aimPos.y -= playerVelocity.y * moveCompensationFactor;
	}
	
	Vector3 aimPunch = {0, 0, 0};
	if (aimConf.rcs && localPlayer.getShotsFired() > 1) {
		aimPunch = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_CSPlayerPawn_["m_aimPunchAngle"]);
		aimPunch.x *= 2.0f;
		aimPunch.y *= 2.0f;
	}
	
	angle = aim::ImprovedCalculateAngle(localPlayer.eyepos, aimPos, localPlayer.viewAngles + aimPunch);
	
	// Fix for zero angles - prevent aimbot from stopping
	if (fabs(angle.x) < 0.01f && fabs(angle.y) < 0.01f) {
		// Force a very small adjustment to ensure the mouse moves
		if (angle.x == 0.0f) angle.x = 0.01f;
		if (angle.y == 0.0f) angle.y = 0.01f;
	}
	
	newAngle = calculateBestAngle(angle, { 0, 0, aimConf.fov });
	
	// Scale for sensitivity
	newAngle.x = (newAngle.x / (0.022f * aimConf.sens));
	newAngle.y = (newAngle.y / (0.022f * aimConf.sens));

	// Failsafe for extremely small values
	if (fabs(newAngle.x) < 0.01f && fabs(newAngle.y) < 0.01f) {
		// Force a minimum movement to prevent stalling
		if (newAngle.x == 0.0f) newAngle.x = 0.1f;
		if (newAngle.y == 0.0f) newAngle.y = 0.1f;
	}
	
	if (aimConf.isHotAim) {
		if (GetAsyncKeyState(aimConf.hotKeyMap[aimConf.hotKey[aimConf.hotSelectAim]])) {
			aim::moveMouseToLocation(newAngle);
		}
	}
	else {
		//aim::moveMouseToLocation(newAngle);
	}
    lastMoveTime = now;
	lockedPlayer = enemyPlayer;
}

// Improved CalculateAngle function to handle edge cases better
Vector3 aim::ImprovedCalculateAngle(Vector3 src, Vector3 dst, Vector3 viewAngles) {
	Vector3 angles;
	Vector3 delta = { (src.x - dst.x), (src.y - dst.y), (src.z - dst.z) };
	float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);

	angles.x = atanf(delta.z / hyp) * 57.295779513082f - viewAngles.x;
	angles.y = atanf(delta.y / delta.x) * 57.295779513082f - viewAngles.y;
	angles.z = 0.0f;

	if (delta.x >= 0.0f)
		angles.y += 180.0f;

	// Normalize angles to prevent invalid values
	while (angles.x > 180.0f)
		angles.x -= 360.0f;
	while (angles.x < -180.0f)
		angles.x += 360.0f;
	while (angles.y > 180.0f)
		angles.y -= 360.0f;
	while (angles.y < -180.0f)
		angles.y += 360.0f;

	// Handle division by zero or very small deltas
	if (isnan(angles.x) || isnan(angles.y)) {
		angles.x = 0.0f;
		angles.y = 0.0f;
		angles.z = 0.0f;
		return angles;
	}

	// Prevent extremely small angles that might get lost in conversion
	if (fabs(angles.x) < 0.01f) angles.x = angles.x < 0 ? -0.01f : 0.01f;
	if (fabs(angles.y) < 0.01f) angles.y = angles.y < 0 ? -0.01f : 0.01f;

	return angles;
}

void aim::moveMouseToLocation(Vector3 pos) {
    if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) return;
    
    // Enforce minimum movement to prevent "stuck" aimbot
    const float MIN_MOVEMENT = 0.1f;
    if (fabs(pos.x) < MIN_MOVEMENT && fabs(pos.y) < MIN_MOVEMENT) {
        // Force minimum movement in the dominant direction
        if (fabs(pos.x) > fabs(pos.y))
            pos.x = (pos.x < 0) ? -MIN_MOVEMENT : MIN_MOVEMENT;
        else
            pos.y = (pos.y < 0) ? -MIN_MOVEMENT : MIN_MOVEMENT;
    }
    
    auto new_x = static_cast<int>(-pos.y);
    auto new_y = static_cast<int>(pos.x);
    
    // Ensure at least 1 pixel of movement in some direction
    if (new_x == 0 && new_y == 0) {
        new_x = 1;  // Default minimal movement
    }
    
    mouse_event(MOUSEEVENTF_MOVE, new_x, new_y, 0, 0);
}

Vector3 aim::recoilControl(LocalPlayer localPlayer, bool move) {
	localPlayer.getAimPunchCache();
	localPlayer.getViewAngles();

	static Vector3 oldAngles = { 0, 0, 0 };
	Vector3 newAngles = { 0, 0, 0 };

	if (localPlayer.getShotsFired() == 54587654) return newAngles; // Spectator check

	if (localPlayer.getShotsFired() > 1) {
		Vector3 aimPunch = MemMan.ReadMem<Vector3>(localPlayer.getPlayerPawn() + clientDLL::C_CSPlayerPawn_["m_aimPunchAngle"]);
		newAngles.x = (aimPunch.x - oldAngles.x) * 2.f / (0.022f * aimConf.sens);
		newAngles.y = (aimPunch.y - oldAngles.y) * 2.f / (0.022f * aimConf.sens);

		if (move) aim::moveMouseToLocation(newAngles * -1);

		oldAngles = aimPunch;
		return newAngles;
	}
	else {
		oldAngles = { 0, 0, 0 };
		return newAngles;
	}
}

bool clicked = false;

const int trigger_cooldown()
{
	// Generate a random float between 0.0 and 0.5, add 0.15F to it, then cast to int milliseconds
	return static_cast<int>((static_cast<float>(rand() % 50) / 100.0F + 0.15F) * 1000);
}

void aim::triggerBot(LocalPlayer localPlayer, DWORD_PTR base) {
	int crossHairEntity = MemMan.ReadMem<int>(localPlayer.getPlayerPawn() + clientDLL::C_CSPlayerPawn_["m_iIDEntIndex"]);
	int localPlayerHealth = MemMan.ReadMem<int>(localPlayer.getPlayerPawn() + clientDLL::C_BaseEntity_["m_iHealth"]);
	if (!crossHairEntity) return;

	C_CSPlayerPawn crossHairPawn(base);
	CCSPlayerController crossHairEntityController(base);

	crossHairPawn.getPlayerPawnByCrossHairID(crossHairEntity);
	crossHairEntityController.value = crossHairPawn.playerPawn;

	bool isValidEntity = (crossHairEntity != -1 && crossHairPawn.getPawnHealth() > 0 && crossHairPawn.getPawnHealth() <= 100 && crossHairEntityController.getPawnTeam() != localPlayer.getTeam());
	bool isDeathMatchEntity = (crossHairEntity != -1 && crossHairPawn.getPawnHealth() > 0 && crossHairPawn.getPawnHealth() <= 100 && miscConf.deathmatchMode);

	if (localPlayerHealth > 100 || localPlayerHealth <= 0) return;

	if (aimConf.isHotTrigger) {
		if (GetAsyncKeyState(aimConf.hotKeyMap[aimConf.hotKey[aimConf.hotSelectTrigger]])) {
			if (isValidEntity || isDeathMatchEntity) {
				if (!clicked)
				{
					clicked = true;
					const int t = trigger_cooldown();
					//printf("Cooldown: %d ms\n", t);  // Correct printf syntax for int
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
					Sleep(t/2);
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
					Sleep(t/2);
					clicked = false;
				}
			};
		}
	}
	else {
		if (isValidEntity || isDeathMatchEntity)
		{
			if (!clicked)
			{
				clicked = true;
				const int t = trigger_cooldown();
				//printf("Cooldown: %d ms\n", t);  // Correct printf syntax for int
				mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
				Sleep(t / 2);
				mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
				Sleep(t / 2);
				clicked = false;
			}
		};
	}
}
