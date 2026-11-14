#include "core/Serialization.h"

#include <random>

#include "core/AdultChildProgression.h"
#include "core/AdultChildTravelService.h"
#include "core/AffectionService.h"
#include "core/BanterCoordinatorService.h"
#include "core/BuffReconciler.h"
#include "core/ChildrenFleeService.h"
#include "core/DeceasedRegistry.h"
#include "core/DeferredActorResolver.h"
#include "core/DialogueEventSink.h"
#include "core/FactionReconciler.h"
#include "core/GlobalVarReconciler.h"
#include "core/IntimateThreadService.h"
#include "core/MatchmakingService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PackageReconciler.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "core/RelationshipTypes.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/Logging.h"
#include "maras/state/CoSaveCodec.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "ui/ConfirmationModal.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "ui/PrismaUIManager.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {
        std::atomic<SessionEpoch> g_sessionEpoch{ 1 };
    }

    SessionEpoch GetSessionEpoch() noexcept {
        return g_sessionEpoch.load(std::memory_order_acquire);
    }

    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept {
        const SessionEpoch next = g_sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        MARAS_LOG_INFO("[SessionEpoch] Advanced session epoch to {} (reason: {})", next, reason);
        return next;
    }

}  // namespace MARAS

namespace MARAS::Serialization {

    namespace {

        // =====================================================================
        // Compile-Time Binary Serialization & Enum Value Parity Assertions
        // =====================================================================

        static_assert(State::CoSaveConstants::kMarasPluginId == kMarasPluginID);
        static_assert(State::CoSaveConstants::kDataVersion == kDataVersion);
        static_assert(State::CoSaveConstants::kMagicNumber == kMagicNumber);
        static_assert(State::CoSaveConstants::kChunkNPCR == kNPCRelationshipData);
        static_assert(State::CoSaveConstants::kChunkSPHR == kSpouseHierarchyData);
        static_assert(State::CoSaveConstants::kChunkAFCT == kAffectionData);
        static_assert(State::CoSaveConstants::kChunkPHOU == kPlayerHouseData);
        static_assert(State::CoSaveConstants::kChunkSPAS == kSpouseAssetsData);
        static_assert(State::CoSaveConstants::kChunkPLCH == kPlayerChildrenData);
        static_assert(State::CoSaveConstants::kChunkCFSN == kChildFactionSnapshotData);
        static_assert(State::CoSaveConstants::kChunkMKMK == kMatchmakingData);
        static_assert(State::CoSaveConstants::kChunkDCSD == kDeceasedRegistryData);
        static_assert(State::CoSaveConstants::kChunkBNTR == kBanterData);
        static_assert(State::CoSaveConstants::kChunkACTR == kAdultChildTravelData);
        static_assert(State::CoSaveConstants::kChunkCNFG == kPluginSettingsData);

        static_assert(static_cast<std::uint8_t>(State::SocialClass::Rulers) ==
                      static_cast<std::uint8_t>(SocialClass::Rulers));
        static_assert(static_cast<std::uint8_t>(State::SocialClass::_Count) ==
                      static_cast<std::uint8_t>(SocialClass::_Count));

        static_assert(static_cast<std::uint8_t>(State::SkillType::Orator) ==
                      static_cast<std::uint8_t>(SkillType::Orator));
        static_assert(static_cast<std::uint8_t>(State::SkillType::_Count) ==
                      static_cast<std::uint8_t>(SkillType::_Count));

