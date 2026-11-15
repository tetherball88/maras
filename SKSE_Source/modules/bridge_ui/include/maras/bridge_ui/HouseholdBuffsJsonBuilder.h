#pragma once

#include <nlohmann/json.hpp>

namespace MARAS::Bridge::Ui {

    /// Builds the read-only `HouseholdBuffs` snapshot JSON payload for the UI dashboard.
    [[nodiscard]] nlohmann::json BuildHouseholdBuffsJson();

}  // namespace MARAS::Bridge::Ui
