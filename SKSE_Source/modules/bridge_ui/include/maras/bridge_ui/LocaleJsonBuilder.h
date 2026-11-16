#pragma once

#include <nlohmann/json.hpp>

namespace MARAS::Bridge::Ui {

    /// Builds the `window.receiveMARASLocale` JSON payload from `Engine::LocalizationService`.
    [[nodiscard]] nlohmann::json BuildLocalePayloadJson();

}  // namespace MARAS::Bridge::Ui
