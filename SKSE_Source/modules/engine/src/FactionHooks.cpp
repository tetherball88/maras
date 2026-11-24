#include "maras/engine/FactionReconciler.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldStore.h"

#include <RE/N/NativeFunctionBase.h>
#include <RE/O/ObjectTypeInfo.h>
#include <RE/P/PackUnpack.h>
#include <RE/S/StackFrame.h>
#include <RE/T/TypeTraits.h>
#include <RE/V/Variable.h>
#include <RE/V/VirtualMachine.h>

#include <cstring>
#include <utility>
#include <vector>

namespace MARAS {

    namespace {
        thread_local int s_internalFactionDepth = 0;

        using AddToFaction_t = void (*)(RE::Actor*, RE::TESFaction*, std::int8_t);
        using RemoveFromFaction_t = void (*)(RE::Actor*, RE::TESFaction*);

        AddToFaction_t _originalAddToFaction = nullptr;
        RemoveFromFaction_t _originalRemoveFromFaction = nullptr;

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

        void NotifyListenersForRank(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t rank) {
            if (rank < 0) {
                FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
            } else {
                FactionReconciler::GetSingleton().NotifyFactionAdded(a_actor, a_faction, rank);
            }
        }

        template <typename ApplyOriginalFn, typename ResolveFinalRankFn>
        void HandleExternalFactionRankMutation(
            RE::Actor* a_actor,
            RE::TESFaction* a_faction,
            std::int8_t a_preCheckRank,
            ApplyOriginalFn&& applyOriginalFn,
            ResolveFinalRankFn&& resolveFinalRankFn) {
            if (!a_actor || !a_faction) {
                applyOriginalFn();
                return;
            }

            // Internal MARAS writes skip external auto-promote/demote hooks and notify listeners directly.
            if (FactionReconciler::IsInternalFactionUpdate()) {
                applyOriginalFn();
                if (FactionReconciler::GetSingleton().HasListeners()) {
                    NotifyListenersForRank(a_actor, a_faction, resolveFinalRankFn());
                }
                return;
            }

            const RE::FormID factionId = a_faction->GetFormID();
            const bool isTrackedFaction = State::IsInterceptedMarriageOrCourtshipFaction(factionId);
            if (!isTrackedFaction && !FactionReconciler::GetSingleton().HasListeners()) {
                applyOriginalFn();
                return;
            }

            const bool isPlayer = a_actor->IsPlayerRef();
            const RE::FormID actorId = a_actor->GetFormID();
            const Foundation::ActorId domainActorId =
                isPlayer ? Foundation::ActorId::Player() : Foundation::ActorId{ actorId };

            if (isTrackedFaction && isPlayer) {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                const auto action = State::EvaluateExternalFactionWrite(
                    *snap, domainActorId, factionId, a_preCheckRank, false);
                if (action == State::ExternalFactionWriteAction::BlockPolygamyPlayerMarried) {
                    MARAS_LOG_INFO(
                        "FactionReconciler: Blocked PlayerMarriedFaction (rank {}) from player (polygamy active)",
                        a_preCheckRank);
                    return;
                }
                if (factionId == State::kVanillaPlayerMarriedFactionRawId &&
                    a_preCheckRank < 0 &&
                    snap->settings &&
                    snap->settings->enablePlayerPolygamy) {
                    MARAS_LOG_INFO(
                        "FactionReconciler: Allowing removal rank ({}) for PlayerMarriedFaction on player",
                        a_preCheckRank);
                }
            }

            if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                applyOriginalFn();
                const std::int8_t finalRank = resolveFinalRankFn();
                if (actorId != 0 && factionId != 0) {
                    AddCurrentEpochTask(
                        "FactionReconciler::ExternalFactionWrite",
                        [actorId, factionId, finalRank, isPlayer, isTrackedFaction]() {
                            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
                            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(factionId);
                            if (!actor || actor->IsDeleted() || !faction) {
                                return;
                            }
                            if (isTrackedFaction && !isPlayer) {
                                std::lock_guard papyrusLock(GetPapyrusMutex());
                                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                                const bool isBlockedFamily =
                                    (finalRank >= 0 && factionId != State::kVanillaPlayerMarriedFactionRawId)
                                        ? FactionReconciler::IsBlockedPlayerFamilyActor(actorId, *snap)
                                        : false;
                                const auto action = State::EvaluateExternalFactionWrite(
                                    *snap,
                                    Foundation::ActorId{ actorId },
                                    factionId,
                                    finalRank,
                                    isBlockedFamily);
                                auto* hooks = Engine::GetEngineEventHooks();

                                if (action == State::ExternalFactionWriteAction::BlockPlayerFamilyCourtship) {
                                    MARAS_LOG_DEBUG(
                                        "FactionReconciler: Reconciling off-thread vanilla courtship/potential faction {:08X} for blocked Player family NPC {:08X}",
                                        factionId, actorId);
                                    FactionReconciler::GetSingleton().ReconcileActor(actorId);
                                    return;
                                }
                                if (action == State::ExternalFactionWriteAction::PromoteNpcToPlayerMarried) {
                                    if (hooks && !hooks->CanAcceptExternalMarriage(actorId)) {
                                        MARAS_LOG_WARN(
                                            "FactionReconciler: Blocked/reverted off-thread auto-promoting NPC {:08X} ({}) to Married via PlayerMarriedFaction addition",
                                            actorId, actor->GetName());
                                        FactionReconciler::GetSingleton().ReconcileActor(actorId);
                                        return;
                                    }
                                    MARAS_LOG_INFO(
                                        "FactionReconciler: Auto-promoting NPC {:08X} ({}) to Married via PlayerMarriedFaction addition (deferred from off-thread)",
                                        actorId, actor->GetName());
                                    if (hooks) {
                                        hooks->OnExternalMarriageFactionAdded(actorId, finalRank);
                                    }
                                    FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
                                } else if (action == State::ExternalFactionWriteAction::DemoteNpcToPlayerDivorced) {
                                    MARAS_LOG_INFO(
                                        "FactionReconciler: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction removal/rank reset (deferred from off-thread)",
                                        actorId, actor->GetName());
                                    if (hooks) {
                                        hooks->OnExternalMarriageFactionRemoved(actorId);
                                    }
                                }
                            }
                            NotifyListenersForRank(actor, faction, finalRank);
                        });
                }
                return;
            }

