#pragma once

// Values of the driver_asvr vrsettings section. Defaults below are the
// last resort; the shipped defaults live in resources/settings/default.vrsettings
// and user overrides (written by the Config tool) in steamvr.vrsettings.
struct DriverSettings
{
    glm::uvec2 resolution = glm::uvec2(1024U, 768U);
    float fps = 60.f;
    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

// Both require an initialized driver context (vrserver or vrcompositor)
DriverSettings LoadDriverSettings();
// Only the pose is driver-written state; everything else is owned by the
// Config tool so it never gets frozen into steamvr.vrsettings as an override
bool SaveDriverPose(const glm::vec3 &f_pos, const glm::quat &f_rot);
