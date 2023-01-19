#pragma once

#include <sstream>
#include <concepts>
#include <future>
#include "Types.h"

namespace Inferno {
    // Creates a four character code to identify file formats
    consteval uint32 MakeFourCC(const char cc[4]) {
        // this is the same as assigning the characters backwards to an int
        // int i = 'dcba';
        return cc[0] | cc[1] << 8 | cc[2] << 16 | cc[3] << 24;
    }

    //constexpr std::array<char, 4> DecodeFourCC(uint32 value) {
    //    std::array<char, 4> cc{};
    //    cc[0] = char(value & 0x000000ff);
    //    cc[1] = char((value & 0x0000ff00) >> 8);
    //    cc[2] = char((value & 0x00ff0000) >> 16);
    //    cc[3] = char((value & 0xff000000) >> 24);
    //    return cc;
    //}

    constexpr bool IsPowerOfTwo(int v) {
        return v != 0 && (v & (v - 1)) == 0;
    }

    // Returns a random value between 0 and 1
    inline float Random() {
        return (float)rand() / RAND_MAX;
    }

    // defined in C++23
    template <class T>
    inline constexpr bool is_scoped_enum_v = std::conjunction_v<std::is_enum<T>, std::negation<std::is_convertible<T, int>>>;

    template <class T>
    struct is_scoped_enum : std::bool_constant<is_scoped_enum_v<T>> {};

    // Templates to enable bitwise operators on all enums. Might be a bad idea.

    template<class T> requires is_scoped_enum_v<T>
    constexpr T operator | (T lhs, T rhs) {
        return T((std::underlying_type_t<T>)lhs | (std::underlying_type_t<T>)rhs);
    }

    template<class T> requires is_scoped_enum_v<T>
    T& operator |= (T& lhs, T rhs) {
        return lhs = lhs | rhs;
    }

    template<class T> requires is_scoped_enum_v<T>
    constexpr T operator & (T lhs, T rhs) {
        return T((std::underlying_type_t<T>)lhs & (std::underlying_type_t<T>)rhs);
    }

    template<class T> requires is_scoped_enum_v<T>
    T& operator &= (T& lhs, T rhs) {
        return lhs = lhs & rhs;
    }

    template<class T> requires is_scoped_enum_v<T>
    T& operator ~ (T& value) {
        return value = T(~((int)value));
    }

    //template <class T>
    //concept IsEnum = is_scoped_enum_v<T>;

    //// Converts an enum to the underlying type
    //constexpr auto ToUnderlying(IsEnum auto e) {
    //    return static_cast<std::underlying_type<declspec(e)>::type>(e);
    //};

    //inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) WIN_NOEXCEPT { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) | ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
    //    inline ENUMTYPE& operator |= (ENUMTYPE& a, ENUMTYPE b) WIN_NOEXCEPT { return (ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) |= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
    //    inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) WIN_NOEXCEPT { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) & ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
    //    inline ENUMTYPE& operator &= (ENUMTYPE& a, ENUMTYPE b) WIN_NOEXCEPT { return (ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) &= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
    //    inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ~ (ENUMTYPE a) WIN_NOEXCEPT { return ENUMTYPE(~((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a)); } \
    //    inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) WIN_NOEXCEPT { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) ^ ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
    //    inline ENUMTYPE& operator ^= (ENUMTYPE& a, ENUMTYPE b) WIN_NOEXCEPT { return (ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) ^= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \


    // Modulus division without negative numbers
    constexpr auto mod(std::integral auto k, std::integral auto n) {
        return (k %= n) < 0 ? k + n : k;
    }

    // Returns 1 for positive numbers, -1 for negative numbers
    template <typename T>
    constexpr int Sign(T val) {
        return (T(0) < val) - (val < T(0));
    }

    // Combines two unsigned ints into an optimized value
    constexpr uint32 SzudzikPairing(uint16 a, uint16 b) {
        return a >= b ? a * a + a + b : a + b * b;
    }

    // Executes a function on a new thread asynchronously
    void StartAsync(auto&& fun) {
        auto future = std::make_shared<std::future<void>>();
        *future = std::async(std::launch::async, [future, fun] {
            fun();
        }); // future disposes itself on exit
    }

    constexpr float Step(float value, float step) {
        if (step == 0.0f) return value;
        return step * std::round(value / step);
    }

    inline float Desaturate(const Color& color) {
        Color desaturate;
        color.AdjustSaturation(0, desaturate);
        return desaturate.x;
    }