            if (isTrackedFaction && !isPlayer) {
                if (factionId != State::kVanillaPlayerMarriedFactionRawId && a_preCheckRank >= 0) {
                    const auto snap = State::WorldStore::GetSingleton().Snapshot();
                    if (snap &&
                        State::EvaluateExternalFactionWrite(
                            *snap,
                            domainActorId,
                            factionId,
                            a_preCheckRank,
                            FactionReconciler::IsBlockedPlayerFamilyActor(actorId, *snap)) ==
                            State::ExternalFactionWriteAction::BlockPlayerFamilyCourtship) {
                        MARAS_LOG_DEBUG(
                            "FactionReconciler: Blocked vanilla courtship/potential faction {:08X} (rank {}) for Player family NPC {:08X} ({})",
                            factionId, a_preCheckRank, actorId, a_actor->GetName());
                        return;
                    }
                }

                if (factionId == State::kVanillaPlayerMarriedFactionRawId) {
                    std::int8_t finalRank = a_preCheckRank;
                    {
                        std::lock_guard papyrusLock(GetPapyrusMutex());
                        const auto snap = State::WorldStore::GetSingleton().Snapshot();
                        const auto action = State::EvaluateExternalFactionWrite(
                            *snap, domainActorId, factionId, a_preCheckRank, false);
                        auto* hooks = Engine::GetEngineEventHooks();

                        if (action == State::ExternalFactionWriteAction::PromoteNpcToPlayerMarried) {
                            if (hooks && !hooks->CanAcceptExternalMarriage(actorId)) {
                                MARAS_LOG_WARN(
                                    "FactionReconciler: Blocked auto-promoting NPC {:08X} ({}) to Married with Player via PlayerMarriedFaction addition",
                                    actorId, a_actor->GetName());
                                return;
                            }
                            MARAS_LOG_INFO(
                                "FactionReconciler: Auto-promoting NPC {:08X} ({}) to Married via PlayerMarriedFaction addition",
                                actorId, a_actor->GetName());
                            applyOriginalFn();
                            finalRank = resolveFinalRankFn();
                            if (finalRank >= 0) {
                                if (hooks) {
                                    hooks->OnExternalMarriageFactionAdded(actorId, finalRank);
                                }
                                FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
                            }
                        } else if (action == State::ExternalFactionWriteAction::DemoteNpcToPlayerDivorced) {
                            MARAS_LOG_INFO(
                                "FactionReconciler: Auto-demoting NPC {:08X} ({}) to Divorced via PlayerMarriedFaction removal/rank reset",
                                actorId, a_actor->GetName());
                            applyOriginalFn();
                            finalRank = resolveFinalRankFn();
                            if (finalRank < 0 && hooks) {
                                hooks->OnExternalMarriageFactionRemoved(actorId);
                            }
                        } else {
                            applyOriginalFn();
                            finalRank = resolveFinalRankFn();
                        }
                    }
                    NotifyListenersForRank(a_actor, a_faction, finalRank);
                    return;
                }
            }

