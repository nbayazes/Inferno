#pragma once

#include <memory>
#include <span>
#include <list>
#include <stack>
#include <numbers>
#include <filesystem>
#include <DirectXTK12/SimpleMath.h>
#include <span>
#include <ranges>

namespace DirectX::SimpleMath {
    struct Matrix3x3 : public XMFLOAT3X3 {
        Matrix3x3() noexcept
            : XMFLOAT3X3(1.f, 0, 0,
                         0, 1.f, 0,
                         0, 0, 1.f) {
        }

        explicit Matrix3x3(const Vector3& r0, const Vector3& r1, const Vector3& r2) noexcept
            : XMFLOAT3X3(r0.x, r0.y, r0.z,
                         r1.x, r1.y, r1.z,
                         r2.x, r2.y, r2.z) {
        }

        explicit Matrix3x3(const Matrix& M) noexcept {
            _11 = M._11; _12 = M._12; _13 = M._13;
            _21 = M._21; _22 = M._22; _23 = M._23;
            _31 = M._31; _32 = M._32; _33 = M._33;
        }

        Vector3 Up() const noexcept { return Vector3(_21, _22, _23); }
        void Up(const Vector3& v) noexcept { _21 = v.x; _22 = v.y; _23 = v.z; }

        Vector3 Down() const  noexcept { return Vector3(-_21, -_22, -_23); }
        void Down(const Vector3& v) noexcept { _21 = -v.x; _22 = -v.y; _23 = -v.z; }

        Vector3 Right() const noexcept { return Vector3(_11, _12, _13); }
        void Right(const Vector3& v) noexcept { _11 = v.x; _12 = v.y; _13 = v.z; }

        Vector3 Left() const noexcept { return Vector3(-_11, -_12, -_13); }
        void Left(const Vector3& v) noexcept { _11 = -v.x; _12 = -v.y; _13 = -v.z; }

        Vector3 Forward() const noexcept { return Vector3(-_31, -_32, -_33); }
        void Forward(const Vector3& v) noexcept { _31 = -v.x; _32 = -v.y; _33 = -v.z; }

        Vector3 Backward() const noexcept { return Vector3(_31, _32, _33); }
        void Backward(const Vector3& v) noexcept { _31 = v.x; _32 = v.y; _33 = v.z; }

        Matrix3x3& operator *= (const Matrix& matrix) {
            DirectX::XMStoreFloat3x3(this, Matrix(*this) * matrix);
            return *this;
        }
    };
}

namespace Inferno {
    consteval auto BIT(auto x) { return 1 << x; }

    //constexpr void SafeRelease(auto x) {
    //    if (x != nullptr) {
    //        x->Release();
    //        x = nullptr;
    //    }
    //}

    // Typed concept that allows iterating over a range
    // Usage: Function(IEnumerable<Type> auto items)
    template <class _Rng, class T>
    concept IEnumerable =
        std::ranges::range<_Rng> && // is a range
        std::input_iterator<std::ranges::iterator_t<_Rng>> && // can forward iterate
        std::is_same_v<T, std::ranges::range_value_t<_Rng>>; // Check that element type matches T

    // Scoped unique pointer.
    template<typename T, typename Deleter = std::default_delete<T>>
    using Ptr = std::unique_ptr<T, Deleter>;

    template<typename T>
    constexpr Ptr<T> MakePtr(auto&& ...args) {
        return std::make_unique<T>(std::forward<decltype(args)>(args)...);
    }

    // Reference counted shared pointer.
    template<typename T>
    using Ref = std::shared_ptr<T>;

    template<typename T>
    constexpr Ref<T> MakeRef(auto&& ...args) {
        return std::make_shared<T>(std::forward<decltype(args)>(args)...);
    }

    using std::make_unique;
    using std::string;
    using std::wstring;
    using std::string_view;
    using std::wstring_view;
    using std::span;

    namespace views = std::views;
    namespace ranges = std::ranges;
    namespace filesystem = std::filesystem;

    using DirectX::SimpleMath::Vector2;
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;
    using DirectX::SimpleMath::Matrix;
    using DirectX::SimpleMath::Matrix3x3;
    using DirectX::SimpleMath::Plane;
    using DirectX::SimpleMath::Color;
    using DirectX::SimpleMath::Ray;
    using DirectX::SimpleMath::Quaternion;

    ///////////////////////////////////////////////////////////////////////////
    // .NET Aliases

    using Exception = std::runtime_error;

    //struct Exception : public std::exception {
    //    template<class...TArgs>
    //    Exception(const string_view format, TArgs&&...args) :
    //        std::runtime_error(fmt::format(format, std::forward<TArgs>(args)...)) {}
    //};
    using ArgumentException = std::invalid_argument;
    struct IndexOutOfRangeException : public std::exception {
        const char* what() const override { return "Index out of range"; }
    };

    struct NotImplementedException : public std::exception {
        const char* what() const override { return "Not Implemented"; }
    };

    // ensure types are the expected size
    static_assert(sizeof(char) == 1);
    static_assert(sizeof(short) == 2);
    static_assert(sizeof(int) == 4);
    static_assert(sizeof(long long) == 8);