        static_assert(static_cast<std::uint8_t>(State::Temperament::Independent) ==
                      static_cast<std::uint8_t>(Temperament::Independent));
        static_assert(static_cast<std::uint8_t>(State::Temperament::_Count) ==
                      static_cast<std::uint8_t>(Temperament::_Count));

        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Candidate) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Candidate));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Engaged) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Engaged));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Married) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Married));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Divorced) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Divorced));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Jilted) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Jilted));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Dating) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Dating));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Lover) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Lover));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::ExDating) ==
                      static_cast<std::uint8_t>(RelationshipStatus::ExDating));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::ExLover) ==
                      static_cast<std::uint8_t>(RelationshipStatus::ExLover));
        static_assert(static_cast<std::uint8_t>(State::RelationshipStatus::Unknown) ==
                      static_cast<std::uint8_t>(RelationshipStatus::Unknown));

        static_assert(static_cast<std::uint8_t>(State::AffairType::_Count) ==
                      static_cast<std::uint8_t>(AffairType::_Count));
        static_assert(static_cast<std::uint8_t>(State::AffairStatus::_Count) ==
                      static_cast<std::uint8_t>(AffairStatus::_Count));
        static_assert(static_cast<std::uint8_t>(State::AffairDiscovery::_Count) ==
                      static_cast<std::uint8_t>(AffairDiscovery::_Count));
        static_assert(static_cast<std::uint8_t>(State::AffairResolution::_Count) ==
                      static_cast<std::uint8_t>(AffairResolution::_Count));
        static_assert(static_cast<std::uint8_t>(State::AffairStage::_Count) ==
                      static_cast<std::uint8_t>(AffairStage::_Count));
        static_assert(static_cast<std::uint8_t>(State::PartnerIntentType::_Count) ==
                      static_cast<std::uint8_t>(PartnerIntentType::_Count));
        static_assert(static_cast<std::uint8_t>(State::ChildStage::Adult) ==
                      static_cast<std::uint8_t>(ChildStage::Adult));

        static_assert(static_cast<std::uint8_t>(State::ChildCustodyGenderPolicy::StayWithFather) ==
                      static_cast<std::uint8_t>(Settings::ChildCustodyGenderPolicy::StayWithFather));
        static_assert(static_cast<std::uint8_t>(State::ChildCustodyPlayerPolicy::NeverWithPlayer) ==
                      static_cast<std::uint8_t>(Settings::ChildCustodyPlayerPolicy::NeverWithPlayer));
        static_assert(static_cast<std::uint8_t>(State::MatchmakingOrientation::HeteroAndLesbian) ==
                      static_cast<std::uint8_t>(Settings::MatchmakingOrientation::HeteroAndLesbian));

        void ForwardFoundationLog(Foundation::LogSeverity severity, std::string_view message) noexcept {
            switch (severity) {
                case Foundation::LogSeverity::Trace:
                    MARAS_LOG_TRACE("{}", message);
                    break;
                case Foundation::LogSeverity::Debug:
                    MARAS_LOG_DEBUG("{}", message);
                    break;
                case Foundation::LogSeverity::Info:
                    MARAS_LOG_INFO("{}", message);
                    break;
                case Foundation::LogSeverity::Warn:
                    MARAS_LOG_WARN("{}", message);
                    break;
                case Foundation::LogSeverity::Error:
                    MARAS_LOG_ERROR("{}", message);
                    break;
            }
        }

    }  // namespace

    bool SkseCoSaveReader::NextRecordInfo(
        std::uint32_t& outType,
        std::uint32_t& outVersion,
        std::uint32_t& outLength) {
        return serialization_ != nullptr && serialization_->GetNextRecordInfo(outType, outVersion, outLength);
    }

    bool SkseCoSaveReader::ReadBytes(void* dst, std::uint32_t numBytes) {
        if (numBytes == 0) {
            return true;
        }
        return serialization_ != nullptr && dst != nullptr && serialization_->ReadRecordData(dst, numBytes);
    }

    bool SkseCoSaveWriter::OpenRecord(std::uint32_t type, std::uint32_t version) {
        return serialization_ != nullptr && serialization_->OpenRecord(type, version);
    }

    bool SkseCoSaveWriter::WriteBytes(const void* src, std::uint32_t numBytes) {
        if (numBytes == 0) {
            return true;
        }
        return serialization_ != nullptr && src != nullptr && serialization_->WriteRecordData(src, numBytes);
    }

    bool SkseFormIdResolver::ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept {
        if (oldRawId == 0) {
            outNewRawId = 0;
            return false;
        }
        if (!serialization_) {
            outNewRawId = oldRawId;
            return true;
        }
        RE::FormID resolved = 0;
        if (!serialization_->ResolveFormID(oldRawId, resolved) || resolved == 0) {
            outNewRawId = 0;
            return false;
        }
        outNewRawId = resolved;
        return true;
    }

    State::ActorSex SkseFormIdResolver::GetSex(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return State::ActorSex::Unknown;
        }
        const std::uint8_t sex = ActorSvc::GetSex(actor.raw());
        if (sex == 0) {
            return State::ActorSex::Male;
        }
        if (sex == 1) {
            return State::ActorSex::Female;
        }
        return State::ActorSex::Unknown;
    }

    bool SkseFormIdResolver::IsAliveAndValid(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return false;
        }
        if (actor.IsPlayer()) {
            return true;
        }
        if (auto* loadedActor = NPCIndex::GetSingleton().GetActorFromFormId(actor.raw())) {
            return !loadedActor->IsDead() && !loadedActor->IsDeleted();
        }
        if (auto* form = RE::TESForm::LookupByID(actor.raw())) {
            if (form->IsDeleted()) {
                return false;
            }
            if (auto* act = form->As<RE::Actor>()) {
                return !act->IsDead();
            }
        }
        return ActorSvc::IsValid(actor.raw());
    }

    bool SkseFormIdResolver::IsDynamicActorPresent(Foundation::RefId refId) const noexcept {
        if (refId.IsNull()) {
            return false;
        }
        auto* form = RE::TESForm::LookupByID(refId.raw());
        if (!form || form->IsDeleted()) {
            return false;
        }
        return form->As<RE::Actor>() != nullptr;
    }

    std::string SkseFormIdResolver::GetActorName(Foundation::ActorId actor) const {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (actor.IsPlayer()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const char* nm = player->GetName();
                if (nm && nm[0] != '\0') {
                    return nm;
                }
            }
            return "Player";
        }
        return ActorSvc::GetName(actor.raw());
    }

    Foundation::FormRawId SkseFormIdResolver::GetActorRaceId(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (auto* race = ActorSvc::GetRace(actor.raw())) {
            return Foundation::FormRawId{ race->GetFormID() };
        }
        return {};
    }

    Foundation::BaseId SkseFormIdResolver::GetActorBaseId(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (actor.IsPlayer()) {
            return Foundation::BaseId{ Foundation::kPlayerBaseRawId };
        }
        if (actor.IsDynamicRuntimeActor()) {
            return Foundation::BaseId{ actor.raw() };
        }
        const RE::FormID baseId = NPCIndex::GetSingleton().GetBaseID(actor.raw());
        if (baseId != 0) {
            return Foundation::BaseId{ baseId };
        }
        if (auto* npcBase = ActorSvc::GetActorBase(actor.raw())) {
            return Foundation::BaseId{ npcBase->GetFormID() };
        }
        return {};
    }

    void InitializeFoundationLogSink() noexcept {
        Foundation::SetLogSink(&ForwardFoundationLog);
    }

    void SaveCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("SaveCallback: serialization interface is null");
            return;
        }

        const State::WorldState stateCopy = State::WorldStore::GetSingleton().ReadLive(
            [](const State::WorldState& ws) { return ws; });
        SkseCoSaveWriter writer(serialization);
        if (!State::SaveWorldState(stateCopy, writer)) {
            MARAS_LOG_ERROR("SaveCallback: Failed to save MARAS WorldState co-save chunks");
        } else {
            MARAS_LOG_INFO("SaveCallback: Successfully saved MARAS WorldState");
        }
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("LoadCallback: serialization interface is null");
            return;
        }

        SkyrimNetMemoryService::SuppressScope suppressMemories;
        SkyrimNetProfileService::SuppressScope suppressProfiles;

        State::WorldState staged{};
        auto initialSettings = State::WorldStore::GetSingleton().GetIniBaselineSettings();
        initialSettings.enablePlayerPolygamy = Settings::GetSingleton().GetEnablePolygamy();
        staged.settings.Reset(initialSettings);

        SkseCoSaveReader reader(serialization);
        SkseFormIdResolver resolver(serialization);
        Foundation::DeterministicRng rng(std::random_device{}());
        const auto report = State::LoadWorldState(staged, reader, resolver, &rng);

        MARAS_LOG_INFO("LoadCallback: Co-save load completed (loaded={}, failed={}, skipped={}, allSucceeded={})",
                       report.loadedChunkCount, report.failedChunkCount, report.skippedChunkCount, report.allChunksSucceeded);

        if (report.loadedChunkCount > 0) {
            State::WorldStore::GetSingleton().ReplaceState(std::move(staged));

            Settings::GetSingleton().ApplyPostLoadEngineSideEffects();
            NPCRelationshipManager::GetSingleton().FinalizeLoad();
            AffectionService::GetSingleton().FinalizeLoad();
            PlayerHouseService::GetSingleton().FinalizeLoad();
            PlayerChildrenService::GetSingleton().ReassertAllOnLoad();
            SpouseAssetsService::GetSingleton().FinalizeLoad();
            SpouseHierarchyManager::GetSingleton().SyncAllFactionsAndGlobals();
            GlobalVarReconciler::GetSingleton().SyncExternalPolygamyGlobalToStore();
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            FactionReconciler::GetSingleton().ReconcileAllOnLoad();
            PackageReconciler::GetSingleton().ReconcileWorld();
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        BumpSessionEpoch("Serialization::RevertCallback");
        ResetPluginEventSinks();
        DeferredActorResolver::GetSingleton().Revert();
        GlobalVarReconciler::GetSingleton().Revert();
        BuffReconciler::GetSingleton().Revert();
        PollingService::GetSingleton().Shutdown();
        State::WorldStore::GetSingleton().Revert();
        Settings::GetSingleton().RevertGameSettings();
        AdultChildProgression::GetSingleton().Revert();
        NPCRelationshipManager::GetSingleton().Revert();
        AffectionService::GetSingleton().Revert();
        PlayerHouseService::GetSingleton().Revert();
        SpouseAssetsService::GetSingleton().Revert();
        PlayerChildrenService::GetSingleton().Revert();
        AdultChildTravelService::GetSingleton().Revert();
        MatchmakingService::GetSingleton().Revert();
        DeceasedRegistry::GetSingleton().Revert();
        BanterCoordinatorService::GetSingleton().Revert();
        PackageReconciler::GetSingleton().Revert();
        IntimateThreadService::GetSingleton().ClearAll();
        ChildrenFleeService::GetSingleton().Reset();
        SkyrimNetMemoryService::GetSingleton().Revert();
        SkyrimNetProfileService::GetSingleton().Revert();
        if (auto* dialogueSink = DialogueEventSink::GetSingleton()) {
            dialogueSink->Revert();
        }
        ConfirmationModal::GetSingleton().Revert();
        PrismaModalManager::GetSingleton().Revert();
        PrismaUIManager::GetSingleton().Revert();
        PrismaUIDataBridge::GetSingleton().Revert();

        MARAS_LOG_INFO("Reverted MARAS co-save runtime state");
    }

}  // namespace MARAS::Serialization

