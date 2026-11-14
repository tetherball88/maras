#pragma once

#include "PCH.h"
#include "maras/state/CoSaveCodec.h"
#include "maras/state/WorldState.h"

namespace MARAS::Serialization {

    // Plugin unique ID for SKSE serialization (must be exactly 4 characters)
    constexpr std::uint32_t kMarasPluginID = 'MARS';

    // Current version of our serialization format (see State::CoSaveConstants for chunk details)
    constexpr std::uint32_t kDataVersion = 7;

    // Top-level SKSE co-save record types (12 records).
    constexpr std::uint32_t kNPCRelationshipData = 'NPCR';
    constexpr std::uint32_t kSpouseHierarchyData = 'SPHR';
    constexpr std::uint32_t kAffectionData = 'AFCT';
    constexpr std::uint32_t kPlayerHouseData = 'PHOU';
    constexpr std::uint32_t kSpouseAssetsData = 'SPAS';
    constexpr std::uint32_t kPlayerChildrenData = 'PLCH';
    constexpr std::uint32_t kChildFactionSnapshotData = 'CFSN';
    constexpr std::uint32_t kMatchmakingData = 'MKMK';
    constexpr std::uint32_t kDeceasedRegistryData = 'DCSD';
    constexpr std::uint32_t kBanterData = 'BNTR';
    constexpr std::uint32_t kAdultChildTravelData = 'ACTR';
    constexpr std::uint32_t kPluginSettingsData = 'CNFG';

    // Magic number to validate data integrity
    constexpr std::uint32_t kMagicNumber = 0xDEADBEEF;

    // Engine adapters bridging SKSE::SerializationInterface and CommonLibSSE actor/form lookups
    // to the engine-header-free MARAS::State::WorldState and CoSaveCodec.
    class SkseCoSaveReader final : public State::ICoSaveReader {
    public:
        explicit SkseCoSaveReader(SKSE::SerializationInterface* serialization) noexcept
            : serialization_(serialization) {}

        bool NextRecordInfo(std::uint32_t& outType, std::uint32_t& outVersion, std::uint32_t& outLength) override;
        bool ReadBytes(void* dst, std::uint32_t numBytes) override;

    private:
        SKSE::SerializationInterface* serialization_{ nullptr };
    };

    class SkseCoSaveWriter final : public State::ICoSaveWriter {
    public:
        explicit SkseCoSaveWriter(SKSE::SerializationInterface* serialization) noexcept
            : serialization_(serialization) {}

        bool OpenRecord(std::uint32_t type, std::uint32_t version) override;
        bool WriteBytes(const void* src, std::uint32_t numBytes) override;

    private:
        SKSE::SerializationInterface* serialization_{ nullptr };
    };

    class SkseFormIdResolver final : public State::IFormIdResolver {
    public:
        explicit SkseFormIdResolver(SKSE::SerializationInterface* serialization = nullptr) noexcept
            : serialization_(serialization) {}

        [[nodiscard]] bool ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept override;
        [[nodiscard]] State::ActorSex GetSex(Foundation::ActorId actor) const noexcept override;
        [[nodiscard]] bool IsAliveAndValid(Foundation::ActorId actor) const noexcept override;
        [[nodiscard]] bool IsDynamicActorPresent(Foundation::RefId refId) const noexcept override;
        [[nodiscard]] std::string GetActorName(Foundation::ActorId actor) const override;
        [[nodiscard]] Foundation::FormRawId GetActorRaceId(Foundation::ActorId actor) const noexcept override;
        [[nodiscard]] Foundation::BaseId GetActorBaseId(Foundation::ActorId actor) const noexcept override;

    private:
        SKSE::SerializationInterface* serialization_{ nullptr };
    };

    // Connects MARAS::Foundation::SetLogSink to MARAS_LOG_* macros
    void InitializeFoundationLogSink() noexcept;

    // SKSE Co-Save Serialization Callbacks
    void SaveCallback(SKSE::SerializationInterface* serialization);
    void LoadCallback(SKSE::SerializationInterface* serialization);
    void RevertCallback(SKSE::SerializationInterface* serialization);

}  // namespace MARAS::Serialization