    using sbyte = char;
    using ubyte = unsigned char; // 'byte' in C#
    using int8 = int8_t;
    using uint8 = uint8_t;
    using int16 = int16_t;
    using uint16 = uint16_t;
    using int32 = int32_t;
    using uint32 = uint32_t;
    using int64 = int64_t;
    using uint64 = uint64_t;

    using ushort = unsigned short;
    using uint = unsigned int;

    template<class T>
    using List = std::vector<T>;

    template<class T, size_t _Size>
    using Array = std::array<T, _Size>;

    template<class T>
    using Option = std::optional<T>;

    template <class T, class TPr = std::less<T>, class TAlloc = std::allocator<T>>
    using Set = std::set<T, TPr, TAlloc>;

    template<class T, class U>
    using Tuple = std::pair<T, U>;

    template<class T>
    using Stack = std::stack<T>;

    // <T, U, class _Hasher = hash<_Kty>, class _Keyeq = equal_to<_Kty>, class _Alloc = allocator<pair<const _Kty, _Ty>>
    template<class T, class U, class THasher = std::hash<T>>
    using Dictionary = std::unordered_map<T, U, THasher>;

    template <class T, class TContainer = std::deque<T>>
    using Queue = std::queue<T, TContainer>;

    using fix64 = int64; //64 bits int, for timers
    using fix = int32; //16 bits int, 16 bits frac
    using fixang = int16; //angles

    using PointID = uint16; // A level vertex

    enum class ObjID : int16 { None = -1 }; // Object ID
    enum class SegID : int16 { None = -1, Exit = -2 }; // Segment ID
    enum class TexID : int16 { None = -1, Invalid = 0 }; // Texture ID (Pig)
    // Level Texture ID. Maps to TexIDs.
    enum class LevelTexID : int16 {
        None = -1,
        Unset = 0 // Used for unset overlays and open connections
    };
    enum class WallID : int16 { None = -1, Max = 255 }; // Unfortunately segments save their wall IDs as bytes, limiting us to 255
    enum class WClipID : sbyte { None = -1, Unset = 2 }; // Wall clip ID
    enum class VClipID : int32 { None = -1 };
    enum class EClipID : int16 { None = -1 }; // Effect clip ID
    enum class SoundID : int16 { None = -1 };
    enum class ModelID : int32 { None = -1 };
    enum class MatcenID : uint8 { None = 255 };
    enum class TriggerID : uint8 { None = 255 };

    namespace Models {
        constexpr auto PlaceableMine = ModelID(159); // D2 editor placeable mine
        constexpr auto D1Coop = ModelID(44);
        constexpr auto D2Coop = ModelID(108);
        constexpr auto D1Player = ModelID(43);
        constexpr auto D2Player = ModelID(108);
    }

    namespace VClips {
        constexpr auto PlayerHit = VClipID(1); // Wall scrape effect
        constexpr auto SmallExplosion = VClipID(2);
        constexpr auto VolatileWallHit = VClipID(5);
        constexpr auto Matcen = VClipID(10);
        constexpr auto PlayerSpawn = VClipID(61);
        constexpr auto PowerupDespawn = VClipID(62);
        constexpr auto WaterHit = VClipID(84);
        constexpr auto AfterburnerBlob = VClipID(95);
    }

    enum class SideID : int16 {
        None = -1,
        Left = 0,
        Top = 1,
        Right = 2,
        Bottom = 3,
        Back = 4,
        Front = 5
    };

    constexpr SideID SideIDs[] = {
        SideID::Left,
        SideID::Top,
        SideID::Right,
        SideID::Bottom,
        SideID::Back,
        SideID::Front
    };

    constexpr SideID OppositeSideIDs[] = {
        SideID::Right,
        SideID::Bottom,
        SideID::Left,
        SideID::Top,
        SideID::Front,
        SideID::Back
    };

    constexpr SideID GetOppositeSide(SideID side) {
        return OppositeSideIDs[(int)side];
    }

    constexpr SegID operator+(const SegID& a, const SegID& b) {
        assert(a > SegID::None);
        auto id = (int)a + (int)b;
        if (id < 0) return SegID(0); // Never allow going negative
        return SegID(id);
    }

    constexpr SegID operator-(const SegID& a, const SegID& b) {
        assert(a > SegID::None);
        auto id = (int)a - (int)b;
        if (id < 0) return SegID(0); // Never allow going negative
        return SegID(id);
    }

    constexpr SegID operator+=(SegID& a, const SegID& b) { return a = a + b; }
    constexpr SegID operator-=(SegID& a, const SegID& b) { return a = a - b; }

    // Prefix
    constexpr SegID& operator++(SegID& id) { return id = id + SegID(1); }
    constexpr SegID& operator--(SegID& id) { return id = id - SegID(1); }

    // Postfix
    constexpr SegID operator++(SegID& id, int) { auto temp = id; ++id; return temp; }
    constexpr SegID operator--(SegID& id, int) { auto temp = id; --id; return temp; }

