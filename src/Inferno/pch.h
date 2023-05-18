#pragma once
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#define _CRT_SECURE_NO_WARNINGS

// Standard libraries
#include <assert.h>
#include <string>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <iostream>
#include <exception>
#include <filesystem>
#include <cstdint>
#include <optional>
#include <set>
#include <future>
#include <span>
#include <queue>
#include <ranges>

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI. But DWM does.
#define NODRAWTEXT
//#define NOGDI
#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

// Windows and DirectX
#include <WinSDKVer.h>
#include <SDKDDKVer.h>
#include "DirectX.h"

#include "Types.h"
#include "Logging.h"
#include "Convert.h"

#pragma warning(pop)
