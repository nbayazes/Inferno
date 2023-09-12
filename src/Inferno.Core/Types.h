#pragma once

#include <memory>
#include <span>
#include <stack>
#include <numbers>
#include <filesystem>
#include <DirectXTK12/SimpleMath.h>
#include <ranges>
#include <fmt/format.h>

namespace DirectX::SimpleMath {
    struct Matrix3x3 : XMFLOAT3X3 {
        Matrix3x3() noexcept
            : XMFLOAT3X3(1.f, 0, 0,
                         0, 1.f, 0,
                         0, 0, 1.f) {}

        // right, up, forward vectors
        explicit Matrix3x3(const Vector3& r0, const Vector3& r1, const Vector3& r2) noexcept
            : XMFLOAT3X3(r0.x, r0.y, r0.z,
                         r1.x, r1.y, r1.z,
                         r2.x, r2.y, r2.z) {}

        explicit Matrix3x3(const Matrix& m) noexcept
            : XMFLOAT3X3(m._11, m._12, m._13,
                         m._21, m._22, m._23,
                         m._31, m._32, m._33) {}

        // Constructs a rotation matrix from a forward and up vector
        explicit Matrix3x3(Vector3 forward, Vector3 up) noexcept
            : XMFLOAT3X3() {
            forward.Normalize();
            up.Normalize();
            auto right = up.Cross(forward);
            Right(right);
            Up(forward.Cross(right));
            Forward(forward);
        }

        Vector3 Up() const noexcept { return { _21, _22, _23 }; }
        void Up(const Vector3& v) noexcept { _21 = v.x; _22 = v.y; _23 = v.z; }

        Vector3 Down() const  noexcept { return { -_21, -_22, -_23 }; }
        void Down(const Vector3& v) noexcept { _21 = -v.x; _22 = -v.y; _23 = -v.z; }

        Vector3 Right() const noexcept { return { _11, _12, _13 }; }
        void Right(const Vector3& v) noexcept { _11 = v.x; _12 = v.y; _13 = v.z; }

        Vector3 Left() const noexcept { return { -_11, -_12, -_13 }; }
        void Left(const Vector3& v) noexcept { _11 = -v.x; _12 = -v.y; _13 = -v.z; }

        Vector3 Forward() const noexcept { return { -_31, -_32, -_33 }; }
        void Forward(const Vector3& v) noexcept { _31 = -v.x; _32 = -v.y; _33 = -v.z; }

        Vector3 Backward() const noexcept { return { _31, _32, _33 }; }
        void Backward(const Vector3& v) noexcept { _31 = v.x; _32 = v.y; _33 = v.z; }

        Matrix3x3& operator *= (const Matrix& matrix) {
            DirectX::XMStoreFloat3x3(this, Matrix(*this) * matrix);
            return *this;
        }

        void Normalize() {
            Vector3 forward, up, right;
            Forward().Normalize(forward);
            Up().Normalize(up);
            Right().Normalize(right);
            Forward(forward);
            Up(up);
            Right(right);
        }

