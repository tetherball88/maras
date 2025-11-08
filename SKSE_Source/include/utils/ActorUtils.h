#pragma once

#include <REL/Relocation.h>

#include "utils/Common.h"

namespace MARAS::Utils {
    // Try to call the engine's RemoveFromFaction via a relocation. If the relocation
    // isn't present for the current address library/version, fall back to setting
    // the faction rank to -1 using AddToFaction (safe behavior used elsewhere).
    inline bool RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        if (!a_actor || !a_faction) {
            return false;
        }

        using func_t = void (*)(RE::Actor*, RE::TESFaction*);
        REL::Relocation<func_t> func{RELOCATION_ID(36680, 37688)};

        if (func.address() != 0) {
            // relocation resolved — call the real engine function
            func(a_actor, a_faction);
            return true;
        }

        // Fallback: clear the actor's rank in the faction by assigning -1.
        a_actor->AddToFaction(a_faction, static_cast<std::int8_t>(-1));
        return false;
    }

    // Add actor to faction helper. Provide overloads for both actor pointer and FormID lookup.
    inline bool AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_actor || !a_faction) {
            return false;
        }

        // Direct engine call to set faction rank for actor
        a_actor->AddToFaction(a_faction, a_rank);
        return true;
    }

    inline bool AddToFaction(RE::FormID a_formID, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_faction) {
            return false;
        }

        auto actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor) {
            return false;
        }

        return AddToFaction(actor, a_faction, a_rank);
    }
}
