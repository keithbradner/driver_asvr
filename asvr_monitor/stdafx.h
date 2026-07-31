#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#ifdef _DEBUG
#include <iostream>
#endif
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

#include "openvr.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
