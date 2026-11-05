#pragma once

#include <cmath>

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

// Default head-local controller pose, shared by the driver (initial offsets)
// and both monitors (startup and the reset key) so they always agree.
//
// SteamVR draws the laser from the render model's "tip" component, not from
// the controller pose itself. For oculus_quest_plus_controller_left/right
// (SteamVR resources/rendermodels/<name>/<name>.json) the tip sits a few cm
// from the pose origin and is pitched 37.4 degrees down. The default
// rotation compensates for that so both lasers converge on the centre of the
// view: the left eye's axis, since that is what the monitor window and
// asvr_ctl screenshots show, at the SteamVR dashboard's distance (measured
// 1.0-1.15 m depending on the page it opens on).

const float g_ctrlAimDistance = 1.1f; // meters ahead of the head where both lasers meet
const float g_ctrlAimEyeX = -0.0315f; // left eye, half of CVirtualHmd's 63 mm IPD
const float g_ctrlTipPitch = -37.4f; // degrees, tip component rotate_xyz.x

// [0] = right, [1] = left; the left model is the right one mirrored in X
const glm::vec3 g_ctrlDefaultPosition[2] = { glm::vec3(0.15f, -0.15f, -0.35f), glm::vec3(-0.15f, -0.15f, -0.35f) };
const glm::vec3 g_ctrlTipOrigin[2] = { glm::vec3(-0.016694f, -0.02522f, 0.024687f), glm::vec3(0.016694f, -0.02522f, 0.024687f) };

// Controller rotation from asvr_ctl's aim angles: degrees, positive yaw turns
// right, positive pitch up. Applied to the controller pose, so (0, 0) leaves
// the laser pointing 37.4 degrees below the controller's horizon
inline glm::quat ControllerAimRotation(float f_yaw, float f_pitch)
{
    return glm::angleAxis(glm::radians(-f_yaw), glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(glm::radians(f_pitch), glm::vec3(1.f, 0.f, 0.f));
}

// Aim angles that put the laser on the view centre at g_ctrlAimDistance.
// The tip origin rotates with the controller, so refine a few times; it
// converges to well under a millimeter by the third pass
inline void GetDefaultControllerAim(bool f_left, float &f_yaw, float &f_pitch)
{
    const size_t l_hand = (f_left ? 1U : 0U);
    const glm::vec3 l_target(g_ctrlAimEyeX, 0.f, -g_ctrlAimDistance);
    f_yaw = 0.f;
    f_pitch = 0.f;
    for(int i = 0; i < 4; i++)
    {
        const glm::vec3 l_tip = g_ctrlDefaultPosition[l_hand] + ControllerAimRotation(f_yaw, f_pitch) * g_ctrlTipOrigin[l_hand];
        const glm::vec3 l_dir = glm::normalize(l_target - l_tip);
        f_yaw = glm::degrees(std::atan2(l_dir.x, -l_dir.z));
        f_pitch = glm::degrees(std::asin(l_dir.y)) - g_ctrlTipPitch;
    }
}

inline glm::quat GetDefaultControllerRotation(bool f_left)
{
    float l_yaw;
    float l_pitch;
    GetDefaultControllerAim(f_left, l_yaw, l_pitch);
    return ControllerAimRotation(l_yaw, l_pitch);
}