        void Transpose() {
            
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

    using namespace std::string_view_literals;

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

    constexpr Color LIGHT_UNSET = { -1, -1, -1 }; // 'Unset' value for lights

    ///////////////////////////////////////////////////////////////////////////
    // .NET Aliases

    using Exception = std::runtime_error;

    //struct Exception : public std::exception {
    //    template<class...TArgs>
    //    Exception(const string_view format, TArgs&&...args) :
    //        std::runtime_error(fmt::format(format, std::forward<TArgs>(args)...)) {}
    //};
    using ArgumentException = std::invalid_argument;
    struct IndexOutOfRangeException final : std::exception {
        const char* what() const override { return "Index out of range"; }
    };

    struct NotImplementedException final : std::exception {
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

    using uchar = unsigned char;
    using ushort = unsigned short;
    using uint = unsigned int;

    template<class T, class TAlloc = std::allocator<T>>
    using List = std::vector<T, TAlloc>;

    template<class T, size_t TSize>
    using Array = std::array<T, TSize>;

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

    enum class DynamicLightMode { Constant, Flicker, Pulse, FastFlicker, BigPulse };

    enum class ObjID : int16 { None = -1 }; // Object ID
    enum class ObjSig : uint16 { None = 0 }; // Object signature
    enum class SegID : int16 { None = -1, Exit = -2 }; // Segment ID
    enum class RoomID : int16 { None = -1 }; // Room ID
    enum class TexID : int16 { None = -1, Invalid = 0 }; // Texture ID (Pig)
    enum class EffectID : int16 { None = -1 }; // Effect ID for visual effects

    // Unique reference to an object that includes the signature.
    // Relying only on ObjIDs causes problems when new objects are created in an existing slot.
    struct ObjRef {
        ObjID Id = ObjID::None;
        ObjSig Signature = ObjSig::None;

        constexpr ObjRef(ObjID id, ObjSig sig) : Id(id), Signature(sig) { }
        constexpr ObjRef() = default;

        bool IsNull() const { return Id == ObjID::None || Signature == ObjSig::None; }

        bool operator ==(const ObjRef ref) const {
            if (IsNull() || ref.IsNull()) return false;
            return Id == ref.Id && Signature == ref.Signature;
        }

        explicit operator bool() const { return !IsNull(); }
    };

    // Level Texture ID. Maps to TexIDs.
    enum class LevelTexID : int16 {
        None = -1,
        Unset = 0 // Used for unset overlays and open connections
    };
    enum class WallID : int16 { None = -1, Max = 255 }; // Unfortunately segments save their wall IDs as bytes, limiting us to 255
    enum class DClipID : sbyte { None = -1, Unset = 2 }; // Door clip ID (wall clips)
    enum class EClipID : int16 { None = -1 }; // Effect clip ID (animation on a wall)
    enum class MatcenID : uint8 { None = 255 };
    enum class TriggerID : uint8 { None = 255 };

    // Video clips of explosions or other particle effects
    enum class VClipID : int32 {
        None = -1,
        HitPlayer = 1,
        SmallExplosion = 2,
        LightExplosion = 3, // A light or monitor exploding
        HitLava = 5,
        Matcen = 10,
        PlayerSpawn = 61,
        Despawn = 62,
        HitWater = 84,
        AfterburnerBlob = 95,
    };

    enum class SoundID : int16 {
        None = -1,

        Explosion = 11,
        RobotHitPlayer = 17,
        HitLava = 20,
        RobotDestroyed = 21,
        DropBomb = 26,
        HitLockedDoor = 27,
        HitInvulnerable = 27,
        HitControlCenter = 30,
        ExplodingWall = 31, // Long sound
        Siren = 32,
        MineBlewUp = 33,
        FusionWarmup = 34,
        DropWeapon = 39, // D2
        PlayerHitForcefield = 40,
        WeaponHitForcefield = 41,
        ForcefieldHum = 42,
        ForcefieldOff = 43,
        TouchMarker = 50,
        BuddyReachedGoal = 51,
        Refuel = 62,
        PlayerHitWall = 70,
        HitPlayer = 71,
        // WallScrape = 72,
        RescueHostage = 91,
        BriefingHum = 94,
        BriefingPrint = 95,
        Countdown0 = 100, // countdown messages are 100-114
        Countdown13 = 113,
        SelfDestructActivated = 114,
        HomingWarning = 122,

        TouchLavafall = 150,
        TouchLava = 151,
        TouchWater = 152,
        TouchWaterfall = 158,

        SelectPrimary = 153,
        SelectSecondary = 154,
        SelectFail = 156,
        AlreadySelected = 155,

        CloakOn = 160,
        CloakOff = 161,

        InvulnOff = 163,

        OpenWall = 246,

        Cheater = 200,

        HitWater = 232,
        MissileHitWater = 233,

        AmbientLava = 222,
        AmbientWater = 223,

        ConvertEnergy = 241,
        ItemStolen = 244,
        LightDestroyed = 157,

        SeismicStart = 251,
        AfterburnerIgnite = 247,
        AfterburnerStop = 248,

        SecretExit = 249,
    };

    enum class ModelID : int32 {
        None = -1,
        D1Reactor = 39,
        D1Player = 43,
        D1Coop = 44,
        D2Player = 108, // Also used for co-op
        Mine = 159, // D2 editor placeable mine
    };

    // ModelIDs over this value are treated as Outage models
    //constexpr int32 OUTRAGE_MODEL_START = 500;

    // A model can be loaded from D1/D2 data, or a path
    struct ModelResource {
        ModelID D1 = ModelID::None;
        ModelID D2 = ModelID::None;
        string Path; // D3 hog file entry or system path

        // Priority is D3, D1, D2
        bool operator== (const ModelResource& rhs) const {
            if (!Path.empty() && !rhs.Path.empty() && Path == rhs.Path) return true;
            if (D1 == rhs.D1) return true;
            return D2 == rhs.D2;
        }
    };

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
        explicit constexpr operator bool() const { return HasValue(); }

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
    struct PointTag : Tag { uint16 Point; };

    // Connection between rooms
    struct Portal {
        //Portal() = default;
        //Portal(Tag tag, RoomID roomLink, int portalLink) : Tag(tag), RoomLink(roomLink), PortalLink(portalLink) {  }

        Tag Tag; // Side the portal is attached to
        RoomID RoomLink = RoomID::None;
        int PortalLink = -1; // Index of portal in connected room
        int Id = -1; // Linked portals share the same id
    };

    constexpr Tag GetOppositeSide(Tag tag) {
        tag.Side = GetOppositeSide(tag.Side);
        return tag;
    }

    //Some handy constants for interacting with fixed precision values
    constexpr auto F1_0 = 0x10000;

    constexpr float DegToRad = (float)std::numbers::pi / 180.0f;
    constexpr float RadToDeg = 180.0f / (float)std::numbers::pi;

    class LerpedColor {
        Color _color, _startColor, _endColor;
        double _startTime = 0;
        float _fadeTime = 1;

    public:
        LerpedColor(const Color& color = {})
            : _color(color) {}

        void SetTarget(const Color& color, double currentTime, float fadeTime = 0.5f) {
            if (fadeTime <= 0) {
                _startColor = _endColor = _color = color;
            }
            else {
                _startColor = _color;
                _endColor = color;
                _startTime = currentTime;
                _fadeTime = std::max(fadeTime, 0.0f);
            }
        }

        void Update(double time) {
            if (_color == _endColor) return;
            auto t = std::clamp(float(time - _startTime) / _fadeTime, 0.0f, 1.0f);
            _color = Color::Lerp(_startColor, _endColor, t);
        }

        const Color& GetColor() const {
            return _color;
        }
    };

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

        // Tries to remove element at index. Shifts remaining elements.
        bool Remove(int index) {
            if (index < 0 || index >= Capacity || _count == 0)
                return false;

            // Shift existing items
            for (int i = index; i < Capacity - 1; i++)
                _data[i] = _data[i + 1];

            _count--;
            _data[_count] = {};
            return true;
        }

        // Number of active elements. Not to be confused with capacity.
        [[nodiscard]] size_t Count() const { return _count; }

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

        [[nodiscard]] bool InRange(size_t index) const { return index < _count; }
        auto at(size_t index) { return _data.at(index); }
        auto begin() { return _data.begin(); }
        auto end() { return _data.begin() + _count; }
        const auto begin() const { return _data.begin(); }
        const auto end() const { return _data.begin() + _count; }
    };

    // enum to int formatters to make fmt happy

    constexpr auto format_as(SegID id) { return (int)id; }
    constexpr auto format_as(SideID id) { return (int)id; }
    constexpr auto format_as(ObjID id) { return (int)id; }
    constexpr auto format_as(WallID id) { return (int)id; }
    constexpr auto format_as(TexID id) { return (int)id; }
    constexpr auto format_as(LevelTexID id) { return (int)id; }
    constexpr auto format_as(ObjSig id) { return (int)id; }
    constexpr auto format_as(RoomID id) { return (int)id; }
    inline auto format_as(Tag tag) { return fmt::format("{}:{}",tag.Segment, tag.Side); }
}

template <>
struct std::hash<Inferno::Tag> {
    std::size_t operator()(const Inferno::Tag& t) const noexcept {
        return std::hash<Inferno::SegID>()(t.Segment) ^ (std::hash<Inferno::SideID>()(t.Side) << 1);
    }
};
