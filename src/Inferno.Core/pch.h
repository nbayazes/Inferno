#pragma once

#include <set>
#include <queue>
#include <optional>
#include <unordered_map>
#include <cassert>
#include <array>
#include <filesystem>

// Copied from <windows.h> to make SimpleMath happy
using UINT = unsigned int;

typedef struct tagRECT {
    long left;
    long top;
    long right;
    long bottom;
} RECT, * PRECT, * NPRECT, * LPRECT;

#define NOMINMAX
#include <DirectXTK12/SimpleMath.h>

#define FMT_HEADER_ONLY
#include <fmt/core.h>