            applyOriginalFn();
            NotifyListenersForRank(a_actor, a_faction, resolveFinalRankFn());
        }

        void Hook_AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
            HandleExternalFactionRankMutation(
                a_actor,
                a_faction,
                a_rank,
                [&]() {
                    if (_originalAddToFaction) {
                        _originalAddToFaction(a_actor, a_faction, a_rank);
                    }
                },
                [a_rank]() noexcept { return a_rank; });
        }

        void Hook_RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
            HandleExternalFactionRankMutation(
                a_actor,
                a_faction,
                std::int8_t{ -1 },
                [&]() {
                    if (_originalRemoveFromFaction) {
                        _originalRemoveFromFaction(a_actor, a_faction);
                    }
                },
                []() noexcept { return std::int8_t{ -1 }; });
        }

        // Intercepts Papyrus Actor.SetFactionRank / Actor.ModFactionRank because vanilla Actor.AddToFaction
        // is scripted around SetFactionRank(akFaction, 0) rather than calling RE::Actor::AddToFaction.
        class ActorFactionRankNativeWrapper final : public RE::BSScript::NF_util::NativeFunctionBase {
        public:
            enum class Mode : std::uint8_t {
                SetRank = 0,
                ModRank = 1
            };

            ActorFactionRankNativeWrapper(
                std::string_view fnName,
                Mode mode,
                RE::BSTSmartPointer<RE::BSScript::IFunction> origFunc) :
                NativeFunctionBase(fnName, "Actor", /*a_isStatic=*/false, /*a_numParams=*/2),
                mode_(mode),
                origFunc_(std::move(origFunc)) {
                if (origFunc_) {
                    for (std::uint32_t i = 0; i < 2; ++i) {
                        RE::BSFixedString paramName{};
                        RE::BSScript::TypeInfo paramType{};
                        origFunc_->GetParam(i, paramName, paramType);
                        _descTable.entries[i].first = paramName;
                        _descTable.entries[i].second = paramType;
                    }
                    _retType = origFunc_->GetReturnType();
                    _userFlags = origFunc_->GetUserFlags();
                    _isCallableFromTasklet = origFunc_->CanBeCalledFromTasklets();
                    if (const auto* origNative =
                            static_cast<const RE::BSScript::NF_util::NativeFunctionBase*>(origFunc_.get())) {
                        _isLatent = origNative->GetIsLatent();
                    }
                } else {
                    _descTable.entries[0].second.SetType(RE::BSScript::GetRawType<RE::TESFaction*>{}());
                    _descTable.entries[1].second.SetType(RE::BSScript::GetRawType<std::int32_t>{}());
                    _retType = RE::BSScript::GetRawType<void>{}();
                }
            }

            bool HasStub() const override {
                return origFunc_ != nullptr;
            }

            bool MarshallAndDispatch(
                RE::BSScript::Variable& a_baseValue,
                RE::BSScript::Internal::VirtualMachine& a_vm,
                RE::VMStackID a_stackID,
                RE::BSScript::Variable& a_resultValue,
                const RE::BSScript::StackFrame& a_frame) const override {
                if (!origFunc_) {
                    return false;
                }
                auto* origNative = static_cast<RE::BSScript::NF_util::NativeFunctionBase*>(origFunc_.get());
                auto* actor = a_baseValue.Unpack<RE::Actor*>();
                const auto page = a_frame.GetPageForFrame();
                auto* faction = a_frame.GetStackFrameVariable(0, page).Unpack<RE::TESFaction*>();
                const auto rawArg = a_frame.GetStackFrameVariable(1, page).Unpack<std::int32_t>();

                if (!actor || !faction) {
                    return origNative->MarshallAndDispatch(
                        a_baseValue, a_vm, a_stackID, a_resultValue, a_frame);
                }

                std::int8_t preCheckRank = 0;
                if (mode_ == Mode::SetRank) {
                    preCheckRank = State::ClampPapyrusFactionRank(rawArg);
                } else {
                    const bool wasIn = actor->IsInFaction(faction);
                    const std::int32_t curRank = wasIn ? actor->GetFactionRank(faction, actor->IsPlayerRef()) : 0;
                    preCheckRank = State::ComputeModdedFactionRank(wasIn, curRank, rawArg);
                }

                bool dispatched = false;
                bool dispatchOk = true;
                auto applyOriginal = [&]() {
                    dispatched = true;
                    FactionReconciler::InternalFactionScope internalScope;
                    dispatchOk = origNative->MarshallAndDispatch(
                        a_baseValue, a_vm, a_stackID, a_resultValue, a_frame);
                };
                auto resolveFinalRank = [&]() -> std::int8_t {
                    if (mode_ == Mode::SetRank) {
                        return preCheckRank;
                    }
                    if (!actor->IsInFaction(faction)) {
                        return std::int8_t{ -1 };
                    }
                    return State::ClampPapyrusFactionRank(actor->GetFactionRank(faction, actor->IsPlayerRef()));
                };

                HandleExternalFactionRankMutation(
                    actor, faction, preCheckRank, applyOriginal, resolveFinalRank);

                if (!dispatched) {
                    a_resultValue.SetNone();
                    return true;
                }
                return dispatchOk;
            }

        private:
            Mode mode_;
            RE::BSTSmartPointer<RE::BSScript::IFunction> origFunc_;
        };

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

    bool FactionReconciler::RegisterPapyrusHooks(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            MARAS_LOG_WARN("FactionReconciler::RegisterPapyrusHooks: VirtualMachine is null");
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> actorType;
        const RE::BSFixedString actorClassName("Actor");
        if (!a_vm->GetScriptObjectTypeNoLoad(actorClassName, actorType) || !actorType) {
            if (!a_vm->GetScriptObjectType(actorClassName, actorType) || !actorType) {
                MARAS_LOG_WARN("FactionReconciler::RegisterPapyrusHooks: Failed to resolve Actor ObjectTypeInfo");
                return false;
            }
        }

        const RE::BSFixedString setRankName("SetFactionRank");
        const RE::BSFixedString modRankName("ModFactionRank");
        bool hookedSet = false;
        bool hookedMod = false;

        auto tryWrapSlot = [&](RE::BSTSmartPointer<RE::BSScript::IFunction>& slot) {
            if (!slot || !slot->GetIsNative() || slot->GetIsStatic() || slot->GetParamCount() != 2) {
                return;
            }
            if (!slot->GetStateName().empty()) {
                return;
            }
            if (dynamic_cast<const ActorFactionRankNativeWrapper*>(slot.get()) != nullptr) {
                if (slot->GetName() == setRankName) {
                    hookedSet = true;
                } else if (slot->GetName() == modRankName) {
                    hookedMod = true;
                }
                return;
            }
            if (slot->GetName() == setRankName) {
                slot = RE::BSTSmartPointer<RE::BSScript::IFunction>{
                    new ActorFactionRankNativeWrapper(
                        "SetFactionRank", ActorFactionRankNativeWrapper::Mode::SetRank, slot)
                };
                hookedSet = true;
            } else if (slot->GetName() == modRankName) {
                slot = RE::BSTSmartPointer<RE::BSScript::IFunction>{
                    new ActorFactionRankNativeWrapper(
                        "ModFactionRank", ActorFactionRankNativeWrapper::Mode::ModRank, slot)
                };
                hookedMod = true;
            }
        };

        if (!actorType->IsLinked()) {
            for (auto* node = actorType->GetUnlinkedFunctionIter(); node; node = node->next) {
                tryWrapSlot(node->func);
            }
        } else if (actorType->IsLinkedValid()) {
            auto* funcs = actorType->GetMemberFuncIter();
            const std::uint32_t count = actorType->GetNumMemberFuncs();
            for (std::uint32_t i = 0; i < count; ++i) {
                tryWrapSlot(funcs[i].func);
            }
        }

        if (hookedSet && hookedMod) {
            MARAS_LOG_INFO(
                "FactionReconciler: Installed Papyrus native hooks (Actor.SetFactionRank=true, Actor.ModFactionRank=true)");
        } else {
            MARAS_LOG_WARN(
                "FactionReconciler: Partial Papyrus native hook install (Actor.SetFactionRank={}, Actor.ModFactionRank={})",
                hookedSet, hookedMod);
        }
        return true;
    }

}  // namespace MARAS
