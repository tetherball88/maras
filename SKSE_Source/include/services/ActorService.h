#pragma once

#include "utils/Common.h"

namespace MARAS::ActorSvc {

    // Object Extraction
    RE::TESNPC* GetActorBase(RE::FormID id);
    RE::TESObjectREFR* GetReference(RE::FormID id);

    // Attributes & Metadata
    std::string GetName(RE::FormID id);
    std::string GetRaceName(RE::FormID id);
    std::string GetRaceEditorID(RE::FormID id);
    RE::TESRace* GetRace(RE::FormID id);
    std::string GetPluginName(RE::FormID id);
    uint8_t GetSex(RE::FormID id);
    bool IsValid(RE::FormID id);

}  // namespace MARAS::ActorSvc
