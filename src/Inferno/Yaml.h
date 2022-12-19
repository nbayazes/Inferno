#pragma once

#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include <spdlog/spdlog.h>
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

    //size_t to_chars(ryml::substr buf, Inferno::Vector3 v) { 
    //    fmt::format("({},{},{})", v.x, v.y, v.z); 
    //}

    // Tries to read a value from the node. Value is unchanged if node is invalid.
    template<class T>
    void ReadValue(ryml::NodeRef node, T& value) {
        static_assert(!std::is_same_v<T, const char*>, "Must be writable value");
        if (!node.is_seed() && node.has_val()) node >> value;
    }

    // Specification for bools. Reading them as bools has undeseriable behavior.
    template<>
    inline void ReadValue(ryml::NodeRef node, bool& value) {
        int val = 0;
        if (!node.is_seed() && node.has_val()) {
            node >> val;
            value = val;
        }
    }

    template<>
    inline void ReadValue(ryml::NodeRef node, std::filesystem::path& value) {
        if (node.is_seed() || !node.has_val()) return;
        std::string path;
        node >> path;
        if (std::filesystem::exists(path))
            value = path;
        else
            SPDLOG_WARN("Invalid path in config:\n{}", path);
    }

    inline void ReadValue(ryml::NodeRef node, std::array<bool, 4>& a) {
        if (node.is_seed() || !node.has_val()) return;
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
    inline void ReadValue(ryml::NodeRef node, DirectX::SimpleMath::Color& value) {
        if (node.is_seed() || !node.has_val()) return;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ',', true);
        if (token.size() != 4 && token.size() != 3)
            return;


        float r{}, g{}, b{}, a{};
        ParseFloat(token[0], r);
        ParseFloat(token[1], g);
        ParseFloat(token[2], b);
        if (token.size() == 4)
            ParseFloat(token[3], a);

        value = DirectX::SimpleMath::Color{ r, g, b, a };
    }

    template<>
    inline void ReadValue(ryml::NodeRef node, Inferno::Tag& value) {
        if (node.is_seed() || !node.has_val()) return;
        std::string str;
        node >> str;
        auto token = Inferno::String::Split(str, ':', true);
        if (token.size() != 2)
            return;

        try {
            value.Segment = (Inferno::SegID)std::stoi(token[0]);
            value.Side = (Inferno::SideID)std::stoi(token[1]);
        }
        catch (...) {
        }
    }

    inline std::string EncodeArray(const std::array<bool, 4>& a) {
        return fmt::format("{}, {}, {}, {}", (int)a[0], (int)a[1], (int)a[2], (int)a[3]);
    }

    inline std::string EncodeColor(const DirectX::SimpleMath::Color& color) {
        return fmt::format("{}, {}, {}, {}", color.R(), color.G(), color.B(), color.A());
    }

    inline std::string EncodeColor3(const DirectX::SimpleMath::Color& color) {
        return fmt::format("{}, {}, {}", color.R(), color.G(), color.B());
    }

    inline std::string EncodeTag(Inferno::Tag tag) {
        return fmt::format("{}:{}", (int)tag.Segment, (int)tag.Side);
    }


    inline void ReadString(ryml::NodeRef node, std::string& value) {
        return ReadValue<std::string>(node, value);
    }

    template<class T>
    void WriteSequence(ryml::NodeRef node, T&& src) {
        node |= ryml::SEQ;

        for (auto& item : src)
            node.append_child() << item.string();
    }

}