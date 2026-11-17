#pragma once

#include "maras/state/WorldState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace MARAS::State {

    [[nodiscard]] constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d) noexcept {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(d));
    }

    [[nodiscard]] inline std::string FourCCToString(std::uint32_t fourCC) {
        const std::array<unsigned char, 4> bytes{
            static_cast<unsigned char>((fourCC >> 24) & 0xFFu),
            static_cast<unsigned char>((fourCC >> 16) & 0xFFu),
            static_cast<unsigned char>((fourCC >> 8) & 0xFFu),
            static_cast<unsigned char>(fourCC & 0xFFu),
        };
        std::string out;
        out.reserve(4);
        for (const unsigned char ch : bytes) {
            out.push_back((ch >= 0x20u && ch <= 0x7Eu) ? static_cast<char>(ch) : '?');
        }
        return out;
    }

    namespace CoSaveConstants {
        inline constexpr std::uint32_t kMarasPluginId = MakeFourCC('M', 'A', 'R', 'S');
        inline constexpr std::uint32_t kDataVersion = 7u;
        inline constexpr std::uint32_t kMagicNumber = 0xDEADBEEFu;

        // 12 top-level SKSE co-save chunk types
        inline constexpr std::uint32_t kChunkNPCR = MakeFourCC('N', 'P', 'C', 'R');
        inline constexpr std::uint32_t kChunkSPHR = MakeFourCC('S', 'P', 'H', 'R');
        inline constexpr std::uint32_t kChunkAFCT = MakeFourCC('A', 'F', 'C', 'T');
        inline constexpr std::uint32_t kChunkPHOU = MakeFourCC('P', 'H', 'O', 'U');
        inline constexpr std::uint32_t kChunkSPAS = MakeFourCC('S', 'P', 'A', 'S');
        inline constexpr std::uint32_t kChunkPLCH = MakeFourCC('P', 'L', 'C', 'H');
        inline constexpr std::uint32_t kChunkCFSN = MakeFourCC('C', 'F', 'S', 'N');
        inline constexpr std::uint32_t kChunkMKMK = MakeFourCC('M', 'K', 'M', 'K');
        inline constexpr std::uint32_t kChunkDCSD = MakeFourCC('D', 'C', 'S', 'D');
        inline constexpr std::uint32_t kChunkBNTR = MakeFourCC('B', 'N', 'T', 'R');
        inline constexpr std::uint32_t kChunkACTR = MakeFourCC('A', 'C', 'T', 'R');
        inline constexpr std::uint32_t kChunkCNFG = MakeFourCC('C', 'N', 'F', 'G');

        // Inline extension magics inside 'NPCR'
        inline constexpr std::uint32_t kExtAgencyAGN1 = MakeFourCC('A', 'G', 'N', '1');
        inline constexpr std::uint32_t kExtPregnancySecretPRG1 = MakeFourCC('P', 'R', 'G', '1');
        inline constexpr std::uint32_t kExtChildVisualRpgCHD1 = MakeFourCC('C', 'H', 'D', '1');
        inline constexpr std::uint32_t kExtChildBeastbloodLYC1 = MakeFourCC('L', 'Y', 'C', '1');
        inline constexpr std::uint32_t kExtChildLineageCHD2 = MakeFourCC('C', 'H', 'D', '2');
        inline constexpr std::uint32_t kExtFertilityBatchFRT1 = MakeFourCC('F', 'R', 'T', '1');
        inline constexpr std::uint32_t kExtChildSkyrimNetUuidSNU1 = MakeFourCC('S', 'N', 'U', '1');

        // Optional inline extension magic inside 'DCSD' (omitted when no deceased secrecy flags exist)
        inline constexpr std::uint32_t kExtDeceasedSecrecyDCS1 = MakeFourCC('D', 'C', 'S', '1');

        // Optional inline extension magic inside 'MKMK' (omitted when no Phase 3 relationship lifecycle state exists)
        inline constexpr std::uint32_t kExtRelationshipStateREL1 = MakeFourCC('R', 'E', 'L', '1');

        // Optional inline extension magic inside 'PLCH' (omitted when all Phase 7 child/forcegreet state is default/empty)
        inline constexpr std::uint32_t kExtPlayerChildrenStatePLC1 = MakeFourCC('P', 'L', 'C', '1');

        // Optional inline extension magic inside 'BNTR' (omitted when no moved actors or muted SkyrimNet participants exist)
        inline constexpr std::uint32_t kExtBanterSceneStateBNT1 = MakeFourCC('B', 'N', 'T', '1');

        // Optional inline extension magic inside 'CNFG' storing the MARAS mod version string that wrote the co-save
        inline constexpr std::uint32_t kExtSaveMetadataVER1 = MakeFourCC('V', 'E', 'R', '1');

        // 'CNFG' sub-version header
        inline constexpr std::uint8_t kSettingsVersionMarker = 0xFFu;
        inline constexpr std::uint32_t kSettingsSubVersion = 4u;
    }  // namespace CoSaveConstants

    /**
     * Pure binary co-save reader port (wraps SKSE::SerializationInterface in game,
     * or InMemoryCoSaveReader in headless unit tests).
     */
    class ICoSaveReader {
    public:
        virtual ~ICoSaveReader() = default;

        virtual bool NextRecordInfo(std::uint32_t& outType, std::uint32_t& outVersion, std::uint32_t& outLength) = 0;
        virtual bool ReadBytes(void* dst, std::uint32_t numBytes) = 0;

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        bool ReadPod(T& outVal) {
            return ReadBytes(&outVal, static_cast<std::uint32_t>(sizeof(T)));
        }
    };

    /**
     * Pure binary co-save writer port.
     */
    class ICoSaveWriter {
    public:
        virtual ~ICoSaveWriter() = default;

        virtual bool OpenRecord(std::uint32_t type, std::uint32_t version) = 0;
        virtual bool WriteBytes(const void* src, std::uint32_t numBytes) = 0;

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        bool WritePod(const T& val) {
            return WriteBytes(&val, static_cast<std::uint32_t>(sizeof(T)));
        }
    };

    /**
     * Load-time FormID remapping & actor liveness/metadata port.
     */
    class IFormIdResolver : public IActorMetadataProvider {
    public:
        ~IFormIdResolver() override = default;

        [[nodiscard]] virtual bool ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept {
            if (oldRawId == 0) {
                outNewRawId = 0;
                return false;
            }
            outNewRawId = oldRawId;
            return true;
        }
    };

    class IdentityFormIdResolver final : public IFormIdResolver {};

    // =========================================================================
    // In-Memory Co-Save Buffer & Reader/Writer for Headless Testing & Fuzzing
    // =========================================================================

    struct CoSaveRecordChunk {
        std::uint32_t type{ 0 };
        std::uint32_t version{ CoSaveConstants::kDataVersion };
        std::vector<std::uint8_t> payload{};
    };

    class InMemoryCoSaveWriter final : public ICoSaveWriter {
    public:
        bool OpenRecord(std::uint32_t type, std::uint32_t version) override {
            records_.push_back(CoSaveRecordChunk{ .type = type, .version = version, .payload = {} });
            hasOpenRecord_ = true;
            return true;
        }

        bool WriteBytes(const void* src, std::uint32_t numBytes) override {
            if (!hasOpenRecord_) {
                return false;
            }
            if (numBytes == 0) {
                return true;
            }
            if (src == nullptr) {
                return false;
            }
            auto& buf = records_.back().payload;
            const auto* bytes = static_cast<const std::uint8_t*>(src);
            buf.insert(buf.end(), bytes, bytes + numBytes);
            return true;
        }

        [[nodiscard]] const std::vector<CoSaveRecordChunk>& Records() const noexcept { return records_; }
        [[nodiscard]] std::vector<CoSaveRecordChunk>& MutableRecords() noexcept { return records_; }
        void Clear() noexcept {
            records_.clear();
            hasOpenRecord_ = false;
        }

    private:
        std::vector<CoSaveRecordChunk> records_{};
        bool hasOpenRecord_{ false };
    };

    class InMemoryCoSaveReader final : public ICoSaveReader {
    public:
        explicit InMemoryCoSaveReader(std::vector<CoSaveRecordChunk> records) noexcept
            : records_(std::move(records)) {}

        bool NextRecordInfo(std::uint32_t& outType, std::uint32_t& outVersion, std::uint32_t& outLength) override {
            if (nextRecordIndex_ >= records_.size()) {
                return false;
            }
            currentRecordIndex_ = nextRecordIndex_++;
            byteCursor_ = 0;
            const auto& rec = records_[currentRecordIndex_];
            outType = rec.type;
            outVersion = rec.version;
            outLength = static_cast<std::uint32_t>(rec.payload.size());
            hasCurrentRecord_ = true;
            return true;
        }

        bool ReadBytes(void* dst, std::uint32_t numBytes) override {
            if (!hasCurrentRecord_ || currentRecordIndex_ >= records_.size()) {
                return false;
            }
            if (numBytes == 0) {
                return true;
            }
            if (dst == nullptr) {
                return false;
            }
            const auto& payload = records_[currentRecordIndex_].payload;
            if (byteCursor_ > payload.size() || numBytes > (payload.size() - byteCursor_)) {
                return false;
            }
            std::memcpy(dst, payload.data() + byteCursor_, numBytes);
            byteCursor_ += numBytes;
            return true;
        }

    private:
        std::vector<CoSaveRecordChunk> records_{};
        std::size_t nextRecordIndex_{ 0 };
        std::size_t currentRecordIndex_{ 0 };
        std::size_t byteCursor_{ 0 };
        bool hasCurrentRecord_{ false };
    };

}  // namespace MARAS::State
