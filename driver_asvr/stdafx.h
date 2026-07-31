#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>

#include "openvr_driver.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
