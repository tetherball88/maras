#include "core/FactionReconciler.h"

#include "core/AdultChildProgression.h"
#include "core/DeferredActorResolver.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

#include <array>
#include <cstring>
#include <unordered_set>

namespace MARAS {

    FactionReconciler& FactionReconciler::GetSingleton() {
        static FactionReconciler instance;
        return instance;
    }

    void FactionReconciler::RegisterFactionAddedListener(FactionAddedCallback callback) {
        std::unique_lock lock(mutex_);
        addedListeners_.push_back(std::move(callback));
    }

    void FactionReconciler::RegisterFactionRemovedListener(FactionRemovedCallback callback) {
        std::unique_lock lock(mutex_);
        removedListeners_.push_back(std::move(callback));
    }

    void FactionReconciler::NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        std::vector<FactionAddedCallback> listeners;
        {
            std::shared_lock lock(mutex_);
            listeners = addedListeners_;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction, a_rank);
            }
        }
    }

    void FactionReconciler::NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        std::vector<FactionRemovedCallback> listeners;
        {
            std::shared_lock lock(mutex_);
            listeners = removedListeners_;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction);
            }
        }
    }

    namespace {
        thread_local int s_internalFactionDepth = 0;

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
                    offset += 1;
                } else if (b >= 0x50 && b <= 0x57) {
                    offset += 1;
                } else if ((b >= 0x40 && b <= 0x4F) && (code[offset + 1] >= 0x50 && code[offset + 1] <= 0x57)) {
                    offset += 2;
                } else if ((b >= 0x40 && b <= 0x4F) &&
                           (code[offset + 1] == 0x89 || code[offset + 1] == 0x8B || code[offset + 1] == 0x8D)) {
                    const std::uint8_t modrm = code[offset + 2];
                    const std::uint8_t mod = (modrm >> 6) & 0x03;
                    const std::uint8_t rm = modrm & 0x07;

                    if (mod == 0 && rm == 5) {
                        return 0;
                    }

                    std::size_t instrLen = 3;
                    if (mod != 3 && rm == 4) {
                        const std::uint8_t sib = code[offset + 3];
                        instrLen += 1;
                        if (mod == 0 && (sib & 0x07) == 5) {
                            instrLen += 4;
                        }
                    }
                    if (mod == 1) {
                        instrLen += 1;
                    } else if (mod == 2) {
                        instrLen += 4;
                    }
                    offset += instrLen;
                } else if (b == 0x48 && code[offset + 1] == 0x83 && code[offset + 2] == 0xEC) {
                    offset += 4;
                } else if (b == 0x48 && code[offset + 1] == 0x81 && code[offset + 2] == 0xEC) {
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
                MARAS_LOG_ERROR("FactionReconciler: Failed to resolve {} address", hookName);
                return false;
            }

            const auto* code = reinterpret_cast<const std::uint8_t*>(targetAddr);

            if (code[0] == 0xE9) {
                const auto relOffset = *reinterpret_cast<const std::int32_t*>(code + 1);
                const auto chainedTarget =
                    static_cast<std::uintptr_t>(static_cast<std::intptr_t>(targetAddr + 5) + relOffset);
                auto* bridge = reinterpret_cast<std::uint8_t*>(trampoline.allocate(14));
                WriteAbsoluteJump(bridge, chainedTarget);
                outOriginal = reinterpret_cast<FnType>(bridge);
                trampoline.write_branch<5>(targetAddr, hookAddr);
                MARAS_LOG_INFO("FactionReconciler: {} chained existing E9 hook at {:016X} -> {:016X}",
                               hookName, targetAddr, chainedTarget);
                return true;
            }

            const std::size_t prologueSize = DecodePrologueSize(code);
            if (prologueSize < 5) {
                MARAS_LOG_ERROR(
                    "FactionReconciler: Unrecognized prologue bytes at {:016X} for {} ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); skipping hook to avoid instruction slicing",
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

            MARAS_LOG_INFO("FactionReconciler: {} detour installed at {:016X} (prologue size: {})",
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

            // Internal MARAS writes skip external auto-promote/demote hooks and notify listeners directly.
            if (FactionReconciler::IsInternalFactionUpdate()) {
                if (_originalAddToFaction) {
                    _originalAddToFaction(a_actor, a_faction, a_rank);
                }
                if (a_rank < 0) {
                    FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                } else {
                    FactionReconciler::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
                }
                return;
            }

            const bool isPlayer = a_actor->IsPlayerRef();
            const bool isMarriedFaction = (a_faction->GetFormID() == kPlayerMarriedFactionID);

            if (isPlayer && isMarriedFaction && Settings::GetSingleton().GetEnablePolygamy()) {
                if (a_rank >= 0) {
                    MARAS_LOG_INFO("FactionReconciler: Blocked PlayerMarriedFaction (rank {}) from player (polygamy active)",
                                   a_rank);
                    return;
                }
                MARAS_LOG_INFO("FactionReconciler: Allowing removal rank ({}) for PlayerMarriedFaction on player", a_rank);
            }

            // Validate relationship eligibility BEFORE mutating engine faction state
            if (!isPlayer && isMarriedFaction) {
                std::lock_guard papyrusLock(PapyrusInterface::GetPapyrusMutex());
                auto& relMgr = NPCRelationshipManager::GetSingleton();
                const auto currentStatus = relMgr.GetRelationshipStatus(a_actor->GetFormID(), kPlayerFormID);

                if (a_rank >= 0) {
                    if (currentStatus != RelationshipStatus::Married) {
                        std::string blockReason;
                        if (!relMgr.CanFormRelationship(a_actor->GetFormID(), kPlayerFormID,
                                                        RelationshipStatus::Married, &blockReason)) {
                            MARAS_LOG_WARN(
                                "FactionReconciler: Blocked auto-promoting NPC {:08X} ({}) to Married with Player via PlayerMarriedFaction addition: {}",
                                a_actor->GetFormID(), a_actor->GetName(), blockReason);
                            return;
                        }

                        MARAS_LOG_INFO(
                            "FactionReconciler: Auto-promoting NPC {:08X} ({}) to Married via PlayerMarriedFaction addition",
                            a_actor->GetFormID(), a_actor->GetName());
                        if (_originalAddToFaction) {
                            _originalAddToFaction(a_actor, a_faction, a_rank);
                        }
                        relMgr.PromoteToMarried(a_actor->GetFormID(), kPlayerFormID);
                        FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
                        FactionReconciler::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
                        return;
                    }
                } else if (currentStatus == RelationshipStatus::Married) {
                    MARAS_LOG_INFO(
                        "FactionReconciler: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction rank reset",
                        a_actor->GetFormID(), a_actor->GetName());
                    if (_originalAddToFaction) {
                        _originalAddToFaction(a_actor, a_faction, a_rank);
                    }
                    relMgr.PromoteToDivorced(a_actor->GetFormID(), kPlayerFormID);
                    FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                    return;
                }
            }

            if (_originalAddToFaction) {
                _originalAddToFaction(a_actor, a_faction, a_rank);
            }

            if (a_rank < 0) {
                FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
            } else {
                FactionReconciler::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
            }
        }

        void Hook_RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
            if (!a_actor || !a_faction) {
                if (_originalRemoveFromFaction) {
                    _originalRemoveFromFaction(a_actor, a_faction);
                }
                return;
            }

            if (FactionReconciler::IsInternalFactionUpdate()) {
                if (_originalRemoveFromFaction) {
                    _originalRemoveFromFaction(a_actor, a_faction);
                }
                FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                return;
            }

            const bool isPlayer = a_actor->IsPlayerRef();
            const bool isMarriedFaction = (a_faction->GetFormID() == kPlayerMarriedFactionID);

            if (!isPlayer && isMarriedFaction) {
                std::lock_guard papyrusLock(PapyrusInterface::GetPapyrusMutex());
                auto& relMgr = NPCRelationshipManager::GetSingleton();
                if (relMgr.GetRelationshipStatus(a_actor->GetFormID(), kPlayerFormID) == RelationshipStatus::Married) {
                    MARAS_LOG_INFO(
                        "FactionReconciler: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction removal",
                        a_actor->GetFormID(), a_actor->GetName());
                    if (_originalRemoveFromFaction) {
                        _originalRemoveFromFaction(a_actor, a_faction);
                    }
                    relMgr.PromoteToDivorced(a_actor->GetFormID(), kPlayerFormID);
                    FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
                    return;
                }
            }

            if (_originalRemoveFromFaction) {
                _originalRemoveFromFaction(a_actor, a_faction);
            }
            FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
        }

        [[nodiscard]] State::ActorSex LookupActorSex(Foundation::ActorId actorId) noexcept {
            if (actorId.IsNull() || actorId.IsSyntheticChild() || actorId.IsUnknownFather()) {
                return State::ActorSex::Unknown;
            }
            const std::uint8_t sex = ActorSvc::GetSex(actorId.raw());
            if (sex == 0) {
                return State::ActorSex::Male;
            }
            if (sex == 1) {
                return State::ActorSex::Female;
            }
            return State::ActorSex::Unknown;
        }

        [[nodiscard]] std::string_view ResolveAdultChildClassId(const State::ChildRecord& childRec) noexcept {
            if (AdultChildProgression::IsValidTrainingClass(childRec.trainingClassIndex)) {
                return AdultChildProgression::GetTrainingClassId(childRec.trainingClassIndex);
            }
            if (childRec.trainingLocation.empty()) {
                return {};
            }
            const auto classIdx = AdultChildProgression::FindTrainingClassByLocation(childRec.trainingLocation);
            if (classIdx < 0) {
                return {};
            }
            return AdultChildProgression::GetTrainingClassId(classIdx);
        }

        void ClearKeywordOnBase(RE::Actor* actor, RE::BGSKeyword* keyword) {
            if (!actor || !keyword) {
                return;
            }
            if (auto* base = actor->GetActorBase()) {
                if (base->HasKeyword(keyword)) {
                    base->RemoveKeyword(keyword);
                    MARAS_LOG_DEBUG("[FactionReconciler] Removed keyword {:08X} from actor {} ({:08X})",
                                    keyword->GetFormID(), actor->GetName() ? actor->GetName() : "Unknown",
                                    actor->GetFormID());
                }
            }
        }

    }  // namespace

    FactionReconciler::InternalFactionScope::InternalFactionScope() {
        ++s_internalFactionDepth;
    }

    FactionReconciler::InternalFactionScope::~InternalFactionScope() {
        if (s_internalFactionDepth > 0) {
            --s_internalFactionDepth;
        }
    }

    bool FactionReconciler::IsInternalFactionUpdate() {
        return s_internalFactionDepth > 0;
    }

    void FactionReconciler::InstallHooks() {
        MARAS_LOG_INFO("FactionReconciler: Installing faction hooks...");

        auto& trampoline = SKSE::GetTrampoline();
        if (trampoline.empty() || trampoline.free_size() < 128) {
            SKSE::AllocTrampoline(256);
        }

        {
            REL::Relocation<std::uintptr_t> addTarget{ RELOCATION_ID(36678, 37686) };
            InstallDetourHook(trampoline, addTarget.address(), reinterpret_cast<std::uintptr_t>(Hook_AddToFaction),
                              _originalAddToFaction, "Hook_AddToFaction");
        }

        {
            REL::Relocation<std::uintptr_t> removeTarget{ RELOCATION_ID(36680, 37688) };
            InstallDetourHook(trampoline, removeTarget.address(), reinterpret_cast<std::uintptr_t>(Hook_RemoveFromFaction),
                              _originalRemoveFromFaction, "Hook_RemoveFromFaction");
        }
    }

    bool FactionReconciler::IsNativeBaseFaction(const RE::Actor* actor, const RE::TESFaction* faction) noexcept {
        if (!actor || !faction) {
            return false;
        }
        const auto* base = actor->GetActorBase();
        if (!base) {
            return false;
        }
        for (const auto& entry : base->factions) {
            if (entry.faction == faction && entry.rank >= 0) {
                return true;
            }
        }
        return false;
    }

    bool FactionReconciler::SyncActorFaction(
        RE::Actor* actor,
        RE::TESFaction* faction,
        std::optional<std::int8_t> desiredRank,
        bool preserveIfNativeOnBase) {
        if (!actor || !faction) {
            return false;
        }

        const bool isPlayer = actor->IsPlayerRef();
        const bool inFaction = actor->IsInFaction(faction);

        if (desiredRank.has_value()) {
            const std::int32_t currentRank = inFaction ? actor->GetFactionRank(faction, isPlayer) : -2;
            if (inFaction && currentRank == static_cast<std::int32_t>(*desiredRank)) {
                return false;
            }
            Utils::AddToFaction(actor, faction, *desiredRank);
            MARAS_LOG_TRACE("[FactionReconciler] Set faction {:08X} rank {} on {} ({:08X})",
                            faction->GetFormID(), *desiredRank,
                            actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
            return true;
        }

        if (!inFaction) {
            return false;
        }

        if (preserveIfNativeOnBase && IsNativeBaseFaction(actor, faction)) {
            return false;
        }

        Utils::RemoveFromFaction(actor, faction);
        MARAS_LOG_TRACE("[FactionReconciler] Removed faction {:08X} from {} ({:08X})",
                        faction->GetFormID(),
                        actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
        return true;
    }

    bool FactionReconciler::ApplyMembershipOp(
        RE::Actor* actor,
        RE::TESFaction* faction,
        State::FactionMembershipOp op,
        std::int8_t defaultRank,
        bool preserveIfNativeOnBase) {
        if (!actor || !faction || op == State::FactionMembershipOp::LeaveUntouched) {
            return false;
        }
        if (op == State::FactionMembershipOp::EnsurePresent) {
            if (actor->IsInFaction(faction)) {
                return false;
            }
            Utils::AddToFaction(actor, faction, defaultRank);
            MARAS_LOG_TRACE("[FactionReconciler] Added faction {:08X} (rank {}) to {} ({:08X})",
                            faction->GetFormID(), defaultRank,
                            actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
            return true;
        }
        return SyncActorFaction(actor, faction, std::nullopt, preserveIfNativeOnBase);
    }

    void FactionReconciler::ReconcilePlayer() {
        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            DeferredActorResolver::GetSingleton().RequestSync(kPlayerFormID, DeferredActorResolver::SyncFlags::Factions);
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto plan = State::ComputePlayerFactionPlan(*snap);
        auto& fc = FormCache::GetSingleton();

        if (auto* marriedFac = fc.GetMarriedFaction()) {
            (void)ApplyMembershipOp(
                player, marriedFac,
                plan.hasMarriedFaction ? State::FactionMembershipOp::EnsurePresent : State::FactionMembershipOp::EnsureAbsent,
                0, false);
        }
        if (auto* nonCheatingFac = fc.GetCanFormNonCheatingMainFaction()) {
            (void)SyncActorFaction(player, nonCheatingFac, plan.canFormNonCheatingMainRank);
        }
    }

    void FactionReconciler::SanitizePlayerMarriedFaction() {
        ReconcilePlayer();
    }

    void FactionReconciler::ReconcileMerchantFactions(
        RE::Actor* actor,
        bool isHousedAdultTenant,
        bool stripNonNativeDefaultMerchants) {
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        struct MerchantMapping {
            RE::TESFaction* skyrimFaction;
            RE::TESFaction* housedFaction;
        };

        const std::array<MerchantMapping, 8> mappings{{
            { fc.GetApothecaryFaction(), fc.GetApothecaryMerchantHousedFaction() },
            { fc.GetBlacksmithFaction(), fc.GetBlacksmithMerchantHousedFaction() },
            { fc.GetFletcherFaction(), fc.GetFletcherMerchantHousedFaction() },
            { fc.GetHunterFaction(), fc.GetHunterMerchantHousedFaction() },
            { fc.GetInnKeeperFaction(), fc.GetInnkeeperMerchantHousedFaction() },
            { fc.GetJewelerFaction(), fc.GetJewelerMerchantHousedFaction() },
            { fc.GetSpellFaction(), fc.GetSpellsMerchantHousedFaction() },
            { fc.GetTailorFaction(), fc.GetTailorMerchantHousedFaction() }
        }};

        auto* miscHousedFac = fc.GetMiscMerchantHousedFaction();
        const bool hadMarasMisc = (miscHousedFac && actor->IsInFaction(miscHousedFac));

        if (isHousedAdultTenant) {
            bool foundSpecific = false;
            for (const auto& m : mappings) {
                const bool hasSpecialty =
                    m.skyrimFaction &&
                    (actor->IsInFaction(m.skyrimFaction) || IsNativeBaseFaction(actor, m.skyrimFaction));
                if (hasSpecialty) {
                    foundSpecific = true;
                    (void)SyncActorFaction(actor, m.housedFaction, std::int8_t{ 0 });
                } else {
                    (void)SyncActorFaction(actor, m.housedFaction, std::nullopt);
                }
            }

            if (!foundSpecific) {
                (void)ApplyMembershipOp(actor, fc.GetMerchantFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
                (void)ApplyMembershipOp(actor, fc.GetMiscFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
                (void)SyncActorFaction(actor, miscHousedFac, std::int8_t{ 0 });
            } else {
                (void)SyncActorFaction(actor, miscHousedFac, std::nullopt);
                (void)SyncActorFaction(actor, fc.GetMerchantFaction(), std::nullopt, true);
                (void)SyncActorFaction(actor, fc.GetMiscFaction(), std::nullopt, true);
            }
            return;
        }

        // Not a housed adult tenant: remove all 9 MARAS *MerchantHousedFaction factions,
        // and remove MerchantFaction/MiscFaction only when non-native and MARAS-relevant/previously MARAS-misc.
        for (const auto& m : mappings) {
            (void)SyncActorFaction(actor, m.housedFaction, std::nullopt);
        }
        (void)SyncActorFaction(actor, miscHousedFac, std::nullopt);

        if (stripNonNativeDefaultMerchants || hadMarasMisc) {
            (void)SyncActorFaction(actor, fc.GetMerchantFaction(), std::nullopt, true);
            (void)SyncActorFaction(actor, fc.GetMiscFaction(), std::nullopt, true);
        }
    }

    void FactionReconciler::ReconcileAdultFactions(
        RE::Actor* actor,
        const State::WorldStateSnapshot& snap,
        const State::AdultActorFactionPlan& plan) {
        (void)snap;
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        (void)SyncActorFaction(actor, fc.GetTrackedFaction(), plan.trackedRank);
        (void)SyncActorFaction(actor, fc.GetSpouseSocialClassFaction(), plan.socialClassRank);
        (void)SyncActorFaction(actor, fc.GetSpouseSkillTypeFaction(), plan.skillTypeRank);
        (void)SyncActorFaction(actor, fc.GetSpouseTemperamentFaction(), plan.temperamentRank);
        (void)SyncActorFaction(actor, fc.GetHierarchyFaction(), plan.hierarchyRank);
        (void)SyncActorFaction(actor, fc.GetAffectionFaction(), plan.affectionRank);
        (void)SyncActorFaction(actor, fc.GetSpouseChildrenFaction(), plan.spouseChildrenRank);
        (void)SyncActorFaction(actor, fc.GetSpouseCheatingFaction(), plan.spouseCheatingRank);
        (void)SyncActorFaction(actor, fc.GetSpouseOpenRelationshipFaction(), plan.spouseOpenRelationshipRank);
        (void)SyncActorFaction(actor, fc.GetCanFormNonCheatingMainFaction(), plan.canFormNonCheatingMainRank);
        (void)SyncActorFaction(actor, fc.GetSpouseHousedFaction(), plan.housedRank);

        // Mirrored vanilla relationship & house factions
        (void)ApplyMembershipOp(actor, fc.GetMarriedFaction(), plan.marriedFactionOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetPlayerFaction(), plan.playerFactionOp, 0, true);
        (void)ApplyMembershipOp(actor, fc.GetPlayerBedOwnershipFaction(), plan.playerBedOwnershipOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetMarriageAskedFaction(), plan.marriageAskedOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetCourtingFaction(), plan.courtingOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetPotentialHirelingFaction(), plan.potentialHirelingOp, 0, true);
        (void)ApplyMembershipOp(actor, fc.GetMarriagePotentialFaction(), plan.marriagePotentialOp, 0, true);

        if (plan.clearIgnoreProposeKeyword) {
            ClearKeywordOnBase(actor, fc.GetIgnoreProposeKeyword());
        }

        if (plan.clearYoungChildFactions) {
            ClearChildHouseholdFactions(actor);
        }

        if (plan.ensurePotentialFollower) {
            (void)ApplyMembershipOp(actor, fc.GetPotentialFollowerFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensurePlayerWerewolf) {
            (void)ApplyMembershipOp(actor, fc.GetPlayerWerewolfFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensureCompanionsCircle) {
            (void)ApplyMembershipOp(actor, fc.GetCompanionsCircleFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensureFrostmoonWerewolves) {
            (void)ApplyMembershipOp(actor, fc.GetFrostmoonWerewolvesFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }

        ReconcileMerchantFactions(actor, plan.isHousedAdultTenant, plan.isRelevantAdult);
    }

    void FactionReconciler::ReconcileChildFactions(
        RE::Actor* actor,
        const State::WorldStateSnapshot& snap,
        const State::ChildFactionPlan& plan) {
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        (void)SyncActorFaction(actor, fc.GetPlayerChildFaction(), plan.playerChildRank);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionChildOwnedFaction(), plan.byohChildOwnedRank);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionFaction(), plan.byohAdoptionRank);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), plan.playerFactionRank, true);
        (void)SyncActorFaction(actor, fc.GetPlayerChildBothParentsFaction(), plan.bothParentsRank);

        if (plan.adoptableOp != State::FactionMembershipOp::LeaveUntouched) {
            if (auto* adoptableFac = fc.GetBYOHRelationshipAdoptableFaction()) {
                (void)SyncActorFaction(actor, adoptableFac, plan.DesiredAdoptableRank(), false);
            }
        }

        if (plan.restoreSavedCrimeFaction) {
            const State::ChildId childKey{ actor->GetFormID() };
            const auto& snaps = snap.household->AllChildFactionSnapshots();
            if (auto it = snaps.find(childKey); it != snaps.end() && !it->second.crimeFactionId.IsNull()) {
                if (auto* crimeFac = RE::TESForm::LookupByID<RE::TESFaction>(it->second.crimeFactionId.raw())) {
                    if (actor->GetCrimeFaction() != crimeFac) {
                        Utils::SetCrimeFaction(actor, crimeFac);
                        MARAS_LOG_DEBUG("[FactionReconciler] Restored crime faction {:08X} for household child {:08X}",
                                        crimeFac->GetFormID(), actor->GetFormID());
                    }
                }
            }
        }
    }

    void FactionReconciler::ReconcileCompanionFactions(
        RE::Actor* actor,
        const State::CompanionFactionPlan& plan) {
        if (!actor) {
            return;
        }
        auto& fc = FormCache::GetSingleton();
        (void)SyncActorFaction(actor, fc.GetPlayerChildPetFaction(), plan.playerChildPetRank);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), plan.playerFactionRank, true);
    }

    void FactionReconciler::ClearUnknownActorMarasFactions(RE::Actor* actor) {
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();
        const std::array<RE::TESFaction*, 13> marasFactions{{
            fc.GetTrackedFaction(),
            fc.GetSpouseSocialClassFaction(),
            fc.GetSpouseSkillTypeFaction(),
            fc.GetSpouseTemperamentFaction(),
            fc.GetHierarchyFaction(),
            fc.GetAffectionFaction(),
            fc.GetSpouseChildrenFaction(),
            fc.GetSpouseCheatingFaction(),
            fc.GetSpouseOpenRelationshipFaction(),
            fc.GetCanFormNonCheatingMainFaction(),
            fc.GetSpouseHousedFaction(),
            fc.GetPlayerChildBothParentsFaction(),
            fc.GetPlayerChildPetFaction()
        }};

        for (auto* f : marasFactions) {
            (void)SyncActorFaction(actor, f, std::nullopt);
        }

        ReconcileMerchantFactions(actor, false, false);
    }

    void FactionReconciler::ClearChildHouseholdFactions(RE::Actor* actor) {
        if (!actor) {
            return;
        }
        auto& fc = FormCache::GetSingleton();
        (void)SyncActorFaction(actor, fc.GetPlayerChildFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionChildOwnedFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetPlayerChildBothParentsFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptableFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), std::nullopt, true);
    }

    void FactionReconciler::ReconcileLoadedActor(RE::Actor* actor) {
        if (!actor || actor->IsDeleted()) {
            return;
        }

        if (actor->IsPlayerRef()) {
            ReconcilePlayer();
            return;
        }

        const RE::FormID actorId = actor->GetFormID();
        if (actorId == 0 || IsSyntheticChildID(actorId)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::RefId refId{ actorId };
        const State::ChildId childKey{ actorId };

        // 1. Companion (pet/critter) role
        if (const auto companionPlan = State::ComputeCompanionFactionPlan(*snap, refId);
            companionPlan.isActiveCompanion) {
            ReconcileCompanionFactions(actor, companionPlan);
            return;
        }

        // 2. Young child (Stage 0-3)
        if (const auto* childRec = snap->lineage->FindChild(childKey)) {
            if (childRec->stage != State::ChildStage::Adult) {
                const Foundation::ActorId otherParent = childRec->guardians.OtherGuardian(Foundation::ActorId::Player());
                const State::ActorSex otherParentSex = LookupActorSex(otherParent);

                Foundation::RefId resolvedFollowTargetId{};
                if (childRec->stage == State::ChildStage::Child) {
                    const auto legacyEntry = NPCRelationshipManager::GetSingleton().GetChildEntry(actorId);
                    if (legacyEntry.has_value()) {
                        resolvedFollowTargetId = Foundation::RefId{
                            NPCRelationshipManager::GetSingleton().ResolveNonPlayerChildTarget(*legacyEntry) };
                    }
                }

                auto* orphanageMarker = FormCache::GetSingleton().GetHonorhallOrphanageCenterMarker();
                const Foundation::RefId orphanageMarkerId{
                    orphanageMarker ? orphanageMarker->GetFormID() : RE::FormID{ 0 } };

                const auto childPlan = State::ComputeChildFactionPlan(
                    *snap,
                    childKey,
                    otherParentSex,
                    resolvedFollowTargetId,
                    orphanageMarkerId);

                ReconcileChildFactions(actor, *snap, childPlan);
                return;
            }
        }

        // 3. Adult / graduated Stage 4 child / tenant
        std::string_view adultClassId{};
        if (const auto* childRec = snap->lineage->FindChild(childKey);
            childRec && childRec->stage == State::ChildStage::Adult) {
            adultClassId = ResolveAdultChildClassId(*childRec);
        }

        const auto adultPlan = State::ComputeAdultActorFactionPlan(*snap, Foundation::ActorId{ actorId }, adultClassId);
        if (adultPlan.isRelevantAdult) {
            ReconcileAdultFactions(actor, *snap, adultPlan);
            return;
        }

        // 4. Unregistered / removed actor: strip any lingering MARAS factions safely
        if (auto* petFac = FormCache::GetSingleton().GetPlayerChildPetFaction();
            petFac && actor->IsInFaction(petFac)) {
            ReconcileCompanionFactions(actor, State::CompanionFactionPlan{});
        }
        ClearUnknownActorMarasFactions(actor);
    }

    void FactionReconciler::ReconcileActor(RE::FormID actorId) {
        if (actorId == 0 || IsSyntheticChildID(actorId)) {
            return;
        }

        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            DeferredActorResolver::GetSingleton().RequestSync(actorId, DeferredActorResolver::SyncFlags::Factions);
            return;
        }

        if (actorId == kPlayerFormID) {
            ReconcilePlayer();
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor) {
            DeferredActorResolver::GetSingleton().MarkUnloaded(actorId, DeferredActorResolver::SyncFlags::Factions);
            return;
        }
        if (actor->IsDeleted()) {
            return;
        }

        ReconcileLoadedActor(actor);
    }

    void FactionReconciler::ReconcileActors(std::span<const RE::FormID> actorIds) {
        for (const RE::FormID actorId : actorIds) {
            ReconcileActor(actorId);
        }
    }

    void FactionReconciler::ReconcileAllOnLoad() {
        ReconcilePlayer();

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> targets;
        targets.reserve(
            snap->actors->AllProfiles().size() +
            snap->lineage->AllChildren().size() +
            snap->household->AllChildStates().size() * 2 +
            snap->household->AllHouses().size() * 4);

        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            (void)prof;
            if (!refId.IsNull() && !refId.IsPlayer() && !IsSyntheticChildID(refId.raw())) {
                targets.insert(refId.raw());
            }
        }

        for (const auto& [childId, rec] : snap->lineage->AllChildren()) {
            (void)rec;
            if (!childId.IsNull() && !childId.IsSynthetic()) {
                targets.insert(childId.raw());
            }
        }

        for (const auto& [childId, state] : snap->household->AllChildStates()) {
            (void)childId;
            if (!state.petId.IsNull() && !IsSyntheticChildID(state.petId.raw())) {
                targets.insert(state.petId.raw());
            }
            if (!state.critterId.IsNull() && !IsSyntheticChildID(state.critterId.raw())) {
                targets.insert(state.critterId.raw());
            }
        }

        for (const auto& house : snap->household->AllHouses()) {
            for (const std::uint32_t tenantRawId : house.tenantOrder) {
                if (tenantRawId != 0 && tenantRawId != kPlayerFormID && !IsSyntheticChildID(tenantRawId)) {
                    targets.insert(tenantRawId);
                }
            }
        }

        std::size_t reconciledCount = 0;
        for (const RE::FormID actorId : targets) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor) {
                DeferredActorResolver::GetSingleton().MarkUnloaded(
                    actorId, DeferredActorResolver::SyncFlags::Factions);
                continue;
            }
            if (!actor->IsDeleted()) {
                ReconcileLoadedActor(actor);
                ++reconciledCount;
            }
        }

        MARAS_LOG_INFO("[FactionReconciler] ReconcileAllOnLoad converged {} loaded actor(s) ({} deferred until load)",
                       reconciledCount, targets.size() - reconciledCount);
    }

}  // namespace MARAS
