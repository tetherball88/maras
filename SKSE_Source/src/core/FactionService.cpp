#include "core/FactionService.h"

#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    FactionService& FactionService::GetSingleton() {
        static FactionService instance;
        return instance;
    }

    void FactionService::RegisterFactionAddedListener(FactionAddedCallback callback) {
        std::unique_lock lock(m_mutex);
        m_addedListeners.push_back(std::move(callback));
    }

    void FactionService::RegisterFactionRemovedListener(FactionRemovedCallback callback) {
        std::unique_lock lock(m_mutex);
        m_removedListeners.push_back(std::move(callback));
    }

    void FactionService::NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        std::vector<FactionAddedCallback> listeners;
        {
            std::shared_lock lock(m_mutex);
            listeners = m_addedListeners;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction, a_rank);
            }
        }
    }

    void FactionService::NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        std::vector<FactionRemovedCallback> listeners;
        {
            std::shared_lock lock(m_mutex);
            listeners = m_removedListeners;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction);
            }
        }
    }

    namespace {
        thread_local int s_internalFactionDepth = 0;
    }

    FactionService::InternalFactionScope::InternalFactionScope() {
        ++s_internalFactionDepth;
    }

    FactionService::InternalFactionScope::~InternalFactionScope() {
        if (s_internalFactionDepth > 0) {
            --s_internalFactionDepth;
        }
    }

    bool FactionService::IsInternalFactionUpdate() {
        return s_internalFactionDepth > 0;
    }

    void FactionService::SanitizePlayerMarriedFaction() {
        if (!Settings::GetSingleton().GetEnablePolygamy()) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        auto* marriedFaction = FormCache::GetSingleton().GetMarriedFaction();
        if (!marriedFaction) {
            return;
        }

        if (player->IsInFaction(marriedFaction)) {
            InternalFactionScope scope;
            Utils::RemoveFromFaction(player, marriedFaction);
            MARAS_LOG_INFO("FactionService::SanitizePlayerMarriedFaction: Removed PlayerMarriedFaction from player (polygamy active)");
        }
    }

    // ─── Engine Hooks ────────────────────────────────────────────────────────────

    namespace {

        using AddToFaction_t = void (*)(RE::Actor*, RE::TESFaction*, std::int8_t);
        using RemoveFromFaction_t = void (*)(RE::Actor*, RE::TESFaction*);

        AddToFaction_t _originalAddToFaction = nullptr;
        RemoveFromFaction_t _originalRemoveFromFaction = nullptr;

        constexpr RE::FormID kPlayerMarriedFactionID = 0x000C6472;

        void WriteAbsoluteJump(std::uint8_t* dst, std::uintptr_t target) {
            dst[0] = 0xFF;
            dst[1] = 0x25;
            *reinterpret_cast<std::int32_t*>(&dst[2]) = 0;
            *reinterpret_cast<std::uint64_t*>(&dst[6]) = target;
        }

        // Decodes standard MSVC x64 prologue instructions to find the instruction boundary >= 5 bytes.
        // Returns 0 if an unrecognized or RIP-relative instruction is encountered so we never slice an instruction.
        std::size_t DecodePrologueSize(const std::uint8_t* code) {
            std::size_t offset = 0;
            while (offset < 5) {
                const std::uint8_t b = code[offset];
                if (b == 0x90) {
                    // nop -> 1 byte
                    offset += 1;
                } else if (b >= 0x50 && b <= 0x57) {
                    // push reg -> 1 byte
                    offset += 1;
                } else if ((b >= 0x40 && b <= 0x4F) && (code[offset + 1] >= 0x50 && code[offset + 1] <= 0x57)) {
                    // REX prefix + push reg -> 2 bytes
                    offset += 2;
                } else if ((b >= 0x40 && b <= 0x4F) &&
                           (code[offset + 1] == 0x89 || code[offset + 1] == 0x8B || code[offset + 1] == 0x8D)) {
                    // REX + MOV/LEA + ModR/M (+ optional SIB + disp8/disp32)
                    const std::uint8_t modrm = code[offset + 2];
                    const std::uint8_t mod = (modrm >> 6) & 0x03;
                    const std::uint8_t rm = modrm & 0x07;

                    if (mod == 0 && rm == 5) {
                        // RIP-relative addressing [rip + disp32] cannot be byte-copied without relocation
                        return 0;
                    }

                    std::size_t instrLen = 3;
                    if (mod != 3 && rm == 4) {
                        // SIB byte present
                        const std::uint8_t sib = code[offset + 3];
                        instrLen += 1;
                        if (mod == 0 && (sib & 0x07) == 5) {
                            instrLen += 4;  // disp32 with no base
                        }
                    }
                    if (mod == 1) {
                        instrLen += 1;  // disp8
                    } else if (mod == 2) {
                        instrLen += 4;  // disp32
                    }
                    offset += instrLen;
                } else if (b == 0x48 && code[offset + 1] == 0x83 && code[offset + 2] == 0xEC) {
                    // sub rsp, imm8 -> 4 bytes
                    offset += 4;
                } else if (b == 0x48 && code[offset + 1] == 0x81 && code[offset + 2] == 0xEC) {
                    // sub rsp, imm32 -> 7 bytes
                    offset += 7;
                } else {
                    return 0;
                }
            }
            return offset;
        }

        template <typename FnType>
        bool InstallDetourHook(SKSE::Trampoline& trampoline, std::uintptr_t targetAddr, std::uintptr_t hookAddr,
                               FnType& outOriginal, std::string_view hookName) {
            if (targetAddr == 0) {
                MARAS_LOG_ERROR("FactionService: Failed to resolve {} address", hookName);
                return false;
            }

            const auto* code = reinterpret_cast<const std::uint8_t*>(targetAddr);

            // Handle case where another SKSE plugin already placed a 5-byte relative JMP (0xE9) at function entry
            if (code[0] == 0xE9) {
                const auto relOffset = *reinterpret_cast<const std::int32_t*>(code + 1);
                const auto chainedTarget =
                    static_cast<std::uintptr_t>(static_cast<std::intptr_t>(targetAddr + 5) + relOffset);
                auto* bridge = reinterpret_cast<std::uint8_t*>(trampoline.allocate(14));
                WriteAbsoluteJump(bridge, chainedTarget);
                outOriginal = reinterpret_cast<FnType>(bridge);
                trampoline.write_branch<5>(targetAddr, hookAddr);
                MARAS_LOG_INFO("FactionService: {} chained existing E9 hook at {:016X} -> {:016X}",
                               hookName, targetAddr, chainedTarget);
                return true;
            }

            const std::size_t prologueSize = DecodePrologueSize(code);
            if (prologueSize < 5) {
                MARAS_LOG_ERROR(
                    "FactionService: Unrecognized prologue bytes at {:016X} for {} ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); skipping hook to avoid instruction slicing",
                    targetAddr, hookName, code[0], code[1], code[2], code[3], code[4], code[5]);
                return false;
            }

            auto* bridge = reinterpret_cast<std::uint8_t*>(trampoline.allocate(prologueSize + 14));
            std::memcpy(bridge, code, prologueSize);
            WriteAbsoluteJump(bridge + prologueSize, targetAddr + prologueSize);

            outOriginal = reinterpret_cast<FnType>(bridge);

            trampoline.write_branch<5>(targetAddr, hookAddr);
            if (prologueSize > 5) {
                std::vector<std::uint8_t> nops(prologueSize - 5, 0x90);
                REL::safe_write(targetAddr + 5, nops.data(), nops.size());
            }

            MARAS_LOG_INFO("FactionService: {} detour installed at {:016X} (prologue size: {})",
                           hookName, targetAddr, prologueSize);
            return true;
        }

        void Hook_AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
            if (!a_actor || !a_faction) {
                if (_originalAddToFaction) {
                    _originalAddToFaction(a_actor, a_faction, a_rank);
                }
                return;
            }

            // If this is an internal MARAS update, bypass external auto-promote/auto-demote hooks,
            // apply engine state first, and then notify listeners.
            if (FactionService::IsInternalFactionUpdate()) {
                if (_originalAddToFaction) {
                    _originalAddToFaction(a_actor, a_faction, a_rank);
                }
                if (a_rank < 0) {
                    FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                } else {
                    FactionService::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
                }
                return;
            }

            const bool isPlayer = a_actor->IsPlayerRef();
            const bool isMarriedFaction = (a_faction->GetFormID() == kPlayerMarriedFactionID);

            if (isPlayer && isMarriedFaction && Settings::GetSingleton().GetEnablePolygamy()) {
                if (a_rank >= 0) {
                    MARAS_LOG_INFO("FactionService: Blocked PlayerMarriedFaction (rank {}) from player (polygamy active)",
                                   a_rank);
                    return;
                } else {
                    MARAS_LOG_INFO("FactionService: Allowing removal rank ({}) for PlayerMarriedFaction on player", a_rank);
                }
            }

            // If an NPC is receiving PlayerMarriedFaction
            if (!isPlayer && isMarriedFaction) {
                auto& relMgr = NPCRelationshipManager::GetSingleton();
                const auto currentStatus = relMgr.GetRelationshipStatus(a_actor->GetFormID(), kPlayerFormID);

                if (a_rank >= 0) {
                    if (currentStatus != RelationshipStatus::Married) {
                        std::string blockReason;
                        if (!relMgr.CanFormRelationship(a_actor->GetFormID(), kPlayerFormID, RelationshipStatus::Married, &blockReason)) {
                            MARAS_LOG_WARN("FactionService: Blocked auto-promoting NPC {:08X} ({}) to Married with Player via PlayerMarriedFaction addition: {}",
                                           a_actor->GetFormID(), a_actor->GetName(), blockReason);
                            if (_originalAddToFaction) {
                                _originalAddToFaction(a_actor, a_faction, a_rank);
                            }
                            return;
                        }

                        MARAS_LOG_INFO("FactionService: Auto-promoting NPC {:08X} ({}) to Married via PlayerMarriedFaction addition",
                                       a_actor->GetFormID(), a_actor->GetName());
                        if (_originalAddToFaction) {
                            _originalAddToFaction(a_actor, a_faction, a_rank);
                        }
                        relMgr.PromoteToMarried(a_actor->GetFormID(), kPlayerFormID);
                        FactionService::GetSingleton().SanitizePlayerMarriedFaction();
                        FactionService::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
                        return;
                    }
                } else {
                    // a_rank < 0 indicates clearing or removal
                    if (currentStatus == RelationshipStatus::Married) {
                        MARAS_LOG_INFO("FactionService: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction rank reset",
                                       a_actor->GetFormID(), a_actor->GetName());
                        if (_originalAddToFaction) {
                            _originalAddToFaction(a_actor, a_faction, a_rank);
                        }
                        relMgr.PromoteToDivorced(a_actor->GetFormID(), kPlayerFormID);
                        FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                        return;
                    }
                }
            }

            if (_originalAddToFaction) {
                _originalAddToFaction(a_actor, a_faction, a_rank);
            }

            if (a_rank < 0) {
                FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
            } else {
                FactionService::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
            }
        }

        void Hook_RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
            if (!a_actor || !a_faction) {
                if (_originalRemoveFromFaction) {
                    _originalRemoveFromFaction(a_actor, a_faction);
                }
                return;
            }

            // If this is an internal MARAS update, bypass external auto-demote hooks,
            // apply engine state first, and then notify listeners.
            if (FactionService::IsInternalFactionUpdate()) {
                if (_originalRemoveFromFaction) {
                    _originalRemoveFromFaction(a_actor, a_faction);
                }
                FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                return;
            }

            const bool isPlayer = a_actor->IsPlayerRef();
            const bool isMarriedFaction = (a_faction->GetFormID() == kPlayerMarriedFactionID);

            if (!isPlayer && isMarriedFaction) {
                auto& relMgr = NPCRelationshipManager::GetSingleton();
                if (relMgr.GetRelationshipStatus(a_actor->GetFormID(), kPlayerFormID) == RelationshipStatus::Married) {
                    MARAS_LOG_INFO("FactionService: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction removal",
                                   a_actor->GetFormID(), a_actor->GetName());
                    if (_originalRemoveFromFaction) {
                        _originalRemoveFromFaction(a_actor, a_faction);
                    }
                    relMgr.PromoteToDivorced(a_actor->GetFormID(), kPlayerFormID);
                    FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                    return;
                }
            }

            if (_originalRemoveFromFaction) {
                _originalRemoveFromFaction(a_actor, a_faction);
            }
            FactionService::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
        }

    }  // namespace

    void FactionService::InstallHooks() {
        MARAS_LOG_INFO("FactionService: Installing faction hooks...");

        auto& trampoline = SKSE::GetTrampoline();
        if (trampoline.empty() || trampoline.free_size() < 128) {
            SKSE::AllocTrampoline(256);
        }

        // 1. Hook Actor::AddToFaction (RELOCATION_ID 36678, 37686)
        {
            REL::Relocation<std::uintptr_t> addTarget{ RELOCATION_ID(36678, 37686) };
            InstallDetourHook(trampoline, addTarget.address(), reinterpret_cast<std::uintptr_t>(Hook_AddToFaction),
                              _originalAddToFaction, "Hook_AddToFaction");
        }

        // 2. Hook Actor::RemoveFromFaction (RELOCATION_ID 36680, 37688)
        {
            REL::Relocation<std::uintptr_t> removeTarget{ RELOCATION_ID(36680, 37688) };
            InstallDetourHook(trampoline, removeTarget.address(), reinterpret_cast<std::uintptr_t>(Hook_RemoveFromFaction),
                              _originalRemoveFromFaction, "Hook_RemoveFromFaction");
        }
    }

}  // namespace MARAS
