#pragma once

#include <string>
#include <stringapiset.h>
#include <vector>

// This would be in Utility but it depends on Windows functions
namespace Convert {
    inline std::wstring ToWideString(std::string_view str) {
        int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
        if (size < 0) return {}; // failure...
        std::vector<WCHAR> buffer(size);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), -1, buffer.data(), size);
        return { buffer.begin(), buffer.end() };
    }

    inline std::string ToString(const std::wstring& str) {
        int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0, nullptr, nullptr);
        if (size < 0) return {}; // failure...
        std::vector<char> buffer(size);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), buffer.data(), size, nullptr, nullptr);
        return { buffer.begin(), buffer.end() };
    }
}

namespace Inferno {
    inline std::wstring Widen(std::string_view str) {
        int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
        if (size < 0) return {}; // failure...
        std::vector<WCHAR> buffer(size);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), -1, buffer.data(), size);
        return { buffer.begin(), buffer.end() };
    }

    inline std::string Narrow(const std::wstring& str) {
        int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0, nullptr, nullptr);
        if (size < 0) return {}; // failure...
        std::vector<char> buffer(size);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), buffer.data(), size, nullptr, nullptr);
        return { buffer.begin(), buffer.end() };
    }
}