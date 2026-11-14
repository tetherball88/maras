#pragma once

#include <cstdint>

namespace MARAS::Bridge::SkyrimNet {

    // Registers all 23 M.A.R.A.S prompt decorators (ttm_*) with SkyrimNet.dll via PublicRegisterDecorator.
    void RegisterSkyrimNetDecorators();

    // Evaluates whether an active paternity secret is confessed or discovered using SkyrimNet Decisions / Chat LLM.
    bool EvaluatePaternitySecret(std::uint32_t childID);

}  // namespace MARAS::Bridge::SkyrimNet
