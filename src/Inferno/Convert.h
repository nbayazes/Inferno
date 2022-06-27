#pragma once

#include <string>
#include <vector>
#include <stringapiset.h>

// This would be in Utility but it depends on Windows functions
namespace Convert {
    inline std::wstring ToWideString(std::string str) {
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
        std::vector<WCHAR> buffer(size);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buffer.data(), size);
        return { buffer.begin(), buffer.end() };
    }

    inline std::string ToString(std::wstring str) {
        int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0, nullptr, nullptr);
        std::vector<char> buffer(size);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), buffer.data(), size, nullptr, nullptr);
        return { buffer.begin(), buffer.end() };
    }
}
