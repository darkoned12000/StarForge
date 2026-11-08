#pragma once

// Shared bundled-shader directory resolution for public facades.
// This header must not be installed or included by applications.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

namespace starforge::public_api_detail
{

[[nodiscard]] inline std::filesystem::path executableDirectory()
{
#if defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path.parent_path();
#elif defined(_WIN32)
    std::array<char, 4096> buffer{};
    const auto length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length == 0 ? std::filesystem::path{} : std::filesystem::path{buffer.data()}.parent_path();
#else
    return {};
#endif
}

// Resolves the bundled shader directory: an explicit override wins, then
// the STARFORGE_SHADER_DIR environment variable, then directories
// relative to the running executable, then the compile-time build
// directory (dev fallback), then a plain "shaders" directory. Returns
// the first candidate containing `markerFile`, or the compile-time
// default so load failures report a concrete path.
[[nodiscard]] inline std::string resolveShaderDirectory(const std::string& requested, const std::string& markerFile)
{
    if (!requested.empty())
    {
        return requested;
    }

    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("STARFORGE_SHADER_DIR"); env != nullptr && env[0] != '\0')
    {
        candidates.emplace_back(env);
    }
    if (const auto exeDir = executableDirectory(); !exeDir.empty())
    {
        candidates.push_back(exeDir / "shaders");
        candidates.push_back(exeDir / ".." / "shaders");
        candidates.push_back(exeDir / ".." / "share" / "starforge" / "shaders");
    }
    candidates.emplace_back(STARFORGE_SHADER_DIR);
    candidates.emplace_back("shaders");

    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::exists(candidate / markerFile, error))
        {
            return candidate.string();
        }
    }
    return STARFORGE_SHADER_DIR;
}

} // namespace starforge::public_api_detail
