#pragma once

// Windows headers
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <ole2.h>
#include <winhttp.h>

// foobar2000 SDK
#include <SDK/foobar2000.h>
#include <helpers/foobar2000+atl.h>

// Standard library
#include <vector>
#include <memory>
#include <functional>
#include <thread>
