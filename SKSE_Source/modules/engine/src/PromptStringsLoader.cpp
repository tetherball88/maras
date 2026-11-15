#include "maras/engine/PromptStringsLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"

#include <nlohmann/json.hpp>

#include <array>
#include <mutex>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 2> kDefaultPromptPaths = {
            "Data/SKSE/Plugins/MARAS/promptStrings.json",
            "SKSE/Plugins/MARAS/promptStrings.json",
        };

    }  // namespace

    PromptStringsLoader& PromptStringsLoader::GetSingleton() {
        static PromptStringsLoader instance;
        return instance;
    }

    bool PromptStringsLoader::Initialize() {
        std::error_code ec;
        for (const char* candidate : kDefaultPromptPaths) {
            const std::filesystem::path path(candidate);
            if (std::filesystem::exists(path, ec)) {
                return LoadFromFile(path);
            }
        }

        Clear();
        MARAS_LOG_INFO("[PromptStringsLoader] promptStrings.json not found in default search paths; continuing with empty map");
        return false;
    }

    bool PromptStringsLoader::LoadFromFile(const std::filesystem::path& filePath) {
        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(filePath, root, "PromptStringsLoader")) {
            return false;
        }
        if (!root.is_object()) {
            MARAS_LOG_ERROR("[PromptStringsLoader] Root is not a JSON object in '{}'", filePath.string());
            return false;
        }

        std::unordered_map<std::string, std::string> parsed;
        parsed.reserve(root.size());
        for (const auto& [key, value] : root.items()) {
            if (!value.is_string()) {
                MARAS_LOG_WARN("[PromptStringsLoader] Prompt entry '{}' is not a string in '{}'",
                               key, filePath.string());
                continue;
            }
            parsed.emplace(key, value.get<std::string>());
        }

        const std::size_t count = parsed.size();
        {
            std::unique_lock lock(mutex_);
            prompts_ = std::move(parsed);
        }
        loaded_.store(true, std::memory_order_release);

        MARAS_LOG_INFO("[PromptStringsLoader] Loaded {} prompt entries from '{}'", count, filePath.string());
        return true;
    }

    void PromptStringsLoader::Clear() noexcept {
        {
            std::unique_lock lock(mutex_);
            prompts_.clear();
        }
        loaded_.store(false, std::memory_order_release);
    }

    std::string PromptStringsLoader::GetPrompt(std::string_view promptKey) const {
        if (promptKey.empty()) {
            return {};
        }
        std::shared_lock lock(mutex_);
        if (const auto it = prompts_.find(std::string(promptKey)); it != prompts_.end()) {
            return it->second;
        }
        return {};
    }

}  // namespace MARAS::Engine
