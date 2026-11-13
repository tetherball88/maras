#pragma once

#include "PCH.h"

#include "maras/state/CoSaveCodec.h"
#include "maras/state/WorldState.h"

namespace MARAS {

    /**
     * Engine adapter bridging SKSE::SerializationInterface and CommonLibSSE actor/form lookups
     * to the engine-header-free `MARAS::State::WorldState` and `CoSaveCodec`.
     */
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

    namespace WorldStateBridge {
        // Connects MARAS::Foundation::SetLogSink to MARAS_LOG_* macros
        void InitializeFoundationLogSink() noexcept;
    }

}  // namespace MARAS