    constexpr MatcenID& operator++(MatcenID& id) { return id = MatcenID(((int)id) + 1); }
    constexpr MatcenID& operator--(MatcenID& id) { return id = MatcenID(((int)id) - 1); }
    constexpr MatcenID operator++(MatcenID& id, int) { auto temp = id; ++id; return temp; }
    constexpr MatcenID operator--(MatcenID& id, int) { auto temp = id; --id; return temp; }

    // Prefix
    constexpr WallID& operator++(WallID& id) { return id = WallID((int)id + 1); }
    constexpr WallID& operator--(WallID& id) { return id = WallID((int)id - 1); }
    constexpr bool operator!(const WallID& id) { return id == WallID::None; }

    // Postfix
    constexpr WallID operator++(WallID& id, int) { auto temp = id; ++id; return temp; }
    constexpr WallID operator--(WallID& id, int) { auto temp = id; --id; return temp; }

    constexpr ObjID operator++(ObjID& id, int) { auto temp = id; id = ObjID((int)id + 1); return temp; }
    constexpr ObjID operator--(ObjID& id, int) { auto temp = id; id = ObjID((int)id - 1); return temp; }

    constexpr TriggerID operator++(TriggerID& id, int) { auto temp = id; id = TriggerID((int)id + 1); return temp; }
    constexpr TriggerID operator--(TriggerID& id, int) { auto temp = id; id = TriggerID((int)id - 1); return temp; }

    constexpr SideID& operator++(SideID& side) {
        return side = side == SideID::Front ? SideID::Left : SideID((int16)side + 1);
    }

    constexpr SideID operator++(SideID& side, int) {
        auto temp = side;
        ++side;
        return temp;
    }

    constexpr SideID& operator--(SideID& side) {
        side = side == SideID::Left ? SideID::Front : SideID((int16)side - 1);
        return side;
    }

    constexpr SideID operator--(SideID& side, int) {
        auto temp = side;
        --side;
        return temp;
    }

    // Returns the inverse (opposite) side
    constexpr SideID operator!(SideID& side) {
        return OppositeSideIDs[(int16)side];
    }

    // Tags a segment side
    struct Tag {
        SegID Segment = SegID::None;
        SideID Side = SideID::Left;

        auto operator<=>(const Tag&) const = default;
        constexpr operator bool() const { return HasValue(); }

        constexpr bool HasValue() const {
            return
                Segment > SegID::None &&
                Side > SideID::None &&
                Side < SideID(6);
        }

        // Helpers for algorithms
        static constexpr SegID GetSegID(const Tag& tag) { return tag.Segment; }
        static constexpr SideID GetSideID(const Tag& tag) { return tag.Side; }
    };

    // Tags a point on a segment side
    struct PointTag : public Tag { uint16 Point; };

    constexpr Tag GetOppositeSide(Tag tag) {
        tag.Side = GetOppositeSide(tag.Side);
        return tag;
    }

    //Some handy constants for interacting with fixed precision values
    constexpr auto F1_0 = 0x10000;

    constexpr float DegToRad = (float)std::numbers::pi / 180.0f;
    constexpr float RadToDeg = 180.0f / (float)std::numbers::pi;

    // Array with a fixed size that allows inserting and removing elements while
    // keeping them contiguous
    template<class T, size_t Capacity>
    class ResizeArray {
        std::array<T, Capacity> _data;
        size_t _count = 0;
    public:
        bool Add(T item) {
            if (_count >= Capacity) return false;

            for (auto& x : _data)
                if (x == item) return false;

            _data[_count] = item;
            _count++;
            return true;
        }

        // Tries to remove element at index. Shifts remaining elements to the left.
        bool Remove(int index) {
            if (index < 0 || index >= Capacity || _count == 0)
                return false;

            // Shift existing items left
            for (int i = index; i < Capacity - 1; i++)
                _data[i] = _data[i + 1];

            _count--;
            _data[_count] = {};
            return true;
        }

        // Number of active elements. Not to be confused with capacity.
        size_t Count() const { return _count; }

        // Sets the count, only use when reading raw data
        void Count(size_t count) { _count = std::clamp(count, (size_t)0, Capacity); }

        T& operator[](size_t index) {
            assert(InRange(index));
            return _data[index];
        };

        const T& operator[](size_t index) const {
            assert(InRange(index));
            return _data[index];
        };

        auto& data() { return _data; }
        const auto& data() const { return _data; }

        bool InRange(size_t index) const { return index >= 0 && index < _count; }
        auto at(size_t index) { return _data.at(index); }
        auto begin() { return _data.begin(); }
        auto end() { return _data.begin() + _count; }
        const auto begin() const { return _data.begin(); }
        const auto end() const { return _data.begin() + _count; }
    };
}

namespace std {
    template <>
    struct hash<Inferno::Tag> {
        std::size_t operator()(const Inferno::Tag& t) const {
            return std::hash<Inferno::SegID>()(t.Segment) ^ (std::hash<Inferno::SideID>()(t.Side) << 1);
        }
    };
}
