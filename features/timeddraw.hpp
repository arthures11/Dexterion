#pragma once

// #include "imgui.h"
#include "../imgui/imgui.h"
#include <string>
#include <vector>
#include <chrono>

namespace timeddraw
{
    void AddTimedText(const char *text, ImVec2 pos, ImU32 col, ImFont *font, float fontSize, float lifetimeSeconds);

    void ProcessTimedDraws();
}