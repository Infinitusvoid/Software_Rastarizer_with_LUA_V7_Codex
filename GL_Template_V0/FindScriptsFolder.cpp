#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <windows.h>


#ifdef _WIN32
#include <conio.h>
#endif



namespace fs = std::filesystem;

// --- get full path to the running executable (works regardless of CWD) ---
static fs::path GetExecutablePath()
{
    std::wstring buf;
    buf.resize(512);

    while (true)
    {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (len == 0)
            throw std::runtime_error("GetModuleFileNameW failed.");

        if (len < buf.size() - 1)
        {
            buf.resize(len);
            return fs::path(buf);
        }

        // buffer was too small, grow and retry
        buf.resize(buf.size() * 2);
    }
}

static fs::path GetExecutableDir()
{
    return GetExecutablePath().parent_path();
}

// Walk upward a few levels and look for a folder named `folderName`.
// Useful when VS runs from project dir or you have bin/Debug layouts.
static std::optional<fs::path> FindUpwardsForFolder(fs::path start, const fs::path& folderName, int maxDepth = 8)
{
    start = fs::weakly_canonical(start);

    for (int i = 0; i <= maxDepth; ++i)
    {
        fs::path candidate = start / folderName;
        if (fs::exists(candidate) && fs::is_directory(candidate))
            return fs::weakly_canonical(candidate);

        if (!start.has_parent_path())
            break;

        auto parent = start.parent_path();
        if (parent == start)
            break;

        start = parent;
    }
    return std::nullopt;
}

// Main resolver.
// - Prefer env var override (handy for dev / testing)
// - Then try scripts next to exe
// - Then try a couple common dev layouts
// - Then try walking upwards from CWD
fs::path find_scripts_folder(
    const fs::path& scriptsFolderName = "scripts",
    const char* envVarOverride = "APP_SCRIPTS_DIR")
{
    std::vector<fs::path> candidates;

    // 1) Optional override via environment variable
    if (envVarOverride && *envVarOverride)
    {
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, envVarOverride) == 0 && v && *v)
        {
            candidates.emplace_back(fs::path(v));
        }
        if (v) free(v);
    }

    const fs::path exeDir = GetExecutableDir();

    // 2) Typical shipping layout: <exeDir>/scripts
    candidates.emplace_back(exeDir / scriptsFolderName);

    // 3) Common Visual Studio layout: <exeDir>/../scripts  (if exe is in bin/Debug)
    candidates.emplace_back(exeDir.parent_path() / scriptsFolderName);

    // 4) Also try current working directory (sometimes VS sets it to project dir)
    candidates.emplace_back(fs::current_path() / scriptsFolderName);

    // 5) Walk up from current directory to find a scripts folder
    if (auto up = FindUpwardsForFolder(fs::current_path(), scriptsFolderName, 10))
        candidates.emplace_back(*up);

    // Pick first existing directory
    for (const auto& c : candidates)
    {
        std::error_code ec;
        if (fs::exists(c, ec) && fs::is_directory(c, ec))
            return fs::weakly_canonical(c, ec);
    }

    // If nothing found, produce a helpful error
    std::ostringstream oss;
    oss << "Could not locate scripts directory.\nTried:\n";
    for (const auto& c : candidates) oss << "  - " << c.string() << "\n";
    oss << "Tip: put a 'scripts' folder next to the exe, or set environment variable "
        << (envVarOverride ? envVarOverride : "(null)") << ".";
    throw std::runtime_error(oss.str());
}