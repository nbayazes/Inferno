#pragma once

// have to use header only for wchar support
#define FMT_HEADER_ONLY
#include <fmt/core.h>
#define SPDLOG_FMT_EXTERNAL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT TRUE
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>