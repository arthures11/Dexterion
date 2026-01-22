#include "timeddraw.hpp"
namespace timeddraw
{

    struct TimedText
    {
        std::string text;
        ImVec2 pos;
        ImU32 col;
        ImFont *font;
        float fontSize;
        std::chrono::steady_clock::time_point expiry;
    };

    // global (or member) storage
    static std::vector<TimedText> g_timedTexts;

    // Caller-visible API
    void AddTimedText(const char *text, ImVec2 pos, ImU32 col, ImFont *font, float fontSize, float lifetimeSeconds)
    {
        TimedText t;
        t.text = text;
        t.pos = pos;
        t.col = col;
        t.font = font;
        t.fontSize = fontSize;
        // t.expiry = std::chrono::steady_clock::now() + std::chrono::duration<float>(lifetimeSeconds);
        auto ms = static_cast<int64_t>(lifetimeSeconds * 1000.0f);
        t.expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        g_timedTexts.push_back(std::move(t));
    }

    void ProcessTimedDraws()
    {
        auto now = std::chrono::steady_clock::now();
        // remove expired entries
        g_timedTexts.erase(
            std::remove_if(g_timedTexts.begin(), g_timedTexts.end(),
                           [&](auto &t)
                           { return t.expiry < now; }),
            g_timedTexts.end());

        // draw the rest
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        for (auto &t : g_timedTexts)
            dl->AddText(t.font, t.fontSize, t.pos, t.col, t.text.c_str());
    }
}