    constexpr void ClampColor(Color& color, float min = 0.0f, float max = 1.0f) {
        color.x = std::clamp(color.x, min, max);
        color.y = std::clamp(color.y, min, max);
        color.z = std::clamp(color.z, min, max);
        color.w = std::clamp(color.w, min, max);
    }

    constexpr void ClampColor(Color& color, const Color& min = { 0, 0, 0, 0 }, const Color max = { 1, 1, 1, 1 }) {
        color.x = std::clamp(color.x, min.x, max.x);
        color.y = std::clamp(color.y, min.y, max.y);
        color.z = std::clamp(color.z, min.z, max.z);
        color.w = std::clamp(color.w, min.w, max.w);
    }

    constexpr Color ColorFromRGB(uint8 r, uint8 g, uint8 b, uint8 a = 255) {
        return { (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, float(a) / 255.0f };
    }

    inline Vector3 AverageVectors(span<Vector3> verts) {
        Vector3 average;

        for (int i = 0; i < verts.size(); i++)
            average += verts[i];

        average /= (float)verts.size();
        return average;
    }

    inline Vector3 AverageVectors(span<const Vector3*> verts) {
        Vector3 average;

        for (int i = 0; i < verts.size(); i++)
            average += *verts[i];

        average /= (float)verts.size();
        return average;
    }

    inline Color AverageColors(span<Color> colors) {
        Vector4 average;

        for (int i = 0; i < colors.size(); i++)
            average += colors[i].ToVector4();

        average /= (float)colors.size();
        return Color(average);
    }

    inline Vector3 VectorMin(const Vector3& a, const Vector3& b) {
        Vector3 r;
        r.x = a.x < b.x ? a.x : b.x;
        r.y = a.y < b.y ? a.y : b.y;
        r.z = a.z < b.z ? a.z : b.z;
        return r;
    }

    inline Vector3 VectorMax(const Vector3& a, const Vector3& b) {
        Vector3 r;
        r.x = a.x > b.x ? a.x : b.x;
        r.y = a.y > b.y ? a.y : b.y;
        r.z = a.z > b.z ? a.z : b.z;
        return r;
    }

    constexpr DirectX::XMVECTORF32 UNIT_VECTOR_EPSILON = { { { 1.0e-4f, 1.0e-4f, 1.0e-4f, 1.0e-4f } } };

    inline bool IsNormalized(const Vector3& v) {
        using namespace DirectX;
        auto difference = XMVectorSubtract(XMVector3Length(v), XMVectorSplatOne());
        return XMVector4Less(XMVectorAbs(difference), UNIT_VECTOR_EPSILON);
    }

    inline bool IsZero(const Vector3& v) {
        using namespace DirectX;
        return XMVector4Less(XMVectorAbs(v), UNIT_VECTOR_EPSILON);
    }

    // Converts a direction vector into a rotation matrix
    inline Matrix DirectionToRotationMatrix(const Vector3& direction, float roll = 0) {
        assert(IsNormalized(direction));
        auto pitch = asin(std::clamp(direction.y, -1.0f, 1.0f));
        auto yaw = atan2(-direction.z, direction.x);
        return Matrix::CreateFromYawPitchRoll(yaw, roll, pitch);
    }

    // Projects a ray onto a plane. What happens if parallel?
    inline Vector3 ProjectRayOntoPlane(const Ray& ray, const Vector3& planeOrigin, Vector3 planeNormal) {
        assert(IsNormalized(planeNormal));
        auto length = planeNormal.Dot(ray.position - planeOrigin) / planeNormal.Dot(-ray.direction);
        return ray.position + ray.direction * length;
    }

    inline Vector3 ProjectPointOntoPlane(const Vector3& point, const Vector3& planeOrigin, Vector3 planeNormal) {
        // q - dot(q - p, n) * n
        assert(IsNormalized(planeNormal));
        return point - (point - planeOrigin).Dot(planeNormal) * planeNormal;
    }

    inline Vector3 ProjectPointOntoPlane(const Vector3& point, const Plane& plane) {
        // p' = p - (n ⋅ p + d) * n
        return point - (plane.DotNormal(point) + plane.D()) * plane.Normal();
    }

    inline float DistanceFromPlane(const Vector3& point, const Vector3& planeOrigin, Vector3 planeNormal) {
        return planeNormal.Dot(point - planeOrigin);
    }

    inline float PointToLineDistance(const Vector3& point, const Vector3& v0, const Vector3& v1) {
        // normalize all points to vector 1
        auto A = v0 - point;
        auto B = v1 - point;

        // use formula from page 505 of "Calculase and Analytical Geometry" Fifth Addition
        // by Tommas/Finney, Addison-Wesley Publishing Company, June 1981
        //          B * A
        // B2 = B - ----- A
        //          A * A

        float a2 = A.Dot(A);
        float c = a2 != 0 ? B.Dot(A) / a2 : 0;
        auto C = B - (A * c);
        return C.Length();
    }

    inline float PointToPlaneDistance(const Vector3& point, const Vector3& planeOrigin, Vector3 planeNormal) {
        assert(IsNormalized(planeNormal));
        auto w = point - planeOrigin;
        auto v = planeNormal;
        return v.Dot(w) / v.Length();
    }

    // v0 and v1 must be normalized. Returns [-PI, PI]
    inline float AngleBetweenVectors(const Vector3& v0, const Vector3& v1, const Vector3& normal) {
        auto dot = v0.Dot(v1);
        auto cross = v0.Cross(v1);
        auto angle = atan2(cross.Length(), dot);
        if (normal.Dot(cross) < 0) angle = -angle;
        return angle;
    }

    // v0 and v1 must be normalized. Returns [0, PI]
    inline float AngleBetweenVectors(const Vector3& v0, const Vector3& v1) {
        auto dot = v0.Dot(v1);
        if (dot <= -0.999f) return (float)std::numbers::pi;
        return acos(v0.Dot(v1));
    }

    // v0 and v1 must be normalized. Returns [0, PI]
    inline float AngleBetweenVectors(const Vector2& v0, const Vector2& v1) {
        return acos(v0.Dot(v1));
    }

    // Rotates vector around 0,0 by an angle in radians.
    inline Vector2 RotateVector(const Vector2& v, float angle) {
        return { v.x * cos(angle) - v.y * sin(angle),
                 v.x * sin(angle) + v.y * cos(angle) };
    }

    // Returns [-PI, PI]
    inline float AngleBetweenPoints(const Vector3& a, const Vector3& b, const Vector3& origin, const Vector3& normal) {
        auto v0 = a - origin;
        auto v1 = b - origin;
        v0.Normalize();
        v1.Normalize();
        return AngleBetweenVectors(v0, v1, normal);
    }

    constexpr float PaletteToRGB(int16 color) {
        return color >= 31 ? 1.0f : float(color) / 31.0f;
    }

    // Unpacks a 16 bpp palette value to a color
    constexpr Color UnpackColor(uint16 color) {
        int16 r = ((color >> 10) & 31) * 2;
        int16 g = ((color >> 5) & 31) * 2;
        int16 b = (color & 31) * 2;

        return { PaletteToRGB(r), PaletteToRGB(g), PaletteToRGB(b) };
    }

    constexpr float FixToFloat(fix f) {
        return (float)f / (float)(1 << 16);
    }

    constexpr int MAX_FIX = 32768; // Maximum fixed point value
    constexpr int MIN_FIX = -32769; // Minimum fixed point value

    constexpr fix FloatToFix(float f) {
        assert(f < MAX_FIX && f > MIN_FIX); // out of range
        return (fix)(f * (1 << 16));
    }

    // Calls a fire-and-forget function
    void CallAsync(auto&& fn) {
        auto future = std::make_shared<std::future<void>>();
        // capturing future adds +1 ref count which will be released when fn completes
        *future = std::async(std::launch::async, [future, fn] {
            fn();
        });
    }

    namespace String {
        constexpr bool Contains(const std::string_view str, const std::string_view value) {
            return str.find(value) != string::npos;
        }

        constexpr Option<size_t> IndexOf(const std::string_view str, const std::string_view value) {
            auto p = str.find(value);
            if (p == string::npos) return {};
            return p;
        }

        //inline bool InvariantContains(const std::wstring_view str, const std::wstring_view value) {
        //    int found;
        //    FindNLSString(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, str.data(), -1, value.data(), -1, &found);
        //    return found;
        //}

        // Returns true if two strings are equal ignoring capitalization
        inline bool InvariantEquals(const std::string_view s1, const std::string_view s2) {
            return _stricmp(s1.data(), s2.data()) == 0;
        }

        // Returns true if two strings are equal ignoring capitalization, up to a number of characters
        inline bool InvariantEquals(const std::string_view s1, const std::string_view s2, size_t maxCount) {
            return _strnicmp(s1.data(), s2.data(), maxCount) == 0;
        }

        // Returns true if two strings are equal ignoring capitalization
        inline bool InvariantEquals(const std::wstring_view s1, const std::wstring_view s2) {
            return _wcsicmp(s1.data(), s2.data()) == 0;
        }

        // Returns the file name without the extension. Returns original string if no extension.
        inline string NameWithoutExtension(string_view str) {
            auto i = str.find('.');
            if (i == string::npos) return string(str);
            return string(str.substr(0, i));
        }

        // Returns the extension without the dot. Returns empty if no extension.
        constexpr string Extension(const string& str) {
            auto i = str.find('.');
            if (i == string::npos) return "";
            return str.substr(i + 1);
        }

        // Returns the extension without the dot. Returns empty if no extension.
        constexpr wstring Extension(const wstring& str) {
            auto i = str.find('.');
            if (i == wstring::npos) return L"";
            return str.substr(i + 1);
        }

        const std::string Whitespace = " \n\r\t\f\v";

        // Remove whitespace from the beginning
        inline string TrimStart(string s) {
            auto start = s.find_first_not_of(Whitespace);
            return start == std::string::npos ? "" : s.substr(start);
        }

        // Remove whitespace from the end
        inline string TrimEnd(string s) {
            auto end = s.find_last_not_of(Whitespace);
            return end == std::string::npos ? "" : s.substr(0, end + 1);
        }

        // Remove whitespace from both ends
        inline string Trim(string s) {
            return TrimStart(TrimEnd(s));
        }

        // Returns an uppercase copy of the string
        auto ToUpper(auto str) {
            std::transform(str.begin(), str.end(), str.begin(), [](auto c) { return (char)std::toupper(c); });
            return str;
        };

        // Returns a lowercase copy of the string.
        // Not safe for non-ascii
        auto ToLower(auto str) {
            std::transform(str.begin(), str.end(), str.begin(), [](auto c) { return (char)std::tolower(c); });
            return str;
        }

        // Splits a string into a vector. Returns the original string if no separator is found.
        inline List<string> Split(string str, const char separator = '\n', bool trim = false) {
            List<string> items;
            std::stringstream ss(str);
            string item;
            while (std::getline(ss, item, separator))
                items.push_back(trim ? String::Trim(item) : item);

            return items;
        }

        // djb2 hash algorithm by Dan Bernstein.
        // Prefer using std::hash when compile time values aren't necessary.
        constexpr auto Hash(std::string_view s) noexcept {
            uint32 hash = 5381;

            for (auto& c : s)
                hash = ((hash << 5) + hash) + c;

            return hash;
        }
    }

    // Comparator for invariant equality of strings
    struct InvariantEquals {
        bool operator()(const string& a, const string& b) const {
            return String::InvariantEquals(a, b);
        }
    };

    namespace Seq {
        // Converts a std::set to a std::vector
        template<class T>
        constexpr auto ofSet(const Set<T>& set) {
            return List<T>(set.begin(), set.end());
        }

        template<class T>
        constexpr auto toList(const span<T> xs) {
            return List<T>(xs.begin(), xs.end());
        }

        constexpr bool inRange(auto&& xs, size_t index) {
            return index < xs.size();
        }

        template<class T>
        constexpr void insert(Set<T>& dest, auto&& src) {
            dest.insert(src.begin(), src.end());
        }

        // Generates a new list by mapping a function to each element. Causes heap allocation.
        template<class T, class Fn>
        [[nodiscard]] auto map(T&& xs, Fn&& fn) {
            // dereference the first element in a collection to determine the type
            using TElement = std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>;
            List<std::invoke_result_t<Fn, TElement>> r;
            r.reserve(std::size(xs));
            for (auto& x : xs)
                r.push_back(fn(x));
            return r;
        }

        // Generates a new list by mapping a function to each element along with an index. 
        // Lambda parameters are (i, elem). Causes heap allocation.
        template<class T, class Fn>
        [[nodiscard]] auto mapi(T&& xs, Fn&& fn) {
            // dereference the first element in a collection to determine the type
            using TElement = std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>;
            List<std::invoke_result_t<Fn, TElement>> r;
            r.reserve(std::size(xs));

            int i = 0;
            for (auto& x : xs)
                r.push_back(fn(i++, x));

            return r;
        }

        // Executes a function on each element.
        constexpr void iter(auto&& xs, auto&& fn) {
            for (auto& x : xs)
                fn(x);
        }

        // Executes a function on each element with the parameters (i, element).
        constexpr void iteri(auto&& xs, auto&& fn) {
            for (size_t i = 0; i < std::size(xs); i++)
                fn(i, xs[i]);
        }

        // Moves the contents of src to the end of dest
        constexpr void move(auto& dest, auto&& src) {
            std::move(src.begin(), src.end(), std::back_inserter(dest));
        }

        // Copies the contents of src to the end of dest
        constexpr void append(auto& dest, const auto& src) {
            std::copy(src.begin(), src.end(), std::back_inserter(dest));
        }

        // Returns a pointer to an element in the collection. Null if not found.
        auto find(auto& xs, auto&& predicate) {
            auto iter = std::find_if(std::begin(xs), std::end(xs), predicate);
            return iter == std::end(xs) ? nullptr : &(*iter);
        }

        // Returns true if an element is found in the collection.
        constexpr bool contains(auto&& xs, auto&& element) {
            auto iter = std::find(std::begin(xs), std::end(xs), element);
            return iter != std::end(xs);
        }

        // Returns a true if any element satisfies the predicate.
        auto exists(auto& xs, auto&& predicate) {
            return std::find_if(std::begin(xs), std::end(xs), predicate) != std::end(xs);
        }

        // Sorts a range in ascending order by a function (a, b) -> bool
        constexpr void sortBy(auto&& xs, auto&& fn) {
            std::ranges::sort(xs, fn);
        }

        // Sorts a range in ascending order
        constexpr void sort(auto&& xs) {
            std::ranges::sort(xs);
        }

        // Sorts a range in descending order
        constexpr void sortDescending(auto&& xs) {
            std::ranges::sort(xs, ranges::greater());
        }

        // Tries to retrieve an element at index. Returns nullptr if not in range.
        constexpr auto* tryItem(auto&& xs, auto index) {
            return inRange(xs, index) ? &xs[index] : nullptr;
        }

        // Returns the index of an element
        constexpr std::optional<size_t> indexOf(auto&& xs, auto&& element) {
            auto iter = std::find(std::begin(xs), std::end(xs), element);
            if (iter == xs.end()) return {};
            return std::distance(std::begin(xs), iter);
        }

        // Returns the index of an element
        constexpr std::optional<size_t> findIndex(auto&& xs, auto&& predicate) {
            auto iter = std::find_if(std::begin(xs), std::end(xs), predicate);
            if (iter == xs.end()) return {};
            return std::distance(std::begin(xs), iter);
        }

        constexpr bool remove(auto&& xs, auto&& element) {
            auto iter = std::find(std::begin(xs), std::end(xs), element);
            if (iter == xs.end()) return false;
            xs.erase(iter);
            return true;
        }

        // Removes an element at index.
        constexpr bool removeAt(auto&& xs, size_t index) {
            if (!inRange(xs, index)) return false;
            xs.erase(xs.begin() + index);
            return true;
        }

        // Filters a collection. Causes heap allocation.
        template<class T>
        [[nodiscard]] auto filter(const T& xs, auto&& predicate) {
            using TElement = std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>;
            List<TElement> result(std::size(xs));
            auto iter = std::copy_if(std::begin(xs), std::end(xs), result.begin(), predicate);
            result.resize(std::distance(result.begin(), iter));
            return result;
        }

        // Specialization to filter a collection of strings by a value. Causes heap allocation.
        [[nodiscard]] List<string> filter(const auto& xs, string value, bool invariant) {
            if (invariant) {
                value = String::ToLower(value);
                return filter(xs, [&](const string& e) {
                    return String::ToLower(e).find(value) != string::npos;
                });
            }
            else {
                return filter(xs, [&](const string& e) { return e.find(value) != string::npos; });
            }
        }
    }

    // Converts a file name to 8.3 format
    inline string FormatShortFileName(string_view fileName) {
        auto i = (int)fileName.find('.', 0);
        auto name = i == -1 ? fileName.substr(0, 8) : fileName.substr(0, std::min(8, i));
        auto ext = i == -1 ? "" : fileName.substr(i, 4); // extension is optional

        // todo: discard spaces, convert invalid characters to underscores

        return String::TrimEnd(string(name)) + String::TrimEnd(string(ext));
    }

    inline bool ExtensionEquals(std::filesystem::path path, wstring ext) {
        if (!path.has_extension()) return false;
        if (!ext.starts_with('.')) ext.insert(0, L".");

        return String::InvariantEquals(path.extension().wstring(), ext);
    }
}
