#pragma once

// Virtual tracker roles, stored as a comma-separated list of names in the
// "trackers" key of the driver_asvr section of steamvr.vrsettings ("none" or
// "" for no trackers; the shipped default is "none"). The Config tool edits
// the list with one checkbox per role; the driver creates one tracker per
// listed role at SteamVR start.

enum ETrackerRole : int
{
    TR_Waist = 0,
    TR_Chest,
    TR_LeftFoot,
    TR_RightFoot,
    TR_LeftKnee,
    TR_RightKnee,
    TR_LeftElbow,
    TR_RightElbow,
    TR_LeftShoulder,
    TR_RightShoulder,
    TR_Handheld,

    TR_Count
};

struct TrackerRoleDef
{
    const char *m_name; // settings / asvr_ctl name
    const char *m_steamvrRole; // TrackerRole_* value SteamVR reads from its "trackers" section
    const char *m_label; // Config tool
};

constexpr TrackerRoleDef g_trackerRoleDefs[TR_Count]
{
    { "waist", "TrackerRole_Waist", "Waist" },
    { "chest", "TrackerRole_Chest", "Chest" },
    { "left_foot", "TrackerRole_LeftFoot", "Left foot" },
    { "right_foot", "TrackerRole_RightFoot", "Right foot" },
    { "left_knee", "TrackerRole_LeftKnee", "Left knee" },
    { "right_knee", "TrackerRole_RightKnee", "Right knee" },
    { "left_elbow", "TrackerRole_LeftElbow", "Left elbow" },
    { "right_elbow", "TrackerRole_RightElbow", "Right elbow" },
    { "left_shoulder", "TrackerRole_LeftShoulder", "Left shoulder" },
    { "right_shoulder", "TrackerRole_RightShoulder", "Right shoulder" },
    { "handheld", "TrackerRole_Handed", "Handheld" },
};
