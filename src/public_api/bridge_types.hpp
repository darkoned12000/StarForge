#pragma once

// Shared plain-data bridge types. One definition avoids redefinition errors
// when a translation unit includes more than one *_bridge.hpp.
namespace starforge::public_api_bridge
{

struct RawVec2f
{
    float x = 0.0f;
    float y = 0.0f;
};

struct RawVec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RawVec4f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct RawVec3d
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct RawMat3d
{
    double columns[3][3] = {};
};

struct RawMat4f
{
    float columns[4][4] = {};
};

} // namespace starforge::public_api_bridge
