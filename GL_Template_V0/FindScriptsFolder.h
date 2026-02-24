#pragma once

#include <filesystem>

std::filesystem::path find_scripts_folder(const std::filesystem::path& scriptsFolderName = "scripts", const char* envVarOverride = "APP_SCRIPTS_DIR");