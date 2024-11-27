#pragma once

#ifndef C4_USE_ASSERT
// Bad data shouldn't cause an assertion, even in debug
#define C4_USE_ASSERT 0
#endif

#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include "Types.h"
#include "Utility.h"

namespace Yaml {
    inline bool ParseFloat(std::string_view s, float& value) {
        float f = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), f);
        if (ec != std::errc()) return false;
        value = f;
        return true;
    }

    inline bool ParseInt(std::string_view s, float& value) {
        float f = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), f);
        if (ec != std::errc()) return false;
        value = f;
        return true;
    }

    //size_t to_chars(ryml::substr buf, Inferno::Vector3 v) { 
    //    fmt::format("({},{},{})", v.x, v.y, v.z); 
    //}

    // Tries to read a value from the node. Value is unchanged if node is invalid.
    template<class T>
    bool ReadValue(ryml::ConstNodeRef node, T& value) {
        static_assert(!std::is_same_v<T, const char*>, "Must be writable value");
        if (!node.valid()) return false;
        node >> value;
        return true;
    }

    template<Inferno::IsEnum T>
    bool ReadValue(ryml::ConstNodeRef node, T& id) {
        if (!node.valid()) return false;
        node >> (std::underlying_type_t<T>&)id;
        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, bool& value) {
        if (!node.valid()) return false;
        int val = 0;
        node >> val;
        value = (bool)val;
        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, std::filesystem::path& value) {
        if (!node.valid()) return false;
        std::string path;
        node >> path;
        value = path;
        return true;
        /*if (std::filesystem::exists(path))
            value = path;
        else
            SPDLOG_WARN("Invalid path in config:\n{}", path);*/
    }

    inline void ReadValue(ryml::ConstNodeRef node, std::array<bool, 4>& a) {
        if (!node.valid()) return;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 4)
            return;

        a[0] = token[0] == "1";
        a[1] = token[1] == "1";
        a[2] = token[2] == "1";
        a[3] = token[3] == "1";
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, DirectX::SimpleMath::Color& value) {
        if (!node.valid()) return false;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 4 && token.size() != 3)
            return false;

        ParseFloat(token[0], value.x);
        ParseFloat(token[1], value.y);
        ParseFloat(token[2], value.z);

        if (token.size() == 4)
            ParseFloat(token[3], value.w);
        else
            Inferno::ColorRGBToRGBV(value);

        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, DirectX::SimpleMath::Vector3& value) {
        if (!node.valid()) return false;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 3)
            return false;

        ParseFloat(token[0], value.x);
        ParseFloat(token[1], value.y);
        ParseFloat(token[2], value.z);
        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, Inferno::uint2& value) {
        if (!node.valid()) return false;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 2)
            return false;

        try {
            auto x = std::stoi(token[0]);
            auto y = std::stoi(token[1]);
            // y might throw, so read both before assignment
            value.x = x;
            value.y = y;
        }
        catch (...) {
            return false;
        }

        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, DirectX::SimpleMath::Vector2& value) {
        if (!node.valid()) return false;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 2)
            return false;

        ParseFloat(token[0], value.x);
        ParseFloat(token[1], value.y);
        return true;
    }

    template<>
    inline bool ReadValue(ryml::ConstNodeRef node, Inferno::Tag& value) {
        if (!node.valid()) return false;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ':', true);
        if (token.size() != 2)
            return false;

        try {
            value.Segment = (Inferno::SegID)std::stoi(token[0]);
            value.Side = (Inferno::SideID)std::stoi(token[1]);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    inline bool ReadString(ryml::ConstNodeRef node, std::string& value) {
        return ReadValue<std::string>(node, value);
    }

    inline std::string EncodeArray(const std::array<bool, 4>& a) {
        return fmt::format("{}, {}, {}, {}", (int)a[0], (int)a[1], (int)a[2], (int)a[3]);
    }

    inline std::string EncodeVector(const DirectX::XMUINT2& v) {
        return fmt::format("{}, {}", v.x, v.y);
    }

    inline std::string EncodeVector(const DirectX::SimpleMath::Vector2& v) {
        return fmt::format("{}, {}", v.x, v.y);
    }

    inline std::string EncodeVector(const DirectX::SimpleMath::Vector3& v) {
        return fmt::format("{}, {}, {}", v.x, v.y, v.z);
    }

    inline std::string EncodeColor(const DirectX::SimpleMath::Color& color) {
        return fmt::format("{:.3g}, {:.3g}, {:.3g}, {:.3g}", color.R(), color.G(), color.B(), color.A());
    }

    inline std::string EncodeColor3(const DirectX::SimpleMath::Color& color) {
        return fmt::format("{:.3g}, {:.3g}, {:.3g}", color.R(), color.G(), color.B());
    }

    inline std::string EncodeTag(Inferno::Tag tag) {
        return fmt::format("{}:{}", (int)tag.Segment, (int)tag.Side);
    }

    inline bool ReadString(const ryml::NodeRef& node, std::string& value) {
        return ReadValue<std::string>(node, value);
    }

    template<class T>
    void WriteSequence(ryml::NodeRef node, T& src) {
        node |= ryml::SEQ;

        for (auto& item : src)
            node.append_child() << item.string();
    }

}