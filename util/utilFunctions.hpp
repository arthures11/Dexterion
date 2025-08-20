#pragma once
#pragma warning (disable: 4302)
#pragma warning (disable: 4129)
#pragma warning (disable: 4244)
#pragma warning (disable: 6031)

#include <string>
#include <vector>
#include <tuple>
#include <iostream>
#include <Windows.h>
#include <algorithm>
#include "../imgui/imgui.h"
#include "Vectors.h"
#include <stringapiset.h>

#include <comdef.h>
#include <Wbemidl.h>
#include <cctype>

#undef min

inline namespace Logger {
	inline HANDLE hConsole;

inline std::wstring StrToWstr(const std::string& str)
{
    if (str.empty()) {
        return L"";
    }
    // CP_UTF8 is the crucial flag that tells the function our source is UTF-8
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed <= 0) {
        // You could add error logging here if you want
        return L"";
    }
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

	// WString
	inline void info(std::wstring str, bool endLine = true) {
		SetConsoleTextAttribute(hConsole, 9);
		if (endLine)
			std::wcout << "[Info] " << str << std::endl;
		else
			std::wcout << "[Info] " << str;
	}

	inline void success(std::wstring str, bool endLine = true) {
		SetConsoleTextAttribute(hConsole, 10);
		if (endLine)
			std::wcout << "[Success] " << str << std::endl;
		else
			std::wcout << "[Success] " << str;
	}

	inline void error(std::wstring str, bool endLine = true) {
		SetConsoleTextAttribute(hConsole, 12);
		if (endLine)
			std::wcout << "[Error] " << str << std::endl;
		else
			std::wcout << "[Error] " << str;
	}

	inline void warn(std::wstring str, bool endLine = true) {
		SetConsoleTextAttribute(hConsole, 14);
		if (endLine)
			std::wcout << "[Warning] " << str << std::endl;
		else
			std::wcout << "[Warning] " << str;
	}






	// String
	inline void info(std::string str, bool endLine = true) {
		info(StrToWstr(str), endLine);
	}

	inline void success(std::string str, bool endLine = true) {
		success(StrToWstr(str), endLine);
	}

	inline void error(std::string str, bool endLine = true) {
		error(StrToWstr(str), endLine);
	}

	inline void warn(std::string str, bool endLine = true) {
		warn(StrToWstr(str), endLine);
	}
}

inline namespace utils {
	inline std::string version = "2.2.2";

	// https://www.unknowncheats.me/forum/dayz-sa/129893-calculate-distance-meters.html
	// https://www.unknowncheats.me/forum/general-programming-and-reversing/478087-calculate-size-esp-boxes-based-distance.html
	inline float getDistance(Vector3 from, Vector3 to) {
		return sqrt(pow(to.x - from.x, 2) + pow(to.y - from.y, 2) + pow(to.z - from.z, 2));
	};

 inline std::string sanitizeString(const std::string& input) {
        std::string cleaned;
        cleaned.reserve(input.length()); // Pre-allocate memory for efficiency
        for (char c : input) {
            // isprint() checks for any printable character (letters, digits, punctuation, space)
            // The cast to unsigned char is crucial for isprint to work correctly with all byte values.
            if (isprint(static_cast<unsigned char>(c))) {
                cleaned += c;
            }
        }
        return cleaned;
    }

	  inline int levenshteinDistance(const std::string &s1, const std::string &s2) {
        const size_t len1 = s1.size(), len2 = s2.size();
        std::vector<unsigned int> col(len2 + 1), prevCol(len2 + 1);

        for (unsigned int i = 0; i < prevCol.size(); i++) {
            prevCol[i] = i;
        }

        for (unsigned int i = 0; i < len1; i++) {
            col[0] = i + 1;
            for (unsigned int j = 0; j < len2; j++) {
                col[j + 1] = std::min({ prevCol[j + 1] + 1, col[j] + 1, prevCol[j] + (s1[i] == s2[j] ? 0 : 1) });
            }
            col.swap(prevCol);
        }
        return prevCol[len2];
    }

	inline std::string get_hwid() {
		HW_PROFILE_INFO hwProfileInfo;
		if (GetCurrentHwProfile(&hwProfileInfo))
			return hwProfileInfo.szHwProfileGuid;
	}

	inline std::string toLower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c){ return std::tolower(c); });
		return s;
	}

	inline std::wstring getExePath() {
		WCHAR buffer[MAX_PATH] = { 0 };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
		return std::wstring(buffer).substr(0, pos);
	}

	inline std::wstring getConfigPath() {
		WCHAR buffer[MAX_PATH] = { 0 };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::wstring::size_type pos = std::wstring(buffer).find_first_of(L"\\/");
		return std::wstring(buffer).substr(0, pos) + L"\\Dexterion\\Config";
	}

	inline std::wstring getDexterionPath() {
		WCHAR buffer[MAX_PATH] = { 0 };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::wstring::size_type pos = std::wstring(buffer).find_first_of(L"\\/");
		return std::wstring(buffer).substr(0, pos) + L"\\Dexterion";
	}

	inline ImColor float3ToImColor(float colours[3], float a = 1.f) {
		return ImColor(colours[0], colours[1], colours[2], a);
	}

	inline uint64_t currentTimeMillis() {
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	inline bool intToBool(int i) {
		return i != 0;
	}

	inline namespace espF {
		// This function now returns a float reduction based on distance for font scaling
		// The 'distance' parameter here is the distance in hundreds of units (e.g., 1 = 100 units)
		// inline float fixFontSize(float distance) {
		// 	// Define constants for scaling behavior
		// 	const float max_reduction = 10.0f; // Maximum font size reduction
		// 	const float scaling_factor = 0.5f; // How much to reduce per unit of 'distance' (hundreds of units)

		// 	// Calculate a linear reduction based on distance
		// 	float reduction = distance * scaling_factor;

		// 	// Cap the reduction to prevent font size from becoming too small or negative
		// 	return std::min(reduction, max_reduction);
		// }

inline float fixFontSize(float distance) {
    const float max_reduction = 16.0f;
    const float scaling_factor = 0.16f;

    float reduction = distance * scaling_factor;

    return std::min(reduction, max_reduction);
}

		// This is with size being 5 !!!
		inline float fixJointSize(float size) {
			int returnSize = 1;

			if (size > 3.f) returnSize = 3.f;
			if (size > 7.f) returnSize = 4.f;
			if (size < 1.f) returnSize = 2.f;

			return returnSize;
		}

		inline float getFontSize(float fontSize,int distance) {
			return (fontSize - utils::fixFontSize(distance));
		}

		inline float getJointSize(float joinSize, int distance) {
			return (joinSize - utils::fixJointSize(distance));
		}

		inline std::tuple<float,float> getTextOffsets(float x,float y, float horizontalDivide,float verticalDivide = 1) {
			float horizontalOffset = x / horizontalDivide;
			float verticalOffset = y - verticalDivide;

			std::tuple<float,float> coords = { horizontalOffset, verticalOffset };
			return coords;
		}
	}
}
