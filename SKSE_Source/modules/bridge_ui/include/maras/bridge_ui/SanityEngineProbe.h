#pragma once

#include "maras/projection/PerspectiveView.h"

#include <nlohmann/json.hpp>

namespace MARAS::Bridge::Ui {

    /// Builds a strictly read-only live Skyrim engine state probe for in-game sanity verification.
    [[nodiscard]] nlohmann::json BuildSanityEngineProbeJson(const MARAS::Projection::PerspectiveView& view);

}  // namespace MARAS::Bridge::Ui
